// Included after the checked signing adapter; registration performs no signing.
#include "registration_payload_paths.hpp"
struct RegistryHandle {
    HKEY value{};~RegistryHandle(){if(value)RegCloseKey(value);}
    RegistryHandle()=default;RegistryHandle(const RegistryHandle&)=delete;
};
struct RegistryTransaction {
    HANDLE value=CreateTransaction(nullptr,nullptr,0,0,0,0,nullptr);bool committed{};
    RegistryTransaction(){signing_require(value!=INVALID_HANDLE_VALUE,"registry_transaction_unavailable");}
    ~RegistryTransaction(){if(!committed)RollbackTransaction(value);CloseHandle(value);}
};
void registry_ok(LSTATUS status){signing_require(status==ERROR_SUCCESS,status==ERROR_ACCESS_DENIED?"registry_access_denied":"registry_io_failed");}
bool registry_open(HKEY parent,const std::wstring& name,REGSAM access,RegistryHandle& result,HANDLE transaction=nullptr){
    const auto status=transaction?RegOpenKeyTransactedW(parent,name.c_str(),REG_OPTION_OPEN_LINK,access,&result.value,transaction,nullptr):
        RegOpenKeyExW(parent,name.c_str(),REG_OPTION_OPEN_LINK,access,&result.value);
    if(status==ERROR_FILE_NOT_FOUND)return false;registry_ok(status);
    DWORD type{},size{};const auto link=RegQueryValueExW(result.value,L"SymbolicLinkValue",nullptr,&type,nullptr,&size);
    signing_require(link==ERROR_FILE_NOT_FOUND,"registry_link_or_unexpected_link_value");return true;
}
// Read-only loaded-user inspection includes Windows' LocalSystem root alias.
bool registry_user_hive(const std::wstring& name,REGSAM view,RegistryHandle& user){
    const auto opened=RegOpenKeyExW(HKEY_USERS,name.c_str(),REG_OPTION_OPEN_LINK,KEY_READ|view,&user.value);
    if(opened==ERROR_FILE_NOT_FOUND)return false;registry_ok(opened);
    DWORD type{},size{};const auto status=RegQueryValueExW(user.value,L"SymbolicLinkValue",nullptr,&type,nullptr,&size);
    if(status==ERROR_FILE_NOT_FOUND)return true;registry_ok(status);
    signing_require(type==REG_LINK && size>=2 && size<=1024 && size%2==0,"registry_link_or_unexpected_link_value");
    RegistrationValue value{type,std::vector<uint8_t>(size)};registry_ok(RegQueryValueExW(user.value,L"SymbolicLinkValue",nullptr,&type,value.bytes.data(),&size));
    value.type=type;value.bytes.resize(size);const auto physical=registration_system_user_hive(value,name);
    RegCloseKey(user.value);user.value=nullptr;
    signing_require(registry_open(HKEY_USERS,physical,KEY_READ|view,user),"system_user_hive_unavailable");return true;
}
// Open one component at a time, never follow a registry symbolic-link ancestor.
bool registry_user_classes(HKEY software,REGSAM view,RegistryHandle& classes){
    // Windows implements HKCU\Software\Classes as a standard link to this
    // user's separate class hive. Validate that exact OS target and open the
    // physical hive directly; an arbitrary registry link remains forbidden.
    const auto opened=RegOpenKeyExW(software,L"Classes",REG_OPTION_OPEN_LINK,KEY_READ|view,&classes.value);
    if(opened==ERROR_FILE_NOT_FOUND)return false;registry_ok(opened);
    DWORD type{},size{};const auto status=RegQueryValueExW(classes.value,L"SymbolicLinkValue",nullptr,&type,nullptr,&size);
    if(status==ERROR_FILE_NOT_FOUND)return true;registry_ok(status);
    signing_require(type==REG_LINK && size>=2 && size<=1024 && size%2==0,"registry_link_or_unexpected_link_value");
    RegistrationValue value{type,std::vector<uint8_t>(size)};registry_ok(RegQueryValueExW(classes.value,L"SymbolicLinkValue",nullptr,&type,
        value.bytes.data(),&size));value.type=type;value.bytes.resize(size);
    Handle token;signing_require(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token.value),"current_user_identity_unavailable");
    DWORD needed{};GetTokenInformation(token.value,TokenUser,nullptr,0,&needed);
    signing_require(needed>0 && needed<65536,"current_user_identity_unavailable");std::vector<BYTE> user(needed);
    signing_require(GetTokenInformation(token.value,TokenUser,user.data(),needed,&needed),"current_user_identity_unavailable");
    Local sid;signing_require(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid,
        reinterpret_cast<LPWSTR*>(&sid.value)),"current_user_identity_unavailable");
    const auto hive=registration_user_class_hive(value,static_cast<wchar_t*>(sid.value));
    RegCloseKey(classes.value);classes.value=nullptr;
    signing_require(registry_open(HKEY_USERS,hive,KEY_READ|view,classes),"current_user_class_hive_unavailable");
    return true;
}
bool registry_parent(HKEY hive,REGSAM view,RegistryHandle& result,HANDLE transaction=nullptr,REGSAM extra=0){
    RegistryHandle software,classes;
    if(!registry_open(hive,L"SOFTWARE",KEY_READ|view,software,transaction))return false;
    if(hive==HKEY_CURRENT_USER){
        if(!registry_user_classes(software.value,view,classes))return false;
    }else if(!registry_open(software.value,L"Classes",KEY_READ|view,classes,transaction))return false;
    return registry_open(classes.value,L"CLSID",KEY_READ|view|extra,result,transaction);
}
RegistrationTree registry_tree(HKEY key,size_t& budget,unsigned depth=0,HANDLE transaction=nullptr,REGSAM view=KEY_WOW64_64KEY){
    signing_require(depth<8 && budget>=16,"registration_snapshot_too_large");budget-=16;
    DWORD children{},values{},max_name{},max_data{};
    registry_ok(RegQueryInfoKeyW(key,nullptr,nullptr,nullptr,&children,nullptr,nullptr,&values,&max_name,&max_data,nullptr,nullptr));
    signing_require(children<=32 && values<=32 && max_name<=256 && max_data<=32768,"registration_snapshot_too_large");
    RegistrationTree tree;
    for(DWORD i=0;i<values;++i){std::array<wchar_t,257> name{};DWORD length=256,type{},size=max_data;
        std::vector<uint8_t> bytes(max_data);registry_ok(RegEnumValueW(key,i,name.data(),&length,nullptr,&type,bytes.data(),&size));
        const auto cost=static_cast<size_t>(size)+static_cast<size_t>(length)*2+8;
        signing_require(cost<=budget,"registration_snapshot_too_large");budget-=cost;bytes.resize(size);
        signing_require(tree.values.emplace(std::wstring(name.data(),length),RegistrationValue{type,std::move(bytes)}).second,"registry_changed_during_read");}
    for(DWORD i=0;i<children;++i){std::array<wchar_t,257> name{};DWORD length=256;
        registry_ok(RegEnumKeyExW(key,i,name.data(),&length,nullptr,nullptr,nullptr,nullptr));RegistryHandle child;
        signing_require(length*2<=budget,"registration_snapshot_too_large");budget-=length*2;
        signing_require(registry_open(key,std::wstring(name.data(),length),KEY_READ|view,child,transaction),"registry_changed_during_read");
        tree.children.emplace(std::wstring(name.data(),length),registry_tree(child.value,budget,depth+1,transaction,view));}
    return tree;
}
RegistrationSnapshot registry_capture(HKEY hive=HKEY_LOCAL_MACHINE,REGSAM view=KEY_WOW64_64KEY,HANDLE transaction=nullptr){
    RegistryHandle parent,key;
    if(!registry_parent(hive,view,parent,transaction) || !registry_open(parent.value,outloud::sapi::clsid_text,KEY_READ|view,key,transaction))return {};
    size_t budget=65536;return registry_tree(key.value,budget,0,transaction,view);
}
void registry_overrides(){
    // Do not touch overrides, redirected classes or any Speech token location.
    signing_require(!registry_capture(HKEY_CURRENT_USER,KEY_WOW64_64KEY) &&
        !registry_capture(HKEY_CURRENT_USER,KEY_WOW64_32KEY) &&
        !registry_capture(HKEY_LOCAL_MACHINE,KEY_WOW64_32KEY),"registration_override_conflict");
}
void registry_write_values(HKEY key,const RegistrationTree& tree){
    for(const auto& [name,value]:tree.values)registry_ok(RegSetValueExW(key,name.c_str(),0,value.type,
        value.bytes.data(),static_cast<DWORD>(value.bytes.size())));
}
void registry_exchange(const RegistrationSnapshot& before,const RegistrationTree& desired){
    signing_require(!before,"registration_replace_not_supported");
    RegistryTransaction transaction;RegistryHandle parent,root,server;
    signing_require(registry_parent(HKEY_LOCAL_MACHINE,KEY_WOW64_64KEY,parent,transaction.value,KEY_CREATE_SUB_KEY),"registry_parent_missing");
    signing_require(registry_capture(HKEY_LOCAL_MACHINE,KEY_WOW64_64KEY,transaction.value)==before,"registration_changed");
    registry_overrides();DWORD disposition{};
    registry_ok(RegCreateKeyTransactedW(parent.value,outloud::sapi::clsid_text,0,nullptr,REG_OPTION_NON_VOLATILE,
        KEY_READ|KEY_WRITE|KEY_WOW64_64KEY,nullptr,&root.value,&disposition,transaction.value,nullptr));
    signing_require(disposition==REG_CREATED_NEW_KEY,"registration_changed");registry_write_values(root.value,desired);
    registry_ok(RegCreateKeyTransactedW(root.value,L"InprocServer32",0,nullptr,REG_OPTION_NON_VOLATILE,
        KEY_READ|KEY_WRITE|KEY_WOW64_64KEY,nullptr,&server.value,&disposition,transaction.value,nullptr));
    signing_require(disposition==REG_CREATED_NEW_KEY,"registration_changed");registry_write_values(server.value,desired.children.at(L"InprocServer32"));
    // Reopen the complete tree through this transaction, including its child.
    signing_require(registry_capture(HKEY_LOCAL_MACHINE,KEY_WOW64_64KEY,transaction.value)==RegistrationSnapshot(desired),"registration_transaction_verification_failed");
    signing_require(CommitTransaction(transaction.value)!=FALSE,"registry_commit_failed");transaction.committed=true;
}
std::wstring registration_inspection(const fs::path& app){
    const auto root=installation_path(app);safe(root);
    // This early inspection does not require a production certificate or installed files.
    const auto before=registry_capture();RegistrationIdentity identity{L"",L"",(root/signing_target).lexically_normal().native()};
    const auto state=root/L"configuration/signing.ini";
    if(fs::exists(state)){require_admin_protected(state);Handle file;pin(state,file);
        signing_require(ini(state,L"ownership",L"product")==L"OutLoud" && ini(state,L"ownership",L"scope")==L"LocalMachine" &&
            ini(state,L"ownership",L"schema")==L"1","ownership_mismatch");
        identity.installation_id=ini(state,L"ownership",L"installation_id");identity.certificate_sha256=ini(state,L"ownership",L"certificate_sha256");
        signing_require(hexadecimal(identity.installation_id,32) && hexadecimal(identity.certificate_sha256,64),"ownership_mismatch");}
    const auto disposition=inspect_registration(before,identity);std::wstring status;
    switch(disposition){case RegistrationDisposition::absent:status=L"absent";break;
        case RegistrationDisposition::owned:status=L"owned_candidate";break;
        case RegistrationDisposition::conflict:status=L"conflict";break;
        default:status=L"malformed";break;}
    try{registry_overrides();}catch(const SigningError&){status=L"override_conflict";}
    // Candidate is not successful post-verification. Do not use this as authority to write.
    return L"[results]\nschema=1\nhive=HKLM\nview=64\nclsid="+std::wstring(outloud::sapi::clsid_text)+
        L"\nregistration="+status+L"\nbefore_encoding=utf16le-typed-tree-v1\nbefore="+encoded_tree(before)+L"\n";
}
struct WindowsRegistration {
    WindowsSigning signing;fs::path policy,probe,requirements,pending,record;
    Handle policy_pin,probe_pin,requirements_pin;std::vector<std::unique_ptr<Handle>> payload_pins;
    WindowsRegistration(const fs::path& app,const fs::path& signing_policy,const fs::path& registration_policy,const fs::path& detector):
        signing(app,signing_policy,L"S-1-5-32-544",false),policy(registration_policy),probe(detector),
        requirements(policy.parent_path()/L"prerequisites.ini"),pending(signing.root/L"configuration/registration.pending.ini"),
        record(signing.root/L"configuration/registration.ini"){}
    RegistrationIdentity identity()const{return {signing.record.id,signing.record.certificate_sha256,signing.target.native()};}
    void verify_signing(){
        signing_require(file_hash(signing.file.value)==signing.record.signed_sha256,"registration_signed_identity_mismatch");
        validate_certificate(signing.certificate_facts(),true);signing.verify_required();
    }
    void preconditions(){
        signing.preflight(false);signing.load_ownership();verify_signing();
        signing_require(!fs::exists(pending),"registration_recovery_required");pin(policy,policy_pin);
        signing_require(ini(policy,L"registration",L"schema")==L"1" && ini(policy,L"registration",L"hive")==L"HKLM" &&
            ini(policy,L"registration",L"view")==L"64" && ini(policy,L"registration",L"clsid")==outloud::sapi::clsid_text &&
            ini(policy,L"registration",L"threading_model")==L"Both","registration_policy_invalid");
        const auto count_text=ini(policy,L"registration",L"file_count");
        signing_require(count_text.find_first_not_of(L"0123456789")==std::wstring::npos,"registration_policy_invalid");
        const auto count=std::stoul(count_text);signing_require(count>0 && count<=64,"registration_policy_invalid");
        signing_require(count==std::size(registration_payload_paths),"registration_payload_incomplete");
        std::set<std::wstring> paths;bool engine=false,runtime=false;
        for(unsigned i=0;i<count;++i){const auto section=L"file"+std::to_wstring(i);const auto relative=ini(policy,section.c_str(),L"path");
            const fs::path part(relative);signing_require(!part.is_absolute() && !part.has_root_path() &&
                relative.find(L"..") == std::wstring::npos && relative.find(L':')==std::wstring::npos &&
                paths.insert(relative).second,"registration_payload_path_invalid");
            const auto target=signing.root/part;require_admin_protected(target);
            const auto expected_hash=ini(policy,section.c_str(),L"sha256");signing_require(hexadecimal(expected_hash,64),"registration_policy_invalid");
            if(part.generic_wstring()==signing_target){engine=true;signing_require(sip_digest(target,signing.file.value)==expected_hash,"registration_payload_invalid");}
            else {auto handle=std::make_unique<Handle>();pin(target,*handle);
                signing_require(file_hash(handle->value)==expected_hash,"registration_payload_invalid");payload_pins.push_back(std::move(handle));}
            if(part.generic_wstring()==L"bin/OutLoud.VoiceManager.runtimeconfig.json")runtime=true;
        }
        signing_require(engine && runtime,"registration_payload_incomplete");
        for(const auto expected:registration_payload_paths)signing_require(paths.contains(expected),"registration_payload_incomplete");
        pin(probe,probe_pin);pin(requirements,requirements_pin);
        signing_require(file_hash(probe_pin.value)==ini(policy,L"registration",L"detector_sha256") &&
            file_hash(requirements_pin.value)==ini(policy,L"registration",L"requirements_sha256"),"registration_prerequisite_identity_invalid");
        verify_prerequisites();
    }
    void verify_prerequisites(){
        const auto report=policy.parent_path()/(L"registration-prerequisites-"+signing.record.id+L".ini");
        signing_require(!fs::exists(report),"registration_prerequisite_report_exists");
        std::wstring command=L"\""+probe.native()+L"\" --requirements \""+requirements.native()+L"\" --runtime-config \""+
            (signing.root/L"bin/OutLoud.VoiceManager.runtimeconfig.json").native()+L"\" --report \""+report.native()+L"\"";
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
        signing_require(CreateProcessW(probe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,
            policy.parent_path().c_str(),&startup,&process),"registration_prerequisite_probe_failed");
        Handle process_handle,thread;process_handle.value=process.hProcess;thread.value=process.hThread;
        signing_require(WaitForSingleObject(process.hProcess,60000)==WAIT_OBJECT_0,"registration_prerequisite_probe_timeout");
        DWORD code{};signing_require(GetExitCodeProcess(process.hProcess,&code) && code==0,"registration_prerequisites_missing");
        Handle report_pin;pin(report,report_pin);
        signing_require(ini(report,L"results",L"schema")==L"1" && ini(report,L"results",L"windows")==L"compatible" &&
            ini(report,L"results",L"vc")==L"compatible" && ini(report,L"results",L"dotnet")==L"compatible","registration_prerequisites_missing");
    }
    RegistrationSnapshot capture(){return registry_capture();}
    void check_overrides(){registry_overrides();}
    void verify_server(const RegistrationTree& tree){
        const fs::path server=registration_field(tree.children.at(L"InprocServer32"),L"");safe(server);
        // Same checked physical-file policy as validate_engine_registration: aliases
        // are acceptable only for the installed pinned file, never another deployment.
        signing_require(same_installation_path(server.parent_path(),signing.target.parent_path()),"registration_unexpected_server_path");
        Handle actual;pin(server,actual);FILE_ID_INFO a{},b{};
        signing_require(GetFileInformationByHandleEx(actual.value,FileIdInfo,&a,sizeof(a)) &&
            GetFileInformationByHandleEx(signing.file.value,FileIdInfo,&b,sizeof(b)) &&
            a.VolumeSerialNumber==b.VolumeSerialNumber && memcmp(&a.FileId,&b.FileId,sizeof(a.FileId))==0,"registration_server_identity_mismatch");
    }
    void capture_recovery(const RegistrationSnapshot& before){
        signing_require(!fs::exists(record),"registration_recovery_required");
        write_new(pending,L"[registration]\nschema=1\nphase=before_registry_transaction\nhive=HKLM\nview=64\nclsid="+
            std::wstring(outloud::sapi::clsid_text)+L"\ninstallation_id="+identity().installation_id+L"\ncertificate_sha256="+
            identity().certificate_sha256+L"\nbefore_encoding=utf16le-typed-tree-v1\nbefore="+encoded_tree(before)+
            L"\ndesired="+encoded_tree(RegistrationSnapshot(desired_registration(identity())))+L"\n",true);
        require_admin_protected(pending);
    }
    void exchange(const RegistrationSnapshot& before,const RegistrationTree& desired){registry_exchange(before,desired);}
    void verify_post(){
        const auto current=capture();signing_require(inspect_registration(current,identity())==RegistrationDisposition::owned,"registration_post_verification_failed");
        check_overrides();verify_server(*current);verify_signing();
    }
    void commit_recovery(){signing_require(MoveFileExW(pending.c_str(),record.c_str(),MOVEFILE_WRITE_THROUGH),"registration_receipt_commit_failed");require_admin_protected(record);}
};
