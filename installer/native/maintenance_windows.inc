// Shared production bindings. Real entry points are hard-gated in signing.cpp.
#include "maintenance_package.hpp"
using MaintenanceIni=std::map<std::wstring,std::map<std::wstring,std::wstring>>;
MaintenanceIni maintenance_ini(const fs::path& path,bool prerequisite_report=false){
    safe(path);Handle h;pin(path,h);LARGE_INTEGER size{};signing_require(GetFileSizeEx(h.value,&size) && size.QuadPart>=0 && size.QuadPart<=262144,"maintenance_input_size_invalid");
    std::vector<char> bytes(static_cast<size_t>(size.QuadPart));DWORD n{};signing_require(ReadFile(h.value,bytes.data(),static_cast<DWORD>(bytes.size()),&n,nullptr) && n==bytes.size(),"maintenance_input_read_failed");
    std::wstring text;
    if(prerequisite_report){
        // The existing prerequisite detector writes BOM-prefixed UTF-16LE.
        // Only this report uses that wire format; ownership/request INIs stay ASCII.
        signing_require(bytes.size()>=2 && bytes.size()%2==0 &&
            static_cast<unsigned char>(bytes[0])==0xff && static_cast<unsigned char>(bytes[1])==0xfe,"maintenance_input_encoding_invalid");
        for(size_t i=2;i<bytes.size();i+=2){
            const auto c=static_cast<unsigned char>(bytes[i]);
            signing_require(c>0 && c<128 && bytes[i+1]==0,"maintenance_input_encoding_invalid");text+=static_cast<wchar_t>(c);
        }
    }else for(auto c:bytes){signing_require(static_cast<unsigned char>(c)<128,"maintenance_input_encoding_invalid");text+=static_cast<wchar_t>(c);}
    MaintenanceIni result;std::wstring section;std::wistringstream lines(text);std::wstring line;
    while(std::getline(lines,line)){if(!line.empty() && line.back()==L'\r')line.pop_back();if(line.empty())continue;
        if(line.front()==L'[' && line.back()==L']'){section=line.substr(1,line.size()-2);signing_require(!section.empty() && result.emplace(section,std::map<std::wstring,std::wstring>{}).second,"maintenance_input_duplicate_section");}
        else{const auto at=line.find(L'=');signing_require(!section.empty() && at!=line.npos && at>0 && result[section].emplace(line.substr(0,at),line.substr(at+1)).second,"maintenance_input_duplicate_or_invalid_field");}}
    return result;
}
MaintenanceOperation maintenance_operation(const std::wstring& s){
    if(s==L"fresh")return MaintenanceOperation::fresh;if(s==L"release")return MaintenanceOperation::release;
    if(s==L"repair")return MaintenanceOperation::repair;if(s==L"development")return MaintenanceOperation::development;throw SigningError("maintenance_operation_invalid");
}
MaintenanceComponents maintenance_components(const std::wstring& s){
    if(s==L"Engine")return MaintenanceComponents::engine;if(s==L"VoiceManager")return MaintenanceComponents::voice_manager;
    if(s==L"Both")return MaintenanceComponents::both;if(s==L"Package")return MaintenanceComponents::package;throw SigningError("maintenance_components_invalid");
}
std::wstring maintenance_version(){std::wstring v;for(const char c:std::string(OUTLOUD_MAINTENANCE_VERSION))v+=static_cast<wchar_t>(c);return v;}
std::array<unsigned,3> maintenance_version_parts(const std::wstring& v){
    std::array<unsigned,3> result{};size_t start{};
    for(size_t i=0;i<3;++i){const auto end=v.find(L'.',start);const auto part=v.substr(start,end==v.npos?v.size()-start:end-start);
        signing_require(!part.empty() && part.size()<=5 && part.find_first_not_of(L"0123456789")==part.npos &&
            (part.size()==1 || part.front()!=L'0') && ((i==2)==(end==v.npos)),"maintenance_version_invalid");
        result[i]=static_cast<unsigned>(std::stoul(part));signing_require(result[i]<=65535,"maintenance_version_invalid");start=end+1;}
    return result;
}
std::wstring maintenance_random(){std::array<BYTE,16> bytes{};signing_require(BCryptGenRandom(nullptr,bytes.data(),static_cast<ULONG>(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,"maintenance_random_failed");return hex(bytes.data(),bytes.size());}
void maintenance_active_files(const std::vector<fs::path>& paths){
    DWORD session{};wchar_t key[CCH_RM_SESSION_KEY+1]{};signing_require(RmStartSession(&session,0,key)==ERROR_SUCCESS,"maintenance_process_inventory_unavailable");
    try{std::vector<LPCWSTR> names;for(const auto& p:paths)if(fs::exists(p))names.push_back(p.c_str());
        if(!names.empty())signing_require(RmRegisterResources(session,static_cast<UINT>(names.size()),names.data(),0,nullptr,0,nullptr)==ERROR_SUCCESS,"maintenance_process_inventory_unavailable");
        UINT required{},count{};DWORD reboot{};auto status=RmGetList(session,&required,&count,nullptr,&reboot);
        if(status==ERROR_MORE_DATA){signing_require(required<=4096,"maintenance_process_inventory_unavailable");std::vector<RM_PROCESS_INFO> users(required);count=required;
            status=RmGetList(session,&required,&count,users.data(),&reboot);signing_require(status==ERROR_SUCCESS,"maintenance_process_inventory_changed");
            for(UINT i=0;i<count;++i)std::wcout<<L"in_use_pid="<<users[i].Process.dwProcessId<<L",application="<<users[i].strAppName<<L"\n";}
        signing_require(status==ERROR_SUCCESS && count==0 && reboot==0,"maintenance_resources_active_close_clients");
    }catch(...){RmEndSession(session);throw;}RmEndSession(session);
}
void maintenance_host_identity(const RegistrationTree& tree){
    signing_require(registration_field(tree,L"Publisher")==L"Nendis","maintenance_host_identity_invalid");
    const auto version=registration_field(tree,L"DisplayVersion"),name=registration_field(tree,L"DisplayName");
    // Inno 7's default is AppName + AppVersion; earlier Inno used "version".
    // Accept only those exact product/version forms, including interrupted hosts.
    signing_require(name==L"Out Loud" || name==L"Out Loud version "+version || name==L"Out Loud "+version,"maintenance_host_identity_invalid");
}
std::optional<std::pair<fs::path,std::wstring>> maintenance_host_record(){
    RegistryHandle current,next;signing_require(registry_open(HKEY_LOCAL_MACHINE,L"SOFTWARE",KEY_READ|KEY_WOW64_64KEY,current),"maintenance_host_registry_unavailable");
    for(const auto* component:{L"Microsoft",L"Windows",L"CurrentVersion",L"Uninstall",L"OutLoud_is1"}){
        if(!registry_open(current.value,component,KEY_READ|KEY_WOW64_64KEY,next))return {};
        RegCloseKey(current.value);current.value=next.value;next.value=nullptr;}
    size_t budget=65536;const auto tree=registry_tree(current.value,budget);
    maintenance_host_identity(tree);const auto version=registration_field(tree,L"DisplayVersion");
    return std::make_pair(installation_path(registration_field(tree,L"InstallLocation")),version);
}
struct MaintenanceInventory {std::wstring installation_id,certificate_hash,generation,version,library_identity,library_parent_identity;std::map<std::wstring,MaintenanceStamp> files;};
std::wstring maintenance_inventory_encode(const MaintenanceInventory& inventory){
    MaintenanceWriter w;w.text=L"OutLoud.Inventory.2\n";w.put(inventory.installation_id);w.put(inventory.certificate_hash);w.put(inventory.generation);w.put(inventory.version);w.put(inventory.library_identity);w.put(inventory.library_parent_identity);w.number(inventory.files.size());
    for(const auto& [p,f]:inventory.files){w.put(p);w.file(f);}return w.text;
}
std::vector<std::wstring> maintenance_inventory_paths(const MaintenanceInventory& inventory){
    std::vector<std::wstring> paths;for(const auto& [p,f]:inventory.files){static_cast<void>(f);paths.push_back(p);}return paths;
}
MaintenanceInventory maintenance_inventory_decode(std::wstring text){
    constexpr std::wstring_view magic=L"OutLoud.Inventory.2\n";signing_require(text.starts_with(magic),"maintenance_inventory_invalid");text.replace(0,magic.size(),L"OutLoud.Maintenance.2\n");
    MaintenanceReader r(text);MaintenanceInventory i;i.installation_id=r.get();i.certificate_hash=r.get();i.generation=r.get();i.version=r.get();i.library_identity=r.get();i.library_parent_identity=r.get();
    signing_require(i.library_parent_identity.empty() || hexadecimal(i.library_parent_identity,48),"maintenance_library_parent_identity_invalid");
    signing_require(hexadecimal(i.library_identity,48),"maintenance_library_identity_invalid");
    const auto count=r.number();
    signing_require(hexadecimal(i.installation_id,32) && hexadecimal(i.certificate_hash,64) && hexadecimal(i.generation,32) &&
        count>0 && count<=maintenance_inventory_limit,"maintenance_inventory_identity_invalid");
    static_cast<void>(maintenance_version_parts(i.version));
    for(uint64_t n=0;n<count;++n){const auto p=r.get();const auto f=r.file();signing_require(f && i.files.emplace(p,*f).second,"maintenance_inventory_paths_invalid");}
    maintenance_payload_paths(maintenance_inventory_paths(i));
    signing_require(r.cursor==r.text.size(),"maintenance_inventory_trailing_data");return i;
}
// Both preflight and recovery prove every before-stamp against the protected
// old receipt. The incoming manifest cannot adopt a colliding unowned file.
void maintenance_validate_baseline(const MaintenanceInventory& baseline,const MaintenanceJournal& j){
    for(const auto& c:j.changes)if(!maintenance_is_metadata(c.path)){
        const auto old=baseline.files.find(c.path);
        if(old==baseline.files.end())signing_require(!c.before && !c.input_hash.empty(),"maintenance_unowned_file_collision");
        else signing_require(c.before==MaintenanceFile(old->second) || (j.operation==MaintenanceOperation::repair && !c.before),"maintenance_baseline_changed");
        if(c.input_hash.empty())signing_require(old!=baseline.files.end(),"maintenance_unowned_removal");
    }
    if(j.components==MaintenanceComponents::package)for(const auto& [p,f]:baseline.files){static_cast<void>(f);
        signing_require(std::any_of(j.changes.begin(),j.changes.end(),[&](const auto& c){return c.path==p;}),"maintenance_baseline_file_omitted");}
}
template<class Current>
std::vector<MaintenanceChange> maintenance_inventory_changes(const MaintenanceInventory& baseline,
        const std::map<std::wstring,std::wstring>& incoming,MaintenanceOperation operation,MaintenanceComponents components,Current&& current){
    for(const auto& [p,f]:baseline.files){const auto actual=current(p);
        signing_require(actual==MaintenanceFile(f) || (operation==MaintenanceOperation::repair && !actual),"maintenance_baseline_changed");}
    std::vector<MaintenanceChange> changes;
    for(const auto& [p,hash]:incoming){maintenance_payload_path(p);signing_require(hexadecimal(hash,64),"maintenance_input_hash_invalid");
        changes.push_back({p,hash,current(p),{}, {}});}
    if(components==MaintenanceComponents::package)for(const auto& [p,f]:baseline.files){static_cast<void>(f);
        if(!incoming.contains(p))changes.push_back({p,L"",current(p),{}, {}});}
    MaintenanceJournal proof;proof.operation=operation;proof.components=components;proof.changes=changes;maintenance_validate_baseline(baseline,proof);
    for(const auto* p:maintenance_metadata)changes.push_back({p,L"",current(p),{}, {}});
    return changes;
}
MaintenanceInventory maintenance_next_inventory(const MaintenanceInventory& baseline,const MaintenanceJournal& j){
    auto inventory=baseline;inventory.installation_id=j.installation_id;inventory.certificate_hash=j.ownership.certificate_sha256;
    inventory.generation=j.transaction_id;inventory.version=j.new_version;inventory.library_identity=j.library_identity;inventory.library_parent_identity=j.library_parent_identity;
    for(const auto& c:j.changes)if(!maintenance_is_metadata(c.path)){
        if(c.after)inventory.files[c.path]=*c.after;
        else {signing_require(c.input_hash.empty(),"maintenance_new_inventory_unprepared");inventory.files.erase(c.path);}
    }
    maintenance_payload_paths(maintenance_inventory_paths(inventory));
    return inventory;
}
struct WindowsMaintenance;
std::wstring maintenance_failed_fresh_identity(const fs::path& root,const std::vector<std::wstring>& package);
struct MaintenanceSigning:WindowsSigning {
    WindowsMaintenance& transaction;MaintenanceJournal& journal;
    MaintenanceSigning(WindowsMaintenance& t,MaintenanceJournal& j,const fs::path& root,const fs::path& staged):WindowsSigning(root,{},L"S-1-5-32-544",j.operation==MaintenanceOperation::fresh),transaction(t),journal(j){target=staged;record=j.ownership;}
    void preflight();void new_ownership(){record=journal.ownership;}
    void checkpoint(const char* phase);void commit(){journal.ownership=record;}
};
struct WindowsMaintenance {
    std::wstring certificate_source;DWORD certificate_source_error{};
    MaintenanceFiles files;fs::path inputs,request;MaintenanceOperation operation{};MaintenanceComponents components{};
    MaintenanceIni manifest;MaintenanceInventory baseline;std::unique_ptr<WindowsSigning> owner;MaintenanceJournal active;
    WindowsMaintenance(const fs::path& root,const fs::path& source,const fs::path& input,MaintenanceOperation o,MaintenanceComponents c):files(root),inputs(installation_path(source)),request(input),operation(o),components(c){}
    explicit WindowsMaintenance(const fs::path& root):files(root){}
    std::vector<std::wstring> package()const{return {std::begin(registration_payload_paths),std::end(registration_payload_paths)};}
    void authority(){signing_require(IsUserAnAdmin(),"elevated_machine_authority_required");require_installation_location(files.root);
        OSVERSIONINFOEXW v{};v.dwOSVersionInfoSize=sizeof(v);using Rtl=LONG(WINAPI*)(OSVERSIONINFOEXW*);
        const auto rtl=reinterpret_cast<Rtl>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion"));SYSTEM_INFO system{};GetNativeSystemInfo(&system);
        signing_require(rtl && rtl(&v)==0 && v.dwMajorVersion==10 && v.dwMinorVersion==0 && v.dwBuildNumber>=22000 && v.wProductType==VER_NT_WORKSTATION &&
            system.wProcessorArchitecture==PROCESSOR_ARCHITECTURE_AMD64,"unsupported_windows");}
    void inspect_owner(){
        owner=std::make_unique<WindowsSigning>(files.root,fs::path{},L"S-1-5-32-544",false);
        signing_require(NCryptOpenStorageProvider(&owner->provider.value,MS_KEY_STORAGE_PROVIDER,0)==ERROR_SUCCESS,"key_provider_failed");
        // Strict parser rejects duplicates before the legacy ownership accessor.
        const auto ini_file=maintenance_ini(owner->state);signing_require(ini_file.size()==1 && ini_file.at(L"ownership").size()==12,"maintenance_ownership_shape_invalid");
        owner->load_ownership();validate_certificate(owner->certificate_facts(),true);
    }
    void version_file(const fs::path& p){
        DWORD ignored{};const auto size=GetFileVersionInfoSizeW(p.c_str(),&ignored);signing_require(size>0 && size<=1024*1024,"maintenance_candidate_version_missing");
        std::vector<BYTE> data(size);signing_require(GetFileVersionInfoW(p.c_str(),0,size,data.data()),"maintenance_candidate_version_unreadable");
        VS_FIXEDFILEINFO* info{};UINT length{};signing_require(VerQueryValueW(data.data(),L"\\",reinterpret_cast<void**>(&info),&length) && length>=sizeof(*info),"maintenance_candidate_version_invalid");
        const auto v=std::to_wstring(HIWORD(info->dwFileVersionMS))+L"."+std::to_wstring(LOWORD(info->dwFileVersionMS))+L"."+std::to_wstring(HIWORD(info->dwFileVersionLS))+L"."+std::to_wstring(LOWORD(info->dwFileVersionLS));
        signing_require(v==maintenance_version()+L".0","maintenance_candidate_version_mismatch");
        WORD* languages{};signing_require(VerQueryValueW(data.data(),L"\\VarFileInfo\\Translation",reinterpret_cast<void**>(&languages),&length) && length>=4,"maintenance_candidate_version_invalid");
        wchar_t prefix[64]{};swprintf_s(prefix,L"\\StringFileInfo\\%04x%04x\\",languages[0],languages[1]);
        wchar_t* product{};signing_require(VerQueryValueW(data.data(),(std::wstring(prefix)+L"ProductName").c_str(),reinterpret_cast<void**>(&product),&length) &&
            product && std::wstring(product)==L"Out Loud","maintenance_candidate_product_mismatch");
    }
    MaintenanceJournal preflight(){
        signing_require(!fs::exists(files.root/L".outloud-fresh-cleanup.dat"),"maintenance_fresh_cleanup_required");
        authority();const auto host=maintenance_host_record();
        maintenance_path_budget(files.root,package());
        signing_require(operation==MaintenanceOperation::fresh?!host:(host && same_installation_path(host->first,files.root)),"maintenance_installer_identity_missing_or_different");
        signing_require(!fs::exists(files.root/L"configuration/maintenance.pending"),"maintenance_recovery_required");
        signing_require(!fs::exists(files.root/L".outloud-bootstrap.dat"),"maintenance_bootstrap_recovery_required");
        signing_require(!fs::exists(files.root/L".outloud-uninstall.dat"),"maintenance_uninstall_recovery_required");
        for(const auto* pending:{L"signing.pending.ini",L"signing.next.ini",L"signing.previous.ini",L"registration.pending.ini"})
            signing_require(!fs::exists(files.root/L"configuration"/pending),"maintenance_legacy_recovery_required");
        manifest=maintenance_ini(request);auto& head=manifest.at(L"maintenance");
        signing_require(head.size()==4 && head.at(L"schema")==L"1" && head.at(L"app_id")==L"OutLoud" && head.at(L"version")==maintenance_version() &&
            head.at(L"clsid")==outloud::sapi::clsid_text,"maintenance_request_identity_invalid");
        const auto chosen=maintenance_selection(components,package());signing_require(manifest.size()==chosen.size()+2,"maintenance_request_files_invalid");
        std::set<std::wstring> requested;
        for(size_t i=0;i<chosen.size();++i){const auto& entry=manifest.at(L"file"+std::to_wstring(i));const auto& p=entry.at(L"path");
            signing_require(entry.size()==2 && hexadecimal(entry.at(L"sha256"),64) && std::find(chosen.begin(),chosen.end(),p)!=chosen.end() && requested.insert(p).second,"maintenance_request_files_invalid");
            const auto source=inputs/p;safe(source);Handle pin_source;pin(source,pin_source);signing_require(file_hash(pin_source.value)==entry.at(L"sha256"),"maintenance_candidate_hash_changed");
            const auto policy=std::find_if(std::begin(maintenance_package),std::end(maintenance_package),[&](const auto& row){return p==row.path;});
            signing_require(policy!=std::end(maintenance_package),"maintenance_request_files_invalid");
            if(policy->pe){static_cast<void>(pe_dependencies(source));version_file(source);}
            if(policy->hash[0])signing_require(entry.at(L"sha256")==policy->hash && maintenance_file_stamp(pin_source.value).size==policy->bytes,"maintenance_pinned_asset_mismatch");
        }
        std::set<std::wstring> allowed=requested;allowed.insert({L"maintenance-input.ini",L"prerequisites.ini",L"bootstrap/OutLoudPrerequisites.exe"});
        signing_require(same_installation_path(request,inputs/L"maintenance-input.ini"),"maintenance_request_location_invalid");
        std::set<std::wstring> directories;for(const auto& p:allowed)for(auto parent=fs::path(p).parent_path();!parent.empty();parent=parent.parent_path())directories.insert(parent.generic_wstring());
        for(const auto& entry:fs::recursive_directory_iterator(inputs)){safe(entry.path());const auto relative=entry.path().lexically_relative(inputs).generic_wstring();
            signing_require(entry.is_directory()?directories.contains(relative):(entry.is_regular_file() && allowed.contains(relative)),"maintenance_unexpected_candidate_file");}
        auto j=MaintenanceJournal{};j.operation=operation;j.components=components;j.host=operation==MaintenanceOperation::development?MaintenanceHost::developer:MaintenanceHost::inno;
        j.transaction_id=maintenance_random();j.root_encoding=maintenance_root_encoding(files.root.native());j.new_version=maintenance_version();
        registry_overrides();j.registration_before=encoded_tree(registry_capture());j.registration_after=j.registration_before;
        if(operation==MaintenanceOperation::fresh){
            const auto retry_id=maintenance_failed_fresh_identity(files.root,package());
            signing_require(!fs::exists(maintenance_library()),"maintenance_unbound_library_preserved");
            signing_require(j.registration_before==L"absent","maintenance_fresh_registration_conflict");j.installation_id=retry_id.empty()?maintenance_random():retry_id;
            j.ownership.id=j.installation_id;j.ownership.owner_sid=L"S-1-5-32-544";j.ownership.key_name=std::wstring(key_prefix)+j.installation_id;
            Key provider,key;signing_require(NCryptOpenStorageProvider(&provider.value,MS_KEY_STORAGE_PROVIDER,0)==ERROR_SUCCESS,"key_provider_failed");
            signing_require(NCryptOpenKey(provider.value,&key.value,j.ownership.key_name.c_str(),0,NCRYPT_MACHINE_KEY_FLAG|NCRYPT_SILENT_FLAG)==NTE_BAD_KEYSET,"maintenance_key_collision");
        }else{
            require_admin_protected(files.root);inspect_owner();j.ownership=owner->record;j.installation_id=owner->record.id;
            baseline=maintenance_inventory_decode(files.read(files.root/L"configuration/inventory.dat"));j.old_version=baseline.version;
            j.library_identity=baseline.library_identity;j.library_parent_identity=baseline.library_parent_identity;maintenance_validate_library(files,j);
            signing_require(baseline.installation_id==j.installation_id && baseline.certificate_hash==j.ownership.certificate_sha256 && host->second==baseline.version,"maintenance_baseline_identity_invalid");
            signing_require((operation==MaintenanceOperation::release && maintenance_version_parts(j.old_version)<=maintenance_version_parts(j.new_version)) || j.old_version==j.new_version,"maintenance_version_change_invalid");
            for(const auto& [p,f]:baseline.files){const auto actual=files.current(p);signing_require(actual==MaintenanceFile(f) || (operation==MaintenanceOperation::repair && !actual),"maintenance_baseline_changed");}
            const RegistrationIdentity identity{j.installation_id,j.ownership.certificate_sha256,(files.root/signing_target).native()};
            signing_require(registry_capture()==RegistrationSnapshot(desired_registration(identity)),"maintenance_com_baseline_changed");
            const auto engine=files.current(signing_target);
            signing_require(baseline.files.at(signing_target).hash==j.ownership.signed_sha256,"maintenance_signing_inventory_mismatch");
            if(engine){Handle h;pin(files.root/signing_target,h);const auto signature=verify_signature(files.root/signing_target,h.value);
                signing_require(signature.status==ERROR_SUCCESS && signature.certificate_sha256==j.ownership.certificate_sha256 && signature.thumbprint==j.ownership.thumbprint,"maintenance_engine_signature_invalid");
                owner->verify_dependencies();}
            const auto receipt=maintenance_ini(files.root/L"configuration/registration.ini").at(L"registration");require_admin_protected(files.root/L"configuration/registration.ini");
            signing_require(receipt.at(L"schema")==L"1" && receipt.at(L"hive")==L"HKLM" && receipt.at(L"view")==L"64" && receipt.at(L"clsid")==outloud::sapi::clsid_text &&
                receipt.at(L"installation_id")==j.installation_id && receipt.at(L"certificate_sha256")==j.ownership.certificate_sha256 && receipt.at(L"desired")==j.registration_before,"maintenance_registration_receipt_invalid");
        }
        std::map<std::wstring,std::wstring> incoming;
        for(const auto& [section,values]:manifest)if(section.starts_with(L"file"))incoming.emplace(values.at(L"path"),values.at(L"sha256"));
        j.changes=maintenance_inventory_changes(baseline,incoming,operation,components,[&](const auto& p){return files.current(p);});
        std::vector<std::wstring> changed_paths;for(const auto& c:j.changes)changed_paths.push_back(c.path);maintenance_path_budget(files.root,changed_paths);
        std::vector<fs::path> paths;for(const auto& c:j.changes)paths.push_back(files.path(c.path));maintenance_active_files(paths);
        ULARGE_INTEGER free{},total{},available{};signing_require(GetDiskFreeSpaceExW(files.root.root_path().c_str(),&free,&total,&available),"maintenance_storage_unknown");
        uint64_t needed=16*1024*1024;for(const auto& c:j.changes){if(c.before)needed+=c.before->size*2;
            if(!c.input_hash.empty()){Handle h;pin(inputs/c.path,h);needed+=maintenance_file_stamp(h.value).size*2;}}
        signing_require(free.QuadPart>=needed,"maintenance_storage_insufficient");active=j;return j;
    }
    void begin(const MaintenanceJournal& j){files.begin(j);}
    void save(const MaintenanceJournal& j){active=j;files.save(j);}
    MaintenanceJournal load(){authority();require_admin_protected(files.root);active=files.load(package());return active;}
    void stage(MaintenanceJournal& j){
        for(auto& c:j.changes)if(!c.input_hash.empty()){files.copy(inputs/c.path,files.vault/L"new"/c.path,c.input_hash);c.after=files.stamp(files.vault/L"new"/c.path);save(j);}
        const auto& bootstrap=manifest.at(L"bootstrap");signing_require(bootstrap.size()==2,"maintenance_bootstrap_invalid");
        files.copy(inputs/L"bootstrap/OutLoudPrerequisites.exe",files.vault/L"bootstrap/OutLoudPrerequisites.exe",bootstrap.at(L"detector_sha256"));
        j.bootstrap_detector=files.stamp(files.vault/L"bootstrap/OutLoudPrerequisites.exe");save(j);
        files.copy(inputs/L"prerequisites.ini",files.vault/L"bootstrap/prerequisites.ini",bootstrap.at(L"requirements_sha256"));
        j.bootstrap_requirements=files.stamp(files.vault/L"bootstrap/prerequisites.ini");save(j);
        const auto report=files.vault/L"bootstrap/results.ini";
        const auto config=components==MaintenanceComponents::engine?files.root/L"bin/OutLoud.VoiceManager.runtimeconfig.json":files.vault/L"new/bin/OutLoud.VoiceManager.runtimeconfig.json";
        const auto probe=files.vault/L"bootstrap/OutLoudPrerequisites.exe";
        std::wstring command=L"\""+probe.native()+L"\" --requirements \""+(files.vault/L"bootstrap/prerequisites.ini").native()+L"\" --runtime-config \""+config.native()+L"\" --report \""+report.native()+L"\"";
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
        signing_require(CreateProcessW(probe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,files.vault.c_str(),&startup,&process),"maintenance_prerequisite_probe_failed");
        Handle process_pin,thread_pin;process_pin.value=process.hProcess;thread_pin.value=process.hThread;DWORD code{};
        signing_require(WaitForSingleObject(process.hProcess,60000)==WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess,&code),"maintenance_prerequisites_unavailable");
        if(fs::exists(report)){j.bootstrap_report=files.stamp(report);save(j);}
        signing_require(code==0,"maintenance_prerequisites_unavailable");
        const auto result=maintenance_ini(report,true).at(L"results");signing_require(result.at(L"schema")==L"1" && result.at(L"windows")==L"compatible" && result.at(L"vc")==L"compatible" && result.at(L"dotnet")==L"compatible","maintenance_prerequisites_unavailable");
    }
    void sign_staged(MaintenanceJournal& j){
        const auto row=std::find_if(j.changes.begin(),j.changes.end(),[](const auto& c){return c.path==signing_target;});
        if(row!=j.changes.end()){
            MaintenanceSigning signer(*this,j,files.root,files.vault/L"new"/signing_target);prepare_signing(j.operation==MaintenanceOperation::fresh,signer);
            CloseHandle(signer.file.value);signer.file.value=INVALID_HANDLE_VALUE;row->after=files.stamp(files.vault/L"new"/signing_target);
        }
        if(j.operation==MaintenanceOperation::fresh)j.registration_after=encoded_tree(desired_registration({j.installation_id,j.ownership.certificate_sha256,(files.root/signing_target).native()}));
        save(j);
    }
    void prepare_metadata(MaintenanceJournal& j){
        if(j.operation==MaintenanceOperation::fresh)maintenance_create_library(files,j);
        const auto inventory=maintenance_next_inventory(baseline,j);
        // Construct using the same ownership/registry record formats already used by A03.6/A03.7.
        WindowsSigning signing(files.root,{},L"S-1-5-32-544",false);signing.record=j.ownership;
        const auto sign=files.vault/L"new/configuration/signing.ini";files.parents(sign,true);write_new(sign,signing.metadata("verified"),true);
        for(auto& c:j.changes)if(c.path==L"configuration/signing.ini")c.after=files.stamp(sign);save(j);
        const auto reg=files.vault/L"new/configuration/registration.ini";
        if(j.operation==MaintenanceOperation::fresh)write_new(reg,L"[registration]\nschema=1\nphase=before_registry_transaction\nhive=HKLM\nview=64\nclsid="+
            std::wstring(outloud::sapi::clsid_text)+L"\ninstallation_id="+j.installation_id+L"\ncertificate_sha256="+j.ownership.certificate_sha256+
            L"\nbefore_encoding=utf16le-typed-tree-v1\nbefore=absent\ndesired="+j.registration_after+L"\n",true);
        else files.copy(files.root/L"configuration/registration.ini",reg,files.current(L"configuration/registration.ini")->hash);
        for(auto& c:j.changes)if(c.path==L"configuration/registration.ini")c.after=files.stamp(reg);save(j);
        write_new(files.vault/L"new/configuration/inventory.dat",maintenance_inventory_encode(inventory),true);
        for(auto& c:j.changes)if(maintenance_is_metadata(c.path))c.after=files.stamp(files.vault/L"new"/c.path);
        save(j);
    }
    void backup_all(MaintenanceJournal& j){files.backup_all(j);save(j);}
    void lock_resources(const MaintenanceJournal& j){std::vector<fs::path> paths;
        for(const auto& [p,f]:baseline.files){static_cast<void>(f);paths.push_back(files.path(p));}for(const auto& c:j.changes)paths.push_back(files.path(c.path));
        maintenance_active_files(paths);files.lock_files(j);for(const auto& [p,f]:baseline.files){static_cast<void>(f);files.acquire(files.path(p));}}
    void revalidate(const MaintenanceJournal& j){
        maintenance_validate_library(files,j);
        signing_require(encoded_tree(registry_capture())==j.registration_before,"maintenance_registration_raced");registry_overrides();
        for(const auto& c:j.changes)signing_require(files.current(c.path)==c.before && files.stamp(files.vault/L"new"/c.path)==c.after && (!c.before || files.backup(c.path)==c.backup),"maintenance_precommit_state_changed");
        for(const auto& [p,f]:baseline.files)signing_require(files.current(p)==MaintenanceFile(f) ||
            (j.operation==MaintenanceOperation::repair && !files.current(p)),"maintenance_baseline_changed_before_commit");
    }
    MaintenanceFile current(const std::wstring& p){return files.current(p);}
    MaintenanceFile backup(const std::wstring& p){return files.backup(p);}
    MaintenanceFile original(const std::wstring& p){return files.original(p);}
    void publish(const MaintenanceChange& c){files.publish(c);}
    void publish_registration(const MaintenanceJournal& j){
        registry_overrides();signing_require(encoded_tree(registry_capture())==j.registration_before,"maintenance_registration_raced");
        if(j.operation==MaintenanceOperation::fresh)registry_exchange({},desired_registration({j.installation_id,j.ownership.certificate_sha256,(files.root/signing_target).native()}));
    }
    void verify_committed(const MaintenanceJournal& j){
        maintenance_validate_library(files,j);
        for(const auto& c:j.changes)signing_require(files.current(c.path)==c.after,"maintenance_committed_file_invalid");
        signing_require(encoded_tree(registry_capture())==j.registration_after,"maintenance_committed_registration_invalid");registry_overrides();
        // Bytes/signatures/certificate were checked with held staged identities before publication.
        // Unchanged package files remain bound to the complete prior inventory.
        for(const auto& [p,f]:baseline.files)if(std::none_of(j.changes.begin(),j.changes.end(),[&](const auto& c){return c.path==p;}))
            signing_require(files.current(p)==MaintenanceFile(f),"maintenance_unchanged_payload_changed");
        const auto engine=files.current(signing_target);signing_require(engine && engine->hash==j.ownership.signed_sha256,"maintenance_committed_engine_invalid");
        const auto signature=verify_signature(files.root/signing_target,files.held(*engine));
        signing_require(signature.status==ERROR_SUCCESS && signature.thumbprint==j.ownership.thumbprint && signature.certificate_sha256==j.ownership.certificate_sha256,"maintenance_committed_signature_invalid");
        verify_owned_resources(j.ownership);
    }
    void verify_host(const MaintenanceJournal& j){
        const auto host=maintenance_host_record();signing_require(host && same_installation_path(host->first,files.root) && host->second==j.new_version,"maintenance_inno_host_incomplete");
        RegistryHandle key;registry_ok(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\OutLoud_is1",0,KEY_READ|KEY_WOW64_64KEY,&key.value));
        size_t budget=65536;const auto tree=registry_tree(key.value,budget);auto command=registration_field(tree,L"UninstallString");
        signing_require(command.size()>2 && command.front()==L'"' && command.back()==L'"',"maintenance_host_command_invalid");
        const auto executable=installation_path(command.substr(1,command.size()-2));
        const auto name=executable.filename().native();signing_require(same_installation_path(executable.parent_path(),files.root) &&
            name.size()==12 && name.starts_with(L"unins") && name.ends_with(L".exe") && name.substr(5,3).find_first_not_of(L"0123456789")==name.npos,"maintenance_host_command_invalid");
        auto data=executable;data.replace_extension(L".dat");
        signing_require(files.stamp(executable) && files.stamp(data) && files.stamp(data)->size>=64,"maintenance_inno_log_incomplete");
    }
    void authenticate_recovery(const MaintenanceJournal& j){
        authority();require_admin_protected(files.root);signing_require(j.root_encoding==maintenance_root_encoding(files.root.native()),"maintenance_recovery_wrong_root");
        const auto host=maintenance_host_record();signing_require(j.operation==MaintenanceOperation::fresh?(!host || same_installation_path(host->first,files.root)):
            (host && same_installation_path(host->first,files.root)),"maintenance_recovery_host_conflict");
        if(j.operation==MaintenanceOperation::fresh && j.registration_after!=L"absent")
            signing_require(j.registration_after==encoded_tree(desired_registration({j.installation_id,j.ownership.certificate_sha256,(files.root/signing_target).native()})),"maintenance_recovery_registration_identity_invalid");
        // The protected pointer and every flushed snapshot authenticate the exact
        // root/installation/transaction. No unjournaled legacy state is adopted.
        lock_resources(j);
        if(j.operation!=MaintenanceOperation::fresh){
            verify_owned_resources(j.ownership);
            const auto receipt=std::find_if(j.changes.begin(),j.changes.end(),[](const auto& c){return c.path==L"configuration/inventory.dat";});
            signing_require(receipt!=j.changes.end() && receipt->before,"maintenance_recovery_inventory_missing");
            const auto current_inventory=files.current(receipt->path);
            const auto source=current_inventory==receipt->before?files.path(receipt->path):files.vault/L"backup"/receipt->path;
            if(current_inventory!=receipt->before)signing_require(files.stamp(source)==receipt->backup && receipt->backup,"maintenance_recovery_inventory_changed");
            // Read the already pinned receipt without opening a conflicting handle.
            const auto inventory_text=files.read_held(source);
            baseline=maintenance_inventory_decode(inventory_text);
            maintenance_validate_baseline(baseline,j);
            signing_require(baseline.installation_id==j.installation_id && baseline.certificate_hash==j.ownership.certificate_sha256 && baseline.version==j.old_version &&
                baseline.library_identity==j.library_identity && baseline.library_parent_identity==j.library_parent_identity &&
                j.registration_before==encoded_tree(desired_registration({j.installation_id,j.ownership.certificate_sha256,(files.root/signing_target).native()})),"maintenance_recovery_identity_invalid");
            for(const auto& [p,f]:baseline.files)if(std::none_of(j.changes.begin(),j.changes.end(),[&](const auto& c){return c.path==p;})){
                files.acquire(files.path(p));signing_require(files.current(p)==MaintenanceFile(f),"maintenance_recovery_unchanged_payload_changed");}
        }
    }
    void verify_owned_resources(const SigningOwnership& ownership){
        owner=std::make_unique<WindowsSigning>(files.root,fs::path{},L"S-1-5-32-544",false);owner->record=ownership;
        signing_require(NCryptOpenStorageProvider(&owner->provider.value,MS_KEY_STORAGE_PROVIDER,0)==ERROR_SUCCESS,"key_provider_failed");
        owner->open_store(owner->personal,L"My",true);owner->certificate.value=owner->find_owned(owner->personal.value);
        signing_require(NCryptOpenKey(owner->provider.value,&owner->key.value,ownership.key_name.c_str(),0,NCRYPT_MACHINE_KEY_FLAG|NCRYPT_SILENT_FLAG)==ERROR_SUCCESS,"private_key_inaccessible");
        validate_certificate(owner->certificate_facts(),true);
    }
    void validate_recovery_resources(const MaintenanceJournal& j){
        const auto actual=encoded_tree(registry_capture());registry_overrides();signing_require(actual==j.registration_before || actual==j.registration_after,"maintenance_recovery_foreign_registration");
        const auto host=maintenance_host_record();
        // Once Inno has recorded a new release version, native rollback alone
        // cannot restore Inno's logs/host record. Require checked finalization or
        // explicit host recovery instead of making the two inventories disagree.
        signing_require(!host || (j.operation!=MaintenanceOperation::fresh && host->second==j.old_version),"maintenance_host_changed_finalize_or_recover_inno_required");
        if(j.operation==MaintenanceOperation::fresh && j.phase!=MaintenancePhase::rolled_back)maintenance_remove_created_signing(j,false);
    }
    void restore_registration(const MaintenanceJournal& j){
        validate_recovery_resources(j);
        if(j.operation==MaintenanceOperation::fresh && encoded_tree(registry_capture())==j.registration_after && j.registration_after!=L"absent")
            maintenance_remove_created_registration(j);
    }
    void restore(const MaintenanceChange& c){files.restore(c);}
    void restore_resources(const MaintenanceJournal& j){if(j.operation==MaintenanceOperation::fresh){maintenance_restore_library(files,j);maintenance_remove_created_signing(j);}}
    void verify_original(const MaintenanceJournal& j){for(const auto& c:j.changes)signing_require(files.current(c.path)==c.before,"maintenance_original_verification_failed");
        signing_require(encoded_tree(registry_capture())==j.registration_before,"maintenance_original_registration_invalid");}
    void release_for_recovery(){files.release();}
    void finish(MaintenanceOutcome outcome){if(outcome!=MaintenanceOutcome::awaiting_host)files.retire(active);}
    void maintenance_remove_created_registration(const MaintenanceJournal& j);
    void maintenance_remove_created_signing(const MaintenanceJournal& j,bool apply=true,bool uninstall_cleanup=false);
};
void MaintenanceSigning::preflight(){
    transaction.authority();require_admin_protected(target);pin(target,file,true);expected_hash=file_hash(file.value);expected_digest=sip_digest(target,file.value);
    signing_require(verify_signature(target,file.value).status==TRUST_E_NOSIGNATURE,"maintenance_candidate_engine_not_unsigned");
    signing_require(NCryptOpenStorageProvider(&provider.value,MS_KEY_STORAGE_PROVIDER,0)==ERROR_SUCCESS,"key_provider_failed");
}
void MaintenanceSigning::checkpoint(const char* phase){journal.resource_phase.clear();for(;*phase;++phase)journal.resource_phase+=static_cast<wchar_t>(*phase);
    journal.ownership=record;transaction.save(journal);}
std::wstring maintenance_result(const WindowsMaintenance& op,MaintenanceOutcome result){
    const auto outcome=result==MaintenanceOutcome::committed?L"committed":result==MaintenanceOutcome::awaiting_host?L"awaiting_host":L"rolled_back";
    return L"[results]\nschema=1\nmaintenance="+std::wstring(outcome)+L"\ntransaction_id="+op.active.transaction_id+L"\ninstallation_id="+op.active.installation_id+L"\n";
}
void WindowsMaintenance::maintenance_remove_created_registration(const MaintenanceJournal& j){
    RegistryHandle voices,next;bool present=true;HKEY parent_key=HKEY_LOCAL_MACHINE;
    for(const auto* part:{L"SOFTWARE",L"Microsoft",L"Speech",L"Voices",L"Tokens"}){
        if(!registry_open(parent_key,part,KEY_READ|KEY_WOW64_64KEY,next)){present=false;break;}
        if(voices.value)RegCloseKey(voices.value);voices.value=next.value;next.value=nullptr;parent_key=voices.value;
    }
    if(present){
        size_t budget=65536;const auto tree=registry_tree(voices.value,budget);
        for(const auto& [name,voice]:tree.children){static_cast<void>(name);const auto field=voice.values.find(L"CLSID");
            signing_require(field==voice.values.end() || _wcsicmp(registration_text(field->second).c_str(),outloud::sapi::clsid_text)!=0,"maintenance_fresh_rollback_voice_dependency");}}
    RegistryTransaction transaction;RegistryHandle parent;
    signing_require(encoded_tree(registry_capture(HKEY_LOCAL_MACHINE,KEY_WOW64_64KEY,transaction.value))==j.registration_after,"maintenance_registration_changed_during_recovery");
    signing_require(registry_parent(HKEY_LOCAL_MACHINE,KEY_WOW64_64KEY,parent,transaction.value,KEY_WRITE),"registry_parent_missing");
    const std::wstring clsid=outloud::sapi::clsid_text;
    registry_ok(RegDeleteKeyTransactedW(parent.value,(clsid+L"\\InprocServer32").c_str(),KEY_WOW64_64KEY,0,transaction.value,nullptr));
    registry_ok(RegDeleteKeyTransactedW(parent.value,clsid.c_str(),KEY_WOW64_64KEY,0,transaction.value,nullptr));
    signing_require(CommitTransaction(transaction.value),"maintenance_registration_restore_commit_failed");transaction.committed=true;
}
BOOL WINAPI maintenance_enumerate_store(const void* name,DWORD,PCERT_SYSTEM_STORE_INFO,void*,void* argument){
    try{auto& stores=*static_cast<std::vector<std::wstring>*>(argument);if(stores.size()>=256){SetLastError(ERROR_MORE_DATA);return FALSE;}
        stores.emplace_back(static_cast<const wchar_t*>(name));return TRUE;}catch(...){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return FALSE;}
}
bool maintenance_certificate_uses_key(PCCERT_CONTEXT cert,NCRYPT_KEY_HANDLE key){
    if(!key || std::string(cert->pCertInfo->SubjectPublicKeyInfo.Algorithm.pszObjId)!=szOID_RSA_RSA)return false;
    DWORD size{};signing_require(NCryptExportKey(key,0,BCRYPT_RSAPUBLIC_BLOB,nullptr,nullptr,0,&size,0)==ERROR_SUCCESS && size<=65536,"maintenance_public_key_unavailable");
    std::vector<BYTE> machine(size);signing_require(NCryptExportKey(key,0,BCRYPT_RSAPUBLIC_BLOB,nullptr,machine.data(),size,&size,0)==ERROR_SUCCESS,"maintenance_public_key_unavailable");machine.resize(size);
    BCRYPT_KEY_HANDLE imported{};signing_require(CryptImportPublicKeyInfoEx2(X509_ASN_ENCODING,&cert->pCertInfo->SubjectPublicKeyInfo,0,nullptr,&imported),"maintenance_certificate_public_key_unavailable");
    try{ULONG bytes{};signing_require(BCryptExportKey(imported,nullptr,BCRYPT_RSAPUBLIC_BLOB,nullptr,0,&bytes,0)>=0 && bytes<=65536,"maintenance_certificate_public_key_unavailable");
        std::vector<BYTE> public_key(bytes);signing_require(BCryptExportKey(imported,nullptr,BCRYPT_RSAPUBLIC_BLOB,public_key.data(),bytes,&bytes,0)>=0,"maintenance_certificate_public_key_unavailable");
        public_key.resize(bytes);BCryptDestroyKey(imported);return public_key==machine;
    }catch(...){BCryptDestroyKey(imported);throw;}
}
struct MaintenanceCertificateSource {
    DWORD location{};std::wstring name,physical;bool registry{},owned{};
};
bool maintenance_certificate_source_owned(DWORD location,const std::wstring& name,const std::wstring& physical,
    bool registry,DWORD source_location,const std::wstring& source_name){
    const auto split=name.find_last_of(L'\\');
    const auto logical=(location==CERT_SYSTEM_STORE_USERS && split!=name.npos && split>0)?name.substr(split+1):name;
    const bool personal_or_root=_wcsicmp(logical.c_str(),L"My")==0 || _wcsicmp(logical.c_str(),L"Root")==0;
    if(!personal_or_root || source_location!=CERT_SYSTEM_STORE_LOCAL_MACHINE || _wcsicmp(logical.c_str(),source_name.c_str())!=0)return false;
    // A separate policy/custom/user copy is not the installation-owned default.
    return (location==CERT_SYSTEM_STORE_LOCAL_MACHINE && registry && physical==L".Default") ||
        ((location==CERT_SYSTEM_STORE_CURRENT_USER || (location==CERT_SYSTEM_STORE_USERS && split!=name.npos && split>0)) && !registry && physical==L".LocalMachine");
}
struct MaintenanceCertificateEnumeration {
    DWORD location{};std::wstring name;std::vector<MaintenanceCertificateSource> sources;bool invalid{};
};
BOOL WINAPI maintenance_enumerate_physical_store(const void*,DWORD,LPCWSTR physical,PCERT_PHYSICAL_STORE_INFO info,void*,void* argument){
    auto& inventory=*static_cast<MaintenanceCertificateEnumeration*>(argument);
    try{
        signing_require(info && info->cbSize>=sizeof(CERT_PHYSICAL_STORE_INFO) && physical && wcslen(physical)<=256 &&
            inventory.sources.size()<256 && !(info->dwOpenFlags&CERT_SYSTEM_STORE_RELOCATE_FLAG),"maintenance_physical_store_invalid");
        const auto provider=info->pszOpenStoreProvider;const auto numeric=reinterpret_cast<uintptr_t>(provider);
        const bool registry=provider==CERT_STORE_PROV_SYSTEM_REGISTRY_W || (numeric>65535 && _stricmp(provider,sz_CERT_STORE_PROV_SYSTEM_REGISTRY_W)==0);
        const bool system=provider==CERT_STORE_PROV_SYSTEM_W || (numeric>65535 && _stricmp(provider,sz_CERT_STORE_PROV_SYSTEM_W)==0);
        signing_require(registry || system,"maintenance_physical_store_provider_unverifiable");
        const auto& parameter=info->OpenParameters;signing_require(parameter.pbData && parameter.cbData>=2 && parameter.cbData<=65536 && parameter.cbData%2==0,
            "maintenance_physical_store_invalid");
        const auto name=registration_text({REG_SZ,{parameter.pbData,parameter.pbData+parameter.cbData}});
        signing_require(!name.empty() && name.front()!=L'\\',"maintenance_physical_store_invalid");
        const auto location=info->dwOpenFlags&CERT_SYSTEM_STORE_LOCATION_MASK;
        signing_require(location==CERT_SYSTEM_STORE_LOCAL_MACHINE || location==CERT_SYSTEM_STORE_CURRENT_USER || location==CERT_SYSTEM_STORE_USERS ||
            location==CERT_SYSTEM_STORE_LOCAL_MACHINE_GROUP_POLICY || location==CERT_SYSTEM_STORE_CURRENT_USER_GROUP_POLICY ||
            location==CERT_SYSTEM_STORE_LOCAL_MACHINE_ENTERPRISE,"maintenance_physical_store_location_unverifiable");
        const auto split=name.find(L'\\');
        signing_require(location==CERT_SYSTEM_STORE_USERS?
            (split!=name.npos && split>0 && split+1<name.size() && name.find(L'\\',split+1)==name.npos &&
                (name.substr(0,split)==L".DEFAULT" || name.substr(0,split).starts_with(L"S-1-"))):split==name.npos,
            "maintenance_physical_store_invalid");
        inventory.sources.push_back({location,name,physical,registry,maintenance_certificate_source_owned(inventory.location,inventory.name,physical,registry,location,name)});
        return TRUE;
    }catch(...){inventory.invalid=true;SetLastError(ERROR_INVALID_DATA);return FALSE;}
}
bool maintenance_certificate_registry_absent(DWORD location,const std::wstring& name){
    // ERROR_FILE_NOT_FOUND from a store provider is ambiguous. Independently
    // authenticate the backing registry namespace, refusing links/access errors.
    RegistryHandle user,current,next;HKEY parent{};std::wstring store=name;
    if(location==CERT_SYSTEM_STORE_USERS){const auto split=name.find(L'\\');
        signing_require(split!=name.npos && split>0 && split+1<name.size(),"maintenance_certificate_store_name_invalid");
        signing_require(registry_user_hive(name.substr(0,split),0,user),"maintenance_certificate_user_unavailable");parent=user.value;store=name.substr(split+1);
    }else parent=(location==CERT_SYSTEM_STORE_CURRENT_USER || location==CERT_SYSTEM_STORE_CURRENT_USER_GROUP_POLICY)?HKEY_CURRENT_USER:HKEY_LOCAL_MACHINE;
    signing_require(store.find(L'\\')==store.npos && store.find(L'/')==store.npos && !store.empty(),"maintenance_certificate_store_name_invalid");
    std::vector<std::wstring> parts{L"SOFTWARE"};
    if(location==CERT_SYSTEM_STORE_CURRENT_USER_GROUP_POLICY || location==CERT_SYSTEM_STORE_LOCAL_MACHINE_GROUP_POLICY)parts.push_back(L"Policies");
    parts.push_back(L"Microsoft");parts.push_back(location==CERT_SYSTEM_STORE_LOCAL_MACHINE_ENTERPRISE?L"EnterpriseCertificates":L"SystemCertificates");parts.push_back(store);
    for(const auto& part:parts){if(!registry_open(parent,part,KEY_READ|KEY_WOW64_64KEY,next))return true;
        if(current.value)RegCloseKey(current.value);current.value=next.value;next.value=nullptr;parent=current.value;}
    return false;
}
bool maintenance_certificate_source_absent_checked(const MaintenanceCertificateSource& source,DWORD error,unsigned& budget,unsigned depth=0){
    if(error!=ERROR_FILE_NOT_FOUND)return false;
    if(!budget || depth>=8)return false;--budget;
    if(source.registry)return maintenance_certificate_registry_absent(source.location,source.name);
    // A logical provider may enumerate an unmaterialized predefined store. Only
    // accept absence if the namespace and every enumerated physical source are
    // independently absent. Provider/access errors never become an empty store.
    if(!maintenance_certificate_registry_absent(source.location,source.name))return false;
    MaintenanceCertificateEnumeration inventory{source.location,source.name,{},false};
    signing_require(CertEnumPhysicalStore(source.name.c_str(),source.location,&inventory,maintenance_enumerate_physical_store) && !inventory.invalid,
        "maintenance_physical_certificate_store_inventory_unavailable");
    for(const auto& physical:inventory.sources)if(!maintenance_certificate_source_absent_checked(physical,ERROR_FILE_NOT_FOUND,budget,depth+1))return false;
    return true;
}
bool maintenance_certificate_source_absent(const MaintenanceCertificateSource& source,DWORD error){
    unsigned budget=1024;return maintenance_certificate_source_absent_checked(source,error,budget);
}
void WindowsMaintenance::maintenance_remove_created_signing(const MaintenanceJournal& j,bool apply,bool uninstall_cleanup){
    signing_require(j.operation==MaintenanceOperation::fresh && j.ownership.id==j.installation_id &&
        j.ownership.key_name==std::wstring(key_prefix)+j.installation_id,"maintenance_resource_ownership_invalid");
    WindowsSigning signer(files.root,{},L"S-1-5-32-544",true);signer.record=j.ownership;
    signing_require(NCryptOpenStorageProvider(&signer.provider.value,MS_KEY_STORAGE_PROVIDER,0)==ERROR_SUCCESS,"key_provider_failed");
    const auto status=NCryptOpenKey(signer.provider.value,&signer.key.value,j.ownership.key_name.c_str(),0,NCRYPT_MACHINE_KEY_FLAG|NCRYPT_SILENT_FLAG);
    signing_require(status==ERROR_SUCCESS || status==NTE_BAD_KEYSET,"maintenance_owned_key_unverifiable");
    // Refuse key deletion if another certificate has acquired the same machine
    // container, even if our own public certificate is no longer present.
    std::vector<std::pair<DWORD,std::wstring>> store_names;
    if(uninstall_cleanup){for(const auto location:{CERT_SYSTEM_STORE_LOCAL_MACHINE,CERT_SYSTEM_STORE_CURRENT_USER,CERT_SYSTEM_STORE_USERS}){
        std::vector<std::wstring> names;signing_require(CertEnumSystemStore(location,nullptr,&names,maintenance_enumerate_store),"maintenance_certificate_store_inventory_unavailable");
        for(const auto& name:names)store_names.emplace_back(location,name);}}
    else for(const auto* name:{L"My",L"Root",L"CA",L"TrustedPublisher"})store_names.emplace_back(CERT_SYSTEM_STORE_LOCAL_MACHINE,name);
    std::vector<MaintenanceCertificateSource> sources;
    for(const auto& [location,name]:store_names){
        if(!uninstall_cleanup){sources.push_back({location,name,L"",false,_wcsicmp(name.c_str(),L"My")==0 || _wcsicmp(name.c_str(),L"Root")==0});continue;}
        certificate_source=std::to_wstring(location)+L"/"+name;
        MaintenanceCertificateEnumeration inventory{location,name,{},false};
        const auto enumerated=CertEnumPhysicalStore(name.c_str(),location,&inventory,maintenance_enumerate_physical_store);
        signing_require(enumerated && !inventory.invalid,"maintenance_physical_certificate_store_inventory_unavailable");
        if(inventory.sources.empty()){
            // Some logical stores (e.g. UserDS) have no physical registration.
            // Inspect the read-only logical provider, rather than infer emptiness.
            sources.push_back({location,name,L"",false,false});
        }else sources.insert(sources.end(),inventory.sources.begin(),inventory.sources.end());
        signing_require(sources.size()<=16384,"maintenance_certificate_store_inventory_limit");
    }
    for(const auto& source:sources){
        const auto location=source.location;const auto& name=source.name;const bool owned_store=source.owned;
        certificate_source=std::to_wstring(location)+L"/"+name+L"/"+source.physical;certificate_source_error=0;
        Store store;
        if(!uninstall_cleanup)signer.open_store(store,name.c_str(),true);
        else{
            store.value=CertOpenStore(source.registry?CERT_STORE_PROV_SYSTEM_REGISTRY_W:CERT_STORE_PROV_SYSTEM_W,0,0,
                location|CERT_STORE_READONLY_FLAG|CERT_STORE_OPEN_EXISTING_FLAG|CERT_STORE_ENUM_ARCHIVED_FLAG|CERT_SYSTEM_STORE_UNPROTECTED_FLAG,name.c_str());
            if(!store.value){const auto error=GetLastError();certificate_source_error=error;
                if(maintenance_certificate_source_absent(source,error))continue;
                throw SigningError("maintenance_certificate_source_unavailable");}}
        PCCERT_CONTEXT cert{};unsigned count{};
        while((cert=CertEnumCertificatesInStore(store.value,cert))!=nullptr){
            bool conflict=false;DWORD bytes{};
            if(CertGetCertificateContextProperty(cert,CERT_KEY_PROV_INFO_PROP_ID,nullptr,&bytes)){
                if(bytes>65536)conflict=true;
                else {std::vector<BYTE> data(bytes);if(!CertGetCertificateContextProperty(cert,CERT_KEY_PROV_INFO_PROP_ID,data.data(),&bytes))conflict=true;
                    else {const auto info=reinterpret_cast<CRYPT_KEY_PROV_INFO*>(data.data());
                        if(info->pwszContainerName && info->pwszContainerName==j.ownership.key_name && (info->dwFlags&CRYPT_MACHINE_KEYSET)!=0)
                            conflict=(!info->pwszProvName || std::wstring(info->pwszProvName)!=MS_KEY_STORAGE_PROVIDER || j.ownership.certificate_sha256.empty() ||
                                cert_hash(cert,CALG_SHA_256)!=j.ownership.certificate_sha256 || !owned_store);}}
            }
            if(uninstall_cleanup && !conflict){try{
                const bool same_hash=cert_hash(cert,CALG_SHA_256)==j.ownership.certificate_sha256;
                conflict=(same_hash || maintenance_certificate_uses_key(cert,signer.key.value)) &&
                    (!same_hash || !owned_store);
            }catch(...){CertFreeCertificateContext(cert);throw;}}
            if(conflict || ++count>4096){CertFreeCertificateContext(cert);throw SigningError("maintenance_key_referenced_by_unowned_certificate");}
        }
        signing_require(GetLastError()==CRYPT_E_NOT_FOUND,"maintenance_certificate_inventory_unavailable");
    }
    certificate_source.clear();certificate_source_error=0;
    if(status==ERROR_SUCCESS){
        const auto security=key_property(signer.key.value,NCRYPT_SECURITY_DESCR_PROPERTY,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION);
        signing_require(system_admin_dacl(reinterpret_cast<PSECURITY_DESCRIPTOR>(const_cast<BYTE*>(security.data())),j.ownership.owner_sid) &&
            key_number(signer.key.value,NCRYPT_EXPORT_POLICY_PROPERTY)==0 && key_number(signer.key.value,NCRYPT_KEY_USAGE_PROPERTY)==NCRYPT_ALLOW_SIGNING_FLAG &&
            (key_number(signer.key.value,NCRYPT_KEY_TYPE_PROPERTY)&NCRYPT_MACHINE_KEY_FLAG)!=0,"maintenance_owned_key_policy_changed");
    }
    if(!j.ownership.thumbprint.empty()){
        validate_ownership(j.ownership,j.ownership.owner_sid);
        Store personal,trust;signer.open_store(personal,L"My",!apply,uninstall_cleanup);signer.open_store(trust,L"Root",!apply,uninstall_cleanup);Certificate my,root;
        my.value=signer.find_owned(personal.value);root.value=signer.find_owned(trust.value);
        for(auto cert:{my.value,root.value})if(cert)signing_require(cert_hash(cert,CALG_SHA_256)==j.ownership.certificate_sha256,"maintenance_owned_certificate_changed");
        if(my.value){signer.certificate.value=CertDuplicateCertificateContext(my.value);signer.open_store(signer.personal,L"My",true);
            if(signer.key.value){auto facts=signer.certificate_facts();
                if(uninstall_cleanup)facts.validity=true; // exact bound certificate may expire; deletion needs ownership, not current trust
                validate_certificate(facts,false);}}
        if(apply && root.value){const auto cert=root.value;root.value=nullptr;signing_require(CertDeleteCertificateFromStore(cert),"maintenance_root_restore_failed");}
        if(apply && my.value){const auto cert=my.value;my.value=nullptr;signing_require(CertDeleteCertificateFromStore(cert),"maintenance_personal_restore_failed");}
    }
    if(apply && signer.key.value){signing_require(NCryptDeleteKey(signer.key.value,NCRYPT_SILENT_FLAG)==ERROR_SUCCESS,"maintenance_owned_key_restore_failed");signer.key.value=0;}
}
