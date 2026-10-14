// Only the logger's three fixed files and its installation marker are managed.
// No settings/cache directory currently exists in the application contract.
fs::path uninstall_profile_localappdata(const RegistrationValue& value,const fs::path& profile){
    signing_require(value.type==REG_SZ || value.type==REG_EXPAND_SZ,"uninstall_profile_localappdata_invalid");
    auto text_value=value;text_value.type=REG_SZ;const auto raw=registration_text(text_value);std::wstring path;
    if(value.type==REG_SZ)path=raw;
    else for(size_t cursor=0;cursor<raw.size();){
        const auto at=raw.find(L'%',cursor);if(at==raw.npos){path+=raw.substr(cursor);break;}
        path+=raw.substr(cursor,at-cursor);const auto end=raw.find(L'%',at+1);
        signing_require(end!=raw.npos && _wcsicmp(raw.substr(at+1,end-at-1).c_str(),L"USERPROFILE")==0,
            "uninstall_profile_expansion_unverifiable");
        path+=profile.native();signing_require(path.size()<32768,"uninstall_profile_localappdata_invalid");cursor=end+1;
    }
    return installation_path(path); // Never expand another user's path with our environment.
}
std::vector<fs::path> uninstall_log_roots(){
    std::set<fs::path> roots{maintenance_known_folder(FOLDERID_LocalAppData)/L"OutLoud/Logs"};RegistryHandle current,next;HKEY parent=HKEY_LOCAL_MACHINE;
    for(const auto* name:{L"SOFTWARE",L"Microsoft",L"Windows NT",L"CurrentVersion",L"ProfileList"}){
        signing_require(registry_open(parent,name,KEY_READ|KEY_WOW64_64KEY,next),"uninstall_profiles_unavailable");
        if(current.value)RegCloseKey(current.value);current.value=next.value;next.value=nullptr;parent=current.value;}
    for(DWORD i=0;;++i){wchar_t name[257]{};DWORD length=256;const auto status=RegEnumKeyExW(parent,i,name,&length,nullptr,nullptr,nullptr,nullptr);
        if(status==ERROR_NO_MORE_ITEMS)break;registry_ok(status);signing_require(i<4096,"uninstall_profiles_limit");RegistryHandle profile;
        signing_require(registry_open(parent,std::wstring(name,length),KEY_READ|KEY_WOW64_64KEY,profile),"uninstall_profiles_raced");
        wchar_t path[32768]{};DWORD size=sizeof(path),type{};registry_ok(RegQueryValueExW(profile.value,L"ProfileImagePath",nullptr,&type,reinterpret_cast<BYTE*>(path),&size));
        signing_require((type==REG_SZ || type==REG_EXPAND_SZ) && size>=2 && size%2==0 && path[size/2-1]==0,"uninstall_profile_path_invalid");
        wchar_t expanded[32768]{};const auto count=ExpandEnvironmentStringsW(path,expanded,32768);signing_require(count && count<=32768,"uninstall_profile_path_invalid");
        const auto profile_path=installation_path(expanded);safe(profile_path);RegistryHandle user;
        if(!registry_user_hive(std::wstring(name,length),0,user)){
            // Without an accessible user hive we cannot establish redirected known
            // folders or exclude foreign per-user dependencies. Never infer absence.
            signing_require(!fs::exists(profile_path/L"NTUSER.DAT"),"uninstall_offline_profile_review_required");
            roots.insert(profile_path/L"AppData/Local/OutLoud/Logs");continue;
        }
        RegistryHandle shell,key;HKEY ancestor=user.value;bool present=true;
        for(const auto* component:{L"SOFTWARE",L"Microsoft",L"Windows",L"CurrentVersion",L"Explorer"}){
            if(!registry_open(ancestor,component,KEY_READ,key)){present=false;break;}
            if(shell.value)RegCloseKey(shell.value);shell.value=key.value;key.value=nullptr;ancestor=shell.value;}
        bool resolved=false;
        if(present)for(const auto* folder:{L"Shell Folders",L"User Shell Folders"}){
            RegistryHandle folders;if(!registry_open(ancestor,folder,KEY_READ,folders))continue;
            DWORD local_type{},local_size=sizeof(path);const auto found=RegQueryValueExW(folders.value,L"Local AppData",nullptr,&local_type,reinterpret_cast<BYTE*>(path),&local_size);
            if(found==ERROR_FILE_NOT_FOUND)continue;registry_ok(found);
            signing_require(local_size>=2 && local_size<=sizeof(path) && local_size%2==0,"uninstall_profile_localappdata_invalid");
            const auto bytes=reinterpret_cast<const BYTE*>(path);RegistrationValue value{local_type,{bytes,bytes+local_size}};
            const auto local=uninstall_profile_localappdata(value,profile_path);safe(local);roots.insert(local/L"OutLoud/Logs");resolved=true;
        }
        signing_require(resolved,"uninstall_profile_known_folder_unavailable");
    }
    std::vector<fs::path> unique;for(const auto& root:roots)if(std::none_of(unique.begin(),unique.end(),[&](const auto& p){return same_installation_path(p,root);}))unique.push_back(root);
    return unique;
}
void WindowsUninstall::logs(){
    for(const auto& path:uninstall_log_roots()){
        blocked_resource=path;
        safe(path);if(!fs::exists(path))continue;
        // User logs are user-owned. Bind the exact physical directory and install
        // ID, constrain names, and pin all identities before any elevated deletion.
        MaintenanceFiles user(path);user.security=[](const fs::path& p){safe(p);};
        const auto marker=path/L".outloud-installation.dat";
        const auto identity=maintenance_directory_identity(path);
        const auto marker_before=user.stamp(marker);
        signing_require(marker_before.has_value(),"uninstall_unbound_logs_preserved");
        signing_require(user.read(marker)==L"OutLoud.Logs.1\n"+plan.owner.installation_id+L"\n"+plan.owner.root_encoding+L"\n"+identity+L"\n","uninstall_unbound_logs_preserved");
        signing_require(marker_before && user.stamp(marker)==marker_before,"uninstall_logs_raced");
        plan.directories.emplace_back(path,identity);
        for(const auto& e:fs::directory_iterator(path)){
            safe(e.path());const auto name=e.path().filename().native();
            signing_require(e.is_regular_file() && std::set<std::wstring>{L"engine.log",L"engine.1.log",L"engine.2.log",L".outloud-installation.dat"}.contains(name),"uninstall_unknown_log_file_preserved");
            const auto stamp=user.stamp(e.path());signing_require(stamp.has_value(),"uninstall_logs_raced");plan.files.push_back({2,e.path(),*stamp});}
    }
}
