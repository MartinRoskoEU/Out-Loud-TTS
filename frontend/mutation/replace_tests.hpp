#pragma once
#include "replacement_audit.hpp"
#include "voice_json.hpp"

void replacement_checks(const std::filesystem::path& prepared,const outloud::SapiVoiceRuntime& runtime,const std::filesystem::path& scratch) {
    using namespace outloud; namespace fs=detail::library_fs; namespace m=frontend::mutation; namespace audit=detail::replacement_audit;
    const auto original_source=scratch/L"original-prepared.safetensors", new_source=scratch/L"replacement-\u017e.safetensors";
    std::filesystem::copy_file(prepared,original_source);
    std::ifstream input(prepared,std::ios::binary); std::vector<unsigned char> content(std::istreambuf_iterator<char>(input),{});
    const auto header_size=static_cast<std::size_t>(detail::u64(content,0));
    const auto header=detail::voice_json::Parser({reinterpret_cast<const char*>(content.data()+8),header_size}).parse();
    std::uint64_t cache_offset=0;
    check(detail::voice_json::unsigned_integer(header.object.at("transformer.layers.0.self_attn/cache").object.at("data_offsets").array.at(0),cache_offset),"Invalid cache fixture offset.");
    const auto data=header_size+8+static_cast<std::size_t>(cache_offset); check(data+4<=content.size(),"Invalid prepared fixture bounds.");
    const auto bits=detail::u32(content,data)==0x3f800000U ? 0x40000000U : 0x3f800000U;
    for (unsigned i=0;i<4;++i) { content[data+i]=static_cast<unsigned char>(bits>>(i*8)); }
    { std::ofstream output(new_source,std::ios::binary); output.write(reinterpret_cast<const char*>(content.data()),static_cast<std::streamsize>(content.size())); }
    VoiceLibrary library(scratch/L"replace-library"); VoiceImportOptions import; import.declared_model_compatibility_id=pocket_model_compatibility_id;
    const auto original=library.import_pocket_voice(original_source,"Undead Female",import).voice;
    const auto directory=fs::directory(original.managed_olv_path.parent_path()); FILE_ID_INFO before{};
    check(GetFileInformationByHandleEx(directory.value,FileIdInfo,&before,sizeof(before))!=0,"Cannot identify original directory.");
    auto registry=std::make_shared<FakeRegistry>(); registry->fixture_setup=true; SapiVoiceManager manager(library,runtime,registry,MachineVoiceLibraryPolicy::development_override);
    (void)manager.install(original.library_id);
    const auto& registered=registry->tokens.at(managed_sapi_token_key(original.library_id));
    check(sapi_registry_text(registered.values.at(L""))==L"Undead Female" && sapi_registry_text(registered.values.at(L"409"))==L"Undead Female" &&
        sapi_registry_text(registered.children.at(L"Attributes").values.at(L"Name"))==L"Undead Female" &&
        sapi_registry_text(registered.children.at(L"Attributes").values.at(L"Vendor"))==L"Out Loud","Replacement fixture exact SAPI names/vendor differ.");
    const auto tokens=registry->tokens; const auto writes=registry->writes;
    const auto pair_before=inventory(library.root()/L"voices");
    VoiceReplaceOptions options; options.import=import; options.expected_display_name=original.metadata->display_name; options.expected_olv_sha256=original.metadata->olv_sha256;
    options.validate_before_publish=[&](const LibraryVoice& target,const std::filesystem::path& staged) { manager.validate_replacement(target,staged); };
    const auto rejected=[&](VoiceLibraryErrorCode code,const std::function<void()>& action) {
        try { action(); } catch (const VoiceLibraryError& error) {
            if (error.code()!=code) { throw std::runtime_error(std::string("Expected ")+std::string(voice_library_error_name(code))+"; got "+std::string(voice_library_error_name(error.code()))+": "+error.what()+" [Win32 "+std::to_string(error.system_error())+"]"); }
            check(true,"Native replace rejection."); return;
        } throw std::runtime_error("Expected replacement rejection.");
    };
    const auto invalid=scratch/L"invalid-replacement.safetensors"; { std::ofstream file(invalid); file << "invalid safetensors"; }
    rejected(VoiceLibraryErrorCode::import_failed,[&] { (void)library.replace_pocket_voice(original.library_id,invalid,options); });
    check(inventory(library.root()/L"voices")==pair_before,"Invalid source changed original bytes.");
    // Model declarations are not proof and do not block valid external caches.
    auto stale=options; stale.expected_display_name="Wrong name";
    rejected(VoiceLibraryErrorCode::replacement_conflict,[&] { (void)library.replace_pocket_voice(original.library_id,new_source,stale); });
    stale=options; stale.expected_olv_sha256=std::string(64,'0');
    rejected(VoiceLibraryErrorCode::replacement_conflict,[&] { (void)library.replace_pocket_voice(original.library_id,new_source,stale); });
    rejected(VoiceLibraryErrorCode::voice_not_found,[&] { (void)library.replace_pocket_voice(std::string(32,'0'),new_source,options); });
    const auto identical=library.replace_pocket_voice(original.library_id,original_source,options);
    check(!identical.changed && inventory(library.root()/L"voices")==pair_before,"Identical replacement changed original metadata/content.");
    {
        OutLoudVoice active(original.managed_olv_path); // actual production mapping, not a fake lock
        rejected(VoiceLibraryErrorCode::voice_in_use,[&] { (void)library.replace_pocket_voice(original.library_id,new_source,options); });
        check(inventory(library.root()/L"voices")==pair_before && active.source_sha256()==original.metadata->source_sha256,"Active mapping refusal changed original.");
    }
    registry->deny=true;
    try { (void)library.replace_pocket_voice(original.library_id,new_source,options); check(false,"SAPI validation failure accepted."); }
    catch (const SapiVoiceError& error) { check(error.code==SapiVoiceErrorCode::registry_access_denied,"SAPI structured preflight error lost."); }
    registry->deny=false; check(inventory(library.root()/L"voices")==pair_before,"Preflight failure changed original.");
    // Inject after the first and second live file rename; immediate rollback
    // must restore both original files exactly, without changing directory ID.
    for (unsigned step : {1U,2U}) {
        static unsigned fault_step=0; fault_step=step;
        audit::probe=[](unsigned at,bool rollback) { if (!rollback && at==fault_step) { throw std::runtime_error("Injected publication failure."); } };
        rejected(VoiceLibraryErrorCode::internal_failure,[&] { (void)library.replace_pocket_voice(original.library_id,new_source,options); });
        audit::probe=nullptr; check(inventory(library.root()/L"voices")==pair_before,"Publication rollback did not restore exact original pair.");
    }
    audit::probe=[](unsigned at,bool rollback) { if (!rollback && at==1) { throw std::runtime_error("Injected interruption."); } }; audit::leave_pending=true;
    rejected(VoiceLibraryErrorCode::replacement_pending,[&] { (void)library.replace_pocket_voice(original.library_id,new_source,options); });
    audit::probe=nullptr; audit::leave_pending=false;
    check(library.inspect_voice(original.library_id).health==VoiceHealth::replacement_pending,"Interrupted pair presented as healthy.");
    rejected(VoiceLibraryErrorCode::replacement_pending,[&] { (void)library.rename_voice(original.library_id,"Changed"); });
    const auto recovery=library.cleanup_pending(); check(recovery.pending.empty() && recovery.cleaned>=1 && inventory(library.root()/L"voices")==pair_before,"Journal recovery did not restore exact original pair.");
    // Recovery itself can fail halfway; a second explicit cleanup is idempotent.
    audit::probe=[](unsigned at,bool rollback) { if ((!rollback && at==2) || (rollback && at==4)) { throw std::runtime_error("Injected rollback failure."); } };
    rejected(VoiceLibraryErrorCode::replacement_pending,[&] { (void)library.replace_pocket_voice(original.library_id,new_source,options); });
    audit::probe=nullptr; check(library.cleanup_pending().pending.empty() && inventory(library.root()/L"voices")==pair_before,"Repeated rollback recovery lost originals.");
    const auto replacement=library.replace_pocket_voice(original.library_id,new_source,options);
    FILE_ID_INFO after{}; check(GetFileInformationByHandleEx(directory.value,FileIdInfo,&after,sizeof(after))!=0,"Cannot identify preserved directory.");
    check(replacement.changed && !replacement.cleanup_pending && replacement.voice.library_id==original.library_id && replacement.voice.metadata->display_name==original.metadata->display_name &&
        replacement.voice.managed_olv_path==original.managed_olv_path && library.list_voices().size()==1 && std::memcmp(&before,&after,sizeof(before))==0,"Replacement did not preserve directory/path/name/ID/count.");
    check(replacement.voice.metadata->olv_sha256!=original.metadata->olv_sha256 && replacement.voice.metadata->source_sha256!=original.metadata->source_sha256 &&
        replacement.voice.metadata->source_filename==fs::utf8(new_source.filename().wstring()) && replacement.voice.metadata->import_kind==VoiceImportKind::pocket_prepared && library.inspect_voice(original.library_id).health==VoiceHealth::valid,"Replacement content metadata/health incorrect.");
    check(registry->tokens==tokens && registry->writes==writes && manager.inspect(original.library_id).status==SapiTokenStatus::correct,"Replacement rewrote or changed SAPI identity/configuration.");
    // Same content at a different ID remains protected.
    const auto second=library.import_pocket_voice(original_source,"Other",import).voice;
    options.expected_olv_sha256=replacement.voice.metadata->olv_sha256;
    rejected(VoiceLibraryErrorCode::duplicate_content,[&] { (void)library.replace_pocket_voice(original.library_id,original_source,options); });
    check(library.list_voices().size()==2 && library.inspect_voice(second.library_id).health==VoiceHealth::valid,"Duplicate replacement changed another entry.");
    // Protocol carries only target/name/hash/source and rejects appended authority fields.
    std::vector<std::uint8_t> request;
    const auto number=[&](std::uint32_t value) { const auto* bytes=reinterpret_cast<const std::uint8_t*>(&value); request.insert(request.end(),bytes,bytes+4); };
    const auto text=[&](const std::string& value) { number(static_cast<std::uint32_t>(value.size())); request.insert(request.end(),value.begin(),value.end()); };
    number(m::magic); number(m::version); number(2); number(0); text(fs::utf8(new_source.wstring())); text("Undead Female"); text(original.library_id); text(replacement.voice.metadata->olv_sha256);
    const auto frame=[&] { const auto length=static_cast<std::uint32_t>(request.size()-16); std::memcpy(request.data()+12,&length,4); }; frame();
    const auto decoded=m::decode_request(request); check(decoded.kind==m::Operation::replace && decoded.target_id==original.library_id,"Replace protocol identity lost.");
    const auto unchanged=m::import_and_install(decoded,library,registry,[&] { return runtime; },MachineVoiceLibraryPolicy::development_override);
    check(unchanged.outcome==m::Outcome::unchanged && registry->writes==writes,"Helper unchanged replace rewrote token.");
    auto malformed=request; malformed.back()='g'; try { (void)m::decode_request(malformed); check(false,"Malformed replace hash accepted."); } catch (const std::invalid_argument&) { check(true,"Invalid hash rejected."); }
    text("C:\\arbitrary-root"); frame(); try { (void)m::decode_request(request); check(false,"Replace authority field accepted."); } catch (const std::invalid_argument&) { check(true,"Extra fields rejected."); }
    const auto third_source=scratch/L"third-replacement.safetensors";
    for (unsigned i=0;i<4;++i) { content[data+i]=static_cast<unsigned char>(0x40400000U>>(i*8)); }
    { std::ofstream output(third_source,std::ios::binary); output.write(reinterpret_cast<const char*>(content.data()),static_cast<std::streamsize>(content.size())); }
    audit::probe=[](unsigned at,bool rollback) { if (!rollback && at==5) { throw std::runtime_error("Injected post-commit cleanup failure."); } };
    const auto cleanup_warning=library.replace_pocket_voice(original.library_id,third_source,options); audit::probe=nullptr;
    check(cleanup_warning.changed && cleanup_warning.cleanup_pending && library.inspect_voice(original.library_id).health==VoiceHealth::valid,"Post-commit cleanup failure misreported published state.");
    check(library.cleanup_pending().pending.empty() && registry->tokens==tokens,"Committed journal cleanup changed tokens or failed.");
}
