// Failed fresh setup only: reuse the maintenance archive audit, stamps and pins.
// No installed-host, voice, registry or certificate deletion is performed here.
struct WindowsFreshCleanup {
    WindowsUninstall audit;fs::path receipt;std::vector<MaintenanceJournal> owners;
    std::function<void()> authority;
    std::function<void(const MaintenanceJournal&)> absent;
    std::function<void(const fs::path&)> before_delete=[](const fs::path&){};
    explicit WindowsFreshCleanup(const fs::path& root):audit(root),receipt(audit.storage().root/L".outloud-fresh-cleanup.dat"){
        authority=[this]{audit.maintenance.authority();signing_require(!maintenance_host_record(),"maintenance_fresh_host_present");
            signing_require(!registry_capture(),"maintenance_fresh_registration_present");registry_overrides();};
        absent=[this](const MaintenanceJournal& j){
            Key provider,key;signing_require(NCryptOpenStorageProvider(&provider.value,MS_KEY_STORAGE_PROVIDER,0)==ERROR_SUCCESS,"key_provider_failed");
            signing_require(NCryptOpenKey(provider.value,&key.value,j.ownership.key_name.c_str(),0,NCRYPT_MACHINE_KEY_FLAG|NCRYPT_SILENT_FLAG)==NTE_BAD_KEYSET,"maintenance_fresh_key_not_absent");
            if(!j.ownership.thumbprint.empty()){
                WindowsSigning signer(storage().root,fs::path{},L"S-1-5-32-544",true);signer.record=j.ownership;
                for(const auto* name:{L"My",L"Root"}){Store store;signer.open_store(store,name,true);Certificate cert;cert.value=signer.find_owned(store.value);
                    signing_require(!cert.value,"maintenance_fresh_certificate_not_absent");}
            }
            if(!j.library_identity.empty())signing_require(!fs::exists(maintenance_library()),"maintenance_fresh_library_not_absent");
        };
    }
    MaintenanceFiles& storage(){return audit.storage();}
    void owner(const MaintenanceJournal& j){
        signing_require(j.operation==MaintenanceOperation::fresh && j.phase==MaintenancePhase::rolled_back && j.old_version.empty() &&
            j.root_encoding==maintenance_root_encoding(storage().root.native()) && j.registration_before==L"absent" &&
            j.installation_id==j.ownership.id && j.ownership.owner_sid==L"S-1-5-32-544" &&
            j.ownership.key_name==std::wstring(key_prefix)+j.installation_id,"maintenance_fresh_cleanup_owner_invalid");
        for(const auto& c:j.changes)signing_require(!c.before,"maintenance_fresh_original_resources_present");absent(j);
    }
    bool file_path(const fs::path& p)const{return p==audit.maintenance.files.root/L"configuration/maintenance.lock" ||
        uninstall_within(p,audit.maintenance.files.root/L"configuration/maintenance");}
    bool directory_path(const fs::path& p)const{const auto& root=audit.maintenance.files.root;
        return p==root || p==root/L"configuration" || p==root/L"configuration/maintenance" ||
            uninstall_within(p,root/L"configuration/maintenance") || p==root/L"bin" || p==root/L"runtime" || p==root/L"licenses";}
    void collect(){
        const auto id=maintenance_failed_fresh_audit(audit,audit.maintenance.package());
        signing_require(!id.empty(),"maintenance_fresh_cleanup_unowned_root");
        const auto base=storage().root/L"configuration/maintenance";
        for(const auto& e:fs::directory_iterator(base)){
            MaintenanceJournal last;for(unsigned n=0;n<256 && fs::exists(e.path()/(L"state-"+std::to_wstring(n)+L".dat"));++n)
                last=maintenance_decode(storage().read(e.path()/(L"state-"+std::to_wstring(n)+L".dat")),audit.maintenance.package());
            owner(last);signing_require(owners.size()<256,"maintenance_fresh_cleanup_limit");owners.push_back(last);
        }
        const auto lock=storage().root/L"configuration/maintenance.lock";if(fs::exists(lock))audit.add(3,lock);
        for(const auto* relative:{L"",L"configuration",L"bin",L"runtime",L"licenses"}){
            const auto p=*relative?storage().root/relative:storage().root;if(fs::exists(p))audit.directory(p);
        }
    }
    void persist(){
        MaintenanceWriter w;w.put(L"OutLoud.FreshCleanup.1");w.put(maintenance_root_encoding(storage().root.native()));
        w.number(owners.size());for(const auto& j:owners)w.put(maintenance_root_encoding(maintenance_encode(j)));
        w.number(audit.plan.files.size());for(const auto& f:audit.plan.files){w.put(maintenance_root_encoding(f.path.native()));w.file(f.stamp);}
        w.number(audit.plan.directories.size());for(const auto& [p,id]:audit.plan.directories){w.put(maintenance_root_encoding(p.native()));w.put(id);}
        signing_require(w.text.size()<=1024*1024,"maintenance_fresh_cleanup_limit");write_new(receipt,w.text,storage().protected_state);
    }
    void load(){
        const auto text=storage().read(receipt);MaintenanceReader r(text);
        signing_require(r.get()==L"OutLoud.FreshCleanup.1" && r.get()==maintenance_root_encoding(storage().root.native()),"maintenance_fresh_cleanup_plan_invalid");
        const auto count=r.number();signing_require(count>0 && count<=256,"maintenance_fresh_cleanup_limit");std::wstring installation;
        for(uint64_t n=0;n<count;++n){const auto hex_text=r.get();signing_require(!hex_text.empty() && hex_text.size()%4==0 &&
            hex_text.find_first_not_of(L"0123456789abcdef")==hex_text.npos,"maintenance_fresh_cleanup_plan_invalid");
            std::wstring journal;for(size_t i=0;i<hex_text.size();i+=4)journal+=static_cast<wchar_t>(std::stoul(hex_text.substr(i,2),nullptr,16)|(std::stoul(hex_text.substr(i+2,2),nullptr,16)<<8));
            auto j=maintenance_decode(journal,audit.maintenance.package());owner(j);
            signing_require(installation.empty() || installation==j.installation_id,"maintenance_fresh_cleanup_owner_invalid");installation=j.installation_id;owners.push_back(j);
        }
        const auto files=r.number();signing_require(files>0 && files<=8192,"maintenance_fresh_cleanup_limit");std::set<fs::path> unique;
        for(uint64_t n=0;n<files;++n){const fs::path p=uninstall_path_decode(r.get());const auto stamp=r.file();
            signing_require(file_path(p) && stamp && unique.insert(p).second,"maintenance_fresh_cleanup_plan_invalid");audit.plan.files.push_back({3,p,*stamp});}
        const auto dirs=r.number();signing_require(dirs>0 && dirs<=8192,"maintenance_fresh_cleanup_limit");unique.clear();
        for(uint64_t n=0;n<dirs;++n){const fs::path p=uninstall_path_decode(r.get());const auto id=r.get();
            signing_require(directory_path(p) && hexadecimal(id,48) && unique.insert(p).second,"maintenance_fresh_cleanup_plan_invalid");audit.plan.directories.emplace_back(p,id);}
        signing_require(unique.contains(storage().root) && r.cursor==r.text.size(),"maintenance_fresh_cleanup_plan_invalid");
    }
    void pin_and_check(){
        audit.add(4,receipt);
        const auto epoch=storage().root/L"configuration/maintenance.lock";if(fs::exists(epoch))storage().acquire_epoch(false);
        std::vector<fs::path> active;for(const auto& f:audit.plan.files)if(f.path!=epoch && fs::exists(f.path))active.push_back(f.path);maintenance_active_files(active);
        for(const auto& [p,id]:audit.plan.directories)if(fs::exists(p)){
            safe(p);storage().security(p);auto h=std::make_unique<Handle>();
            h->value=CreateFileW(p.c_str(),FILE_READ_ATTRIBUTES|DELETE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            signing_require(h->value!=INVALID_HANDLE_VALUE && maintenance_directory_identity(h->value)==id,"maintenance_fresh_directory_changed_or_locked");audit.directories.push_back({p,id,std::move(h)});
        }
        for(const auto& f:audit.plan.files)if(f.path!=epoch && fs::exists(f.path)){
            signing_require(storage().stamp(f.path)==MaintenanceFile(f.stamp),"maintenance_fresh_file_changed");storage().acquire(f.path);
        }
        for(const auto& d:audit.directories)for(const auto& e:fs::directory_iterator(d.path)){
            safe(e.path());signing_require(std::any_of(audit.plan.files.begin(),audit.plan.files.end(),[&](const auto& f){return f.path==e.path();}) ||
                std::any_of(audit.plan.directories.begin(),audit.plan.directories.end(),[&](const auto& p){return p.first==e.path();}),"maintenance_fresh_new_resource_preserved");
        }
        if(storage().epoch.value!=INVALID_HANDLE_VALUE){const auto expected=std::find_if(audit.plan.files.begin(),audit.plan.files.end(),[&](const auto& f){return f.path==epoch;});
            signing_require(expected!=audit.plan.files.end() && maintenance_file_stamp(storage().epoch.value)==expected->stamp,"maintenance_fresh_epoch_changed");}
        authority();for(const auto& j:owners)owner(j);
    }
    void remove_files(){
        const auto epoch=storage().root/L"configuration/maintenance.lock";
        for(const auto& f:audit.plan.files)if(f.group==3 && f.path!=epoch && fs::exists(f.path)){
            before_delete(f.path);signing_require(storage().stamp(f.path)==MaintenanceFile(f.stamp),"maintenance_fresh_file_changed");
            uninstall_delete_held(storage().held(f.stamp));for(auto& pin:storage().pins)if(pin.stamp==f.stamp){pin.handle.reset();break;}
        }
        storage().pins.erase(std::remove_if(storage().pins.begin(),storage().pins.end(),[](const auto& pin){return !pin.handle;}),storage().pins.end());
        if(fs::exists(epoch)){
            CloseHandle(storage().epoch.value);storage().epoch.value=INVALID_HANDLE_VALUE;
            const auto expected=std::find_if(audit.plan.files.begin(),audit.plan.files.end(),[&](const auto& f){return f.path==epoch;});
            signing_require(expected!=audit.plan.files.end() && storage().stamp(epoch)==MaintenanceFile(expected->stamp),"maintenance_fresh_epoch_changed");
            storage().acquire(epoch);uninstall_delete_held(storage().held(expected->stamp));
            for(auto& pin:storage().pins)if(pin.stamp==expected->stamp){pin.handle.reset();break;}
            storage().pins.erase(std::remove_if(storage().pins.begin(),storage().pins.end(),[](const auto& pin){return !pin.handle;}),storage().pins.end());
        }
    }
    void remove_directories(){
        std::sort(audit.directories.begin(),audit.directories.end(),[](const auto& a,const auto& b){return a.path.native().size()>b.path.native().size();});
        for(auto& d:audit.directories)if(d.path!=storage().root){
            before_delete(d.path);signing_require(fs::is_empty(d.path),"maintenance_fresh_directory_not_empty");uninstall_delete_held(d.pin->value);d.pin.reset();
        }
        for(const auto& e:fs::directory_iterator(storage().root))signing_require(e.path()==receipt,"maintenance_fresh_new_resource_preserved");
        authority();for(const auto& j:owners)owner(j); // keep the receipt if a resource has returned
        const auto stamp=storage().stamp(receipt);signing_require(stamp.has_value(),"maintenance_fresh_plan_missing");
        uninstall_delete_held(storage().held(*stamp));storage().release();
        for(auto& d:audit.directories)if(d.path==storage().root){signing_require(fs::is_empty(d.path),"maintenance_fresh_directory_not_empty");uninstall_delete_held(d.pin->value);d.pin.reset();}
    }
    void run(){
        authority();if(!fs::exists(storage().root))return;storage().security(storage().root);
        signing_require(!fs::exists(storage().root/L"configuration/maintenance.pending") && !fs::exists(storage().root/L".outloud-bootstrap.dat"),"maintenance_fresh_rollback_required");
        if(fs::exists(receipt))load();else{collect();persist();}
        pin_and_check();remove_files();remove_directories();
    }
};
