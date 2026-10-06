// Private implementation included inside voice_library.cpp's anonymous namespace.
// A journal protects the pair across failures; individual file renames are atomic,
// but the pair is not a filesystem transaction. Entry directory is never replaced.
struct ReplaceJournal {
    std::string id, directory_id, old_voice, old_metadata, new_voice, new_metadata;
    std::uint64_t volume = 0;
};
std::string file_hash(const std::filesystem::path& path) {
    const detail::MappedFile mapping(path,maximum_voice); return detail::hex(detail::sha256(mapping.bytes()));
}
std::string handle_hash(HANDLE handle,const std::filesystem::path& path) {
    LARGE_INTEGER zero{}; if (!SetFilePointerEx(handle,zero,nullptr,FILE_BEGIN)) { fs::fail(Code::source_io,"Cannot rewind pinned file",path,GetLastError()); }
    detail::Sha256 digest; std::array<unsigned char,65536> bytes{};
    for (;;) { DWORD count=0; if (!ReadFile(handle,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)) { fs::fail(Code::source_io,"Cannot hash pinned file",path,GetLastError()); }
        if (!count) { break; } digest.update({bytes.data(),count}); }
    return detail::hex(digest.finish());
}
FILE_ID_INFO directory_identity(HANDLE directory,const std::filesystem::path& path) {
    FILE_ID_INFO result{}; if (!GetFileInformationByHandleEx(directory,FileIdInfo,&result,sizeof(result))) { fs::fail(Code::unsafe_path,"Cannot identify replacement directory",path,GetLastError()); } return result;
}
std::string journal_text(const ReplaceJournal& j) {
    return "{\"schema\":1,\"id\":"+quote(j.id)+",\"directory_id\":"+quote(j.directory_id)+",\"volume\":"+std::to_string(j.volume)+
        ",\"old_voice\":"+quote(j.old_voice)+",\"old_metadata\":"+quote(j.old_metadata)+",\"new_voice\":"+quote(j.new_voice)+",\"new_metadata\":"+quote(j.new_metadata)+"}";
}
void save_control(const std::filesystem::path& path,std::string_view text,bool machine) {
    detail::library_security::FileCreationSecurity security(machine);
    auto file=fs::file(path,GENERIC_WRITE,0,CREATE_NEW,Code::destination_io,security.get()); fs::write(file.value,{reinterpret_cast<const unsigned char*>(text.data()),text.size()},path);
}
ReplaceJournal load_journal(const std::filesystem::path& path,bool machine) {
    auto file=fs::file(path); if (machine) { detail::library_security::validate(file.value,path,false); }
    const auto bytes=fs::read(file.value,path,4096); const auto json=detail::voice_json::Parser({reinterpret_cast<const char*>(bytes.data()),bytes.size()}).parse();
    auto invalid=[&] { fs::fail(Code::replacement_pending,"Invalid replacement journal; retained for manual recovery",path); };
    if (json.kind!=Json::Kind::object || json.object.size()!=8) { invalid(); }
    auto str=[&](const char* key) { auto it=json.object.find(key); if (it==json.object.end() || it->second.kind!=Json::Kind::string) { invalid(); } return it->second.text; };
    auto num=[&](const char* key) { std::uint64_t value=0; auto it=json.object.find(key); if (it==json.object.end() || !detail::voice_json::unsigned_integer(it->second,value)) { invalid(); } return value; };
    ReplaceJournal j{str("id"),str("directory_id"),str("old_voice"),str("old_metadata"),str("new_voice"),str("new_metadata"),num("volume")};
    if (num("schema")!=1 || !fs::id(j.id) || !fs::id(j.directory_id) || !hash(j.old_voice) || !hash(j.old_metadata) || !hash(j.new_voice) || !hash(j.new_metadata)) { invalid(); } return j;
}
bool committed(const std::filesystem::path& path,const ReplaceJournal& journal,bool machine) {
    if (!fs::exists(path/L"commit")) { return false; }
    auto file=fs::file(path/L"commit"); if (machine) { detail::library_security::validate(file.value,path/L"commit",false); }
    const auto bytes=fs::read(file.value,path/L"commit",128); const auto text=journal_text(journal);
    const auto expected=detail::hex(detail::sha256({reinterpret_cast<const unsigned char*>(text.data()),text.size()}));
    if (std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size())!=expected) { fs::fail(Code::replacement_pending,"Invalid replacement commit marker; retained",path); } return true;
}
bool replacement_pending(Session& session,std::string_view id) {
    if (!session.root.exists || !session.staging->exists) { return false; }
    for (const auto& name : fs::children(session.staging->path)) {
        if (!fs::id(fs::utf8(name))) { continue; }
        const auto path=session.staging->path/name; const auto pin=fs::directory(path,session.machine ? READ_CONTROL : 0);
        if (session.machine) { detail::library_security::validate(pin.value,path,true); }
        if (fs::exists(path/L"replace.json")) { auto journal=load_journal(path/L"replace.json",session.machine);
            if (journal.id==id && !committed(path,journal,session.machine)) { return true; } }
    } return false;
}
// Never recursively delete. Control record is deleted last, so an interrupted
// cleanup always retains a recognisable journal/commit and remaining originals.
void discard_replacement(const std::filesystem::path& path,fs::Handle& directory,bool machine) {
    const std::array<const wchar_t*,8> allowed{L"voice.olv",L"metadata.json",L"old.olv",L"old.json",L"retired.olv",L"retired.json",L"replace.json",L"commit"};
    std::vector<fs::Handle> handles; std::vector<std::filesystem::path> paths;
    const auto names=fs::children(path);
    for (const auto& name : names) { if (std::none_of(allowed.begin(),allowed.end(),[&](auto item) { return name==item; })) { fs::fail(Code::replacement_pending,"Unexpected recovery files retained",path); } }
    for (auto name : allowed) { const auto item=path/name; if (!fs::exists(item)) { continue; }
        auto file=fs::file(item,GENERIC_READ|DELETE,FILE_SHARE_READ|FILE_SHARE_DELETE,OPEN_EXISTING,Code::replacement_pending);
        if (machine) { detail::library_security::validate(file.value,item,false); } handles.push_back(std::move(file)); paths.push_back(item); }
    for (std::size_t i=0;i<handles.size();++i) { fs::discard(handles[i].value,paths[i]); handles[i].close(); }
    fs::discard(directory.value,path); directory.close();
}
fs::Handle replacement_file(const std::filesystem::path& path,bool machine) {
    try { auto file=fs::file(path,GENERIC_READ|DELETE,FILE_SHARE_READ|FILE_SHARE_DELETE,OPEN_EXISTING,Code::destination_io);
        if (machine) { detail::library_security::validate(file.value,path,false); } return file;
    } catch (const VoiceLibraryError& error) {
        if (error.system_error()==ERROR_SHARING_VIOLATION || error.system_error()==ERROR_LOCK_VIOLATION) {
            throw VoiceLibraryError(Code::voice_in_use,"Voice is in use. Close WoW and other active speech consumers, then retry.",path,error.system_error()); } throw;
    }
}
void recover_replacement(Session& session,const std::filesystem::path& path,fs::Handle& stage_directory) {
    const auto journal=load_journal(path/L"replace.json",session.machine);
    const bool was_committed=committed(path,journal,session.machine);
    if (was_committed) {
        // A durable marker alone is not proof of healthy live data after disk/
        // filesystem failure. Never discard originals of an unhealthy target.
        const auto target=entry_path(session,journal.id);
        if (inspect(target,journal.id,session.machine).health!=VoiceHealth::valid) { fs::fail(Code::replacement_pending,"Committed replacement target is unhealthy; originals retained for review",path); }
    }
    if (!was_committed) {
        const auto target=entry_path(session,journal.id); const auto directory=fs::directory(target,READ_CONTROL);
        if (session.machine) { detail::library_security::validate(directory.value,target,true); }
        const auto identity=directory_identity(directory.value,target);
        if (identity.VolumeSerialNumber!=journal.volume || detail::hex({identity.FileId.Identifier,16})!=journal.directory_id) { fs::fail(Code::replacement_pending,"Recovery target directory identity changed",target); }
        // Both backup files must validate before changing anything. Unknown live
        // data is never overwritten. Acquire all handles before the first rename.
        if (file_hash(path/L"old.olv")!=journal.old_voice || file_hash(path/L"old.json")!=journal.old_metadata) { fs::fail(Code::replacement_pending,"Recovery originals changed",path); }
        const auto old_asset=validate_olv(path/L"old.olv"); const auto old_record=metadata(path/L"old.json",journal.id);
        if (old_record.olv_sha256!=old_asset.file_sha256 || old_record.source_sha256!=old_asset.source_sha256 || old_record.compatibility_id!=old_asset.compatibility_id) {
            fs::fail(Code::replacement_pending,"Recovery original metadata/voice pair disagrees",path); }
        auto old_voice=replacement_file(path/L"old.olv",session.machine), old_metadata=replacement_file(path/L"old.json",session.machine);
        std::vector<fs::Handle> live;
        std::vector<std::filesystem::path> live_paths;
        for (const auto& item : {std::tuple{L"voice.olv",journal.old_voice,journal.new_voice},std::tuple{L"metadata.json",journal.old_metadata,journal.new_metadata}}) {
            const auto current=target/std::get<0>(item);
            if (fs::exists(current)) { auto file=replacement_file(current,session.machine); const auto digest=handle_hash(file.value,current);
                if (digest!=std::get<1>(item) && digest!=std::get<2>(item)) { fs::fail(Code::replacement_pending,"Unexpected live state prevents rollback",current); } live.push_back(std::move(file)); live_paths.push_back(current); }
        }
        // Copy backups to fresh recovery files first. Keep original backups and
        // journal intact until both restores succeed, allowing repeated recovery.
        // Existing staged new files can be reused only after safe owned deletion.
        old_voice.close(); old_metadata.close();
        for (const auto* name : {L"voice.olv",L"metadata.json"}) { if (fs::exists(path/name)) { auto stale=replacement_file(path/name,session.machine); fs::discard(stale.value,path/name); stale.close(); } }
        copy_voice(path/L"old.olv",path/L"voice.olv",session.machine); copy_voice(path/L"old.json",path/L"metadata.json",session.machine);
        auto restore_voice=replacement_file(path/L"voice.olv",session.machine), restore_metadata=replacement_file(path/L"metadata.json",session.machine);
        // FILE_RENAME_INFO cannot reliably overwrite an open destination.
        // Move pinned live files aside first; never weaken consumers' sharing.
        for (std::size_t i=0;i<live.size();++i) {
            const auto retired=path/(live_paths[i].filename()==L"voice.olv" ? L"retired.olv" : L"retired.json");
            if (fs::exists(retired)) { auto previous=replacement_file(retired,session.machine); fs::discard(previous.value,retired); previous.close(); }
            fs::rename(live[i].value,retired,false,Code::replacement_pending);
        }
        #ifdef OUTLOUD_REPLACEMENT_AUDIT
        detail::replacement_audit::point(3,true);
        #endif
        fs::rename(restore_voice.value,target/L"voice.olv",false,Code::replacement_pending);
        #ifdef OUTLOUD_REPLACEMENT_AUDIT
        detail::replacement_audit::point(4,true);
        #endif
        fs::rename(restore_metadata.value,target/L"metadata.json",false,Code::replacement_pending);
        // Mark a completed rollback too, before deleting backup/control state.
        restore_voice.close(); restore_metadata.close(); live.clear();
        const auto text=journal_text(journal); save_control(path/L"commit",detail::hex(detail::sha256({reinterpret_cast<const unsigned char*>(text.data()),text.size()})),session.machine);
    }
    discard_replacement(path,stage_directory,session.machine);
}
LibraryReplaceResult replace_prepared(Session& session,std::string_view id,const std::filesystem::path& input,const VoiceReplaceOptions& options) {
    auto target=healthy(session,id);
    if (!hash(options.expected_olv_sha256) || target.metadata->display_name!=options.expected_display_name || target.metadata->olv_sha256!=options.expected_olv_sha256) {
        fs::fail(Code::replacement_conflict,"Voice name or content changed since confirmation. Refresh and confirm again.",target.managed_olv_path); }
    if (options.import.replace_existing) { fs::fail(Code::replace_required,"Replacement staging must not overwrite an arbitrary destination",input); }
    const auto source=fs::local(input,Code::source_io); const fs::Pins source_parents(source.parent_path()); const auto source_file=fs::file(source);
    Stage stage(session); const auto converted=convert_pocket_voice_to_olv(source,stage.path/L"voice.olv",options.import);
    if (session.machine) { detail::library_security::protect_created_file(stage.path/L"voice.olv"); }
    const auto record=new_metadata(target.library_id,target.metadata->display_name,converted.voice,source,VoiceImportKind::pocket_prepared);
    // Compare validated conditioning independently of source-header provenance.
    // A differently encoded safetensors header need not change actual voice data.
    const auto same_data=[&](const std::filesystem::path& path) {
        OutLoudVoice candidate(stage.path/L"voice.olv"), existing(path);
        if (candidate.compatibility_id()!=existing.compatibility_id() || candidate.prefix_length()!=existing.prefix_length()) { return false; }
        for (std::uint32_t layer=0;layer<OutLoudVoice::layers();++layer) {
            const auto a=candidate.cache(layer), b=existing.cache(layer);
            if (a.size()!=b.size() || std::memcmp(a.data(),b.data(),a.size_bytes())!=0) { return false; }
        } return true;
    };
    const bool unchanged=same_data(target.managed_olv_path);
    auto matches=duplicates(session,record.olv_sha256); std::erase(matches,target.library_id);
    for (const auto& name : fs::children(session.voices->path)) {
        const auto other=fs::utf8(name); if (!fs::id(other) || other==target.library_id || std::find(matches.begin(),matches.end(),other)!=matches.end()) { continue; }
        try { const auto pinned=fs::directory(session.voices->path/name); const auto file=fs::file(session.voices->path/name/L"voice.olv");
            if (same_data(session.voices->path/name/L"voice.olv")) { matches.push_back(other); }
        } catch (const std::exception&) {} // Unverifiable contents cannot establish equality.
    }
    if (!matches.empty()) { throw VoiceLibraryError(Code::duplicate_content,"Replacement content belongs to another voice; no duplicate created",session.root.path,0,{},matches); }
    save_metadata(stage.path/L"metadata.json",record,session.machine);
    if (validate_olv(stage.path/L"voice.olv").file_sha256!=record.olv_sha256) { fs::fail(Code::corrupt_voice,"Staged replacement identity changed",stage.path); }
    if (options.validate_before_publish) { try { options.validate_before_publish(target,stage.path/L"voice.olv"); } catch (...) { throw ValidationFailure{std::current_exception()}; } }
    if (unchanged) { return {target,false,false,{}}; }
    const auto entry=target.managed_olv_path.parent_path(); const auto pinned_entry=fs::directory(entry,READ_CONTROL);
    const auto identity=directory_identity(pinned_entry.value,entry);
    copy_voice(entry/L"voice.olv",stage.path/L"old.olv",session.machine); copy_voice(entry/L"metadata.json",stage.path/L"old.json",session.machine);
    const auto backup_asset=validate_olv(stage.path/L"old.olv"); const auto backup_record=metadata(stage.path/L"old.json",target.library_id);
    if (backup_asset.file_sha256!=target.metadata->olv_sha256 || backup_record.olv_sha256!=backup_asset.file_sha256 ||
        backup_record.source_sha256!=backup_asset.source_sha256 || backup_record.display_name!=options.expected_display_name) { fs::fail(Code::replacement_conflict,"Original backup pair changed before publication",stage.path); }
    ReplaceJournal journal{target.library_id,detail::hex({identity.FileId.Identifier,16}),target.metadata->olv_sha256,file_hash(stage.path/L"old.json"),record.olv_sha256,file_hash(stage.path/L"metadata.json"),identity.VolumeSerialNumber};
    auto old_voice=replacement_file(entry/L"voice.olv",session.machine), old_metadata=replacement_file(entry/L"metadata.json",session.machine);
    if (handle_hash(old_voice.value,entry/L"voice.olv")!=journal.old_voice || handle_hash(old_metadata.value,entry/L"metadata.json")!=journal.old_metadata) { fs::fail(Code::replacement_conflict,"Target changed before publication",entry); }
    auto new_voice=replacement_file(stage.path/L"voice.olv",session.machine), new_metadata_file=replacement_file(stage.path/L"metadata.json",session.machine);
    if (handle_hash(new_voice.value,stage.path/L"voice.olv")!=journal.new_voice || handle_hash(new_metadata_file.value,stage.path/L"metadata.json")!=journal.new_metadata) { fs::fail(Code::corrupt_voice,"Staged replacement changed before publication",stage.path); }
    const auto text=journal_text(journal); save_control(stage.path/L"replace.json",text,session.machine); stage.published=true; // recovery owns it now
    try {
        #ifdef OUTLOUD_REPLACEMENT_AUDIT
        detail::replacement_audit::point(0);
        #endif
        fs::rename(old_voice.value,stage.path/L"retired.olv",false,Code::destination_io);
        fs::rename(old_metadata.value,stage.path/L"retired.json",false,Code::destination_io);
        fs::rename(new_voice.value,entry/L"voice.olv",false,Code::destination_io);
        #ifdef OUTLOUD_REPLACEMENT_AUDIT
        detail::replacement_audit::point(1);
        #endif
        fs::rename(new_metadata_file.value,entry/L"metadata.json",false,Code::destination_io);
        #ifdef OUTLOUD_REPLACEMENT_AUDIT
        detail::replacement_audit::point(2);
        #endif
        save_control(stage.path/L"commit",detail::hex(detail::sha256({reinterpret_cast<const unsigned char*>(text.data()),text.size()})),session.machine);
    } catch (...) {
        const auto original=std::current_exception(); old_voice.close(); old_metadata.close(); new_voice.close(); new_metadata_file.close();
        #ifdef OUTLOUD_REPLACEMENT_AUDIT
        if (detail::replacement_audit::leave_pending) { fs::fail(Code::replacement_pending,"Injected interruption; originals retained in staging",stage.path); }
        #endif
        try { recover_replacement(session,stage.path,stage.directory); }
        catch (const VoiceLibraryError& error) { throw VoiceLibraryError(Code::replacement_pending,std::string("Publication failed; originals retained. Recovery: ")+error.what()+" ("+fs::utf8(error.path().wstring())+")",stage.path,error.system_error()); }
        catch (const std::exception& error) { throw VoiceLibraryError(Code::replacement_pending,std::string("Publication failed; originals retained. Recovery: ")+error.what(),stage.path); }
        std::rethrow_exception(original);
    }
    old_voice.close(); old_metadata.close(); new_voice.close(); new_metadata_file.close();
    LibraryReplaceResult result{{target.library_id,record,entry/L"voice.olv",VoiceHealth::valid,{}},true,false,{}};
    try {
        #ifdef OUTLOUD_REPLACEMENT_AUDIT
        detail::replacement_audit::point(5);
        #endif
        discard_replacement(stage.path,stage.directory,session.machine);
    }
    catch (const std::exception& error) { result.cleanup_pending=true; result.detail=error.what(); }
    return result;
}
