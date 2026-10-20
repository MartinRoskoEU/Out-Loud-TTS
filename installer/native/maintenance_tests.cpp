#define OUTLOUD_MAINTENANCE_CHECK
#include "signing.cpp"
namespace {
unsigned checks{};
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);++checks;}
template<class F> void refuses(F&& f,const char* code){bool found=false;std::string observed="no refusal";try{f();}catch(const SigningError& e){observed=e.what();found=observed.find(code)!=std::string::npos;}
    if(!found)throw std::runtime_error(std::string("expected ")+code+", observed "+observed);check(found,code);}
struct Fixture {
    MaintenanceFiles files;MaintenanceJournal state;std::vector<std::wstring> payload;std::wstring registry=L"abcd",host_registry;
    unsigned generation{},reuse{};bool host_confirmed=true;std::string fault;bool interrupt{},failed{};unsigned publications{};
    Fixture(const fs::path& root,MaintenanceOperation operation=MaintenanceOperation::development,MaintenanceComponents components=MaintenanceComponents::both,bool reopen=false):files(root){
        payload={std::begin(registration_payload_paths),std::end(registration_payload_paths)};
        files.security=[root](const fs::path& p){safe(p);const auto relative=p.lexically_relative(root);signing_require(!relative.empty() && !relative.native().starts_with(L".."),"fixture_escape");};
        files.directory=[](const fs::path& p,bool){safe(p);if(!fs::exists(p))signing_require(fs::create_directory(p),"fixture_directory_failed");};files.protected_state=false;
        if(reopen){state=files.load(payload);return;}
        fs::create_directories(root/L"configuration");
        state.transaction_id=maintenance_random();state.installation_id=maintenance_random();state.root_encoding=maintenance_root_encoding(root.native());
        state.operation=operation;state.components=components;state.host=operation==MaintenanceOperation::development?MaintenanceHost::developer:MaintenanceHost::inno;
        state.old_version=operation==MaintenanceOperation::fresh?L"":L"0.1.0";state.new_version=L"0.1.0";
        state.ownership={state.installation_id,L"S-1-5-32-544",std::wstring(key_prefix)+state.installation_id,std::wstring(40,L'b'),std::wstring(64,L'c'),std::wstring(64,L'd')};
        state.registration_before=operation==MaintenanceOperation::fresh?L"absent":L"abcd";state.registration_after=state.registration_before;registry=state.registration_before;
        auto selected=maintenance_selection(components,payload);selected.insert(selected.end(),std::begin(maintenance_metadata),std::end(maintenance_metadata));
        for(const auto& p:selected){MaintenanceChange c;c.path=p;
            if(operation!=MaintenanceOperation::fresh){files.parents(files.path(p),false);write_new(files.path(p),L"original "+p);c.before=files.current(p);}
            if(std::find(payload.begin(),payload.end(),p)!=payload.end())c.input_hash=std::wstring(64,L'a');state.changes.push_back(c);}
        fs::create_directories(root/L"untouched");write_new(root/L"untouched/settings.txt",L"keep settings");write_new(root/L"untouched/export.olv",L"keep external voice fixture");
    }
    std::vector<std::wstring> package()const{return payload;}
    void point(const char* name){if(!failed && fault==name){failed=true;if(interrupt)throw MaintenanceInterrupted{};throw SigningError("fixture_fault");}}
    MaintenanceJournal preflight(){for(const auto& c:state.changes)signing_require(files.current(c.path)==c.before,"fixture_baseline_changed");return state;}
    void begin(const MaintenanceJournal& j){files.begin(j);}
    void save(const MaintenanceJournal& j){state=j;files.save(j);if(j.phase==MaintenancePhase::committed)point("after_commit");}
    MaintenanceJournal load(){state=files.load(payload);return state;}
    void stage(MaintenanceJournal& j){for(auto& c:j.changes)if(!c.input_hash.empty()){
        const auto p=files.vault/L"new"/c.path;files.parents(p,true);write_new(p,L"replacement "+c.path);c.after=files.stamp(p);}point("stage");}
    void sign_staged(MaintenanceJournal& j){
        if(j.operation==MaintenanceOperation::fresh)++generation;else ++reuse;
        j.resource_phase=L"verified";
        const auto engine=std::find_if(j.changes.begin(),j.changes.end(),[](const auto& c){return c.path==signing_target;});
        if(engine!=j.changes.end())j.ownership.signed_sha256=engine->after->hash;
        if(j.operation==MaintenanceOperation::fresh)j.registration_after=L"1234";
        save(j);point("signing");
    }
    void prepare_metadata(MaintenanceJournal& j){for(auto& c:j.changes)if(maintenance_is_metadata(c.path)){
        const auto p=files.vault/L"new"/c.path;files.parents(p,true);
        write_new(p,L"receipt "+c.path+L" "+j.ownership.signed_sha256+L" "+j.installation_id);c.after=files.stamp(p);}}
    void backup_all(MaintenanceJournal& j){for(auto& c:j.changes)if(c.before){files.copy(files.path(c.path),files.vault/L"backup"/c.path,c.before->hash);c.backup=files.backup(c.path);}save(j);}
    void lock_resources(const MaintenanceJournal& j){files.lock_files(j);}
    void revalidate(const MaintenanceJournal& j){for(const auto& c:j.changes)signing_require(files.current(c.path)==c.before,"fixture_baseline_changed");point("ready");}
    MaintenanceFile current(const std::wstring& p){return files.current(p);}
    MaintenanceFile backup(const std::wstring& p){return files.backup(p);}
    MaintenanceFile original(const std::wstring& p){return files.original(p);}
    void publish(const MaintenanceChange& c){files.publish(c);++publications;if(publications==2)point("files");if(c.path==L"configuration/signing.ini")point("metadata");}
    void publish_registration(const MaintenanceJournal& j){signing_require(registry==j.registration_before,"fixture_registration_conflict");registry=j.registration_after;
        if(maintenance_development_upgrade(j)){signing_require(host_registry==j.host_before,"fixture_foreign_host");host_registry=j.host_after;}point("registration");}
    void verify_committed(const MaintenanceJournal& j){for(const auto& c:j.changes)signing_require(current(c.path)==c.after,"fixture_commit_invalid");signing_require(registry==j.registration_after,"fixture_registration_invalid");
        if(maintenance_development_upgrade(j))signing_require(host_registry==j.host_after,"fixture_foreign_host");}
    void verify_host(const MaintenanceJournal&){signing_require(host_confirmed,"fixture_host_unconfirmed");}
    void authenticate_recovery(const MaintenanceJournal& j){signing_require(j.installation_id==state.installation_id,"fixture_identity_mismatch");}
    void validate_recovery_resources(const MaintenanceJournal& j){signing_require(registry==j.registration_before || registry==j.registration_after,"fixture_foreign_registry");
        if(maintenance_development_upgrade(j))signing_require(host_registry==j.host_before || host_registry==j.host_after,"fixture_foreign_host");}
    void restore_registration(const MaintenanceJournal& j){validate_recovery_resources(j);registry=j.registration_before;host_registry=j.host_before;}
    void restore(const MaintenanceChange& c){files.restore(c);}
    void restore_resources(const MaintenanceJournal& j){if(j.operation==MaintenanceOperation::fresh)generation=0;}
    void verify_original(const MaintenanceJournal& j){for(const auto& c:j.changes)signing_require(current(c.path)==c.before,"fixture_rollback_invalid");signing_require(registry==j.registration_before,"fixture_registry_rollback_invalid");}
    void release_for_recovery(){files.release();}
    void preserved(){files.release();check(files.read(files.root/L"untouched/settings.txt")==L"keep settings","settings preserved");
        check(files.read(files.root/L"untouched/export.olv")==L"keep external voice fixture","voice preserved");}
};
// Real private files, receipt codec, diff planner, publication and recovery;
// certificate/registry/host operations stay simulated as in Fixture above.
struct InventoryFixture:Fixture {
    MaintenanceInventory baseline;std::map<std::wstring,std::wstring> incoming;
    fs::path candidates;std::wstring original_inventory;
    InventoryFixture(const fs::path& root,const std::vector<std::wstring>& old_paths,const std::vector<std::wstring>& new_paths,
            MaintenanceOperation op=MaintenanceOperation::release):Fixture(root,op,MaintenanceComponents::package),candidates(root.native()+L"-candidates"){
        for(const auto& p:payload)if(std::find(old_paths.begin(),old_paths.end(),p)==old_paths.end())check(fs::remove(files.path(p)),"creator removes fixture-only file");
        for(const auto& p:old_paths){if(!fs::exists(files.path(p))){files.parents(files.path(p),false);write_new(files.path(p),L"original "+p);}
            baseline.files.emplace(p,*files.current(p));}
        payload=new_paths;state.library_identity=std::wstring(48,L'd');state.library_parent_identity=std::wstring(48,L'e');
        baseline.installation_id=state.installation_id;baseline.certificate_hash=state.ownership.certificate_sha256;
        baseline.generation=maintenance_random();baseline.version=state.old_version;baseline.library_identity=state.library_identity;baseline.library_parent_identity=state.library_parent_identity;
        state.ownership.signed_sha256=baseline.files.at(signing_target).hash;
        const auto inventory=files.path(L"configuration/inventory.dat");check(fs::remove(inventory),"creator replaces fixture receipt");
        original_inventory=maintenance_inventory_encode(baseline);write_new(inventory,original_inventory);
        for(const auto& p:payload){const auto source=candidates/p;files.parents(source,false);write_new(source,L"replacement "+p);
            Handle candidate;pin(source,candidate);incoming.emplace(p,file_hash(candidate.value));}
    }
    MaintenanceJournal preflight(){
        baseline=maintenance_inventory_decode(files.read(files.path(L"configuration/inventory.dat")));
        state.changes=maintenance_inventory_changes(baseline,incoming,state.operation,state.components,[&](const auto& p){return files.current(p);});
        std::vector<std::wstring> paths;for(const auto& c:state.changes)paths.push_back(c.path);maintenance_path_budget(files.root,paths);return state;
    }
    void stage(MaintenanceJournal& j){for(auto& c:j.changes)if(!c.input_hash.empty()){
        files.copy(candidates/c.path,files.vault/L"new"/c.path,c.input_hash);c.after=files.stamp(files.vault/L"new"/c.path);save(j);}point("stage");}
    void prepare_metadata(MaintenanceJournal& j){
        const auto next=maintenance_next_inventory(baseline,j);
        for(auto& c:j.changes)if(maintenance_is_metadata(c.path)){
            const auto p=files.vault/L"new"/c.path;files.parents(p,true);
            write_new(p,c.path==L"configuration/inventory.dat"?maintenance_inventory_encode(next):L"receipt "+c.path+L" "+j.ownership.signed_sha256+L" "+j.installation_id);
            c.after=files.stamp(p);save(j);}
    }
    void publish(const MaintenanceChange& c){Fixture::publish(c);if(c.input_hash.empty() && !maintenance_is_metadata(c.path))point("removed");}
    void authenticate_recovery(const MaintenanceJournal& j){
        Fixture::authenticate_recovery(j);files.lock_files(j);
        const auto row=std::find_if(j.changes.begin(),j.changes.end(),[](const auto& c){return c.path==L"configuration/inventory.dat";});
        signing_require(row!=j.changes.end() && row->before,"fixture_missing_baseline_receipt");
        const auto source=current(row->path)==row->before?files.path(row->path):files.vault/L"backup"/row->path;
        if(source!=files.path(row->path))signing_require(files.stamp(source)==row->backup && row->backup,"fixture_backup_receipt_changed");
        baseline=maintenance_inventory_decode(files.read_held(source));maintenance_validate_baseline(baseline,j);
        signing_require(baseline.installation_id==j.installation_id && baseline.certificate_hash==j.ownership.certificate_sha256 &&
            baseline.library_identity==j.library_identity && baseline.library_parent_identity==j.library_parent_identity,"fixture_binding_changed");
    }
    void verify_committed(const MaintenanceJournal& j){
        Fixture::verify_committed(j);const auto recorded=maintenance_inventory_decode(files.read_held(files.path(L"configuration/inventory.dat")));
        const auto expected=maintenance_next_inventory(baseline,j);
        signing_require(recorded.files==expected.files && recorded.installation_id==baseline.installation_id && recorded.certificate_hash==baseline.certificate_hash &&
            recorded.library_identity==baseline.library_identity && recorded.library_parent_identity==baseline.library_parent_identity,"fixture_new_inventory_changed");point("verify");
    }
    void verify_original(const MaintenanceJournal& j){Fixture::verify_original(j);
        signing_require(files.read_held(files.path(L"configuration/inventory.dat"))==original_inventory,"fixture_original_inventory_not_restored");}
};
void inventory_checks(const fs::path& suite){
    const std::vector<std::wstring> current{std::begin(registration_payload_paths),std::end(registration_payload_paths)};
    auto old=current;std::erase(old,L"licenses/Sonic-Apache-2.0.txt");check(old.size()==23 && current.size()==24,"23-to-24 production inventory fixture");
    const auto child=[&](const wchar_t* p){return suite/p;};
    for(const auto mode:{MaintenanceOperation::release,MaintenanceOperation::repair}){
        InventoryFixture f(child(mode==MaintenanceOperation::release?L"custom-release-directory":L"custom-repair-directory"),old,current,mode);
        const auto id=f.state.installation_id,certificate=f.state.ownership.certificate_sha256,key=f.state.ownership.key_name;
        check(maintain_installation(f)==MaintenanceOutcome::awaiting_host,"23-to-24 reaches verified Inno boundary");
        auto j=f.load();check(recover_maintenance(f,j,true)==MaintenanceOutcome::committed,"23-to-24 finalizes");
        check(f.state.installation_id==id && f.state.ownership.certificate_sha256==certificate && f.state.ownership.key_name==key && f.generation==0 && f.reuse==1,"installation and certificate reused");
        f.files.release();const auto inventory=maintenance_inventory_decode(f.files.read(f.files.path(L"configuration/inventory.dat")));
        check(inventory.files.size()==24 && inventory.files.contains(L"licenses/Sonic-Apache-2.0.txt"),"new 24-file receipt includes Sonic license");
        check(f.files.read(f.files.path(L"licenses/Sonic-Apache-2.0.txt"))==L"replacement licenses/Sonic-Apache-2.0.txt","added file published");
        f.preserved();
        auto future=current;future.push_back(L"licenses/Future-Notice.txt");
        check(maintenance_decode(maintenance_encode(j),future)==j,"historical journal independent of next package");
    }
    auto obsolete=old;obsolete.push_back(L"licenses/Obsolete-Notice.txt");
    {
        InventoryFixture f(child(L"add-replace-remove"),obsolete,current);
        write_new(f.files.path(L"bin/user-notes.txt"),L"unowned user notes");
        check(maintain_installation(f)==MaintenanceOutcome::awaiting_host,"add/replace/remove succeeds");
        auto j=f.load();check(recover_maintenance(f,j,true)==MaintenanceOutcome::committed,"changed inventory committed");f.files.release();
        const auto inventory=maintenance_inventory_decode(f.files.read(f.files.path(L"configuration/inventory.dat")));
        check(!inventory.files.contains(L"licenses/Obsolete-Notice.txt") && !fs::exists(f.files.path(L"licenses/Obsolete-Notice.txt")),"obsolete owned file and entry removed");
        check(f.files.read(f.files.path(L"bin/user-notes.txt"))==L"unowned user notes","unknown file never removed");f.preserved();
        f.files.retire(f.state);
        const auto archived=maintenance_decode(f.files.read(f.files.vault/L"state-0.dat"),current);
        check(archived.changes.size()==current.size()+1+std::size(maintenance_metadata),"removal journal survives archive decode");
    }
    for(const auto* fault:{"removed","metadata","verify"}){
        std::wstring name;for(const auto* c=fault;*c;++c)name+=static_cast<wchar_t>(*c);
        InventoryFixture f(child((L"rollback-"+name).c_str()),obsolete,current);f.fault=fault;
        refuses([&]{static_cast<void>(maintain_installation(f));},"fixture_fault");auto j=f.load();
        check(j.phase==MaintenancePhase::rolled_back,"inventory-changing failure rolled back");f.verify_original(j);f.files.release();
        check(!fs::exists(f.files.path(L"licenses/Sonic-Apache-2.0.txt")) && fs::exists(f.files.path(L"licenses/Obsolete-Notice.txt")),"addition removed and retired original restored");
        check(f.files.read(f.files.path(L"configuration/inventory.dat"))==f.original_inventory,"original receipt bytes restored");f.preserved();
    }
    {
        InventoryFixture f(child(L"restart-after-removal"),obsolete,current);f.fault="removed";f.interrupt=true;
        bool interrupted=false;try{static_cast<void>(maintain_installation(f));}catch(const MaintenanceInterrupted&){interrupted=true;}
        check(interrupted,"process death after retirement injected");f.files.release();
        // A later helper must recover using the recorded transaction, not its
        // own (now different) compiled package.
        f.payload.push_back(L"licenses/Future-Notice.txt");auto j=f.load();
        check(recover_maintenance(f,j)==MaintenanceOutcome::rolled_back,"restart with different package restores original");f.verify_original(j);f.preserved();
    }
    {
        InventoryFixture f(child(L"collision"),old,current);const auto path=f.files.path(L"licenses/Sonic-Apache-2.0.txt");
        write_new(path,L"user-owned collision");refuses([&]{static_cast<void>(maintain_installation(f));},"maintenance_unowned_file_collision");
        check(f.files.read(path)==L"user-owned collision" && !fs::exists(f.files.root/L".outloud-bootstrap.dat"),"unknown addition collision refused before transaction");f.preserved();
    }
    {
        auto future=current;future.push_back(L"licenses/Future-Notice.txt");
        InventoryFixture f(child(L"future-upgrade"),current,future);
        check(maintain_installation(f)==MaintenanceOutcome::awaiting_host,"future 24-to-25 package accepted");
        auto j=f.load();check(recover_maintenance(f,j,true)==MaintenanceOutcome::committed,"future inventory finalizes");f.files.release();
        check(maintenance_inventory_decode(f.files.read(f.files.path(L"configuration/inventory.dat"))).files.size()==25,"future receipt records its own file count");f.preserved();
    }
    {
        InventoryFixture f(child(L"foreign-recovery"),obsolete,current);f.fault="removed";f.interrupt=true;
        try{static_cast<void>(maintain_installation(f));}catch(const MaintenanceInterrupted&){}
        f.files.release();auto j=f.load();const auto path=f.files.path(L"licenses/Obsolete-Notice.txt");write_new(path,L"new user-owned content");
        refuses([&]{static_cast<void>(recover_maintenance(f,j));},"maintenance_recovery_foreign_file");f.files.release();
        check(f.files.read(path)==L"new user-owned content","rollback preserves foreign replacement at retired path");
        check(j.phase==MaintenancePhase::publishing,"foreign edit refused before any rollback writes");f.preserved();
    }
    {
        InventoryFixture f(child(L"changed-obsolete"),obsolete,current);const auto path=f.files.path(L"licenses/Obsolete-Notice.txt");
        check(fs::remove(path),"creator replaces owned obsolete fixture");write_new(path,L"edited user content");
        refuses([&]{static_cast<void>(maintain_installation(f));},"maintenance_baseline_changed");
        check(f.files.read(path)==L"edited user content","modified obsolete file preserved");f.preserved();
    }
    {
        InventoryFixture f(child(L"receipt-validation"),old,current);
        for(const auto* bad:{L"../voice.olv",L"configuration/signing.ini",L"licenses/../voice.olv",L"bin/NUL.txt",L"bin/",L"/voice.olv"}){
            auto inventory=f.baseline;inventory.files.emplace(bad,inventory.files.begin()->second);
            refuses([&]{static_cast<void>(maintenance_inventory_decode(maintenance_inventory_encode(inventory)));},"maintenance_payload_path_invalid");}
        auto inventory=f.baseline;inventory.files.emplace(L"bin/outloudpocketsapi.DLL",inventory.files.at(signing_target));
        refuses([&]{static_cast<void>(maintenance_inventory_decode(maintenance_inventory_encode(inventory)));},"maintenance_inventory_paths_invalid");
        auto j=f.preflight();j.changes.push_back({L"licenses/Unowned.txt",L"",f.baseline.files.begin()->second,{},{}});
        refuses([&]{maintenance_validate_baseline(f.baseline,j);},"maintenance_unowned_file_collision");
    }
}
}
int wmain(int argc,wchar_t** argv){
    try{check(argc==2 || (argc==3 && std::wstring(argv[2])==L"inventory"),"supply a workspace fixture parent [inventory]");const auto parent=installation_path(argv[1]);safe(parent);check(fs::is_directory(parent),"fixture parent exists");
        const auto suite=parent/(L"maintenance-"+maintenance_random());check(fs::create_directory(suite),"new private fixture suite");
        const auto child=[&](const wchar_t* name){return suite/name;};
        if(argc==3){inventory_checks(suite);std::wcout<<L"PASS: "<<checks<<L" focused inventory upgrade checks; private files, simulated signing/registry. Evidence: "<<suite.native()<<L"\n";return 0;}
        {
            RegistrationTree host;host.values={{L"Publisher",registration_string(L"Nendis")},{L"DisplayVersion",registration_string(L"0.1.0")}};
            for(const auto* name:{L"Out Loud",L"Out Loud version 0.1.0",L"Out Loud 0.1.0"}){
                host.values[L"DisplayName"]=registration_string(name);maintenance_host_identity(host);check(true,"exact Inno host display name accepted");
                host.values[L"UninstallString"]=registration_string(L"\"C:\\Program Files\\OutLoud\\unins000.exe\"");
                host.children[L"unchanged"].values[L"opaque"]={REG_BINARY,{0,1,255}};
                const auto upgraded=maintenance_host_version(host,L"0.2.0");
                check(registration_field(upgraded,L"DisplayVersion")==L"0.2.0" && maintenance_host_version(upgraded,L"0.1.0")==host,
                    "host version roundtrip preserves all typed values and children");}
            host.values[L"DisplayName"]=registration_string(L"Out Loud 9.9.9");
            refuses([&]{maintenance_host_identity(host);},"maintenance_host_identity_invalid");
            host.values[L"DisplayName"]=registration_string(L"Out Loud");host.values[L"Publisher"]=registration_string(L"foreign");
            refuses([&]{maintenance_host_identity(host);},"maintenance_host_identity_invalid");
        }
        {
            wchar_t self[MAX_PATH]{};const auto length=GetModuleFileNameW(nullptr,self,MAX_PATH);
            signing_require(length>0 && length<MAX_PATH,"fixture_self_path_failed");const auto image=child(L"pinned-image.exe");
            check(fs::copy_file(self,image),"private PE fixture copied");const auto expected=pe_dependencies(image);Handle writable;pin(image,writable,true);
            static_cast<void>(file_hash(writable.value));
            check(pe_dependencies(writable.value)==expected,"dependency inspection reuses writable pin and resets its cursor");
            refuses([&]{static_cast<void>(pe_dependencies(image));},"dependency_inaccessible");
        }
        {
            const auto ascii=child(L"request.ini");write_new(ascii,L"[results]\nschema=1\nwindows=compatible\n");
            const auto expected=maintenance_ini(ascii);check(expected.at(L"results").at(L"schema")==L"1","ASCII request remains supported");
            const std::wstring report=L"\xfeff[results]\nschema=1\nwindows=compatible\n";
            const auto raw=reinterpret_cast<const BYTE*>(report.data());std::vector<BYTE> bytes(raw,raw+report.size()*sizeof(wchar_t));
            const auto write_bytes=[&](const wchar_t* name,const std::vector<BYTE>& data){const auto p=child(name);Handle h;
                h.value=CreateFileW(p.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
                signing_require(h.value!=INVALID_HANDLE_VALUE,"fixture_report_create_failed");DWORD written{};
                signing_require(WriteFile(h.value,data.data(),static_cast<DWORD>(data.size()),&written,nullptr) && written==data.size(),"fixture_report_write_failed");return p;};
            const auto encoded=write_bytes(L"prerequisites.ini",bytes);
            check(maintenance_ini(encoded,true)==expected,"detector UTF-16LE report agrees with ASCII fields");
            refuses([&]{static_cast<void>(maintenance_ini(encoded));},"maintenance_input_encoding_invalid");
            refuses([&]{static_cast<void>(maintenance_ini(ascii,true));},"maintenance_input_encoding_invalid");
            auto truncated=bytes;truncated.pop_back();const auto odd=write_bytes(L"odd.ini",truncated);
            refuses([&]{static_cast<void>(maintenance_ini(odd,true));},"maintenance_input_encoding_invalid");
            auto non_ascii=bytes;non_ascii[3]=1;const auto foreign=write_bytes(L"non-ascii.ini",non_ascii);
            refuses([&]{static_cast<void>(maintenance_ini(foreign,true));},"maintenance_input_encoding_invalid");
        }
        for(const auto mode:{MaintenanceComponents::engine,MaintenanceComponents::voice_manager,MaintenanceComponents::both}){
            Fixture f(child(mode==MaintenanceComponents::engine?L"engine":mode==MaintenanceComponents::voice_manager?L"manager":L"both"),MaintenanceOperation::development,mode);
            const auto selection=maintenance_selection(mode,f.payload);check(selection.size()==(mode==MaintenanceComponents::engine?1:mode==MaintenanceComponents::voice_manager?6:7),"exact selection");
            check(maintain_installation(f)==MaintenanceOutcome::committed,"development committed");check(f.generation==0 && f.reuse==1,"certificate reused");
            f.verify_committed(f.state);check(maintenance_decode(maintenance_encode(f.state),f.payload)==f.state,"journal exact roundtrip");f.preserved();}
        for(const auto* failure:{"stage","signing","ready","files","registration","metadata","success","postflight","foreign"}){
            std::wstring name;for(const char* c=failure;*c;++c)name+=static_cast<wchar_t>(*c);
            Fixture f(child((L"dev-upgrade-"+name).c_str()),MaintenanceOperation::development,MaintenanceComponents::package);
            f.state.new_version=L"0.2.0";f.state.host_before=L"abcd";f.state.host_after=L"1234";f.host_registry=f.state.host_before;
            check(maintenance_decode(maintenance_encode(f.state),f.payload)==f.state,"upgrade v3 journal roundtrip");
            auto bad=f.state;bad.host_after.clear();refuses([&]{validate_maintenance(bad,f.payload);},"host_snapshot_invalid");
            bad=f.state;bad.new_version=bad.old_version;refuses([&]{validate_maintenance(bad,f.payload);},"development_identity_change");
            if(name==L"success" || name==L"postflight" || name==L"foreign"){
                check(maintain_installation(f)==MaintenanceOutcome::awaiting_host,"explicit developer upgrade awaits installed validation");
                f.files.release();auto j=f.load();
                if(name==L"foreign"){f.host_registry=L"ffff";refuses([&]{static_cast<void>(recover_maintenance(f,j));},"fixture_foreign_host");
                    check(j.phase==MaintenancePhase::awaiting_host,"foreign host prevents any rollback");f.host_registry=j.host_after;}
                const bool commit=name==L"success";
                check(recover_maintenance(f,j,commit)==(commit?MaintenanceOutcome::committed:MaintenanceOutcome::rolled_back),"installed validation commits or rolls back");
                check(f.host_registry==(commit?j.host_after:j.host_before),"host version follows payload transaction");
            }else{f.fault=failure;refuses([&]{static_cast<void>(maintain_installation(f));},"fixture_fault");
                f.files.release();auto j=f.load();f.verify_original(j);check(j.phase==MaintenancePhase::rolled_back && f.host_registry==j.host_before,"upgrade fault restores files and version metadata");}
            check(f.generation==0,"upgrade never creates another certificate");f.preserved();
        }
        for(const auto operation:{MaintenanceOperation::fresh,MaintenanceOperation::release,MaintenanceOperation::repair}){
            Fixture f(child(operation==MaintenanceOperation::fresh?L"fresh":operation==MaintenanceOperation::release?L"release":L"repair"),operation,MaintenanceComponents::package);
            check(maintain_installation(f)==MaintenanceOutcome::awaiting_host,"Inno host boundary");
            check(f.generation==(operation==MaintenanceOperation::fresh?1u:0u),"fresh-only certificate creation simulated");
            f.files.release();auto j=f.load();check(recover_maintenance(f,j,true)==MaintenanceOutcome::committed,"host finalized");f.preserved();}
        for(const auto* failure:{"stage","signing","ready","files","registration","metadata"}){
            std::wstring name;for(const char* c=failure;*c;++c)name+=static_cast<wchar_t>(*c);Fixture f(child((L"fail-"+name).c_str()));f.fault=failure;
            refuses([&]{static_cast<void>(maintain_installation(f));},"fixture_fault");auto j=f.load();check(j.phase==MaintenancePhase::rolled_back,"failure restored original installation");f.verify_original(j);f.preserved();}
        {Fixture f(child(L"interrupted"));f.fault="metadata";f.interrupt=true;try{static_cast<void>(maintain_installation(f));}catch(const MaintenanceInterrupted&){}
            f.files.release();auto j=f.load();check(j.phase==MaintenancePhase::metadata,"interruption retains durable phase");
            check(recover_maintenance(f,j)==MaintenanceOutcome::rolled_back,"interrupted publication/receipts rollback");f.preserved();}
        {Fixture f(child(L"foreign"));f.fault="files";f.interrupt=true;try{static_cast<void>(maintain_installation(f));}catch(const MaintenanceInterrupted&){}
            f.files.release();auto j=f.load();const auto& last=j.changes.back();fs::remove(f.files.path(last.path));write_new(f.files.path(last.path),L"foreign bytes");
            refuses([&]{static_cast<void>(recover_maintenance(f,j));},"foreign_file");check(j.phase==MaintenancePhase::publishing,"foreign state not overwritten, evidence retained");f.preserved();}
        {Fixture f(child(L"locked"));Handle lock;lock.value=CreateFileW(f.files.path(signing_target).c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);
            check(lock.value!=INVALID_HANDLE_VALUE,"fixture lock acquired");refuses([&]{static_cast<void>(maintain_installation(f));},"inaccessible_or_locked");check(f.publications==0,"lock prevents every publication");}
        {Fixture f(child(L"commit-marker"));f.fault="after_commit";f.interrupt=true;try{static_cast<void>(maintain_installation(f));}catch(const MaintenanceInterrupted&){}
            f.files.release();auto j=f.load();check(recover_maintenance(f,j)==MaintenanceOutcome::committed,"durable commit not undone after interrupted report");f.preserved();}
        {const auto root=child(L"reopened");std::wstring actual_registry;
            {Fixture f(root);f.fault="metadata";f.interrupt=true;try{static_cast<void>(maintain_installation(f));}catch(const MaintenanceInterrupted&){}
                actual_registry=f.registry;}
            Fixture reopened(root,MaintenanceOperation::development,MaintenanceComponents::both,true);reopened.registry=actual_registry;
            auto j=reopened.state;check(recover_maintenance(reopened,j)==MaintenanceOutcome::rolled_back,"recreated controller recovers without old handles or memory");reopened.preserved();}
        {Fixture f(child(L"exclusive"));f.files.begin(f.state);MaintenanceFiles competitor(f.files.root);competitor.security=f.files.security;
            refuses([&]{static_cast<void>(competitor.load(f.payload));},"maintenance_already_active");check(fs::exists(f.files.root/L"configuration/maintenance.pending"),"competing controller preserves journal");}
        {Fixture f(child(L"host-refusal"),MaintenanceOperation::release,MaintenanceComponents::package);check(maintain_installation(f)==MaintenanceOutcome::awaiting_host,"host pending");
            auto j=f.load();f.host_confirmed=false;refuses([&]{static_cast<void>(recover_maintenance(f,j,true));},"fixture_host_unconfirmed");
            check(j.phase==MaintenancePhase::awaiting_host,"unconfirmed Inno identity cannot commit");}
        {Fixture f(child(L"host-resume"),MaintenanceOperation::fresh,MaintenanceComponents::package);
            check(maintain_installation(f)==MaintenanceOutcome::awaiting_host,"fresh native payload ready");
            f.files.release();auto j=f.load();f.host_confirmed=false;
            check(reconcile_maintenance(f,j)==MaintenanceOutcome::awaiting_host,"missing or partial Inno host resumes without rollback");
            check(j.phase==MaintenancePhase::awaiting_host,"host resume keeps durable intent");f.verify_committed(j);f.preserved();}
        {Fixture f(child(L"bootstrap"),MaintenanceOperation::fresh,MaintenanceComponents::package);
            write_new(f.files.root/L".outloud-bootstrap.dat",maintenance_encode(f.state));
            auto j=f.load();check(j==f.state,"bootstrap authority reconstructs missing pointer and initial frame");
            check(!fs::exists(f.files.root/L".outloud-bootstrap.dat"),"bootstrap retired only after pointer publication");
            check(reconcile_maintenance(f,j)==MaintenanceOutcome::rolled_back,"unpublished bootstrap safely rolls back");f.preserved();}
        {Fixture f(child(L"bootstrap-frame"),MaintenanceOperation::fresh,MaintenanceComponents::package);
            write_new(f.files.root/L".outloud-bootstrap.dat",maintenance_encode(f.state));
            const auto vault=f.files.root/L"configuration/maintenance"/f.state.transaction_id;fs::create_directories(vault);
            write_new(vault/L"frame-123-0.tmp",maintenance_encode(f.state));
            write_new(f.files.root/L"configuration/maintenance.pending.tmp",f.state.transaction_id+L"\n");
            check(f.load()==f.state,"complete interrupted frame and pointer can be published");
            check(!fs::exists(vault/L"frame-123-0.tmp"),"promotion retains one authoritative initial frame");}
        {Fixture f(child(L"bootstrap-corrupt"),MaintenanceOperation::fresh,MaintenanceComponents::package);
            write_new(f.files.root/L".outloud-bootstrap.dat",L"truncated");
            refuses([&]{static_cast<void>(f.load());},"maintenance_journal_invalid");
            check(!fs::exists(f.files.root/L"configuration/maintenance.pending"),"corrupt bootstrap creates no pending authority");}
        {const auto payload=std::vector<std::wstring>{std::begin(registration_payload_paths),std::end(registration_payload_paths)};
            maintenance_path_budget(fs::path(L"C:/")/std::wstring(110,L'x'),payload);check(true,"root beyond former 100-character cap fits actual vault");
            refuses([&]{maintenance_path_budget(fs::path(L"C:/")/std::wstring(100,L'x')/std::wstring(100,L'y'),payload);},"maintenance_path_budget_exceeded");}
        {Fixture f(child(L"corrupt"));f.files.begin(f.state);const auto frame=f.files.vault/L"state-0.dat";write_new(f.files.vault/L"unrelated-evidence.txt",L"retain");
            Handle h;h.value=CreateFileW(frame.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);check(h.value!=INVALID_HANDLE_VALUE,"test owns journal");
            LARGE_INTEGER end{};end.QuadPart=3;check(SetFilePointerEx(h.value,end,nullptr,FILE_BEGIN) && SetEndOfFile(h.value),"truncate owned frame");CloseHandle(h.value);h.value=INVALID_HANDLE_VALUE;
            refuses([&]{static_cast<void>(f.load());},"journal_invalid");check(fs::exists(frame),"corrupt journal preserved");}
        for(unsigned mode=0;mode<7;++mode){
            Fixture f(child((L"fresh-cleanup-"+std::to_wstring(mode)).c_str()),MaintenanceOperation::fresh,MaintenanceComponents::package);
            // These two unrelated fixture files are deliberately outside product
            // ownership. Only the clean-case fixtures remove their own test data.
            if(mode!=1){check(fs::remove(f.files.root/L"untouched/settings.txt") && fs::remove(f.files.root/L"untouched/export.olv") &&
                fs::remove(f.files.root/L"untouched"),"private fixture data removed by its creator");}
            f.fault="signing";refuses([&]{static_cast<void>(maintain_installation(f));},"fixture_fault");f.files.retire(f.state);
            CloseHandle(f.files.epoch.value);f.files.epoch.value=INVALID_HANDLE_VALUE;
            const auto setup=[&](WindowsFreshCleanup& cleanup){cleanup.storage().security=f.files.security;cleanup.storage().protected_state=false;
                cleanup.authority=[]{};cleanup.absent=[](const MaintenanceJournal&){};};
            WindowsFreshCleanup cleanup(f.files.root);setup(cleanup);
            if(mode==1){refuses([&]{cleanup.run();},"maintenance_fresh_directory_not_empty");
                check(fs::exists(f.files.root/L"untouched/export.olv") && !fs::exists(cleanup.receipt),"unknown data preserved before cleanup receipt");continue;}
            if(mode==2){cleanup.authority=[](){throw SigningError("fixture_host_present");};
                refuses([&]{cleanup.run();},"fixture_host_present");check(!fs::exists(cleanup.receipt),"host refusal precedes cleanup writes");continue;}
            if(mode==5){cleanup.absent=[](const MaintenanceJournal&){throw SigningError("fixture_key_present");};
                refuses([&]{cleanup.run();},"fixture_key_present");check(!fs::exists(cleanup.receipt),"resource absence refusal precedes cleanup writes");continue;}
            if(mode==6){cleanup.collect();cleanup.persist();const auto path=cleanup.audit.plan.files.front().path;
                {Handle writable;pin(path,writable,true);const char changed='x';DWORD written{};
                    signing_require(WriteFile(writable.value,&changed,1,&written,nullptr) && written==1,"fixture_change_failed");}
                WindowsFreshCleanup changed(f.files.root);setup(changed);refuses([&]{changed.run();},"maintenance_fresh_file_changed");
                check(fs::exists(changed.receipt) && fs::exists(path),"changed resource and recovery receipt preserved");continue;}
            if(mode==3 || mode==4){
                unsigned deleted{};cleanup.before_delete=[&](const fs::path&){if(++deleted==2)throw MaintenanceInterrupted{};};
                try{cleanup.run();throw std::runtime_error("expected cleanup interruption");}catch(const MaintenanceInterrupted&){}
                check(fs::exists(cleanup.receipt),"cleanup receipt survives partial deletion");cleanup.storage().release();
                cleanup.audit.directories.clear();if(cleanup.storage().epoch.value!=INVALID_HANDLE_VALUE){CloseHandle(cleanup.storage().epoch.value);cleanup.storage().epoch.value=INVALID_HANDLE_VALUE;}
                if(mode==4){write_new(f.files.root/L"unexpected.txt",L"preserve incoming file");WindowsFreshCleanup changed(f.files.root);setup(changed);
                    refuses([&]{changed.run();},"maintenance_fresh_new_resource_preserved");
                    check(fs::exists(changed.receipt) && fs::exists(f.files.root/L"unexpected.txt"),"new resource and recovery receipt preserved");continue;}
                WindowsFreshCleanup resumed(f.files.root);setup(resumed);resumed.run();
            }else cleanup.run();
            check(!fs::exists(f.files.root),"verified failed fresh installation directory removed");
        }
        std::wcout<<L"PASS: "<<checks<<L" focused maintenance checks; private files with simulated signing/registry only. Evidence: "<<suite.native()<<L"\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
