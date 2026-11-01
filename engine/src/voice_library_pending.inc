// Read-only conflict evidence for existing pending transactions. No asset loading,
// replacement, rollback or deletion is supported by this retained journal reader.
struct ReplaceJournal {
    std::string id, directory_id, old_voice, old_metadata, new_voice, new_metadata;
    std::uint64_t volume = 0;
};

std::string journal_text(const ReplaceJournal& j) {
    return "{\"schema\":1,\"id\":"+quote(j.id)+",\"directory_id\":"+quote(j.directory_id)+",\"volume\":"+std::to_string(j.volume)+
        ",\"old_voice\":"+quote(j.old_voice)+",\"old_metadata\":"+quote(j.old_metadata)+",\"new_voice\":"+quote(j.new_voice)+",\"new_metadata\":"+quote(j.new_metadata)+"}";
}

ReplaceJournal load_journal(const std::filesystem::path& path,bool machine) {
    detail::refresh_audit::event(detail::refresh_audit::Event::replacement_journal);
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
    detail::refresh_audit::event(detail::refresh_audit::Event::replacement_scan);
    for (const auto& name : fs::children(session.staging->path)) {
        if (!fs::id(fs::utf8(name))) { continue; }
        const auto path=session.staging->path/name; const auto pin=fs::directory(path,session.machine ? READ_CONTROL : 0);
        if (session.machine) { detail::library_security::validate(pin.value,path,true); }
        if (fs::exists(path/L"replace.json")) { auto journal=load_journal(path/L"replace.json",session.machine);
            if (journal.id==id && !committed(path,journal,session.machine)) { return true; } }
    } return false;
}
