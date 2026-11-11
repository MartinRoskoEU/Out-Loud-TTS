// Plans are immutable, protected, length-framed receipts. Absence is accepted only
// on a retry of this plan; any changed surviving identity refuses further removal.
struct UninstallFile {unsigned group{};fs::path path;MaintenanceStamp stamp;};
// Bound the deletion plan, not the number of historical updates. Completed
// archives are retired automatically; older installations can contain more than
// the previous 8192 entries. The encoded-plan byte limit remains enforced too.
inline constexpr size_t uninstall_inventory_limit=65536;
struct UninstallDirectory {fs::path path;std::wstring identity;std::unique_ptr<Handle> pin;};
struct UninstallPlan {
    MaintenanceJournal owner;fs::path host_executable;std::vector<UninstallFile> files;
    std::vector<std::pair<fs::path,std::wstring>> directories;
    std::map<std::wstring,RegistrationTree> tokens;
};
struct WindowsUninstall;
std::set<fs::path> uninstall_pending_archive_cleanup(WindowsUninstall&);
bool uninstall_winui_path(const fs::path&,const fs::path&,bool);
bool uninstall_within(const fs::path& path,const fs::path& root){const auto rel=path.lexically_relative(root);
    return !rel.empty() && !rel.native().starts_with(L"..") && !rel.is_absolute();}
