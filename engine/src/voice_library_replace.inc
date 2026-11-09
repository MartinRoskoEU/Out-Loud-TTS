// Native profile exchange under Session's exclusive lock. The live directory,
// library ID, display name and SAPI paths do not change. No registry edit is needed.
struct NativeReplaceJournal {
    std::string id, directory_id, old_voice, old_metadata, new_voice, new_metadata, phase;
    std::uint64_t volume=0;
};
void native_replace_files(const std::filesystem::path& path,std::vector<fs::Handle>& files,bool machine) {
    const std::vector<std::wstring> expected{L"metadata.json",L"voice.safetensors"};
    if (fs::children(path)!=expected) { fs::fail(Code::replacement_conflict,"Replacement requires exactly the verified profile and metadata files",path); }
    for (const auto& name:expected) {
        auto file=fs::file(path/name,GENERIC_READ|DELETE,FILE_SHARE_READ,OPEN_EXISTING,Code::replacement_conflict);
        if (machine) { detail::library_security::validate(file.value,path/name,false); }
        if (fs::info(file.value,path/name,false).dwFileAttributes&FILE_ATTRIBUTE_READONLY) { fs::fail(Code::replacement_conflict,"Read-only voice cannot be replaced",path/name); }
        files.push_back(std::move(file));
    }
}
std::string native_replace_text(const NativeReplaceJournal& j) {
    return "{\"schema\":1,\"operation\":\"replace_native\",\"id\":"+quote(j.id)+
        ",\"directory_id\":"+quote(j.directory_id)+",\"volume\":"+std::to_string(j.volume)+
        ",\"old_voice\":"+quote(j.old_voice)+",\"old_metadata\":"+quote(j.old_metadata)+
        ",\"new_voice\":"+quote(j.new_voice)+",\"new_metadata\":"+quote(j.new_metadata)+",\"phase\":"+quote(j.phase)+"}";
}
NativeReplaceJournal load_native_replace(const std::filesystem::path& path,bool machine) {
    auto file=fs::file(path);
    if (machine) { detail::library_security::validate(file.value,path,false); }
    const auto bytes=fs::read(file.value,path,4096);
    const auto bad=[&] { fs::fail(Code::replacement_pending,"Invalid native replacement journal; data retained",path); };
    Json json;
    try { json=detail::voice_json::Parser({reinterpret_cast<const char*>(bytes.data()),bytes.size()}).parse(); }
    catch (const std::exception&) { bad(); }
    if (json.kind!=Json::Kind::object || json.object.size()!=10) { bad(); }
    const auto str=[&](const char* key) { const auto it=json.object.find(key);
        if (it==json.object.end() || it->second.kind!=Json::Kind::string) { bad(); } return it->second.text; };
    const auto num=[&](const char* key) { std::uint64_t value=0; const auto it=json.object.find(key);
        if (it==json.object.end() || !detail::voice_json::unsigned_integer(it->second,value)) { bad(); } return value; };
    NativeReplaceJournal j{str("id"),str("directory_id"),str("old_voice"),str("old_metadata"),str("new_voice"),str("new_metadata"),str("phase"),num("volume")};
    if (num("schema")!=1 || str("operation")!="replace_native" || !fs::id(j.id) || !fs::id(j.directory_id) ||
        !hash(j.old_voice) || !hash(j.old_metadata) || !hash(j.new_voice) || !hash(j.new_metadata) ||
        j.old_voice==j.new_voice || (j.phase!="prepared" && j.phase!="committed" && j.phase!="restored")) { bad(); }
    return j;
}
void publish_native_replace(const std::filesystem::path& path,const NativeReplaceJournal& j,bool machine) {
    const auto temporary=path/L"replace-native.tmp";
    if (fs::exists(temporary)) {
        auto previous=load_native_replace(temporary,machine); previous.phase=j.phase;
        if (native_replace_text(previous)!=native_replace_text(j)) { fs::fail(Code::replacement_pending,"Changed temporary journal retained",temporary); }
        remove_control(temporary,machine);
    }
    save_control(temporary,native_replace_text(j),machine);
    auto file=fs::file(temporary,DELETE,0,OPEN_EXISTING,Code::replacement_pending);
    fs::rename(file.value,path/L"replace-native.json",true,Code::replacement_pending);
}
void native_replace_point(unsigned step,void* handle=nullptr) {
    #ifdef OUTLOUD_REPLACEMENT_AUDIT
    detail::native_replace_audit::point(step,handle);
    #else
    (void)step;
    (void)handle;
    #endif
}
// Validate every known file before any recovery write/delete. Handles deny
// competing writes/deletion; active synthesis mappings deny our DELETE access.
void recover_native_replace(Session& session,const std::filesystem::path& path,fs::Handle& transaction) {
    if (session.machine) { detail::library_security::validate(transaction.value,path,true); }
    auto j=load_native_replace(path/L"replace-native.json",session.machine);
    const auto live=entry_path(session,j.id);
    auto directory=fs::directory(live,READ_CONTROL);
    if (session.machine) { detail::library_security::validate(directory.value,live,true); }
    const auto identity=directory_identity(directory.value,live);
    if (identity.VolumeSerialNumber!=j.volume || detail::hex({identity.FileId.Identifier,16})!=j.directory_id) {
        fs::fail(Code::replacement_pending,"Replacement target directory changed; data retained",live);
    }
    for (const auto& name:fs::children(path)) {
        if (name!=L"voice.safetensors" && name!=L"metadata.json" && name!=L"old.safetensors" && name!=L"old.json" &&
            name!=L"replace-native.json" && name!=L"replace-native.tmp") {
            fs::fail(Code::replacement_pending,"Unknown replacement files retained",path/name);
        }
    }
    // Also refuse unknown live files; do not exchange an entry that changed shape.
    for (const auto& name:fs::children(live)) {
        if (name!=L"voice.safetensors" && name!=L"metadata.json") { fs::fail(Code::replacement_pending,"Unknown live files retained",live/name); }
    }
    if (fs::exists(path/L"replace-native.tmp")) {
        auto temporary=load_native_replace(path/L"replace-native.tmp",session.machine); temporary.phase=j.phase;
        if (native_replace_text(temporary)!=native_replace_text(j)) { fs::fail(Code::replacement_pending,"Temporary replacement evidence differs",path); }
    }
    struct Pair {
        std::filesystem::path live, backup, staged;
        std::string old_hash,new_hash,live_hash;
        fs::Handle active,old,fresh;
    };
    std::array<Pair,2> pairs;
    pairs[0].live=live/L"voice.safetensors"; pairs[0].backup=path/L"old.safetensors"; pairs[0].staged=path/L"voice.safetensors";
    pairs[0].old_hash=j.old_voice; pairs[0].new_hash=j.new_voice;
    pairs[1].live=live/L"metadata.json"; pairs[1].backup=path/L"old.json"; pairs[1].staged=path/L"metadata.json";
    pairs[1].old_hash=j.old_metadata; pairs[1].new_hash=j.new_metadata;
    const auto open=[&](const std::filesystem::path& file,fs::Handle& handle) {
        if (!fs::exists(file)) { return std::string{}; }
        handle=fs::file(file,GENERIC_READ|DELETE,FILE_SHARE_READ,OPEN_EXISTING,Code::replacement_pending);
        if (session.machine) { detail::library_security::validate(handle.value,file,false); }
        if (fs::info(handle.value,file,false).dwFileAttributes&FILE_ATTRIBUTE_READONLY) { fs::fail(Code::replacement_pending,"Read-only replacement file retained",file); }
        return handle_hash(handle.value,file);
    };
    for (auto& p:pairs) {
        p.live_hash=open(p.live,p.active); const auto old=open(p.backup,p.old), fresh=open(p.staged,p.fresh);
        const bool prepared=j.phase=="prepared";
        const auto old_count=unsigned(p.live_hash==p.old_hash)+unsigned(old==p.old_hash);
        const auto new_count=unsigned(p.live_hash==p.new_hash)+unsigned(fresh==p.new_hash);
        const bool known_live=p.live_hash.empty() || p.live_hash==p.old_hash || p.live_hash==p.new_hash;
        const bool final_pair=p.live_hash==(j.phase=="committed" ? p.new_hash : p.old_hash) &&
            (j.phase=="committed" ? fresh.empty() : old.empty());
        if ((!old.empty() && old!=p.old_hash) || (!fresh.empty() && fresh!=p.new_hash) ||
            (prepared ? old_count!=1 || new_count!=1 || !known_live : !final_pair)) {
            fs::fail(Code::replacement_pending,"Replacement content changed or is missing; evidence retained",path);
        }
    }
    if (j.phase=="prepared") {
        for (auto& p:pairs) {
            if (p.live_hash==p.new_hash) { fs::rename(p.active.value,p.staged,false,Code::replacement_pending); p.fresh=std::move(p.active); }
            if (p.old.value!=INVALID_HANDLE_VALUE) { fs::rename(p.old.value,p.live,false,Code::replacement_pending); p.old.close(); }
        }
        j.phase="restored"; publish_native_replace(path,j,session.machine);
    }
    // Verify the final profile/metadata pair before retiring any backups.
    for (auto& p:pairs) { p.active.close(); }
    std::vector<fs::Handle> final_pins;
    for (const auto& p:pairs) {
        auto file=fs::file(p.live);
        if (session.machine) { detail::library_security::validate(file.value,p.live,false); }
        if (handle_hash(file.value,p.live)!=(j.phase=="committed" ? p.new_hash : p.old_hash)) {
            fs::fail(Code::replacement_pending,"Final replacement content changed; backups retained",p.live);
        }
        final_pins.push_back(std::move(file));
    }
    const auto verified=inspect(live,j.id,session.machine);
    if (verified.health!=VoiceHealth::valid || verified.metadata->profile_sha256!=(j.phase=="committed" ? j.new_voice : j.old_voice)) {
        fs::fail(Code::replacement_pending,"Final replacement verification failed; evidence retained",live);
    }
    for (auto& p:pairs) {
        auto& obsolete=j.phase=="committed" ? p.old : p.fresh;
        const auto& obsolete_path=j.phase=="committed" ? p.backup : p.staged;
        if (obsolete.value!=INVALID_HANDLE_VALUE) { fs::discard(obsolete.value,obsolete_path); obsolete.close(); native_replace_point(6); }
    }
    if (fs::exists(path/L"replace-native.tmp")) { remove_control(path/L"replace-native.tmp",session.machine); }
    remove_control(path/L"replace-native.json",session.machine); // durable evidence is last
    fs::discard(transaction.value,path); transaction.close();
}
void recover_native_replacements(Session& session,std::string_view id) {
    for (const auto& name:fs::children(session.staging->path)) {
        if (!fs::id(fs::utf8(name))) { continue; }
        const auto path=session.staging->path/name;
        auto directory=fs::directory(path,DELETE|READ_CONTROL);
        if (session.machine) { detail::library_security::validate(directory.value,path,true); }
        if (fs::exists(path/L"replace-native.json") && (id.empty() || load_native_replace(path/L"replace-native.json",session.machine).id==id)) {
            recover_native_replace(session,path,directory);
        }
    }
}
bool native_replacement_pending(Session& session,std::string_view id) {
    for (const auto& name:fs::children(session.staging->path)) {
        if (!fs::id(fs::utf8(name))) { continue; }
        const auto path=session.staging->path/name;
        auto directory=fs::directory(path,READ_CONTROL);
        if (session.machine) { detail::library_security::validate(directory.value,path,true); }
        if (fs::exists(path/L"replace-native.json")) {
            const auto journal=load_native_replace(path/L"replace-native.json",session.machine);
            if (id.empty() || journal.id==id) { return true; }
        }
    }
    return false;
}
