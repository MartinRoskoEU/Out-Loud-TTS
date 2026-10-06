#pragma once
#include "replacement_audit.hpp"

// Existing checker only: private files, fake tokens, no helper/UAC/real registry.
void removal_checks(const std::filesystem::path& prepared,const outloud::SapiVoiceRuntime& runtime,
    const std::filesystem::path& scratch) {
    using namespace outloud;
    namespace m=frontend::mutation;
    namespace fs=detail::library_fs;
    namespace audit=detail::removal_audit;
    VoiceLibrary library(scratch/L"removal-library"); VoiceImportOptions import;
    import.declared_model_compatibility_id=pocket_model_compatibility_id;
    const auto target=library.import_pocket_voice(prepared,"Remove test",import).voice;
    const auto peer=library.import_pocket_voice(prepared,"Remove test",import,DuplicateContentPolicy::allow_separate_entry).voice;
    auto registry=std::make_shared<FakeRegistry>(); registry->fixture_setup=true;
    SapiVoiceManager manager(library,runtime,registry,MachineVoiceLibraryPolicy::development_override);
    (void)manager.install(target.library_id); (void)manager.install(peer.library_id);
    registry->tokens[L"Microsoft_Unrelated"]=SapiTokenTree{};
    const auto tokens=registry->tokens; const auto writes=registry->writes, verifies=registry->verifies;
    const auto original=inventory(library.root()/L"voices");
    const m::Request request{{},target.metadata->display_name,m::Operation::remove,target.library_id,target.metadata->olv_sha256,"valid"};
    unsigned resolutions=0;
    const auto run=[&](const m::Request& value) {
        return m::import_and_install(value,library,registry,[&]() -> SapiVoiceRuntime { ++resolutions; throw std::runtime_error("Removal must not resolve runtime/engine registration."); },MachineVoiceLibraryPolicy::development_override);
    };
    const auto unchanged=[&] {
        check(inventory(library.root()/L"voices")==original && registry->tokens==tokens && registry->writes==writes,"Failed removal changed library/registration.");
    };
    {
        OutLoudVoice mapped(target.managed_olv_path);
        const auto result=run(request);
        check(result.outcome==m::Outcome::failed && result.error.code=="voice_in_use" && result.library_id==target.library_id,"Actual mapped voice removal did not report in-use before token mutation."); unchanged();
    }
    auto stale=request; stale.display_name="Changed snapshot name";
    auto result=run(stale); check(result.error.code=="removal_conflict","Stale remove name accepted."); unchanged();
    stale=request; stale.expected_hash=std::string(64,'0'); result=run(stale);
    check(result.error.code=="removal_conflict","Stale remove hash accepted."); unchanged();
    stale=request; stale.expected_health="corrupt_voice"; result=run(stale);
    check(result.error.code=="removal_conflict","Stale remove health accepted."); unchanged();
    auto changed_bytes=original.at(target.managed_olv_path); changed_bytes.back()^=1;
    { std::ofstream file(target.managed_olv_path,std::ios::binary); file.write(changed_bytes.data(),static_cast<std::streamsize>(changed_bytes.size())); }
    result=run(request); check(result.error.code=="removal_conflict" && registry->tokens==tokens,"Changed/corrupt asset was removed under a formerly healthy snapshot.");
    { const auto& bytes=original.at(target.managed_olv_path); std::ofstream file(target.managed_olv_path,std::ios::binary); file.write(bytes.data(),static_cast<std::streamsize>(bytes.size())); } unchanged();
    stale=request; stale.target_id=std::string(32,'0'); result=run(stale);
    check(result.error.code=="voice_not_found","Missing removal target accepted."); unchanged();
    registry->deny=true; result=run(request); registry->deny=false;
    check(result.outcome==m::Outcome::failed && result.error.code=="registry_access_denied","SAPI read failure lost or misreported as partial."); unchanged();
    registry->deny_exchange=true; result=run(request); registry->deny_exchange=false;
    check(result.outcome==m::Outcome::failed && result.error.code=="registry_io","Failed SAPI transaction lost structured cause."); unchanged();
    const auto token_key=managed_sapi_token_key(target.library_id);
    registry->tokens[token_key].values[L"OutLoudManagedOwner"]=sapi_registry_string(L"Unowned");
    const auto foreign=registry->tokens; result=run(request);
    check(result.error.code=="token_conflict" && registry->tokens==foreign && inventory(library.root()/L"voices")==original,"Unowned token or library deleted."); registry->tokens=tokens;
    registry->tokens[token_key].values[L"Unexpected"]=sapi_registry_string(L"Keep"); result=run(request);
    check(result.error.code=="malformed_token" && inventory(library.root()/L"voices")==original && registry->tokens[token_key].values.contains(L"Unexpected"),"Malformed token was removed."); registry->tokens=tokens;
    // A library failure before detach cannot change the token.
    audit::probe=[](unsigned step) { if (step==0) { throw std::runtime_error("Injected library detach failure."); } };
    result=run(request); audit::probe=nullptr;
    check(result.outcome==m::Outcome::failed && result.error.code=="internal_failure","Library failure outcome incorrect."); unchanged();
    // A successful registry removal with failed physical cleanup is partial,
    // already absent from enumeration; existing cleanup completes the trash.
    audit::probe=[](unsigned step) { if (step==3) { throw std::runtime_error("Injected trash cleanup failure."); } };
    result=run(request); audit::probe=nullptr;
    check(result.outcome==m::Outcome::removal_warning && result.error.code=="cleanup_pending" && library.list_voices().size()==1 &&
        library.list_voices()[0].library_id==peer.library_id && !registry->tokens.contains(token_key) && registry->tokens.at(managed_sapi_token_key(peer.library_id))==tokens.at(managed_sapi_token_key(peer.library_id)),"Cleanup warning does not represent actual removal state.");
    check(library.cleanup_pending().pending.empty(),"Removal trash cleanup failed.");
    // Fresh target exercises restoration failure after a registry failure.
    const auto rollback_target=library.import_pocket_voice(prepared,"Rollback test",import,DuplicateContentPolicy::allow_separate_entry).voice;
    (void)manager.install(rollback_target.library_id);
    const m::Request rollback_request{{},rollback_target.metadata->display_name,m::Operation::remove,rollback_target.library_id,rollback_target.metadata->olv_sha256,"valid"};
    const auto rollback_token=registry->tokens.at(managed_sapi_token_key(rollback_target.library_id));
    registry->deny_exchange=true;
    audit::probe=[](unsigned step) { if (step==2) { throw std::runtime_error("Injected directory restoration failure."); } };
    result=run(rollback_request); audit::probe=nullptr; registry->deny_exchange=false;
    check(result.outcome==m::Outcome::removal_warning && result.error.code=="removal_partial" && result.error.message.find("registry_io")!=std::string::npos &&
        library.list_voices().size()==1 && registry->tokens.at(managed_sapi_token_key(rollback_target.library_id))==rollback_token &&
        std::filesystem::exists(std::filesystem::path(fs::wide(result.error.path))/L"voice.olv"),"Failed restoration did not report retained trash and remaining token.");
    // Explicit existing reconciliation/cleanup, on the fake library only.
    (void)manager.remove(rollback_target.library_id); check(library.cleanup_pending().pending.empty(),"Partial removal reconciliation failed.");
    const m::Request peer_request{{},peer.metadata->display_name,m::Operation::remove,peer.library_id,peer.metadata->olv_sha256,"valid"};
    result=run(peer_request);
    check(result.outcome==m::Outcome::removed && library.list_voices().empty() && registry->tokens.size()==1 && registry->tokens.contains(L"Microsoft_Unrelated") &&
        registry->verifies==verifies+1 && resolutions==0,"Successful removal touched peers/runtime/shared engine or failed to remove target.");
    // Missing token is already absent: allow removal of the managed library row.
    const auto absent=library.import_pocket_voice(prepared,"Already unregistered",import).voice;
    result=run({{},absent.metadata->display_name,m::Operation::remove,absent.library_id,absent.metadata->olv_sha256,"valid"});
    check(result.outcome==m::Outcome::removed && library.list_voices().empty() && registry->tokens.size()==1,"Already-absent owned token prevented safe library removal.");
    const auto unhealthy=library.import_pocket_voice(prepared,"Missing metadata",import).voice;
    std::filesystem::remove(unhealthy.managed_olv_path.parent_path()/L"metadata.json");
    result=run({{},"",m::Operation::remove,unhealthy.library_id,"","metadata_corrupt"});
    check(result.outcome==m::Outcome::removed && library.list_voices().empty() && registry->tokens.size()==1,"Confirmed metadata-unavailable managed entry could not be removed safely.");
    // Removal frames contain ID/name/hash/health only. No path/root/hive fields.
    std::vector<std::uint8_t> frame;
    const auto number=[&](std::uint32_t value) { const auto* bytes=reinterpret_cast<const std::uint8_t*>(&value); frame.insert(frame.end(),bytes,bytes+4); };
    const auto text=[&](std::string_view value) { number(static_cast<std::uint32_t>(value.size())); frame.insert(frame.end(),value.begin(),value.end()); };
    number(m::magic); number(m::version); number(3); number(0); text(request.target_id); text(request.display_name); text(request.expected_hash); text(request.expected_health);
    const auto length=[&] { const auto count=static_cast<std::uint32_t>(frame.size()-16); std::memcpy(frame.data()+12,&count,4); }; length();
    const auto decoded=m::decode_request(frame); check(decoded.kind==m::Operation::remove && decoded.source.empty() && decoded.target_id==request.target_id,"Remove protocol lost immutable identity or inferred path.");
    const auto rejects=[](const std::vector<std::uint8_t>& bytes) { try { (void)m::decode_request(bytes); return false; } catch (...) { return true; } };
    auto malformed=frame; malformed[20]='G'; check(rejects(malformed),"Malformed remove ID accepted.");
    malformed=frame; malformed[malformed.size()-request.expected_health.size()-5]='g'; check(rejects(malformed),"Malformed remove hash accepted.");
    malformed=frame; malformed[8]=99; check(rejects(malformed),"Unknown remove operation accepted.");
    malformed=frame; malformed[4]=2; check(rejects(malformed),"Legacy protocol version accepted.");
    text("C:\\arbitrary-root"); length(); check(rejects(frame),"Remove request accepted arbitrary authority field.");
}
