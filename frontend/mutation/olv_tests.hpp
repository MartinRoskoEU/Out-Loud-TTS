#pragma once
// Existing bridge checker, private libraries/fake registry only.
void olv_checks(const std::filesystem::path& source,const std::filesystem::path& scratch,
                const outloud::SapiVoiceRuntime& runtime) {
    using namespace outloud;
    namespace m=frontend::mutation;
    namespace fs=detail::library_fs;
    VoiceLibrary library(scratch/L"olv-import-export"), roundtrip(scratch/L"olv-roundtrip");
    auto registry=std::make_shared<FakeRegistry>(); registry->fixture_setup=true;
    const auto resolve=[&] { return runtime; };
    m::Request request{source,"OLV Hlas \xc5\xbe",m::Operation::import_olv};
    const auto imported=m::import_and_install(request,library,registry,resolve,MachineVoiceLibraryPolicy::development_override);
    check(imported.outcome==m::Outcome::success && registry->writes==1,"OLV import/install failed");
    const auto voice=library.inspect_voice(imported.library_id);
    check(voice.health==VoiceHealth::valid && voice.metadata->import_kind==VoiceImportKind::olv,"OLV metadata/compatibility invalid");
    const auto original_files=inventory(library.root());
    const auto original_tokens=registry->tokens;
    const auto output=scratch/L"hlas-\u017e-\U0001f3b5.olv";
    outloud_vm_result* raw=nullptr;
    check(frontend::export_injected(library,voice.library_id,voice.metadata->display_name,voice.metadata->olv_sha256,output,"absent",&raw)==OUTLOUD_VM_OK,"Bridge export failed");
    Result result(raw,outloud_vm_result_free);
    check(!outloud_vm_result_error(raw) && !outloud_vm_result_snapshot(raw),"Export returned a fabricated snapshot/error");
    check(validate_olv(output).file_sha256==voice.metadata->olv_sha256,"Roundtrip export bytes differ");
    const auto again=roundtrip.import_olv(output,"Roundtrip").voice;
    check(again.health==VoiceHealth::valid && again.metadata->olv_sha256==voice.metadata->olv_sha256,"Reimport not compatible/exact");
    check(inventory(library.root())==original_files && registry->tokens==original_tokens,"Export mutated source or token");
    const auto empty_destination=scratch/L"empty-destination.olv"; { std::ofstream file(empty_destination); }
    outloud_vm_result* inspected=nullptr;
    const auto destination_utf8=fs::utf8(empty_destination.wstring());
    check(outloud_vm_export_destination(destination_utf8.c_str(),&inspected)==OUTLOUD_VM_OK,"Empty overwrite target cannot be inspected");
    Result inspected_result(inspected,outloud_vm_result_free);
    check(outloud_vm_result_destination(inspected) && std::strlen(outloud_vm_result_destination(inspected))==64 && !outloud_vm_result_snapshot(inspected),"Destination ABI result invalid");
    VoiceExportOptions empty_options; empty_options.replace_existing=true;
    empty_options.expected_destination=outloud_vm_result_destination(inspected);
    (void)library.export_olv(voice.library_id,empty_destination,empty_options);
    check(validate_olv(empty_destination).file_sha256==voice.metadata->olv_sha256,"Empty confirmed overwrite is not exact");
    auto duplicate=request; duplicate.display_name="Another name";
    const auto dupe=m::import_and_install(duplicate,library,registry,resolve,MachineVoiceLibraryPolicy::development_override);
    check(dupe.outcome==m::Outcome::failed && dupe.error.code=="duplicate_content" && dupe.error.duplicate_ids==std::vector<std::string>{voice.library_id},"OLV duplicate IDs lost");
    auto replace=request; replace.kind=m::Operation::replace_olv; replace.target_id=voice.library_id; replace.expected_hash=voice.metadata->olv_sha256;
    const auto same=m::import_and_install(replace,library,registry,resolve,MachineVoiceLibraryPolicy::development_override);
    check(same.outcome==m::Outcome::unchanged && same.library_id==voice.library_id && registry->tokens==original_tokens,"Identical OLV replacement changed identity");
    replace.expected_hash=std::string(64,'0');
    const auto stale=m::import_and_install(replace,library,registry,resolve,MachineVoiceLibraryPolicy::development_override);
    check(stale.error.code=="replacement_conflict" && inventory(library.root())==original_files,"Stale OLV replacement published");
    auto failed_export=[&](std::string_view hash,std::string_view identity,const char* code) {
        outloud_vm_result* value=nullptr;
        check(frontend::export_injected(library,voice.library_id,voice.metadata->display_name,hash,output,identity,&value)==OUTLOUD_VM_ERROR,"Invalid export succeeded");
        Result owned(value,outloud_vm_result_free); check(text(outloud_vm_result_error(value)->code,code),"Export structured cause lost");
    };
    failed_export(std::string(64,'0'),"absent","export_conflict");
    failed_export(voice.metadata->olv_sha256,"absent","export_conflict");
    check(SetFileAttributesW(output.c_str(),FILE_ATTRIBUTE_READONLY)!=0,"Cannot create read-only export fixture");
    failed_export(voice.metadata->olv_sha256,"absent","destination_io");
    check(SetFileAttributesW(output.c_str(),FILE_ATTRIBUTE_NORMAL)!=0,"Cannot restore export fixture attributes");
    const auto identity=inspect_olv_export_destination(output);
    { std::ofstream changed(output,std::ios::binary|std::ios::trunc); changed<<"different destination"; }
    const auto preserved=inventory(scratch).at(output);
    failed_export(voice.metadata->olv_sha256,identity,"export_conflict");
    check(inventory(scratch).at(output)==preserved,"Changed destination overwritten");
    VoiceExportOptions options; options.expected_display_name=voice.metadata->display_name; options.expected_olv_sha256=voice.metadata->olv_sha256;
    options.expected_destination=inspect_olv_export_destination(output); options.replace_existing=true;
    (void)library.export_olv(voice.library_id,output,options);
    check(validate_olv(output).file_sha256==voice.metadata->olv_sha256,"Confirmed overwrite failed");
    // Inject a partial disk write and a pre-publication failure using test-only probes.
    const auto before_failure=inventory(scratch);
    for (unsigned step : {0u,1u,2u,3u}) {
        options.expected_destination=inspect_olv_export_destination(output);
        static unsigned fail_at; fail_at=step;
        detail::export_audit::probe=[](unsigned at,void* handle) {
            if (at!=fail_at) return;
            if (at==0) { DWORD written=0; const char bytes[]="partial"; WriteFile(handle,bytes,7,&written,nullptr); }
            throw VoiceLibraryError(VoiceLibraryErrorCode::destination_io,"Injected failed/partial export",{},ERROR_DISK_FULL);
        };
        try { (void)library.export_olv(voice.library_id,output,options); check(false,"Export fault not exercised"); }
        catch (const VoiceLibraryError& error) { check(error.system_error()==ERROR_DISK_FULL,"Export error replaced"); }
        detail::export_audit::probe=nullptr;
        check(inventory(scratch)==before_failure,"Failed export changed existing files or left a temporary");
    }
    // Operation extensions retain v3 framing and reject crossed source formats.
    auto wire=[&](std::uint32_t op,const std::filesystem::path& path,bool replacement) {
        std::vector<std::uint8_t> bytes;
        auto number=[&](std::uint32_t n) { const auto* v=reinterpret_cast<const std::uint8_t*>(&n); bytes.insert(bytes.end(),v,v+4); };
        auto string=[&](const std::string& s) { number(static_cast<std::uint32_t>(s.size())); bytes.insert(bytes.end(),s.begin(),s.end()); };
        number(m::magic); number(m::version); number(op); number(0); string(fs::utf8(path.wstring())); string(request.display_name);
        if (replacement) { string(voice.library_id); string(voice.metadata->olv_sha256); }
        const auto size=static_cast<std::uint32_t>(bytes.size()-16); std::memcpy(bytes.data()+12,&size,4); return bytes;
    };
    check(m::decode_request(wire(4,source,false)).kind==m::Operation::import_olv && m::decode_request(wire(5,source,true)).target_id==voice.library_id,"OLV operation framing lost");
    for (const auto op : {1u,2u,6u}) {
        try { (void)m::decode_request(wire(op,source,op==2)); check(false,"Cross-format/unknown wire operation accepted"); }
        catch (const std::invalid_argument&) { check(true,"Invalid OLV operation rejected"); }
    }
    const auto corrupt=scratch/L"invalid.olv"; { std::ofstream file(corrupt); file<<"bad"; }
    request.source=corrupt;
    const auto invalid=m::import_and_install(request,library,registry,resolve,MachineVoiceLibraryPolicy::development_override);
    check(invalid.outcome==m::Outcome::failed && !invalid.error.code.empty() && inventory(library.root())==original_files,"Corrupt OLV published");
    // Reuse the changed-conditioning prepared fixture from the existing replacement checks.
    const auto different=scratch/L"changed-conditioning.olv";
    VoiceImportOptions conversion; conversion.declared_model_compatibility_id=pocket_model_compatibility_id;
    (void)convert_pocket_voice_to_olv(scratch/L"replacement-\u017e.safetensors",different,conversion);
    replace.source=different; replace.expected_hash=voice.metadata->olv_sha256;
    for (unsigned step : {0u,1u,2u}) {
        static unsigned fail_at; fail_at=step;
        detail::replacement_audit::probe=[](unsigned at,bool rollback) {
            if (!rollback && at==fail_at) { throw VoiceLibraryError(VoiceLibraryErrorCode::destination_io,"Injected OLV publication failure"); }
        };
        const auto rolled_back=m::import_and_install(replace,library,registry,resolve,MachineVoiceLibraryPolicy::development_override);
        detail::replacement_audit::probe=nullptr;
        check(rolled_back.outcome==m::Outcome::failed && inventory(library.root())==original_files && registry->tokens==original_tokens,"OLV replacement rollback changed originals/token");
    }
    const auto replaced=m::import_and_install(replace,library,registry,resolve,MachineVoiceLibraryPolicy::development_override);
    const auto updated=library.inspect_voice(voice.library_id);
    check(replaced.outcome==m::Outcome::replaced && replaced.library_id==voice.library_id && updated.metadata->import_kind==VoiceImportKind::olv &&
        updated.metadata->olv_sha256==validate_olv(different).file_sha256 && updated.metadata->display_name==voice.metadata->display_name && registry->tokens==original_tokens,
        "Changed OLV replacement lost ID/name/token or altered input bytes");
    failed_export(voice.metadata->olv_sha256,inspect_olv_export_destination(output),"export_conflict");
    const auto imported_other=library.import_olv(source,"Other voice").voice;
    replace.source=source; replace.expected_hash=updated.metadata->olv_sha256;
    const auto duplicate_replace=m::import_and_install(replace,library,registry,resolve,MachineVoiceLibraryPolicy::development_override);
    check(duplicate_replace.error.code=="duplicate_content" && duplicate_replace.error.duplicate_ids==std::vector<std::string>{imported_other.library_id},"OLV replacement guessed another duplicate target");
    // A new destination appearing during the copy must survive a failed create.
    const auto raced=scratch/L"concurrent-create.olv";
    static std::filesystem::path race_path; race_path=raced;
    detail::export_audit::probe=[](unsigned at,void*) { if (at==2) { std::ofstream file(race_path); file<<"created by another writer"; } };
    try { (void)library.export_olv(updated.library_id,raced); check(false,"Concurrent destination creation overwritten"); }
    catch (const VoiceLibraryError& error) { check(error.code()==VoiceLibraryErrorCode::destination_io,"Concurrent create cause lost"); }
    detail::export_audit::probe=nullptr;
    { std::ifstream file(raced); std::string contents; std::getline(file,contents); check(contents=="created by another writer","Concurrent created destination altered"); }
    std::cout<<"PASS: OLV import/install, duplicate/protocol/immutable replacement, bridge export, exact isolated roundtrip, destination/source conflicts, four copy/validation/publication failures and cleanup.\n";
}
