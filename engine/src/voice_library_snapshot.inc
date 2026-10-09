// Private read-only enumeration; Session keeps the shared library lock throughout.
struct PendingIndex {
    std::set<std::string> replacement, removal;
};
PendingIndex pending_index(Session& session,const std::vector<std::wstring>& names) {
    PendingIndex result; std::set<std::string> active;
    for (const auto& name:names) {
        std::string id;
        try { id=fs::utf8(name); } catch (const std::invalid_argument&) { continue; }
        if (fs::id(id)) { active.insert(std::move(id)); }
    }
    if (active.empty()) { return result; }
    if (session.staging->exists) {
        detail::refresh_audit::event(detail::refresh_audit::Event::replacement_scan);
        for (const auto& name:fs::children(session.staging->path)) {
            if (!fs::id(fs::utf8(name))) { continue; }
            const auto path=session.staging->path/name;
            const auto pin=fs::directory(path,session.machine ? READ_CONTROL : 0);
            if (session.machine) { detail::library_security::validate(pin.value,path,true); }
            if (fs::exists(path/L"replace.json")) {
                const auto journal=load_journal(path/L"replace.json",session.machine);
                if (active.contains(journal.id) && !result.replacement.contains(journal.id) && !committed(path,journal,session.machine)) {
                    result.replacement.insert(journal.id);
                }
            }
        }
    }
    if (session.trash->exists) {
        detail::refresh_audit::event(detail::refresh_audit::Event::removal_scan);
        for (const auto& name:fs::children(session.trash->path)) {
            if (!fs::id(fs::utf8(name))) { continue; }
            const auto path=session.trash->path/name; const auto pin=fs::directory(path,READ_CONTROL);
            if (session.machine) { detail::library_security::validate(pin.value,path,true); }
            if (fs::exists(path/L"remove.json")) {
                const auto journal=load_remove(path/L"remove.json",session.machine);
                if (active.contains(journal.id) && !result.removal.insert(journal.id).second) {
                    fs::fail(Code::removal_pending,"Multiple removal journals for one ID; retain for reconciliation",session.trash->path);
                }
            }
        }
    }
    return result;
}
std::vector<LibraryVoice> list(Session& session) {
    std::vector<LibraryVoice> result;
    if (!session.root.exists || !session.voices->exists) { return result; }
    const auto names=fs::children(session.voices->path);
    #ifdef OUTLOUD_REPLACEMENT_AUDIT
    // Reproduce the pre-A06 enumeration in the existing private checker only.
    const bool legacy=detail::refresh_audit::legacy_enumeration;
    const auto pending=legacy ? PendingIndex{} : pending_index(session,names);
    #else
    const auto pending=pending_index(session,names);
    #endif
    result.reserve(names.size());
    for (const auto& name:names) {
        std::string id;
        try { id=fs::utf8(name); } catch (...) {}
        if (!fs::id(id)) {
            result.push_back({id,{}, {},VoiceHealth::unsafe_entry,"Unexpected entry name; no path inferred from metadata"});
        } else {
            auto voice=inspect(session.voices->path/name,id,session.machine);
            bool replacement=pending.replacement.contains(id), removal=pending.removal.contains(id);
            #ifdef OUTLOUD_REPLACEMENT_AUDIT
            if (legacy) { replacement=replacement_pending(session,id); removal=removal_pending(session,id); }
            #endif
            if (replacement) { voice.health=VoiceHealth::replacement_pending; voice.detail="Interrupted replacement needs cleanup_pending recovery; close active consumers first."; }
            if (removal) { voice.health=VoiceHealth::removal_pending; voice.detail="Interrupted removal requires authorized token-aware recovery; data retained."; }
            result.push_back(std::move(voice));
        }
    }
    return result;
}