bool uninstall_payload_directory(const fs::path& path,const fs::path& root){
    if(!uninstall_within(path,root))return false;
    try{maintenance_payload_path(path.lexically_relative(root).generic_wstring()+L"/owned.placeholder");return true;}
    catch(const SigningError&){return false;}
}
bool uninstall_library_path(const fs::path& path,const fs::path& library,bool directory){
    if(path==library)return directory;if(!uninstall_within(path,library))return false;
    std::vector<std::wstring> parts;for(const auto& p:path.lexically_relative(library))parts.push_back(p.native());
    const auto n=parts.size();const auto& top=parts.front();
    if(directory){if(n==1)return top==L"voices" || top==L".staging" || top==L".trash";
        if(n==2)return (top==L"voices" || top==L".staging" || top==L".trash") && hexadecimal(parts[1],32);
        return n==3 && top==L".trash" && hexadecimal(parts[1],32) && parts[2]==L"retained";}
    const auto voice=[](const std::wstring& name){return name==L"voice.safetensors" || name==L"metadata.json";};
    if(n==1)return top==L".outloud-installation.dat" || top==L".library.lock";
    if(n==3 && hexadecimal(parts[1],32)){
        if(top==L"voices")return voice(parts[2]);
        if(top==L".staging")return std::set<std::wstring>{L"voice.safetensors",L"metadata.json"}.contains(parts[2]);
        if(top==L".trash")return parts[2]==L"remove.json" || parts[2]==L"control.tmp";
    }return n==4 && top==L".trash" && hexadecimal(parts[1],32) && parts[2]==L"retained" && voice(parts[3]);
}
std::wstring uninstall_path_decode(const std::wstring& s){
    signing_require(!s.empty() && s.size()%4==0 && s.find_first_not_of(L"0123456789abcdef")==s.npos,"uninstall_path_encoding_invalid");
    std::wstring result;for(size_t i=0;i<s.size();i+=4){const auto byte=[&](size_t at){return std::stoul(s.substr(at,2),nullptr,16);};
        result+=static_cast<wchar_t>(byte(i)|(byte(i+2)<<8));}return installation_path(result).native();
}
RegistrationSnapshot uninstall_decode_tree(const std::wstring& text){
    signing_require(text!=L"absent" && text.size()<=131072 && text.size()%2==0 && text.find_first_not_of(L"0123456789abcdef")==text.npos,"uninstall_registry_encoding_invalid");
    std::vector<uint8_t> bytes;for(size_t i=0;i<text.size();i+=2)bytes.push_back(static_cast<uint8_t>(std::stoul(text.substr(i,2),nullptr,16)));
    size_t cursor{};const auto number=[&](){signing_require(cursor+4<=bytes.size(),"uninstall_registry_encoding_invalid");uint32_t n{};
        for(unsigned i=0;i<4;++i)n|=static_cast<uint32_t>(bytes[cursor++])<<(i*8);return n;};
    const auto value=[&](uint32_t type){const auto n=number();signing_require(n<=32768 && cursor+n<=bytes.size(),"uninstall_registry_encoding_invalid");
        RegistrationValue v{type,{bytes.begin()+cursor,bytes.begin()+cursor+n}};cursor+=n;return v;};
    const auto name=[&](){return registration_text(value(REG_SZ));};
    const auto read=[&](auto&& self,unsigned depth)->RegistrationTree{signing_require(depth<8,"uninstall_registry_encoding_invalid");RegistrationTree t;
        const auto values=number();signing_require(values<=32,"uninstall_registry_encoding_invalid");for(uint32_t i=0;i<values;++i){const auto key=name();const auto type=number();
            signing_require(t.values.emplace(key,value(type)).second,"uninstall_registry_encoding_invalid");}
        const auto children=number();signing_require(children<=32,"uninstall_registry_encoding_invalid");for(uint32_t i=0;i<children;++i){const auto key=name();
            signing_require(t.children.emplace(key,self(self,depth+1)).second,"uninstall_registry_encoding_invalid");}return t;};
    const auto t=read(read,0);signing_require(cursor==bytes.size(),"uninstall_registry_encoding_invalid");return t;
}
bool uninstall_tokens_root(HKEY hive,REGSAM view,RegistryHandle& root,HANDLE tx=nullptr,bool writable=false,const wchar_t* speech=L"Speech"){
    RegistryHandle current,next;HKEY parent=hive;
    for(const auto* name:{L"SOFTWARE",L"Microsoft",speech,L"Voices",L"Tokens"}){
        if(!registry_open(parent,name,KEY_READ|view|((writable && std::wstring(name)==L"Tokens")?KEY_WRITE:0),next,tx))return false;
        if(current.value)RegCloseKey(current.value);current.value=next.value;next.value=nullptr;parent=current.value;}
    root.value=current.value;current.value=nullptr;return true;
}
std::map<std::wstring,RegistrationTree> uninstall_tokens(HKEY hive,REGSAM view,HANDLE tx=nullptr,const wchar_t* speech=L"Speech"){
    RegistryHandle root;std::map<std::wstring,RegistrationTree> result;if(!uninstall_tokens_root(hive,view,root,tx,false,speech))return result;
    for(DWORD i=0;;++i){wchar_t name[257]{};DWORD length=256;const auto status=RegEnumKeyExW(root.value,i,name,&length,nullptr,nullptr,nullptr,nullptr);
        if(status==ERROR_NO_MORE_ITEMS)break;registry_ok(status);signing_require(i<4096,"uninstall_token_inventory_limit");RegistryHandle key;
        signing_require(registry_open(root.value,std::wstring(name,length),KEY_READ|view,key,tx),"uninstall_token_inventory_raced");size_t budget=65536;
        result.emplace(std::wstring(name,length),registry_tree(key.value,budget,0,tx,view));}return result;
}
bool uninstall_dependency(const RegistrationTree& tree){const auto it=tree.values.find(L"CLSID");
    return it!=tree.values.end() && _wcsicmp(registration_text(it->second).c_str(),outloud::sapi::clsid_text)==0;
}
bool managed_voice_registration_candidate(const std::wstring& name,const RegistrationTree& tree){
    constexpr std::wstring_view prefix=L"OutLoud_Pocket_";
    if((name.size()>=prefix.size() && CompareStringOrdinal(name.data(),static_cast<int>(prefix.size()),prefix.data(),
        static_cast<int>(prefix.size()),TRUE)==CSTR_EQUAL))return true;
    // Registry value names are case-insensitive. Malformed references are review
    // candidates, never ownership proof; the strict validator still rejects them.
    for(const auto& [field,value]:tree.values){
        if(_wcsicmp(field.c_str(),L"OutLoudManagedOwner")==0)return true;
        if(_wcsicmp(field.c_str(),L"CLSID")==0){
            auto claimed=value;claimed.type=REG_SZ;
            try{if(_wcsicmp(registration_text(claimed).c_str(),outloud::sapi::clsid_text)==0)return true;}catch(const SigningError&){}
        }
    }
    return false;
}
struct UninstallVoiceMismatch {std::wstring token,display_name,field;fs::path actual,expected;};
std::wstring uninstall_voice_mismatch_report(const UninstallVoiceMismatch& mismatch){
    if(mismatch.token.empty())return {};
    return L"blocked_voice_encoding=utf16le-hex\nblocked_voice="+maintenance_root_encoding(mismatch.token)+L"\nblocked_voice_name="+maintenance_root_encoding(mismatch.display_name)+
        L"\nvoice_field="+mismatch.field+L"\nvoice_actual="+maintenance_root_encoding(mismatch.actual.native())+
        L"\nvoice_expected="+maintenance_root_encoding(mismatch.expected.native())+L"\n";
}
void uninstall_token_schema_owned(const std::wstring& name,const RegistrationTree& t){
    signing_require(name.size()==47 && name.starts_with(L"OutLoud_Pocket_") && hexadecimal(name.substr(15),32),"uninstall_unowned_voice_dependency");
    const std::set<std::wstring> values{L"",L"409",L"CLSID",L"OutLoudManagedOwner",L"LibraryId",L"AssetRoot",L"Backend",L"RustRuntimePath",L"VoicePath"};
    const std::set<std::wstring> attributes{L"Name",L"Language",L"Vendor"};
    signing_require(t.values.size()==values.size() && t.children.size()==1 && t.children.contains(L"Attributes"),"uninstall_voice_schema_changed");
    for(const auto& [key,value]:t.values){signing_require(values.contains(key),"uninstall_voice_schema_changed");static_cast<void>(registration_text(value));}
    const auto& a=t.children.at(L"Attributes");signing_require(a.children.empty() && a.values.size()==attributes.size(),"uninstall_voice_schema_changed");
    for(const auto& [key,value]:a.values){signing_require(attributes.contains(key),"uninstall_voice_schema_changed");static_cast<void>(registration_text(value));}
    signing_require(registration_field(t,L"OutLoudManagedOwner")==L"OutLoud.ManagedVoice.1" && registration_field(t,L"LibraryId")==name.substr(15) &&
        registration_field(a,L"Vendor")==L"Out Loud" && registration_field(t,L"Backend")==L"Rust" && uninstall_dependency(t),"uninstall_unowned_voice_dependency");
}
// Registry-only cleanup of dependencies of the independently verified owned engine.
// The caller authenticates the shared library binding separately. AssetRoot and
// RustRuntimePath are typed metadata, never eligibility or file-deletion authority.
void uninstall_engine_token_owned(const std::wstring& name,const RegistrationTree& t,const fs::path& library,UninstallVoiceMismatch* mismatch=nullptr){
    UninstallVoiceMismatch context;context.token=name;
    const auto display=t.values.find(L"");
    if(display!=t.values.end())try{context.display_name=registration_text(display->second);}catch(const SigningError&){}
    try{
        uninstall_token_schema_owned(name,t);
        context.field=L"VoicePath";context.actual=registration_field(t,L"VoicePath");
        context.expected=library/L"voices"/name.substr(15)/L"voice.safetensors";
        signing_require(same_installation_path(context.actual,context.expected),"uninstall_unverified_voice_path");
    }catch(...){if(mismatch)*mismatch=std::move(context);throw;}
}
std::map<std::wstring,RegistrationTree> uninstall_collect_engine_tokens(const std::map<std::wstring,RegistrationTree>& current,
    const fs::path& library,UninstallVoiceMismatch* mismatch=nullptr){
    std::map<std::wstring,RegistrationTree> result;
    for(const auto& [name,t]:current)if(managed_voice_registration_candidate(name,t)){
        uninstall_engine_token_owned(name,t,library,mismatch);result.emplace(name,t);
    }
    return result;
}
void uninstall_verify_voice_snapshot(const std::map<std::wstring,RegistrationTree>& current,
    const std::map<std::wstring,RegistrationTree>& planned,bool retry,const char* reason){
    for(const auto& [name,t]:current)if(managed_voice_registration_candidate(name,t)){
        const auto expected=planned.find(name);signing_require(expected!=planned.end() && expected->second==t,reason);
    }
    for(const auto& [name,t]:planned){const auto now=current.find(name);signing_require(now==current.end()?retry:now->second==t,reason);}
}
void uninstall_no_foreign_dependencies(){
    // Our manager only writes HKLM64 Speech. Other views/categories/users are never adopted.
    for(const auto view:{KEY_WOW64_64KEY,KEY_WOW64_32KEY})for(const auto hive:{HKEY_LOCAL_MACHINE,HKEY_CURRENT_USER})
        for(const auto* speech:{L"Speech",L"Speech_OneCore"}){
            if(hive==HKEY_LOCAL_MACHINE && view==KEY_WOW64_64KEY && std::wstring(speech)==L"Speech")continue;
            for(const auto& [name,t]:uninstall_tokens(hive,view,nullptr,speech))signing_require(!managed_voice_registration_candidate(name,t),"uninstall_foreign_voice_dependency");}
    for(DWORD i=0;;++i){wchar_t sid[257]{};DWORD length=256;auto status=RegEnumKeyExW(HKEY_USERS,i,sid,&length,nullptr,nullptr,nullptr,nullptr);
        if(status==ERROR_NO_MORE_ITEMS)break;registry_ok(status);signing_require(i<4096,"uninstall_user_inventory_limit");RegistryHandle user;
        signing_require(registry_user_hive(std::wstring(sid,length),0,user),"uninstall_user_inventory_raced");
        if(std::wstring(sid,length).ends_with(L"_Classes"))for(const auto view:{KEY_WOW64_64KEY,KEY_WOW64_32KEY}){RegistryHandle clsids,engine;
            if(registry_open(user.value,L"CLSID",KEY_READ|view,clsids))signing_require(!registry_open(clsids.value,outloud::sapi::clsid_text,KEY_READ|view,engine),"uninstall_foreign_com_override");}
        for(const auto view:{KEY_WOW64_64KEY,KEY_WOW64_32KEY})for(const auto* speech:{L"Speech",L"Speech_OneCore"})
            for(const auto& [name,t]:uninstall_tokens(user.value,view,nullptr,speech))signing_require(!managed_voice_registration_candidate(name,t),"uninstall_foreign_voice_dependency");}
}
void uninstall_delete_held(HANDLE handle){FILE_DISPOSITION_INFO disposition{TRUE};
    signing_require(SetFileInformationByHandle(handle,FileDispositionInfo,&disposition,sizeof(disposition)),"uninstall_checked_delete_failed");}
