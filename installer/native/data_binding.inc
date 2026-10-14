// Installation data authority: a protected root identity, never a directory name alone.
fs::path maintenance_known_folder(REFKNOWNFOLDERID id){
    PWSTR raw{};signing_require(SUCCEEDED(SHGetKnownFolderPath(id,KF_FLAG_DONT_VERIFY,nullptr,&raw)),"data_known_folder_unavailable");
    const fs::path result(raw);CoTaskMemFree(raw);safe(result);return result;
}
fs::path maintenance_library(){return maintenance_known_folder(FOLDERID_ProgramData)/L"OutLoud/VoiceLibrary";}
void maintenance_library_security(const fs::path& path){
    safe(path);Local descriptor;PSID owner{};PACL acl{};
    signing_require(GetNamedSecurityInfoW(path.c_str(),SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,&owner,nullptr,&acl,nullptr,
        reinterpret_cast<PSECURITY_DESCRIPTOR*>(&descriptor.value))==ERROR_SUCCESS,"maintenance_library_security_unreadable");
    BYTE admins[SECURITY_MAX_SID_SIZE]{},system[SECURITY_MAX_SID_SIZE]{},users[SECURITY_MAX_SID_SIZE]{};DWORD a=sizeof(admins),s=sizeof(system),u=sizeof(users);
    SECURITY_DESCRIPTOR_CONTROL control{};DWORD revision{};
    signing_require(CreateWellKnownSid(WinBuiltinAdministratorsSid,nullptr,admins,&a) && CreateWellKnownSid(WinLocalSystemSid,nullptr,system,&s) &&
        CreateWellKnownSid(WinBuiltinUsersSid,nullptr,users,&u) && owner && (EqualSid(owner,admins) || EqualSid(owner,system)) &&
        GetSecurityDescriptorControl(descriptor.value,&control,&revision) && (control&SE_DACL_PROTECTED) && acl && IsValidAcl(acl) && acl->AceCount==3,"maintenance_library_security_invalid");
    bool seen_admin{},seen_system{},seen_user{};
    for(DWORD i=0;i<3;++i){void* raw{};signing_require(GetAce(acl,i,&raw),"maintenance_library_security_invalid");auto ace=static_cast<ACCESS_ALLOWED_ACE*>(raw);
        signing_require(ace->Header.AceType==ACCESS_ALLOWED_ACE_TYPE && (ace->Header.AceFlags&~INHERITED_ACE)==(OBJECT_INHERIT_ACE|CONTAINER_INHERIT_ACE),"maintenance_library_security_invalid");
        const auto sid=const_cast<DWORD*>(&ace->SidStart);bool* seen{};DWORD mask{};
        if(EqualSid(sid,admins)){seen=&seen_admin;mask=FILE_ALL_ACCESS;}else if(EqualSid(sid,system)){seen=&seen_system;mask=FILE_ALL_ACCESS;}
        else if(EqualSid(sid,users)){seen=&seen_user;mask=FILE_GENERIC_READ|FILE_GENERIC_EXECUTE;}
        signing_require(seen && !*seen && ace->Mask==mask,"maintenance_library_security_invalid");*seen=true;}
}
std::wstring maintenance_directory_identity(HANDLE handle){
    FILE_ID_INFO id{};BY_HANDLE_FILE_INFORMATION info{};
    signing_require(GetFileInformationByHandleEx(handle,FileIdInfo,&id,sizeof(id)) && GetFileInformationByHandle(handle,&info) &&
        (info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) && !(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT),"data_directory_identity_invalid");
    return hex(reinterpret_cast<const BYTE*>(&id.VolumeSerialNumber),8)+hex(id.FileId.Identifier,16);
}
std::wstring maintenance_directory_identity(const fs::path& path){
    safe(path);Handle h;h.value=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,
        OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    signing_require(h.value!=INVALID_HANDLE_VALUE,"data_directory_inaccessible");return maintenance_directory_identity(h.value);
}
std::wstring maintenance_library_binding(const MaintenanceJournal& j){
    return L"OutLoud.Data.1\n"+j.installation_id+L"\n"+j.root_encoding+L"\n"+j.library_identity+L"\n";
}
void maintenance_create_library(MaintenanceFiles& files,MaintenanceJournal& j){
    const auto root=maintenance_library();safe(root);
    signing_require(!fs::exists(root),"maintenance_unbound_library_preserved");
    const bool create_parent=!fs::exists(root.parent_path());maintenance_directory(root.parent_path(),false);
    maintenance_library_security(root.parent_path());
    if(create_parent){j.library_parent_identity=maintenance_directory_identity(root.parent_path());files.save(j);}
    maintenance_directory(root,false);
    j.library_identity=maintenance_directory_identity(root);files.save(j); // durable intent before marker
    write_new(root/L".outloud-installation.dat",maintenance_library_binding(j),true);
}
void maintenance_validate_library(MaintenanceFiles& files,const MaintenanceJournal& j){
    const auto root=maintenance_library();safe(root);maintenance_library_security(root.parent_path());maintenance_library_security(root);
    signing_require(hexadecimal(j.library_identity,48) && maintenance_directory_identity(root)==j.library_identity &&
        fs::exists(root/L".outloud-installation.dat") &&
        files.read(root/L".outloud-installation.dat")==maintenance_library_binding(j),"maintenance_library_binding_invalid");
}
void maintenance_restore_library(MaintenanceFiles& files,const MaintenanceJournal& j){
    if(j.library_identity.empty())return;const auto root=maintenance_library();safe(root);if(!fs::exists(root))return;
    require_admin_protected(root);signing_require(maintenance_directory_identity(root)==j.library_identity,"maintenance_library_binding_invalid");
    const auto marker=root/L".outloud-installation.dat";
    for(const auto& e:fs::directory_iterator(root))signing_require(e.path()==marker,"maintenance_library_recovery_data_preserved");
    if(fs::exists(marker)){signing_require(files.read(marker)==maintenance_library_binding(j),"maintenance_library_binding_invalid");
        signing_require(DeleteFileW(marker.c_str()),"maintenance_library_marker_cleanup_failed");}
    signing_require(RemoveDirectoryW(root.c_str()),"maintenance_library_cleanup_failed");
    if(!j.library_parent_identity.empty() && fs::exists(root.parent_path())){
        signing_require(maintenance_directory_identity(root.parent_path())==j.library_parent_identity,"maintenance_library_parent_changed");
        if(fs::is_empty(root.parent_path()))signing_require(RemoveDirectoryW(root.parent_path().c_str()),"maintenance_library_parent_cleanup_failed");}
}
