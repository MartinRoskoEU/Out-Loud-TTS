// Private implementation; caller owns Session's exclusive library lock.
// Filesystem rename and registry commit are separate. Current token state is
// required for coordinated recovery, even when the journal says committed.
struct RemoveJournal {
    std::string id, directory_id, voice, metadata, registration;
    std::uint64_t volume=0;
    bool committed=false;
};
std::string remove_text(const RemoveJournal& j) {
    return "{\"schema\":1,\"operation\":\"remove\",\"phase\":"+quote(j.committed ? "committed" : "prepared")+
        ",\"id\":"+quote(j.id)+",\"directory_id\":"+quote(j.directory_id)+",\"volume\":"+std::to_string(j.volume)+
        ",\"voice\":"+quote(j.voice)+",\"metadata\":"+quote(j.metadata)+",\"registration\":"+quote(j.registration)+"}";
}
void removal_point(unsigned step) {
    #ifdef OUTLOUD_REPLACEMENT_AUDIT
    detail::removal_audit::point(step);
    #else
    (void)step;
    #endif
}
void remove_control(const std::filesystem::path& path,bool machine) {
    auto file=fs::file(path,GENERIC_READ|DELETE,FILE_SHARE_READ|FILE_SHARE_DELETE,OPEN_EXISTING,Code::removal_pending);
    if (machine) { detail::library_security::validate(file.value,path,false); }
    fs::discard(file.value,path); file.close();
}
void publish_remove(const std::filesystem::path& path,const RemoveJournal& j,bool machine) {
    const auto temporary=path/L"control.tmp";
    if (fs::exists(temporary)) { remove_control(temporary,machine); }
    save_control(temporary,remove_text(j),machine); // complete write + FlushFileBuffers
    auto file=fs::file(temporary,DELETE,0,OPEN_EXISTING,Code::removal_pending);
    fs::rename(file.value,path/L"remove.json",true,Code::removal_pending);
}
RemoveJournal load_remove(const std::filesystem::path& path,bool machine) {
    detail::refresh_audit::event(detail::refresh_audit::Event::removal_journal);
    auto file=fs::file(path,GENERIC_READ,FILE_SHARE_READ,OPEN_EXISTING,Code::removal_pending);
    if (machine) { detail::library_security::validate(file.value,path,false); }
    const auto bytes=fs::read(file.value,path,4096);
    const auto invalid=[&] { fs::fail(Code::removal_pending,"Invalid removal journal; evidence retained",path); };
    Json json;
    try { json=detail::voice_json::Parser({reinterpret_cast<const char*>(bytes.data()),bytes.size()}).parse(); }
    catch (const std::exception&) { invalid(); }
    if (json.kind!=Json::Kind::object || json.object.size()!=9) { invalid(); }
    const auto str=[&](const char* key) { const auto it=json.object.find(key);
        if (it==json.object.end() || it->second.kind!=Json::Kind::string) { invalid(); } return it->second.text; };
    const auto num=[&](const char* key) { std::uint64_t value=0; const auto it=json.object.find(key);
        if (it==json.object.end() || !detail::voice_json::unsigned_integer(it->second,value)) { invalid(); } return value; };
    const auto phase=str("phase");
    RemoveJournal j{str("id"),str("directory_id"),str("voice"),str("metadata"),str("registration"),num("volume"),phase=="committed"};
    if (num("schema")!=1 || str("operation")!="remove" || (phase!="prepared" && phase!="committed") ||
        !fs::id(j.id) || !fs::id(j.directory_id) || (!j.voice.empty() && !hash(j.voice)) ||
        (!j.metadata.empty() && !hash(j.metadata)) ||
        (j.registration!="library_only" && j.registration!="absent" && !hash(j.registration))) { invalid(); }
    return j;
}
void validate_remove_folder(const std::filesystem::path& path,HANDLE directory,bool machine) {
    if (!fs::id(fs::utf8(path.filename().native()))) { fs::fail(Code::removal_pending,"Invalid removal transaction name",path); }
    (void)fs::info(directory,path,true);
    if (machine) { detail::library_security::validate(directory,path,true); }
    for (const auto& name:fs::children(path)) {
        if (name!=L"remove.json" && name!=L"control.tmp" && name!=L"retained") {
            fs::fail(Code::removal_pending,"Unexpected removal transaction contents retained",path/name);
        }
    }
}
std::pair<std::string,std::string> removal_hashes(const std::filesystem::path& path,std::vector<fs::Handle>& files) {
    const auto names=fs::children(path);
    if (names.size()!=files.size()) { fs::fail(Code::removal_pending,"Removal files changed during validation",path); }
    std::pair<std::string,std::string> result;
    for (std::size_t i=0;i<names.size();++i) {
        auto& value=names[i]==L"voice.olv" ? result.first : result.second;
        value=handle_hash(files[i].value,path/names[i]);
    }
    return result;
}
void validate_removed_data(const std::filesystem::path& path,fs::Handle& directory,std::vector<fs::Handle>& files,
        const RemoveJournal& j,bool machine,bool partial) {
    directory=fs::directory(path,DELETE|READ_CONTROL);
    if (machine) { detail::library_security::validate(directory.value,path,true); }
    const auto identity=directory_identity(directory.value,path);
    if (identity.VolumeSerialNumber!=j.volume || detail::hex({identity.FileId.Identifier,16})!=j.directory_id) {
        fs::fail(Code::removal_pending,"Removal directory identity differs; evidence retained",path);
    }
    removal_files(path,files,machine); const auto actual=removal_hashes(path,files);
    if ((!partial || !actual.first.empty()) && actual.first!=j.voice) { fs::fail(Code::removal_pending,"Retained voice identity differs or is missing",path); }
    if ((!partial || !actual.second.empty()) && actual.second!=j.metadata) { fs::fail(Code::removal_pending,"Retained metadata identity differs or is missing",path); }
}
std::string query_registration(const RemovalRegistrationIdentity& query,std::string_view id) {
    try {
        const auto value=query(id);
        if (value!="absent" && !hash(value)) { fs::fail(Code::removal_pending,"Invalid registration evidence",{}); }
        return value;
    } catch (...) { throw ValidationFailure{std::current_exception()}; }
}
void discard_remove_control(const std::filesystem::path& path,fs::Handle& directory,bool machine) {
    if (fs::exists(path/L"retained")) { fs::fail(Code::removal_pending,"Retained removal data cannot be discarded as control",path); }
    if (fs::exists(path/L"control.tmp")) { remove_control(path/L"control.tmp",machine); }
    if (fs::exists(path/L"remove.json")) { remove_control(path/L"remove.json",machine); } // last durable evidence
    fs::discard(directory.value,path); directory.close();
}
RemovalRecoveryResult recover_remove(Session& session,const std::filesystem::path& path,fs::Handle& transaction,
        const RemovalRegistrationIdentity& query) {
    validate_remove_folder(path,transaction.value,session.machine); auto j=load_remove(path/L"remove.json",session.machine);
    const auto live=session.voices->path/fs::wide(j.id), retained=path/L"retained";
    const bool active=fs::exists(live), detached=fs::exists(retained);
    if (active && detached) { fs::fail(Code::removal_pending,"Both original and retained entries exist; no overwrite or deletion",path); }
    const bool coordinated=j.registration!="library_only";
    if (coordinated && !query) { fs::fail(Code::removal_pending,"Coordinated removal requires authorized token inspection; data retained",path); }
    // No control phase is proof of current registry state. Query can fail closed.
    std::string token="library_only";
    if (coordinated) {
        try { token=query_registration(query,j.id); }
        catch (const ValidationFailure& failure) {
            try { std::rethrow_exception(failure.error); }
            catch (const std::exception& error) { throw VoiceLibraryError(Code::removal_pending,std::string("Registration outcome unresolved; data retained: ")+error.what(),path); }
            catch (...) { fs::fail(Code::removal_pending,"Registration outcome unresolved; data retained",path); }
        }
    }
    if (coordinated && token!="absent" && token!=j.registration) { fs::fail(Code::removal_pending,"Owned token changed since preparation; data retained",path); }
    if (active) {
        if (j.committed || (coordinated && token!=j.registration)) { fs::fail(Code::removal_pending,"Active entry contradicts removal evidence; retain for reconciliation",path); }
        fs::Handle entry; std::vector<fs::Handle> files;
        validate_removed_data(live,entry,files,j,session.machine,false);
        files.clear(); entry.close(); discard_remove_control(path,transaction,session.machine);
        return {RemovalRecoveryState::restored,false,{}}; // preparation cancelled / rollback already restored
    }
    if (!detached && !j.committed) { fs::fail(Code::removal_pending,"Uncommitted removal data is missing; no outcome inferred",path); }
    const bool remove=coordinated ? token=="absent" : j.committed;
    if (j.committed && !remove) { fs::fail(Code::removal_pending,"Committed removal now has a token; retain for reconciliation",path); }
    fs::Handle entry; std::vector<fs::Handle> files;
    if (detached) { validate_removed_data(retained,entry,files,j,session.machine,j.committed); }
    if (!remove) {
        files.clear(); removal_point(2);
        fs::rename(entry.value,live,false,Code::removal_pending); entry.close();
        removal_point(9); discard_remove_control(path,transaction,session.machine);
        return {RemovalRecoveryState::restored,false,{}};
    }
    if (!j.committed) { j.committed=true; publish_remove(path,j,session.machine); }
    // Only a verified absent token (or an explicit library-only commit) permits deletion.
    removal_point(7);
    try {
        removal_point(3);
        if (detached) {
            for (auto& file:files) { fs::discard(file.value,retained); file.close(); removal_point(8); }
            fs::discard(entry.value,retained); entry.close();
        }
        discard_remove_control(path,transaction,session.machine);
    } catch (const std::exception& error) { return {RemovalRecoveryState::removed,true,error.what()}; }
    return {RemovalRecoveryState::removed,false,{}};
}
std::vector<std::filesystem::path> removal_transactions(Session& session,std::string_view id,const std::filesystem::path& held={}) {
    std::vector<std::filesystem::path> result;
    if (!session.root.exists || !session.trash->exists) { return result; }
    detail::refresh_audit::event(detail::refresh_audit::Event::removal_scan);
    for (const auto& name:fs::children(session.trash->path)) {
        if (!fs::id(fs::utf8(name))) { continue; }
        const auto path=session.trash->path/name; if (path==held) { continue; } const auto directory=fs::directory(path,READ_CONTROL);
        if (session.machine) { detail::library_security::validate(directory.value,path,true); }
        if (fs::exists(path/L"remove.json") && load_remove(path/L"remove.json",session.machine).id==id) { result.push_back(path); }
    }
    if (result.size()>1) { fs::fail(Code::removal_pending,"Multiple removal journals for one ID; retain for reconciliation",session.trash->path); }
    return result;
}
bool removal_pending(Session& session,std::string_view id) { return !removal_transactions(session,id).empty(); }
VoiceRemovalResult remove_prepared(Session& session,std::string_view id,const VoiceRemoveOptions& options) {
    if (replacement_pending(session,id)) { fs::fail(Code::replacement_pending,"Recover pending replacement before removal",session.root.path); }
    if (removal_pending(session,id)) { fs::fail(Code::removal_pending,"Recover existing removal before starting another",session.root.path); }
    const auto path=entry_path(session,id); LibraryVoice current;
    if (options.expected_display_name || options.expected_olv_sha256 || options.expected_health || options.remove_registration) {
        current=inspect(path,std::string(id),session.machine);
        if (!options.expected_display_name || !options.expected_olv_sha256 || !options.expected_health ||
            *options.expected_health!=voice_health_name(current.health) ||
            (!options.expected_olv_sha256->empty() && !hash(*options.expected_olv_sha256)) ||
            *options.expected_display_name!=(current.metadata ? current.metadata->display_name : "") ||
            *options.expected_olv_sha256!=(current.metadata ? current.metadata->olv_sha256 : "")) {
            fs::fail(Code::removal_conflict,"Voice identity/content changed since confirmation. Refresh and confirm again.",path);
        }
    }
    if (static_cast<bool>(options.remove_registration)!=static_cast<bool>(options.registration_identity)) {
        fs::fail(Code::removal_pending,"Coordinated removal requires both registration callbacks",path);
    }
    auto entry=fs::directory(path,DELETE|READ_CONTROL);
    if (session.machine) { detail::library_security::validate(entry.value,path,true); }
    std::vector<fs::Handle> files;
    try { removal_files(path,files,session.machine); }
    catch (const VoiceLibraryError& error) {
        if (options.remove_registration && (error.system_error()==ERROR_SHARING_VIOLATION || error.system_error()==ERROR_LOCK_VIOLATION)) {
            throw VoiceLibraryError(Code::voice_in_use,"Voice is in use. Close active speech consumers, then retry.",error.path(),error.system_error());
        } throw;
    }
    const auto hashes=removal_hashes(path,files);
    if (options.expected_olv_sha256 && current.health==VoiceHealth::valid && hashes.first!=*options.expected_olv_sha256) {
        fs::fail(Code::removal_conflict,"Voice changed before removal",path);
    }
    const auto identity=directory_identity(entry.value,path);
    RemoveJournal j{std::string(id),detail::hex({identity.FileId.Identifier,16}),hashes.first,hashes.second,
        options.registration_identity ? query_registration(options.registration_identity,id) : "library_only",identity.VolumeSerialNumber,false};
    std::filesystem::path transaction_path; fs::Handle transaction;
    for (unsigned attempt=0;attempt<8;++attempt) {
        transaction_path=session.trash->path/fs::wide(fs::random_id());
        if (session.machine ? detail::library_security::create_directory(transaction_path) : CreateDirectoryW(transaction_path.c_str(),nullptr)!=0) {
            transaction=fs::directory(transaction_path,DELETE|READ_CONTROL); break;
        }
    }
    if (transaction.value==INVALID_HANDLE_VALUE) { fs::fail(Code::destination_io,"Cannot create removal transaction",session.trash->path); }
    bool published=false;
    try {
        removal_point(4); publish_remove(transaction_path,j,session.machine); published=true;
        removal_point(5); files.clear(); removal_point(0);
        fs::rename(entry.value,transaction_path/L"retained",false,Code::remove_failed); entry.close();
        fs::Handle retained; validate_removed_data(transaction_path/L"retained",retained,files,j,session.machine,false);
        removal_point(1);
        if (options.remove_registration) { options.remove_registration(current,j.registration); }
        removal_point(6); files.clear(); retained.close();
        if (!options.registration_identity) { j.committed=true; publish_remove(transaction_path,j,session.machine); }
        const auto recovered=recover_remove(session,transaction_path,transaction,options.registration_identity);
        if (recovered.state!=RemovalRecoveryState::removed) { fs::fail(Code::removal_pending,"Removal did not commit; restored voice",transaction_path); }
        return {recovered.cleanup_pending,recovered.detail};
    } catch (...) {
        const auto cause=std::current_exception(); files.clear(); entry.close();
        #ifdef OUTLOUD_REPLACEMENT_AUDIT
        if (detail::removal_audit::leave_pending && published) { fs::fail(Code::removal_partial,"Simulated interruption; removal journal/data retained",transaction_path); }
        #endif
        if (!published) {
            // This invocation created the directory; detach was never attempted.
            try { discard_remove_control(transaction_path,transaction,session.machine); } catch (...) {}
            throw ValidationFailure{cause};
        }
        RemovalRecoveryResult recovered;
        try { recovered=recover_remove(session,transaction_path,transaction,options.registration_identity); }
        catch (...) {
            fs::fail(Code::removal_partial,"Removal failed and recovery could not complete; journal/data retained for authorized reconciliation",transaction_path);
        }
        if (recovered.state==RemovalRecoveryState::removed) {
            throw VoiceLibraryError(Code::removal_partial,
                recovered.cleanup_pending ? "Removal committed after an error; cleanup pending: "+recovered.detail :
                    "Token absence verified after an error; removal committed and cleanup completed",transaction_path);
        }
        throw ValidationFailure{cause};
    }
}
