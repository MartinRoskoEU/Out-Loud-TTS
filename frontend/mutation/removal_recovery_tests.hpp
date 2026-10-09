#pragma once
#include "replacement_audit.hpp"

// Private libraries and fake registries only. No process termination/inference.
void removal_recovery_checks(const std::filesystem::path& prepared,const outloud::SapiVoiceRuntime& runtime,
        const std::filesystem::path& scratch) {
    using namespace outloud;
    namespace fs=detail::library_fs; namespace m=frontend::mutation; namespace audit=detail::removal_audit;
    VoiceLibrary seed(scratch/L"recovery-seed"); VoiceImportOptions options; options.declared_model_compatibility_id=pocket_model_compatibility_id;
    const auto original=seed.import_pocket_voice(prepared,"Recovery voice",options).voice;
    auto seed_registry=std::make_shared<FakeRegistry>(); seed_registry->fixture_setup=true;
    SapiVoiceManager seed_manager(seed,runtime,seed_registry,MachineVoiceLibraryPolicy::development_override);
    (void)seed_manager.install(original.library_id);
    const auto prototype=seed_registry->tokens.at(managed_sapi_token_key(original.library_id));
    struct Case {
        VoiceLibrary library; std::shared_ptr<FakeRegistry> registry=std::make_shared<FakeRegistry>();
        SapiVoiceManager manager; LibraryVoice voice; m::Request request; Inventory before; SapiTokenTree token;
        Case(const std::filesystem::path& root,const std::filesystem::path& source,const SapiTokenTree& prototype)
            : library(root),manager(library,{},registry,MachineVoiceLibraryPolicy::development_override) {
            voice=library.import_olv(source,"Recovery voice").voice; registry->fixture_setup=true;
            token=prototype; token.values[L"LibraryId"]=sapi_registry_string(fs::wide(voice.library_id));
            token.values[L"VoicePath"]=sapi_registry_string(voice.managed_olv_path.wstring());
            registry->tokens[managed_sapi_token_key(voice.library_id)]=token;
            registry->tokens[L"Microsoft_Unrelated"]=SapiTokenTree{};
            request={{},voice.metadata->display_name,m::Operation::remove,voice.library_id,voice.metadata->olv_sha256,"valid"};
            before=inventory(voice.managed_olv_path.parent_path());
        }
        RemovalRegistrationIdentity query() { return [&](std::string_view id) { return manager.removal_registration_identity(id); }; }
        m::Result run() { return m::import_and_install(request,library,registry,[]() -> SapiVoiceRuntime { throw std::runtime_error("Recovery must not load runtime"); },MachineVoiceLibraryPolicy::development_override); }
        std::filesystem::path transaction() {
            const auto names=fs::children(library.root()/L".trash"); check(names.size()==1,"Expected one retained transaction");
            return library.root()/L".trash"/names[0];
        }
        void original_intact() {
            check(inventory(voice.managed_olv_path.parent_path())==before,"Recovered original bytes differ");
            check(registry->tokens.at(managed_sapi_token_key(voice.library_id))==token,"Recovery changed token");
        }
        void interrupt(unsigned at) {
            static unsigned fault; fault=at; audit::leave_pending=true;
            audit::probe=[](unsigned step) { if (step==fault) { throw std::runtime_error("Simulated process interruption"); } };
            const auto result=run(); audit::probe=nullptr; audit::leave_pending=false;
            check(result.outcome==m::Outcome::removal_warning,"Interrupted operation reported complete success");
            check(std::filesystem::exists(transaction()/L"remove.json"),"Interruption lost durable journal");
        }
        void cleared() {
            check(fs::children(library.root()/L".trash").empty(),"Recovery left unnecessary backups/control");
            check(registry->tokens.contains(L"Microsoft_Unrelated"),"Recovery removed unrelated token");
            check(library.recover_removal(voice.library_id,query()).state==RemovalRecoveryState::none,"Repeated recovery is not idempotent");
        }
    };
    unsigned index=0;
    const auto make=[&] { return Case(scratch/fs::wide("recovery-case-"+std::to_string(++index)),original.managed_olv_path,prototype); };
    const auto rejected=[](const auto& operation) {
        try { operation(); } catch (const VoiceLibraryError& error) { check(error.code()!=VoiceLibraryErrorCode::voice_not_found,"Lost recovery problem"); return; }
        catch (const SapiVoiceError&) { return; } throw std::runtime_error("Unsafe recovery accepted");
    };
    {
        auto c=make(); const auto result=c.run();
        check(result.outcome==m::Outcome::removed && !std::filesystem::exists(c.voice.managed_olv_path.parent_path()) &&
            !c.registry->tokens.contains(managed_sapi_token_key(c.voice.library_id)),"Normal coordinated remove failed"); c.cleared();
    }
    for (unsigned step:{4U,0U}) {
        auto c=make(); static unsigned fault; fault=step;
        audit::probe=[](unsigned at) { if (at==fault) { throw std::runtime_error("Journal/detach failure"); } };
        const auto result=c.run(); audit::probe=nullptr;
        check(result.outcome==m::Outcome::failed,"Precommit failure misreported"); c.original_intact(); c.cleared();
    }
    {
        auto c=make(); c.registry->deny_exchange=true; const auto result=c.run(); c.registry->deny_exchange=false;
        check(result.error.code=="registry_io" && result.outcome==m::Outcome::failed,"Registry failure lost truthful rollback"); c.original_intact(); c.cleared();
    }
    for (unsigned step:{5U,1U}) {
        auto c=make(); c.interrupt(step); const auto evidence=inventory(c.transaction());
        check(!c.library.cleanup_pending().pending.empty() && inventory(c.transaction())==evidence,"Generic cleanup destroyed unresolved removal");
        const auto recovery=c.library.recover_removal(c.voice.library_id,c.query());
        check(recovery.state==RemovalRecoveryState::restored,"Existing token did not restore/cancel removal"); c.original_intact(); c.cleared();
    }
    for (unsigned step:{6U,7U,8U}) {
        auto c=make(); c.interrupt(step);
        check(!c.registry->tokens.contains(managed_sapi_token_key(c.voice.library_id)),"Simulated post-unregister state retained token");
        const auto recovery=c.library.recover_removal(c.voice.library_id,c.query());
        check(recovery.state==RemovalRecoveryState::removed && !recovery.cleanup_pending &&
            !std::filesystem::exists(c.voice.managed_olv_path.parent_path()),"Confirmed committed removal was restored or leaked"); c.cleared();
    }
    {
        auto c=make(); audit::probe=[](unsigned step) { if (step==6) { throw std::runtime_error("Post-unregister exception"); } };
        const auto result=c.run(); audit::probe=nullptr;
        check(result.outcome==m::Outcome::removal_warning && result.error.code=="removal_partial" &&
            result.error.message.find("removal committed and cleanup completed")!=std::string::npos,
            "Successful post-error recovery was reported as unresolved retained data");
        check(!std::filesystem::exists(c.voice.managed_olv_path.parent_path()) &&
            !c.registry->tokens.contains(managed_sapi_token_key(c.voice.library_id)),"Post-error commit lost its verified outcome"); c.cleared();
    }
    {
        auto c=make(); c.interrupt(1); const auto data=inventory(c.transaction()/L"retained");
        c.registry->deny=true; rejected([&] { (void)c.library.recover_removal(c.voice.library_id,c.query()); }); c.registry->deny=false;
        check(inventory(c.transaction()/L"retained")==data,"Unreadable token outcome destroyed voice");
        (void)c.library.recover_removal(c.voice.library_id,c.query()); c.original_intact(); c.cleared();
    }
    for (const auto* field:{L"OutLoudManagedOwner",L"Seed"}) {
        auto c=make(); c.interrupt(1); const auto data=inventory(c.transaction()); const auto key=managed_sapi_token_key(c.voice.library_id);
        c.registry->tokens[key].values[field]=sapi_registry_string(L"changed"); const auto foreign=c.registry->tokens;
        rejected([&] { (void)c.library.recover_removal(c.voice.library_id,c.query()); });
        check(inventory(c.transaction())==data && c.registry->tokens==foreign,"Foreign/changed token recovery mutated evidence");
        c.registry->tokens[key]=c.token; (void)c.library.recover_removal(c.voice.library_id,c.query()); c.original_intact(); c.cleared();
    }
    {
        auto c=make(); c.interrupt(1); const auto transaction=c.transaction(); const auto data=inventory(transaction/L"retained");
        {
            OutLoudVoice held(transaction/L"retained"/L"voice.olv");
            rejected([&] { (void)c.library.recover_removal(c.voice.library_id,c.query()); });
            check(inventory(transaction/L"retained")==data,"Locked recovery force-deleted voice");
        }
        (void)c.library.recover_removal(c.voice.library_id,c.query()); c.original_intact(); c.cleared();
    }
    {
        auto c=make(); c.interrupt(1); const auto transaction=c.transaction();
        audit::probe=[](unsigned step) { if (step==9) { throw std::runtime_error("Interrupted recovery after restoration"); } };
        rejected([&] { (void)c.library.recover_removal(c.voice.library_id,c.query()); }); audit::probe=nullptr;
        c.original_intact(); check(std::filesystem::exists(transaction/L"remove.json"),"Interrupted restoration lost journal");
        (void)c.library.recover_removal(c.voice.library_id,c.query()); c.original_intact(); c.cleared();
    }
    for (unsigned malformed=0;malformed<5;++malformed) {
        auto c=make(); c.interrupt(1); const auto transaction=c.transaction(), journal=transaction/L"remove.json";
        const auto original_journal=inventory(transaction).at(journal); const auto retained=inventory(transaction/L"retained");
        auto bad=original_journal;
        if (!malformed) { bad="{corrupted"; }
        else if (malformed==1) { bad.replace(bad.find(c.voice.library_id),c.voice.library_id.size(),"../outside"); }
        else if (malformed==2) { bad.insert(1,"\"path\":\"C:\\\\arbitrary\","); }
        else if (malformed==3) { bad.replace(bad.find("prepared"),8,"unknown"); }
        else { bad.replace(bad.find("directory_id")+15,32,std::string(32,'0')); }
        { std::ofstream file(journal,std::ios::binary|std::ios::trunc); file << bad; }
        rejected([&] { (void)c.library.recover_removal(c.voice.library_id,c.query()); });
        const auto pending=c.library.cleanup_pending(c.query());
        check(!pending.pending.empty() && !pending.problems.empty() && inventory(transaction/L"retained")==retained,"Corrupt/malicious journal lost data or diagnosis");
        { std::ofstream file(journal,std::ios::binary|std::ios::trunc); file << original_journal; }
        (void)c.library.recover_removal(c.voice.library_id,c.query()); c.original_intact(); c.cleared();
    }
    {
        auto c=make(); c.interrupt(1); const auto transaction=c.transaction(), voice=transaction/L"retained"/L"voice.olv";
        const auto bytes=inventory(transaction/L"retained").at(voice); std::filesystem::remove(voice);
        const auto remaining=inventory(transaction/L"retained");
        rejected([&] { (void)c.library.recover_removal(c.voice.library_id,c.query()); });
        check(inventory(transaction/L"retained")==remaining,"Missing data caused remaining evidence deletion");
        { std::ofstream file(voice,std::ios::binary); file.write(bytes.data(),static_cast<std::streamsize>(bytes.size())); }
        (void)c.library.recover_removal(c.voice.library_id,c.query()); c.original_intact(); c.cleared();
    }
    {
        auto c=make(); c.interrupt(1); const auto transaction=c.transaction(), journal=transaction/L"remove.json", alias=scratch/L"journal-hardlink";
        check(CreateHardLinkW(alias.c_str(),journal.c_str(),nullptr)!=0,"Cannot create private hardlink fixture"); const auto data=inventory(transaction/L"retained");
        rejected([&] { (void)c.library.recover_removal(c.voice.library_id,c.query()); });
        check(inventory(transaction/L"retained")==data,"Hardlinked journal recovery lost data");
        check(DeleteFileW(alias.c_str())!=0,"Cannot remove private journal alias");
        (void)c.library.recover_removal(c.voice.library_id,c.query()); c.original_intact(); c.cleared();
    }
    {
        auto c=make(); c.interrupt(1); const auto transaction=c.transaction(), journal=transaction/L"remove.json";
        const auto control=inventory(transaction).at(journal); const auto data=inventory(transaction/L"retained");
        std::filesystem::remove(journal); const auto pending=c.library.cleanup_pending(c.query());
        check(!pending.pending.empty() && inventory(transaction/L"retained")==data,"Missing journal authorized blind trash deletion");
        { std::ofstream file(journal,std::ios::binary); file << control; }
        (void)c.library.recover_removal(c.voice.library_id,c.query()); c.original_intact(); c.cleared();
    }
    {
        auto c=make(); c.interrupt(1); const auto transaction=c.transaction(), voice=transaction/L"retained"/L"voice.olv";
        const auto bytes=inventory(transaction/L"retained").at(voice); auto corrupt=bytes; corrupt.back()^=1;
        { std::ofstream file(voice,std::ios::binary|std::ios::trunc); file.write(corrupt.data(),static_cast<std::streamsize>(corrupt.size())); }
        rejected([&] { (void)c.library.recover_removal(c.voice.library_id,c.query()); });
        check(inventory(transaction/L"retained").at(voice)==corrupt,"Corrupted retained data was overwritten/deleted");
        { std::ofstream file(voice,std::ios::binary|std::ios::trunc); file.write(bytes.data(),static_cast<std::streamsize>(bytes.size())); }
        (void)c.library.recover_removal(c.voice.library_id,c.query()); c.original_intact(); c.cleared();
    }
    {
        auto c=make(); const auto old=c.library.root()/L".trash"/L"dddddddddddddddddddddddddddddddd";
        std::filesystem::create_directory(old); std::filesystem::copy_file(c.voice.managed_olv_path,old/L"voice.olv"); const auto data=inventory(old);
        const auto pending=c.library.cleanup_pending(c.query());
        check(pending.pending==std::vector<std::filesystem::path>{old} && inventory(old)==data,"Unjournaled voice trash was treated as committed"); c.original_intact();
    }
    {
        auto c=make(); c.interrupt(1); const auto result=c.run();
        check(result.outcome==m::Outcome::removed && !std::filesystem::exists(c.voice.managed_olv_path.parent_path()),"Authorized selected-ID retry failed to recover then remove"); c.cleared();
    }
    std::cout << "PASS: " << index << " isolated removal recovery scenarios; exact data/token/evidence checks, no inference or real registry.\n";
}
