#pragma once
#include "voice_refresh_audit.hpp"
#include <thread>

// Only the existing isolated checker: deterministic counts instead of short timings.
void refresh_checks(const std::filesystem::path& olv,const std::filesystem::path& prepared,
        const outloud::SapiVoiceRuntime& runtime,const std::filesystem::path& scratch) {
    using namespace outloud;
    namespace fs=detail::library_fs; namespace audit=detail::refresh_audit;
    VoiceLibrary library(scratch/L"refresh-library"); auto registry=std::make_shared<FakeRegistry>();
    const auto first=library.import_olv(olv,"Refresh A").voice;
    const auto second=library.import_olv(olv,"Refresh B",DuplicateContentPolicy::allow_separate_entry).voice;
    const auto third=library.import_olv(olv,"Refresh C",DuplicateContentPolicy::allow_separate_entry).voice;
    SapiVoiceManager setup(library,runtime,registry,MachineVoiceLibraryPolicy::development_override);
    registry->fixture_setup=true; (void)setup.install(first.library_id); registry->fixture_setup=false;
    const auto prototype=registry->tokens.at(managed_sapi_token_key(first.library_id));
    for (const auto& voice:{second,third}) {
        auto token=prototype; token.values[L"LibraryId"]=sapi_registry_string(fs::wide(voice.library_id));
        token.values[L"VoicePath"]=sapi_registry_string(voice.managed_olv_path.wstring());
        const auto name=fs::wide(voice.metadata->display_name);
        token.values[L""]=sapi_registry_string(name); token.values[L"409"]=sapi_registry_string(name);
        token.children[L"Attributes"].values[L"Name"]=sapi_registry_string(name);
        registry->tokens[managed_sapi_token_key(voice.library_id)]=std::move(token);
    }
    SapiVoiceManager manager(library,{},registry,MachineVoiceLibraryPolicy::development_override);
    const auto baseline=[&] {
        audit::legacy_enumeration=true;
        struct Reset { ~Reset() { audit::legacy_enumeration=false; } } reset;
        std::vector<SapiVoiceSnapshotRow> rows;
        for (auto& voice:library.list_voices()) {
            SapiVoiceSnapshotRow row; row.token=manager.inspect(voice.library_id); row.voice=std::move(voice); rows.push_back(std::move(row));
        }
        return rows;
    };
    const auto compare=[&](const auto& expected,const Result& actual) {
        const auto* snapshot=outloud_vm_result_snapshot(actual.get());
        check(snapshot && snapshot->voice_count==expected.size(),"Refresh row count changed");
        for (std::size_t at=0;at<expected.size();++at) {
            const auto& old=expected[at]; const auto& next=snapshot->voices[at];
            check(text(next.library_id,old.voice.library_id) && text(next.health,voice_health_name(old.voice.health)) &&
                text(next.library_detail,old.voice.detail) && text(next.sapi_status,sapi_token_status_name(old.token->status)) &&
                text(next.sapi_detail,old.token->detail) && !next.sapi_error,"Refresh ordering/health/status/detail changed");
            check(next.metadata_present==static_cast<uint32_t>(old.voice.metadata.has_value()),"Refresh metadata presence changed");
            if (old.voice.metadata) {
                const auto& m=*old.voice.metadata; const auto* content=outloud_vm_result_content(actual.get(),at);
                check(text(next.display_name,m.display_name) && next.olv_version==m.olv_version &&
                    text(next.compatibility_id,m.compatibility_id) && next.reserved_metadata==0 &&
                    text(next.import_kind,m.import_kind==VoiceImportKind::olv ? "olv" : "pocket_prepared") &&
                    content && text(content->olv_sha256,m.olv_sha256) && text(content->source_sha256,m.source_sha256) &&
                    text(content->source_filename,m.source_filename) && text(content->imported_utc,m.imported_utc),"Refresh metadata/content changed");
            } else { check(!next.display_name && !next.compatibility_id && !next.import_kind,"Refresh invented metadata"); }
        }
    };
    audit::counts={}; registry->reads=0; const auto original=baseline(); const auto before=audit::counts;
    check(before.metadata==6 && before.olv_mapping==6 && before.olv_validation==6 &&
        before.replacement_scan==6 && before.removal_scan==6 && registry->reads==3,"Pre-A06 work count reproduction differs");
    audit::counts={}; registry->reads=0; const auto unchanged=read(library,registry); const auto after=audit::counts;
    compare(original,unchanged);
    check(after.metadata==3 && after.olv_mapping==3 && after.olv_validation==3 &&
        after.replacement_scan==1 && after.removal_scan==1 && registry->reads==3,"Refresh did not reduce duplicate validation/scans");
    audit::counts={}; (void)read(library,registry);
    check(audit::counts.metadata==3 && audit::counts.olv_validation==3,"Trusted validation survived across refreshes");

    // A deterministic overlap at token-read time: a separate thread attempts every
    // supported mutation/recovery class, all under the still-held read lock.
    unsigned busy=0; bool overlapped=false;
    registry->before_read=[&](std::wstring_view) {
        if (overlapped) { return; } overlapped=true;
        std::thread writer([&] {
            const auto refused=[&](const auto& operation) {
                try { operation(); } catch (const VoiceLibraryError& error) { if (error.code()==VoiceLibraryErrorCode::library_busy) { ++busy; } }
            };
            VoiceImportOptions import; import.declared_model_compatibility_id=pocket_model_compatibility_id;
            VoiceReplaceOptions replacement; replacement.import=import; replacement.expected_display_name=first.metadata->display_name;
            replacement.expected_olv_sha256=first.metadata->olv_sha256;
            refused([&] { (void)library.import_pocket_voice(prepared,"Concurrent",import); });
            refused([&] { (void)library.replace_pocket_voice(first.library_id,prepared,replacement); });
            refused([&] { (void)library.remove_voice(first.library_id); });
            refused([&] { (void)library.rename_voice(first.library_id,"Concurrent rename"); });
            refused([&] { (void)library.recover_removal(first.library_id); });
            refused([&] { (void)library.cleanup_pending(); });
        }); writer.join();
    };
    const auto files=inventory(library.root()); const auto tokens=registry->tokens;
    const auto coherent=read(library,registry); registry->before_read={}; compare(original,coherent);
    check(overlapped && busy==6 && inventory(library.root())==files && registry->tokens==tokens,"Concurrent mutation split refresh epoch");
    {
        auto lock=fs::file(library.root()/L".library.lock",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE);
        OVERLAPPED offset{}; check(LockFileEx(lock.value,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&offset)!=0,"Cannot acquire writer fixture lock");
        const auto blocked=read(library,registry,OUTLOUD_VM_ERROR);
        check(text(outloud_vm_result_error(blocked.get())->code,"library_busy"),"Busy refresh substituted stale success");
        check(UnlockFileEx(lock.value,0,1,0,&offset)!=0,"Cannot release writer fixture lock");
    }
    bool changed=false;
    registry->before_read=[&](std::wstring_view key) {
        if (!changed && key==managed_sapi_token_key(first.library_id)) {
            changed=true; registry->tokens[std::wstring(key)].values[L"OutLoudManagedOwner"]=sapi_registry_string(L"foreign");
        }
    };
    check(text(row(read(library,registry),first.library_id).sapi_status,"conflict"),"Token change reused stale status");
    registry->before_read={}; registry->tokens=tokens;
    (void)library.rename_voice(first.library_id,"Changed display name");
    check(text(row(read(library,registry),first.library_id).sapi_status,"stale_name") &&
        text(row(unchanged,first.library_id).display_name,"Refresh A"),"Stale names/result lifetime changed");
    (void)library.rename_voice(first.library_id,"Refresh A");

    const auto path=second.managed_olv_path; const auto metadata=path.parent_path()/L"metadata.json";
    const auto saved=inventory(path.parent_path());
    const auto write=[&](const std::filesystem::path& target,const std::string& bytes) {
        std::ofstream output(target,std::ios::binary|std::ios::trunc); output.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
        check(bool(output),"Cannot write private refresh fixture");
    };
    const auto health=[&](VoiceHealth expected) {
        const auto old=baseline(); const auto refreshed=read(library,registry); compare(old,refreshed);
        check(text(row(refreshed,second.library_id).health,voice_health_name(expected)) &&
            text(row(refreshed,second.library_id).sapi_status,"unhealthy_voice"),"Unhealthy refresh became successful");
    };
    write(metadata,"invalid JSON"); health(VoiceHealth::metadata_corrupt); write(metadata,saved.at(metadata));
    check(DeleteFileW(path.c_str())!=0,"Cannot create missing voice fixture"); health(VoiceHealth::missing_voice); write(path,saved.at(path));
    auto corrupt=saved.at(path); corrupt.back()^=1; write(path,corrupt); health(VoiceHealth::corrupt_voice);
    audit::counts={}; registry->fixture_setup=true;
    try { (void)setup.install(second.library_id); check(false,"Privileged install trusted old refresh"); }
    catch (const SapiVoiceError& error) { check(error.code==SapiVoiceErrorCode::unhealthy_voice,"Privileged install lost fresh failure"); }
    registry->fixture_setup=false;
    check(audit::counts.metadata==1 && audit::counts.olv_validation==1 && registry->tokens==tokens,"Privileged install skipped authoritative validation");
    VoiceReplaceOptions replacement; replacement.import.declared_model_compatibility_id=pocket_model_compatibility_id;
    replacement.expected_display_name=second.metadata->display_name; replacement.expected_olv_sha256=second.metadata->olv_sha256;
    audit::counts={};
    try { (void)library.replace_pocket_voice(second.library_id,prepared,replacement); check(false,"Replace trusted old refresh"); }
    catch (const VoiceLibraryError& error) { check(error.code()==VoiceLibraryErrorCode::corrupt_voice,"Replace fresh health failure changed"); }
    check(audit::counts.olv_validation==1,"Replace skipped fresh OLV validation");
    VoiceRemoveOptions removal; removal.expected_display_name=second.metadata->display_name;
    removal.expected_olv_sha256=second.metadata->olv_sha256; removal.expected_health="valid";
    audit::counts={};
    try { (void)library.remove_voice(second.library_id,removal); check(false,"Remove trusted old refresh"); }
    catch (const VoiceLibraryError& error) { check(error.code()==VoiceLibraryErrorCode::removal_conflict,"Remove ignored fresh health confirmation"); }
    check(audit::counts.olv_validation==1,"Remove skipped fresh OLV validation");
    write(path,saved.at(path));
    auto nonfinite=saved.at(path); nonfinite[256]=0; nonfinite[257]=0; nonfinite[258]=static_cast<char>(0x80); nonfinite[259]=static_cast<char>(0x7f);
    auto bytes=detail::Bytes{reinterpret_cast<const unsigned char*>(nonfinite.data()),nonfinite.size()};
    const auto payload=detail::sha256(bytes.subspan(256)); std::memcpy(nonfinite.data()+128,payload.data(),32);
    const auto integrity=detail::asset_integrity(bytes); std::memcpy(nonfinite.data()+96,integrity.data(),32);
    write(path,nonfinite); health(VoiceHealth::corrupt_voice);
    check(std::string_view(row(read(library,registry),second.library_id).library_detail).find("Non-finite")!=std::string_view::npos,"Finite-value validation was skipped");
    write(path,saved.at(path));
    {
        auto locked=fs::file(path,GENERIC_READ,0); health(VoiceHealth::unavailable);
    }
    const auto alias=scratch/L"refresh-hardlink.olv";
    check(CreateHardLinkW(alias.c_str(),path.c_str(),nullptr)!=0,"Cannot create private hardlink fixture"); health(VoiceHealth::unsafe_entry);
    check(DeleteFileW(alias.c_str())!=0,"Cannot remove private hardlink alias");

    // Genuine journal publication interruptions, with original data still active.
    std::ifstream source(prepared,std::ios::binary); std::vector<unsigned char> prepared_bytes(std::istreambuf_iterator<char>(source),{});
    const auto header_size=static_cast<std::size_t>(detail::u64(prepared_bytes,0));
    const auto header=detail::voice_json::Parser({reinterpret_cast<const char*>(prepared_bytes.data()+8),header_size}).parse();
    std::uint64_t cache=0; check(detail::voice_json::unsigned_integer(header.object.at("transformer.layers.0.self_attn/cache").object.at("data_offsets").array.at(0),cache),"Invalid private cache fixture");
    const auto offset=header_size+8+static_cast<std::size_t>(cache); const auto value=detail::u32(prepared_bytes,offset)==0x3f800000U ? 0x40000000U : 0x3f800000U;
    for (unsigned i=0;i<4;++i) { prepared_bytes[offset+i]=static_cast<unsigned char>(value>>(8*i)); }
    const auto new_source=scratch/L"refresh-replacement.safetensors";
    write(new_source,{reinterpret_cast<const char*>(prepared_bytes.data()),prepared_bytes.size()});
    detail::replacement_audit::leave_pending=true;
    detail::replacement_audit::probe=[](unsigned step,bool rollback) { if (!rollback && step==0) { throw std::runtime_error("Prepared replacement interruption"); } };
    try { (void)library.replace_pocket_voice(second.library_id,new_source,replacement); check(false,"Replacement interruption missing"); }
    catch (const VoiceLibraryError& error) { check(error.code()==VoiceLibraryErrorCode::replacement_pending,"Replacement journal not retained"); }
    detail::replacement_audit::probe=nullptr; detail::replacement_audit::leave_pending=false;
    detail::removal_audit::leave_pending=true;
    detail::removal_audit::probe=[](unsigned step) { if (step==5) { throw std::runtime_error("Prepared removal interruption"); } };
    const frontend::mutation::Request request{{},third.metadata->display_name,frontend::mutation::Operation::remove,
        third.library_id,third.metadata->olv_sha256,"valid"};
    registry->fixture_setup=true;
    const auto pending=frontend::mutation::import_and_install(request,library,registry,[] { return SapiVoiceRuntime{}; },MachineVoiceLibraryPolicy::development_override);
    registry->fixture_setup=false; detail::removal_audit::probe=nullptr; detail::removal_audit::leave_pending=false;
    check(pending.outcome==frontend::mutation::Outcome::removal_warning,"Removal journal not retained");
    const auto evidence=inventory(library.root());
    audit::counts={}; const auto old_pending=baseline(); const auto old_counts=audit::counts;
    audit::counts={}; const auto snapshot=read(library,registry); const auto new_counts=audit::counts; compare(old_pending,snapshot);
    check(text(row(snapshot,second.library_id).health,"replacement_pending") && text(row(snapshot,third.library_id).health,"removal_pending"),"Pending health visibility changed");
    check(old_counts.replacement_journal==6 && old_counts.removal_journal==6 &&
        new_counts.replacement_journal==1 && new_counts.removal_journal==1 &&
        new_counts.olv_validation==3 && inventory(library.root())==evidence,"Pending index repeated parsing or performed recovery during refresh");
    const auto cleanup=library.cleanup_pending([&](std::string_view id) { return manager.removal_registration_identity(id); });
    check(cleanup.pending.empty() && inventory(path.parent_path())==saved && registry->tokens==tokens,"Snapshot changed replacement/removal recovery guarantees");
    compare(original,read(library,registry));
    std::cout << "PASS: A06 three-voice refresh: metadata/OLV validation 6 -> 3; pending scans 6 -> 1 each; journal parses 6 -> 1 each; fresh token reads 3 -> 3. No timing speedup claimed.\n";
}