struct WindowsUninstall {
    WindowsMaintenance maintenance;UninstallPlan plan;fs::path receipt,library,blocked_resource;bool retry{};
    UninstallVoiceMismatch voice_mismatch;
    std::vector<UninstallDirectory> directories;
    std::vector<std::unique_ptr<Handle>> data_ancestors;
    explicit WindowsUninstall(const fs::path& app):maintenance(app),receipt(maintenance.files.root/L".outloud-uninstall.dat"),library(maintenance_library()){}
    MaintenanceFiles& storage(){return maintenance.files;}
    void add(unsigned group,const fs::path& path,const MaintenanceFile& expected={}){
        blocked_resource=path;
        safe(path);const auto actual=storage().stamp(path);signing_require(actual && (!expected || actual==expected),"uninstall_file_ownership_changed");
        signing_require(plan.files.size()<uninstall_inventory_limit,"uninstall_file_inventory_limit");plan.files.push_back({group,path,*actual});
    }
    void directory(const fs::path& path){safe(path);storage().security(path);if(std::none_of(plan.directories.begin(),plan.directories.end(),[&](const auto& d){return d.first==path;}))plan.directories.emplace_back(path,maintenance_directory_identity(path));}
    void library_inventory(const fs::path& path,unsigned depth=0){
        signing_require(depth<5,"uninstall_library_depth_invalid");directory(path);
        for(const auto& entry:fs::directory_iterator(path)){blocked_resource=entry.path();
            if(!uninstall_library_path(entry.path(),library,true) && !uninstall_library_path(entry.path(),library,false))continue;
            if(!uninstall_library_path(entry.path(),library,entry.is_directory()))continue;
            safe(entry.path());
            if(entry.is_directory()){
                signing_require(uninstall_library_path(entry.path(),library,true),"uninstall_unknown_library_directory_preserved");library_inventory(entry.path(),depth+1);
            }else{
                signing_require(entry.is_regular_file() && uninstall_library_path(entry.path(),library,false),"uninstall_unknown_library_file_preserved");add(1,entry.path());
            }}
    }
    void archives(){
        const auto base=storage().root/L"configuration/maintenance";if(!fs::exists(base))return;directory(base);
        const auto resuming=uninstall_pending_archive_cleanup(*this);
        for(const auto& entry:fs::directory_iterator(base)){
            if(resuming.contains(entry.path()))continue;
            if(!entry.is_directory() || !hexadecimal(entry.path().filename().native(),32))continue;
            safe(entry.path());
            const auto vault=entry.path();MaintenanceFiles archive(storage().root);archive.vault=vault;archive.security=storage().security;
            if(!fs::exists(vault/L"terminal.pointer"))continue; // unowned contents left by a previous cleanup
            const auto pointer=archive.read(vault/L"terminal.pointer");signing_require(pointer==vault.filename().native()+L"\n","uninstall_archive_not_terminal");
            unsigned frames{};while(frames<4*maintenance_inventory_limit+256 && fs::exists(vault/(L"state-"+std::to_wstring(frames)+L".dat")))++frames;
            signing_require(frames>0,"uninstall_archive_not_terminal");
            const auto terminal=maintenance_decode(archive.read(vault/(L"state-"+std::to_wstring(frames-1)+L".dat")),maintenance.package());
            signing_require(terminal.phase==MaintenancePhase::committed || terminal.phase==MaintenancePhase::rolled_back,"uninstall_archive_not_terminal");
            std::set<fs::path> archive_directories{vault/L"bootstrap"},payload_directories;
            std::set<std::wstring> indexed_paths;
            // Index the same verified frames once: a self-contained payload has hundreds of files.
            std::map<std::wstring,MaintenanceFile> archived_files{{L"terminal.pointer",{}}};
            for(unsigned n=0;n<frames;++n){
                archived_files[L"state-"+std::to_wstring(n)+L".dat"]={};
                // Each checkpoint contains the whole transaction. Audit one at
                // a time; retaining all of them multiplies memory by file count.
                const auto j=maintenance_decode(archive.read(vault/(L"state-"+std::to_wstring(n)+L".dat")),maintenance.package());
                signing_require(j.installation_id==plan.owner.installation_id && j.root_encoding==plan.owner.root_encoding && j.transaction_id==vault.filename().native(),"uninstall_archive_identity_invalid");
                signing_require(j.ownership.certificate_sha256.empty() || j.ownership.certificate_sha256==plan.owner.ownership.certificate_sha256 ||
                    (terminal.phase==MaintenancePhase::rolled_back && j.operation==MaintenanceOperation::fresh && j.old_version.empty()),"uninstall_archive_certificate_invalid");
                for(const auto& c:j.changes){
                    // Every checkpoint repeats the full payload list. Directory
                    // authority depends only on the validated relative path;
                    // derive it once while still checking every frame/stamp.
                    if(indexed_paths.insert(c.path).second){
                        for(const auto* part:{L"backup",L"original",L"new",L"failed"})
                            for(auto path=(vault/part/c.path).parent_path();path!=vault;path=path.parent_path())archive_directories.insert(path);
                        if(!maintenance_is_metadata(c.path))for(auto parent=storage().path(c.path).parent_path();parent!=storage().root;parent=parent.parent_path())
                            payload_directories.insert(parent);
                    }
                    if(c.backup)archived_files[L"backup/"+c.path]=c.backup;
                    if(c.before)archived_files[L"original/"+c.path]=c.before;
                    if(c.after){archived_files[L"new/"+c.path]=c.after;archived_files[L"failed/"+c.path]=c.after;}
                }
                if(j.bootstrap_detector)archived_files[L"bootstrap/OutLoudPrerequisites.exe"]=j.bootstrap_detector;
                if(j.bootstrap_requirements)archived_files[L"bootstrap/prerequisites.ini"]=j.bootstrap_requirements;
                if(j.bootstrap_report)archived_files[L"bootstrap/results.ini"]=j.bootstrap_report;
            }
            for(const auto& parent:payload_directories)if(fs::exists(parent))directory(parent);
            directory(vault);
            for(auto it=fs::recursive_directory_iterator(vault);it!=fs::recursive_directory_iterator{};++it){const auto& row=*it;blocked_resource=row.path();
                if(!archive_directories.contains(row.path()) && !archived_files.contains(row.path().lexically_relative(vault).generic_wstring())){
                    it.disable_recursion_pending();continue;}
                safe(row.path());if(row.is_directory()){
                    if(!archive_directories.contains(row.path())){it.disable_recursion_pending();continue;}
                    directory(row.path());continue;}
                const auto rel=row.path().lexically_relative(vault).generic_wstring();const auto known=archived_files.find(rel);
                signing_require(known!=archived_files.end() && row.is_regular_file(),"uninstall_unknown_archive_file_preserved");add(3,row.path(),known->second);
            }
        }
    }
    void preflight(){
        maintenance.authority();const auto host=maintenance_host_record();signing_require(host && same_installation_path(host->first,storage().root),"uninstall_host_identity_invalid");
        require_admin_protected(storage().root);retry=fs::exists(receipt);
        signing_require(!fs::exists(storage().root/L"configuration/maintenance.pending"),"uninstall_maintenance_recovery_required");
        if(retry){load();installation_inventory();revalidate();return;}
        for(const auto* name:{L"signing.pending.ini",L"signing.next.ini",L"signing.previous.ini",L"registration.pending.ini"})
            signing_require(!fs::exists(storage().root/L"configuration"/name),"uninstall_legacy_recovery_required");
        maintenance.owner=std::make_unique<WindowsSigning>(storage().root,fs::path{},L"S-1-5-32-544",false);
        signing_require(NCryptOpenStorageProvider(&maintenance.owner->provider.value,MS_KEY_STORAGE_PROVIDER,0)==ERROR_SUCCESS,"key_provider_failed");
        const auto owner_ini=maintenance_ini(maintenance.owner->state);signing_require(owner_ini.size()==1 && owner_ini.at(L"ownership").size()==12,"uninstall_ownership_shape_invalid");
        maintenance.owner->load_ownership(true);auto facts=maintenance.owner->certificate_facts();facts.validity=true;validate_certificate(facts,false);
        const auto inventory=maintenance_inventory_decode(storage().read(storage().root/L"configuration/inventory.dat"));
        auto& j=plan.owner;j.installation_id=inventory.installation_id;j.ownership=maintenance.owner->record;j.root_encoding=maintenance_root_encoding(storage().root.native());j.library_identity=inventory.library_identity;
        j.library_parent_identity=inventory.library_parent_identity;
        signing_require(j.installation_id==j.ownership.id && inventory.certificate_hash==j.ownership.certificate_sha256 && host->second==inventory.version &&
            inventory.files.at(signing_target).hash==j.ownership.signed_sha256,"uninstall_inventory_identity_invalid");
        j.registration_after=encoded_tree(desired_registration({j.installation_id,j.ownership.certificate_sha256,(storage().root/signing_target).native()}));
        signing_require(encoded_tree(registry_capture())==j.registration_after,"uninstall_com_ownership_changed");registry_overrides();uninstall_no_foreign_dependencies();
        const auto registration=maintenance_ini(storage().root/L"configuration/registration.ini").at(L"registration");
        signing_require(registration.at(L"installation_id")==j.installation_id && registration.at(L"certificate_sha256")==j.ownership.certificate_sha256 &&
            registration.at(L"desired")==j.registration_after,"uninstall_registration_receipt_changed");
        for(const auto& [path,stamp]:inventory.files){add(0,storage().path(path),stamp);
            for(auto parent=storage().path(path).parent_path();parent!=storage().root;parent=parent.parent_path())directory(parent);}
        for(const auto* path:maintenance_metadata)add(3,storage().path(path));
        add(3,storage().root/L"configuration/maintenance.lock");storage().acquire_epoch(false,true);
        blocked_resource=library;
        maintenance_validate_library(storage(),j);library_inventory(library);archives();
        blocked_resource.clear();
        plan.tokens=uninstall_collect_engine_tokens(uninstall_tokens(HKEY_LOCAL_MACHINE,KEY_WOW64_64KEY),library,&voice_mismatch);
        // Only the current shared store is inventoried; no user-profile discovery.
        winui();recovery_reports();installation_inventory();maintenance.maintenance_remove_created_signing(signing_journal(),false,true);
        lock();revalidate();
    }
    MaintenanceJournal signing_journal()const{auto j=plan.owner;j.operation=MaintenanceOperation::fresh;return j;}
    void winui();void winui_store(const fs::path&);
    void recovery_reports(){for(const auto* name:{L"uninstall.failure.ini",L"uninstall.failure.tmp"}){
        const auto path=storage().root/L"configuration"/name;if(!fs::exists(path))continue;
        const auto text=storage().read(path);signing_require(text.starts_with(L"[results]\nschema=1\nuninstall=incomplete\n") &&
            text.find(L"\ninstallation_id="+plan.owner.installation_id+L"\n")!=text.npos &&
            text.find(L"\nroot="+plan.owner.root_encoding+L"\n")!=text.npos,"uninstall_foreign_failure_report_preserved");
        if(std::none_of(plan.files.begin(),plan.files.end(),[&](const auto& f){return f.path==path;}))add(3,path);}}
    void installation_inventory(bool host_present=true){
        std::set<fs::path> allowed,parents;
        for(const auto& f:plan.files)if(f.group==0 || f.group==3){allowed.insert(f.path);for(auto p=f.path.parent_path();p!=storage().root;p=p.parent_path())parents.insert(p);}
        // The host owns exactly its recorded executable and corresponding data log.
        if(host_present){RegistryHandle record,next;HKEY parent=HKEY_LOCAL_MACHINE;
        for(const auto* component:{L"SOFTWARE",L"Microsoft",L"Windows",L"CurrentVersion",L"Uninstall",L"OutLoud_is1"}){
            signing_require(registry_open(parent,component,KEY_READ|KEY_WOW64_64KEY,next),"uninstall_host_unavailable");
            if(record.value)RegCloseKey(record.value);record.value=next.value;next.value=nullptr;parent=record.value;}
        size_t budget=65536;const auto tree=registry_tree(record.value,budget);const auto uninstall=registration_field(tree,L"UninstallString");
        const auto host=installation_uninstall_executable(uninstall,storage().root,"uninstall_host_command_invalid");
        signing_require(plan.host_executable.empty() || same_installation_path(plan.host_executable,host),"uninstall_host_command_changed");plan.host_executable=host;}
        allowed.insert(plan.host_executable);auto dat=plan.host_executable;dat.replace_extension(L".dat");allowed.insert(dat);
        auto messages=plan.host_executable;messages.replace_extension(L".msg");allowed.insert(messages); // Inno signed-uninstaller messages; host owns removal
        allowed.insert(receipt);allowed.insert(storage().root/L"configuration/uninstall.failure.ini");
        allowed.insert(storage().root/L"configuration/uninstall.failure.tmp");
        for(const auto& [path,id]:plan.directories){static_cast<void>(id);const auto rel=path.lexically_relative(storage().root);
            if(!rel.empty() && !rel.native().starts_with(L".."))parents.insert(path);}
        for(auto it=fs::recursive_directory_iterator(storage().root);it!=fs::recursive_directory_iterator{};++it){const auto& e=*it;blocked_resource=e.path();
            if(!parents.contains(e.path()) && !allowed.contains(e.path())){it.disable_recursion_pending();continue;}
            safe(e.path());signing_require(e.is_directory()?parents.contains(e.path()):(e.is_regular_file() && allowed.contains(e.path())),"uninstall_installation_resource_changed");}
        blocked_resource.clear();
    }
    void file_security(){storage().security=[](const fs::path& p){safe(p);require_admin_protected(p);};}
    std::wstring encode()const{
        MaintenanceWriter w;w.put(L"OutLoud.Uninstall.1");const auto& j=plan.owner;
        for(const auto* s:{&j.installation_id,&j.root_encoding,&j.library_identity,&j.library_parent_identity,&j.ownership.id,&j.ownership.owner_sid,&j.ownership.key_name,&j.ownership.thumbprint,
            &j.ownership.certificate_sha256,&j.ownership.signed_sha256,&j.registration_after})w.put(*s);
        w.put(maintenance_root_encoding(plan.host_executable.native()));
        w.number(plan.files.size());for(const auto& f:plan.files){w.number(f.group);w.put(maintenance_root_encoding(f.path.native()));w.file(f.stamp);}
        w.number(plan.directories.size());for(const auto& [path,id]:plan.directories){w.put(maintenance_root_encoding(path.native()));w.put(id);}
        w.number(plan.tokens.size());for(const auto& [name,t]:plan.tokens){w.put(name);w.put(encoded_tree(t));}return w.text;
    }
    void persist(){if(!retry){signing_require(plan.files.size()<=uninstall_inventory_limit && plan.directories.size()<=uninstall_inventory_limit && plan.tokens.size()<=4096,"uninstall_plan_limit");
        const auto text=encode();signing_require(text.size()<=16*1024*1024,"uninstall_plan_limit");write_new(receipt,text,true);}}
    void load(){
        const auto text=storage().read(receipt);MaintenanceReader r(text);signing_require(r.get()==L"OutLoud.Uninstall.1","uninstall_plan_invalid");auto& j=plan.owner;
        for(auto* s:{&j.installation_id,&j.root_encoding,&j.library_identity,&j.library_parent_identity,&j.ownership.id,&j.ownership.owner_sid,&j.ownership.key_name,&j.ownership.thumbprint,
            &j.ownership.certificate_sha256,&j.ownership.signed_sha256,&j.registration_after})*s=r.get();
        plan.host_executable=uninstall_path_decode(r.get());
        signing_require(same_installation_path(plan.host_executable.parent_path(),storage().root) && plan.host_executable.extension()==L".exe" &&
            plan.host_executable.filename().native().starts_with(L"unins"),"uninstall_plan_host_invalid");
        signing_require(j.root_encoding==maintenance_root_encoding(storage().root.native()) && j.installation_id==j.ownership.id &&
            hexadecimal(j.library_identity,48) && (j.library_parent_identity.empty() || hexadecimal(j.library_parent_identity,48)),"uninstall_plan_wrong_installation");validate_ownership(j.ownership,L"S-1-5-32-544");
        signing_require(j.registration_after==encoded_tree(desired_registration({j.installation_id,j.ownership.certificate_sha256,(storage().root/signing_target).native()})),"uninstall_plan_registration_invalid");
        const auto shared=maintenance_application_data();
        const auto winui_path=[&](const fs::path& path,bool directory){return uninstall_winui_path(path,shared,directory);};
        const auto packaged=maintenance.package();
        const auto count=r.number();signing_require(count<=uninstall_inventory_limit,"uninstall_plan_limit");std::set<fs::path> unique;
        for(uint64_t n=0;n<count;++n){const auto group=r.number();const fs::path path=uninstall_path_decode(r.get());const auto stamp=r.file();
            signing_require(group<=4 && group!=2 && stamp && unique.insert(path).second,"uninstall_plan_invalid");
            const auto rel=path.lexically_relative(group==1?library:storage().root);
            if(group!=4)signing_require(!rel.empty() && !rel.native().starts_with(L".."),"uninstall_plan_escape");
            if(group==0)signing_require(std::find(packaged.begin(),packaged.end(),rel.generic_wstring())!=packaged.end(),"uninstall_plan_payload_invalid");
            if(group==1)signing_require(uninstall_library_path(path,library,false),"uninstall_plan_library_invalid");
            if(group==4)signing_require(winui_path(path,false),"uninstall_plan_winui_invalid");
            if(group==3)signing_require(uninstall_within(path,storage().root/L"configuration") && path!=receipt,"uninstall_plan_metadata_invalid");
            plan.files.push_back({static_cast<unsigned>(group),path,*stamp});}
        const auto dirs=r.number();signing_require(dirs<=uninstall_inventory_limit,"uninstall_plan_limit");
        for(uint64_t n=0;n<dirs;++n){const fs::path path=uninstall_path_decode(r.get());auto id=r.get();signing_require(hexadecimal(id,48) &&
            (uninstall_library_path(path,library,true) || winui_path(path,true) || uninstall_payload_directory(path,storage().root) || path==storage().root/L"configuration/maintenance" ||
                uninstall_within(path,storage().root/L"configuration/maintenance")),"uninstall_plan_directory_invalid");plan.directories.emplace_back(path,id);}
        const auto tokens=r.number();signing_require(tokens<=4096,"uninstall_plan_limit");
        for(uint64_t n=0;n<tokens;++n){const auto name=r.get();const auto t=uninstall_decode_tree(r.get());signing_require(t.has_value(),"uninstall_plan_invalid");
            uninstall_engine_token_owned(name,*t,library,&voice_mismatch);signing_require(plan.tokens.emplace(name,*t).second,"uninstall_plan_duplicate_token");}
        signing_require(r.cursor==r.text.size(),"uninstall_plan_trailing_data");
        // Final cleanup may have removed the epoch immediately before process death.
        // Recreate only after the protected plan has been completely authenticated.
        if(!fs::exists(storage().root/L"configuration"))maintenance_directory(storage().root/L"configuration",false);
        storage().acquire_epoch(true,true);recovery_reports();file_security();lock();
    }
    void lock(){
        file_security();
        for(const auto& f:plan.files)if(f.group==4)for(auto p=f.path.parent_path().parent_path();p!=p.root_path();p=p.parent_path()){
            safe(p);auto h=std::make_unique<Handle>();h->value=CreateFileW(p.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,
                OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            signing_require(h->value!=INVALID_HANDLE_VALUE,"uninstall_data_ancestor_in_use");data_ancestors.push_back(std::move(h));}
        std::vector<fs::path> active;for(const auto& f:plan.files)if(f.path!=storage().root/L"configuration/maintenance.lock")active.push_back(f.path);
        maintenance_active_files(active);
        for(const auto& [path,id]:plan.directories)if(fs::exists(path)){
            safe(path);auto h=std::make_unique<Handle>();h->value=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES|DELETE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,
                OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            signing_require(h->value!=INVALID_HANDLE_VALUE && maintenance_directory_identity(h->value)==id,"uninstall_directory_changed_or_in_use");
            directories.push_back({path,id,std::move(h)});}
        for(const auto& f:plan.files)if(f.path!=storage().root/L"configuration/maintenance.lock")storage().acquire(f.path);
    }
    void revalidate(){
        uninstall_no_foreign_dependencies();registry_overrides();const auto actual=encoded_tree(registry_capture());
        signing_require(actual==plan.owner.registration_after || (retry && actual==L"absent"),"uninstall_com_changed");
        for(const auto& f:plan.files){
            // epoch is already held exclusively, cannot be re-opened
            if(f.path==storage().root/L"configuration/maintenance.lock")continue;
            const auto now=storage().stamp(f.path);
            signing_require(now==MaintenanceFile(f.stamp) || (retry && !now),"uninstall_remaining_file_changed");}
        // Planned resources remain pinned and identity checked. Other entries
        // are not deletion authority and are left in place, including late arrivals.
        for(const auto& d:directories)signing_require(maintenance_directory_identity(d.path)==d.identity,"uninstall_directory_changed");
        const auto tokens=uninstall_tokens(HKEY_LOCAL_MACHINE,KEY_WOW64_64KEY);
        uninstall_verify_voice_snapshot(tokens,plan.tokens,retry,"uninstall_voice_changed");
        maintenance.maintenance_remove_created_signing(signing_journal(),false,true);
    }
    void remove_registration(){
        revalidate();RegistryTransaction tx;RegistryHandle root;
        const auto current=encoded_tree(registry_capture(HKEY_LOCAL_MACHINE,KEY_WOW64_64KEY,tx.value));
        signing_require(current==plan.owner.registration_after || (retry && current==L"absent"),"uninstall_com_raced");
        const auto tokens=uninstall_tokens(HKEY_LOCAL_MACHINE,KEY_WOW64_64KEY,tx.value);
        uninstall_verify_voice_snapshot(tokens,plan.tokens,retry,"uninstall_voice_raced");
        if(uninstall_tokens_root(HKEY_LOCAL_MACHINE,KEY_WOW64_64KEY,root,tx.value,true))for(const auto& [name,t]:plan.tokens){
            const auto at=tokens.find(name);if(at==tokens.end()){signing_require(retry,"uninstall_voice_raced");continue;}
            signing_require(at->second==t,"uninstall_voice_raced");
            registry_ok(RegDeleteKeyTransactedW(root.value,(name+L"\\Attributes").c_str(),KEY_WOW64_64KEY,0,tx.value,nullptr));
            registry_ok(RegDeleteKeyTransactedW(root.value,name.c_str(),KEY_WOW64_64KEY,0,tx.value,nullptr));}
        if(current!=L"absent"){RegistryHandle parent;signing_require(registry_parent(HKEY_LOCAL_MACHINE,KEY_WOW64_64KEY,parent,tx.value,KEY_WRITE),"registry_parent_missing");
            const std::wstring clsid=outloud::sapi::clsid_text;
            registry_ok(RegDeleteKeyTransactedW(parent.value,(clsid+L"\\InprocServer32").c_str(),KEY_WOW64_64KEY,0,tx.value,nullptr));
            registry_ok(RegDeleteKeyTransactedW(parent.value,clsid.c_str(),KEY_WOW64_64KEY,0,tx.value,nullptr));}
        signing_require(!registry_capture(HKEY_LOCAL_MACHINE,KEY_WOW64_64KEY,tx.value),"uninstall_registry_verification_failed");
        for(const auto& [name,t]:uninstall_tokens(HKEY_LOCAL_MACHINE,KEY_WOW64_64KEY,tx.value))signing_require(!managed_voice_registration_candidate(name,t),"uninstall_voice_dependency_remains");
        uninstall_no_foreign_dependencies();signing_require(CommitTransaction(tx.value),"uninstall_registry_commit_failed");tx.committed=true;
    }
    void verify_no_dependencies(){signing_require(!registry_capture(),"uninstall_com_remains");uninstall_no_foreign_dependencies();
        for(const auto& [name,t]:uninstall_tokens(HKEY_LOCAL_MACHINE,KEY_WOW64_64KEY))signing_require(!managed_voice_registration_candidate(name,t),"uninstall_voice_dependency_remains");}
    void remove_group(unsigned group,bool final=false){for(const auto& f:plan.files)if(f.group==group && fs::exists(f.path) &&
        (final || f.path!=storage().root/L"bin/OutLoudInstallSigning.exe") && f.path!=storage().root/L"configuration/maintenance.lock"){
        signing_require(storage().stamp(f.path)==MaintenanceFile(f.stamp),"uninstall_file_changed_before_delete");uninstall_delete_held(storage().held(f.stamp));
        // Closing our pin completes delete disposition; never schedule on reboot.
        for(auto& p:storage().pins)if(p.stamp==f.stamp){CloseHandle(p.handle->value);p.handle->value=INVALID_HANDLE_VALUE;}
        signing_require(!fs::exists(f.path),"uninstall_file_delete_pending");}}
    void remove_data(){remove_group(1);remove_group(4);}
    void remove_payload(){verify_no_dependencies();remove_group(0);}
    void remove_signing(){maintenance.owner.reset();maintenance.maintenance_remove_created_signing(signing_journal(),true,true);}
    void verify_native_removed(){verify_no_dependencies();for(const auto& f:plan.files)if((f.group<3 || f.group==4) && f.path!=storage().root/L"bin/OutLoudInstallSigning.exe")signing_require(!fs::exists(f.path),"uninstall_components_remain");
        WindowsSigning signer(storage().root,{},L"S-1-5-32-544",true);signer.record=plan.owner.ownership;
        for(const auto* name:{L"My",L"Root"}){Store store;signer.open_store(store,name,true,true);Certificate cert;cert.value=signer.find_owned(store.value);
            signing_require(!cert.value,"uninstall_certificate_remains");}
        Key provider,key;signing_require(NCryptOpenStorageProvider(&provider.value,MS_KEY_STORAGE_PROVIDER,0)==ERROR_SUCCESS,"uninstall_key_verification_failed");
        signing_require(NCryptOpenKey(provider.value,&key.value,plan.owner.ownership.key_name.c_str(),0,NCRYPT_MACHINE_KEY_FLAG|NCRYPT_SILENT_FLAG)==NTE_BAD_KEYSET,"uninstall_key_remains_or_unverified");}
    void await_host(){} // immutable plan remains until checked final cleanup
    void release_data_ancestor(const std::wstring& identity){
        // A separate DELETE-capable directory pin still protects this identity.
        // Close every extra attributes handle before completing its deletion.
        for(auto& ancestor:data_ancestors)if(ancestor && maintenance_directory_identity(ancestor->value)==identity)ancestor.reset();
    }
    void finish(){
        // Inno's host deletion can fail even after its registry record is gone.
        // Keep the helper/plan until those host files are also actually absent.
        auto host_data=plan.host_executable;host_data.replace_extension(L".dat");
        auto host_message=plan.host_executable;host_message.replace_extension(L".msg");
        signing_require(!fs::exists(plan.host_executable) && !fs::exists(host_data) && !fs::exists(host_message),"uninstall_inno_host_files_remain");
        verify_native_removed();maintenance.maintenance_remove_created_signing(signing_journal(),false,true);remove_group(0,true);remove_group(3,true);
        std::sort(directories.begin(),directories.end(),[](const auto& a,const auto& b){return a.path.native().size()>b.path.native().size();});
        for(auto& d:directories){
            blocked_resource=d.path;
            if(!fs::is_empty(d.path))continue;
            release_data_ancestor(d.identity);uninstall_delete_held(d.pin->value);d.pin.reset();
        }
        if(!plan.owner.library_parent_identity.empty() && fs::exists(library.parent_path())){
            blocked_resource=library.parent_path();
            safe(library.parent_path());require_admin_protected(library.parent_path());
            signing_require(maintenance_directory_identity(library.parent_path())==plan.owner.library_parent_identity,"uninstall_library_parent_changed");
            if(fs::is_empty(library.parent_path())){
                Handle parent;parent.value=CreateFileW(library.parent_path().c_str(),FILE_READ_ATTRIBUTES|DELETE,FILE_SHARE_READ|FILE_SHARE_WRITE,
                    nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
                signing_require(parent.value!=INVALID_HANDLE_VALUE && maintenance_directory_identity(parent.value)==plan.owner.library_parent_identity,
                    "uninstall_library_parent_changed_or_in_use");
                release_data_ancestor(plan.owner.library_parent_identity);uninstall_delete_held(parent.value);
            }
        }
        blocked_resource.clear();
        storage().release();data_ancestors.clear();
        if(storage().epoch.value!=INVALID_HANDLE_VALUE){uninstall_delete_held(storage().epoch.value);CloseHandle(storage().epoch.value);storage().epoch.value=INVALID_HANDLE_VALUE;}
        const auto configuration=storage().root/L"configuration";
        if(fs::exists(configuration) && fs::is_empty(configuration))signing_require(RemoveDirectoryW(configuration.c_str()),"uninstall_configuration_directory_remains");
        // The plan is last. A failure before this point retains identities for recovery.
        Handle record;storage().security(receipt);record.value=CreateFileW(receipt.c_str(),GENERIC_READ|DELETE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        signing_require(record.value!=INVALID_HANDLE_VALUE,"uninstall_plan_locked");uninstall_delete_held(record.value);
        CloseHandle(record.value);record.value=INVALID_HANDLE_VALUE;
    }
    void report_failure(const fs::path& destination,const char* reason,const wchar_t* phase){
        std::wstring text=L"[results]\nschema=1\nuninstall=incomplete\ninventory=unverified\ninstallation_id="+
            plan.owner.installation_id+L"\nroot="+maintenance_root_encoding(storage().root.native())+L"\n";
        if(!blocked_resource.empty())text+=L"blocked_resource="+maintenance_root_encoding(blocked_resource.native())+L"\n";
        try{text=remaining();}catch(const std::exception& error){std::cerr<<"Uninstall inventory diagnostics unavailable: "<<error.what()<<"\n";}
        catch(...){std::cerr<<"Uninstall inventory diagnostics unavailable.\n";}
        text+=L"reason=";for(const char c:std::string(reason))text+=static_cast<wchar_t>(c);
        text+=L"\nphase="+std::wstring(phase)+L"\n";
        try{write_new(destination,text);}catch(const std::exception& error){std::cerr<<"Uninstall temporary report unavailable: "<<error.what()<<"\n";}
        catch(...){std::cerr<<"Uninstall temporary report unavailable.\n";}
        try{durable_failure(text);}catch(const std::exception& error){std::cerr<<"Uninstall recovery report unavailable: "<<error.what()<<"\n";}
        catch(...){std::cerr<<"Uninstall recovery report unavailable.\n";}
    }
    std::wstring remaining(){std::wstring report=L"[results]\nschema=1\nuninstall=incomplete\n";
        report+=uninstall_voice_mismatch_report(voice_mismatch);
        if(!maintenance.certificate_source.empty())report+=L"certificate_source="+maintenance_root_encoding(maintenance.certificate_source)+
            L"\ncertificate_source_error="+std::to_wstring(maintenance.certificate_source_error)+L"\n";
        report+=L"installation_id="+plan.owner.installation_id+L"\nroot="+maintenance_root_encoding(storage().root.native())+L"\n";
        report+=L"inventory="+std::wstring(fs::exists(receipt)?L"protected_plan":L"preflight_incomplete")+L"\n";
        if(!blocked_resource.empty())report+=L"blocked_resource="+maintenance_root_encoding(blocked_resource.native())+L"\n";
        unsigned index{};for(const auto& f:plan.files)if(fs::exists(f.path))report+=L"remaining"+std::to_wstring(index++)+L"="+maintenance_root_encoding(f.path.native())+L"\n";
        report+=L"remaining_files="+std::to_wstring(index)+L"\npaths_encoding=utf16le-hex\nrecovery="+maintenance_root_encoding(receipt.native())+L"\n";
        try{report+=L"registration="+std::wstring(registry_capture()?L"present":L"absent")+L"\n";}catch(...){report+=L"registration=unknown\n";}
        try{unsigned count{};for(const auto& [name,t]:uninstall_tokens(HKEY_LOCAL_MACHINE,KEY_WOW64_64KEY)){static_cast<void>(name);if(uninstall_dependency(t))++count;}
            report+=L"remaining_voice_dependencies="+std::to_wstring(count)+L"\n";}catch(...){report+=L"remaining_voice_dependencies=unknown\n";}
        for(const auto* store_name:{L"My",L"Root"}){
            const auto field=std::wstring(store_name)==L"My"?L"certificate_my":L"certificate_root";
            try{if(plan.owner.ownership.id.empty())throw SigningError("unverified");WindowsSigning signer(storage().root,{},L"S-1-5-32-544",true);signer.record=plan.owner.ownership;
                Store store;signer.open_store(store,store_name,true,true);Certificate cert;cert.value=signer.find_owned(store.value);
                report+=std::wstring(field)+L"="+(cert.value?L"present":L"absent")+L"\n";}catch(...){report+=std::wstring(field)+L"=unknown\n";}}
        try{if(plan.owner.ownership.id.empty())throw SigningError("unverified");Key provider,key;
            signing_require(NCryptOpenStorageProvider(&provider.value,MS_KEY_STORAGE_PROVIDER,0)==ERROR_SUCCESS,"unverified");
            const auto status=NCryptOpenKey(provider.value,&key.value,plan.owner.ownership.key_name.c_str(),0,NCRYPT_MACHINE_KEY_FLAG|NCRYPT_SILENT_FLAG);
            report+=L"private_key="+std::wstring(status==ERROR_SUCCESS?L"present":status==NTE_BAD_KEYSET?L"absent":L"unknown")+L"\n";
        }catch(...){report+=L"private_key=unknown\n";}return report;}
    void durable_failure(const std::wstring& text){
        if(!fs::exists(receipt))return;const auto final=storage().root/L"configuration/uninstall.failure.ini";
        if(!fs::exists(final.parent_path()))maintenance_directory(final.parent_path(),false);
        if(fs::exists(final)){const auto old=storage().read_held(final);require_admin_protected(final);
            signing_require(old.find(L"\ninstallation_id="+plan.owner.installation_id+L"\n")!=old.npos &&
                old.find(L"\nroot="+plan.owner.root_encoding+L"\n")!=old.npos,"uninstall_foreign_failure_report_preserved");}
        const auto temporary=storage().root/L"configuration/uninstall.failure.tmp";
        if(fs::exists(temporary))return;write_new(temporary,text,true);
        signing_require(MoveFileExW(temporary.c_str(),final.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH),"uninstall_failure_report_save_failed");
    }
};
