// Retire completed maintenance only. Reuse the archive audit, exclusive file
// pins and checked-handle deletion; never touch payload, models or registration.
// One immutable protected plan survives interruption of archive deletion.
struct WindowsArchiveCleanup {
    WindowsUninstall audit;
    MaintenanceJournal owner;
    fs::path receipt;
    std::wstring root_identity;
    std::vector<std::unique_ptr<Handle>> ancestors;
    std::function<void(const fs::path&)> before_delete=[](const fs::path&){};
    explicit WindowsArchiveCleanup(const fs::path& root,const MaintenanceJournal& expected)
        :audit(root),owner(expected),receipt(audit.storage().root/L"configuration/archive-cleanup.dat"){}
    MaintenanceFiles& storage(){return audit.storage();}
    fs::path base()const{return audit.maintenance.files.root/L"configuration/maintenance";}
    bool in_archive(const fs::path& path)const{
        const auto relative=path.lexically_relative(base());
        if(relative.empty() || relative.is_absolute() || relative.native().starts_with(L".."))return false;
        auto it=relative.begin();if(it==relative.end() || !hexadecimal(it->native(),32))return false;
        for(const auto& part:relative)if(part==L"." || part==L"..")return false;
        return true;
    }
    void collect(){
        audit.plan.owner=owner;audit.archives();
        // Archive audit also records current payload parent directories. They
        // are not part of archive retirement and must never be deleted here.
        auto& dirs=audit.plan.directories;
        dirs.erase(std::remove_if(dirs.begin(),dirs.end(),[&](const auto& d){return !in_archive(d.first);}),dirs.end());
        for(const auto& f:audit.plan.files)signing_require(f.group==3 && in_archive(f.path),"archive_cleanup_path_invalid");
    }
    void persist(){
        signing_require(audit.plan.files.size()<=uninstall_inventory_limit && audit.plan.directories.size()<=uninstall_inventory_limit,"archive_cleanup_plan_limit");
        MaintenanceWriter w;w.put(L"OutLoud.ArchiveCleanup.1");
        w.put(maintenance_root_encoding(storage().root.native()));w.put(root_identity);
        w.put(owner.installation_id);w.put(owner.ownership.certificate_sha256);
        w.number(audit.plan.files.size());
        for(const auto& f:audit.plan.files){const auto relative=f.path.lexically_relative(base()).generic_wstring();maintenance_payload_path(relative);w.put(relative);w.file(f.stamp);}
        w.number(audit.plan.directories.size());
        for(const auto& [p,id]:audit.plan.directories){const auto relative=p.lexically_relative(base()).generic_wstring();maintenance_payload_path(relative);w.put(relative);w.put(id);}
        signing_require(w.text.size()<=16*1024*1024,"archive_cleanup_plan_limit");
        write_new(receipt,w.text,storage().protected_state);
    }
    void load(){
        const auto text=storage().read(receipt);MaintenanceReader r(text);
        signing_require(r.get()==L"OutLoud.ArchiveCleanup.1" &&
            r.get()==maintenance_root_encoding(storage().root.native()) && r.get()==root_identity &&
            r.get()==owner.installation_id && r.get()==owner.ownership.certificate_sha256,"archive_cleanup_owner_invalid");
        const auto path=[&](){
            const auto relative=r.get();maintenance_payload_path(relative);
            const auto p=base()/relative;signing_require(in_archive(p),"archive_cleanup_path_invalid");return p;};
        const auto files=r.number();signing_require(files>0 && files<=uninstall_inventory_limit,"archive_cleanup_plan_limit");
        std::set<fs::path> unique;
        for(uint64_t n=0;n<files;++n){auto p=path();auto stamp=r.file();
            signing_require(stamp && unique.insert(p).second,"archive_cleanup_plan_invalid");audit.plan.files.push_back({3,p,*stamp});}
        const auto dirs=r.number();signing_require(dirs<=uninstall_inventory_limit,"archive_cleanup_plan_limit");unique.clear();
        for(uint64_t n=0;n<dirs;++n){auto p=path();auto id=r.get();
            signing_require(hexadecimal(id,48) && unique.insert(p).second,"archive_cleanup_plan_invalid");
            audit.plan.directories.emplace_back(p,id);}
        signing_require(r.cursor==r.text.size(),"archive_cleanup_plan_trailing_data");
    }
    void pin_and_check(){
        // The caller holds the existing maintenance epoch throughout collection,
        // publication and retirement. Pin every surviving resource before deleting.
        signing_require(maintenance_directory_identity(storage().root)==root_identity,"archive_cleanup_root_changed");
        for(const auto& p:{storage().root,storage().root/L"configuration",base()})if(fs::exists(p)){
            safe(p);storage().security(p);auto h=std::make_unique<Handle>();
            h->value=CreateFileW(p.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,
                OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            signing_require(h->value!=INVALID_HANDLE_VALUE,"archive_cleanup_ancestor_locked");ancestors.push_back(std::move(h));
        }
        std::vector<fs::path> active;
        for(const auto& f:audit.plan.files)if(fs::exists(f.path))active.push_back(f.path);
        maintenance_active_files(active);
        for(const auto& [p,id]:audit.plan.directories)if(fs::exists(p)){
            safe(p);storage().security(p);auto h=std::make_unique<Handle>();
            h->value=CreateFileW(p.c_str(),FILE_READ_ATTRIBUTES|DELETE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,
                OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            signing_require(h->value!=INVALID_HANDLE_VALUE && maintenance_directory_identity(h->value)==id,"archive_cleanup_directory_changed_or_locked");
            audit.directories.push_back({p,id,std::move(h)});
        }
        for(const auto& f:audit.plan.files)if(fs::exists(f.path)){
            signing_require(storage().stamp(f.path)==MaintenanceFile(f.stamp),"archive_cleanup_file_changed");storage().acquire(f.path);}
        storage().acquire(receipt);
    }
    void remove(){
        for(const auto& f:audit.plan.files)if(fs::exists(f.path)){
            before_delete(f.path);signing_require(storage().stamp(f.path)==MaintenanceFile(f.stamp),"archive_cleanup_file_changed");
            uninstall_delete_held(storage().held(f.stamp));
            for(auto& pin:storage().pins)if(pin.stamp==f.stamp){pin.handle.reset();break;}
            signing_require(!fs::exists(f.path),"archive_cleanup_delete_pending");
        }
        std::sort(audit.directories.begin(),audit.directories.end(),[](const auto& a,const auto& b){return a.path.native().size()>b.path.native().size();});
        for(auto& d:audit.directories){
            before_delete(d.path);
            // A file added by another actor is not deletion authority. Keep its
            // directory; cleanup has still retired all recorded owned resources.
            if(fs::is_empty(d.path)){uninstall_delete_held(d.pin->value);d.pin.reset();}
        }
        for(const auto& f:audit.plan.files)signing_require(!fs::exists(f.path),"archive_cleanup_files_remain");
        const auto stamp=storage().stamp(receipt);signing_require(stamp.has_value(),"archive_cleanup_plan_missing");
        uninstall_delete_held(storage().held(*stamp));storage().release();
    }
    void run(){
        storage().security(storage().root);root_identity=maintenance_directory_identity(storage().root);
        signing_require(!fs::exists(storage().root/L"configuration/maintenance.pending") &&
            !fs::exists(storage().root/L".outloud-bootstrap.dat"),"archive_cleanup_active_maintenance");
        if(fs::exists(receipt))load();else{collect();if(audit.plan.files.empty())return;persist();}
        pin_and_check();remove();
    }
};

// Uninstall can consume an interrupted retirement plan without first repairing
// or updating the application. No mutation is performed during preflight.
std::set<fs::path> uninstall_pending_archive_cleanup(WindowsUninstall& op){
    WindowsArchiveCleanup cleanup(op.storage().root,op.plan.owner);
    cleanup.storage().security=op.storage().security;
    const auto receipt=op.storage().stamp(cleanup.receipt);if(!receipt)return {};
    cleanup.root_identity=maintenance_directory_identity(op.storage().root);cleanup.load();
    std::set<fs::path> roots;
    for(const auto& f:cleanup.audit.plan.files){
        const auto relative=f.path.lexically_relative(cleanup.base());roots.insert(cleanup.base()/ *relative.begin());
        if(fs::exists(f.path))op.add(3,f.path,f.stamp);
    }
    for(const auto& [p,id]:cleanup.audit.plan.directories)if(fs::exists(p)){
        signing_require(maintenance_directory_identity(p)==id,"archive_cleanup_directory_changed_or_locked");op.directory(p);}
    op.add(3,cleanup.receipt,receipt);return roots;
}

void WindowsMaintenance::finish(MaintenanceOutcome outcome){
    if(outcome==MaintenanceOutcome::awaiting_host)return;
    files.retire(active);
    // Failed fresh setup has its own checked cleanup of signing/library state.
    if(active.phase==MaintenancePhase::committed || active.operation!=MaintenanceOperation::fresh){
        try{
        WindowsArchiveCleanup cleanup(files.root,active);cleanup.storage().security=files.security;
        cleanup.storage().protected_state=files.protected_state;cleanup.run();
        // A retry may have consumed an older immutable cleanup plan. Collect
        // this just-completed operation too, without growing another history.
        WindowsArchiveCleanup remaining(files.root,active);remaining.storage().security=files.security;
        remaining.storage().protected_state=files.protected_state;remaining.run();
        archive_cleanup_completed=true;
        }catch(const std::exception&){
            // Host/payload are already committed. Report cleanup separately;
            // throwing here would make Inno abort its completed host metadata.
            archive_cleanup_pending=true;
        }
    }
}
