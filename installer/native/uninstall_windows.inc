// Plans are immutable, protected, length-framed receipts. Absence is accepted only
// on a retry of this plan; any changed surviving identity refuses further removal.
struct UninstallFile {unsigned group{};fs::path path;MaintenanceStamp stamp;};
struct UninstallDirectory {fs::path path;std::wstring identity;std::unique_ptr<Handle> pin;};
struct UninstallPlan {
    MaintenanceJournal owner;fs::path host_executable;std::vector<UninstallFile> files;
    std::vector<std::pair<fs::path,std::wstring>> directories;
    std::map<std::wstring,RegistrationTree> tokens;
};
std::vector<fs::path> uninstall_log_roots();
bool uninstall_within(const fs::path& path,const fs::path& root){const auto rel=path.lexically_relative(root);
    return !rel.empty() && !rel.native().starts_with(L"..") && !rel.is_absolute();}
bool uninstall_library_path(const fs::path& path,const fs::path& library,bool directory){
    if(path==library)return directory;if(!uninstall_within(path,library))return false;
    std::vector<std::wstring> parts;for(const auto& p:path.lexically_relative(library))parts.push_back(p.native());
    const auto n=parts.size();const auto& top=parts.front();
    if(directory){if(n==1)return top==L"voices" || top==L".staging" || top==L".trash";
        if(n==2)return (top==L"voices" || top==L".staging" || top==L".trash") && hexadecimal(parts[1],32);
        return n==3 && top==L".trash" && hexadecimal(parts[1],32) && parts[2]==L"retained";}
    const auto voice=[](const std::wstring& name){return name==L"voice.olv" || name==L"metadata.json";};
    if(n==1)return top==L".outloud-installation.dat" || top==L".library.lock";
    if(n==3 && hexadecimal(parts[1],32)){
        if(top==L"voices")return voice(parts[2]);
        if(top==L".staging")return std::set<std::wstring>{L"voice.olv",L"metadata.json",L"old.olv",L"old.json",L"retired.olv",L"retired.json",L"replace.json",L"commit"}.contains(parts[2]);
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
void uninstall_token_owned(const std::wstring& name,const RegistrationTree& t,const fs::path& app,const fs::path& library){
    signing_require(name.size()==47 && name.starts_with(L"OutLoud_Pocket_") && hexadecimal(name.substr(15),32),"uninstall_unowned_voice_dependency");
    const std::set<std::wstring> values{L"",L"409",L"CLSID",L"OutLoudManagedOwner",L"LibraryId",L"AssetRoot",L"ModelPath",L"TokenizerPath",L"VoicePath",L"Seed",L"MaximumSegmentTokens",L"KernelPath"};
    const std::set<std::wstring> attributes{L"Name",L"Language",L"Vendor"};
    signing_require(t.values.size()==values.size() && t.children.size()==1 && t.children.contains(L"Attributes"),"uninstall_voice_schema_changed");
    for(const auto& [key,value]:t.values){signing_require(values.contains(key),"uninstall_voice_schema_changed");static_cast<void>(registration_text(value));}
    const auto& a=t.children.at(L"Attributes");signing_require(a.children.empty() && a.values.size()==attributes.size(),"uninstall_voice_schema_changed");
    for(const auto& [key,value]:a.values){signing_require(attributes.contains(key),"uninstall_voice_schema_changed");static_cast<void>(registration_text(value));}
    signing_require(registration_field(t,L"OutLoudManagedOwner")==L"OutLoud.ManagedVoice.1" && registration_field(t,L"LibraryId")==name.substr(15) &&
        registration_field(a,L"Vendor")==L"Out Loud" && uninstall_dependency(t),"uninstall_unowned_voice_dependency");
    for(const auto& [field,path]:std::map<std::wstring,fs::path>{{L"AssetRoot",app},{L"ModelPath",app/L"runtime/pocket-english.olp"},
        {L"TokenizerPath",app/L"runtime/pocket-english-tokenizer.bin"},{L"VoicePath",library/L"voices"/name.substr(15)/L"voice.olv"}})
        signing_require(same_installation_path(registration_field(t,field.c_str()),path),"uninstall_voice_other_installation");
}
void uninstall_no_foreign_dependencies(){
    // Our manager only writes HKLM64 Speech. Other views/categories/users are never adopted.
    for(const auto view:{KEY_WOW64_64KEY,KEY_WOW64_32KEY})for(const auto hive:{HKEY_LOCAL_MACHINE,HKEY_CURRENT_USER})
        for(const auto* speech:{L"Speech",L"Speech_OneCore"}){
            if(hive==HKEY_LOCAL_MACHINE && view==KEY_WOW64_64KEY && std::wstring(speech)==L"Speech")continue;
            for(const auto& [name,t]:uninstall_tokens(hive,view,nullptr,speech)){static_cast<void>(name);signing_require(!uninstall_dependency(t),"uninstall_foreign_voice_dependency");}}
    for(DWORD i=0;;++i){wchar_t sid[257]{};DWORD length=256;auto status=RegEnumKeyExW(HKEY_USERS,i,sid,&length,nullptr,nullptr,nullptr,nullptr);
        if(status==ERROR_NO_MORE_ITEMS)break;registry_ok(status);signing_require(i<4096,"uninstall_user_inventory_limit");RegistryHandle user;
        signing_require(registry_user_hive(std::wstring(sid,length),0,user),"uninstall_user_inventory_raced");
        if(std::wstring(sid,length).ends_with(L"_Classes"))for(const auto view:{KEY_WOW64_64KEY,KEY_WOW64_32KEY}){RegistryHandle clsids,engine;
            if(registry_open(user.value,L"CLSID",KEY_READ|view,clsids))signing_require(!registry_open(clsids.value,outloud::sapi::clsid_text,KEY_READ|view,engine),"uninstall_foreign_com_override");}
        for(const auto view:{KEY_WOW64_64KEY,KEY_WOW64_32KEY})for(const auto* speech:{L"Speech",L"Speech_OneCore"})
            for(const auto& [name,t]:uninstall_tokens(user.value,view,nullptr,speech)){static_cast<void>(name);signing_require(!uninstall_dependency(t),"uninstall_foreign_voice_dependency");}}
}
void uninstall_delete_held(HANDLE handle){FILE_DISPOSITION_INFO disposition{TRUE};
    signing_require(SetFileInformationByHandle(handle,FileDispositionInfo,&disposition,sizeof(disposition)),"uninstall_checked_delete_failed");}
struct WindowsUninstall {
    WindowsMaintenance maintenance;UninstallPlan plan;fs::path receipt,library,blocked_resource;bool retry{};
    std::vector<UninstallDirectory> directories;
    std::vector<std::unique_ptr<Handle>> log_ancestors;
    explicit WindowsUninstall(const fs::path& app):maintenance(app),receipt(maintenance.files.root/L".outloud-uninstall.dat"),library(maintenance_library()){}
    MaintenanceFiles& storage(){return maintenance.files;}
    void add(unsigned group,const fs::path& path,const MaintenanceFile& expected={}){
        blocked_resource=path;
        safe(path);const auto actual=storage().stamp(path);signing_require(actual && (!expected || actual==expected),"uninstall_file_ownership_changed");
        signing_require(plan.files.size()<8192,"uninstall_file_inventory_limit");plan.files.push_back({group,path,*actual});
    }
    void directory(const fs::path& path){safe(path);storage().security(path);plan.directories.emplace_back(path,maintenance_directory_identity(path));}
    void library_inventory(const fs::path& path,unsigned depth=0){
        signing_require(depth<5,"uninstall_library_depth_invalid");directory(path);
        for(const auto& entry:fs::directory_iterator(path)){blocked_resource=entry.path();safe(entry.path());
            if(entry.is_directory()){
                signing_require(uninstall_library_path(entry.path(),library,true),"uninstall_unknown_library_directory_preserved");library_inventory(entry.path(),depth+1);
            }else{
                signing_require(entry.is_regular_file() && uninstall_library_path(entry.path(),library,false),"uninstall_unknown_library_file_preserved");add(1,entry.path());
            }}
    }
    void archives(){
        const auto base=storage().root/L"configuration/maintenance";if(!fs::exists(base))return;directory(base);
        for(const auto& entry:fs::directory_iterator(base)){
            safe(entry.path());signing_require(entry.is_directory() && hexadecimal(entry.path().filename().native(),32),"uninstall_unknown_archive_preserved");
            const auto vault=entry.path();MaintenanceFiles archive(storage().root);archive.vault=vault;archive.security=storage().security;
            const auto pointer=archive.read(vault/L"terminal.pointer");signing_require(pointer==vault.filename().native()+L"\n","uninstall_archive_not_terminal");
            std::vector<MaintenanceJournal> frames;for(unsigned n=0;n<256 && fs::exists(vault/(L"state-"+std::to_wstring(n)+L".dat"));++n){
                auto j=maintenance_decode(archive.read(vault/(L"state-"+std::to_wstring(n)+L".dat")),maintenance.package());
                signing_require(j.installation_id==plan.owner.installation_id && j.root_encoding==plan.owner.root_encoding && j.transaction_id==vault.filename().native(),"uninstall_archive_identity_invalid");frames.push_back(j);}
            signing_require(!frames.empty() && (frames.back().phase==MaintenancePhase::committed || frames.back().phase==MaintenancePhase::rolled_back),"uninstall_archive_not_terminal");
            for(const auto& j:frames)signing_require(j.ownership.certificate_sha256.empty() || j.ownership.certificate_sha256==plan.owner.ownership.certificate_sha256 ||
                (frames.back().phase==MaintenancePhase::rolled_back && j.operation==MaintenanceOperation::fresh && j.old_version.empty()),"uninstall_archive_certificate_invalid");
            std::set<fs::path> archive_directories{vault/L"bootstrap"};
            for(const auto& j:frames)for(const auto& c:j.changes)for(const auto* part:{L"backup",L"original",L"new",L"failed"})
                for(auto path=(vault/part/c.path).parent_path();path!=vault;path=path.parent_path())archive_directories.insert(path);
            directory(vault);
            for(const auto& row:fs::recursive_directory_iterator(vault)){blocked_resource=row.path();safe(row.path());if(row.is_directory()){
                signing_require(archive_directories.contains(row.path()),"uninstall_unknown_archive_directory_preserved");directory(row.path());continue;}
                const auto rel=row.path().lexically_relative(vault).generic_wstring();bool allowed=rel==L"terminal.pointer";
                for(size_t n=0;n<frames.size();++n)allowed=allowed || rel==L"state-"+std::to_wstring(n)+L".dat";
                MaintenanceFile expected;
                for(const auto& j:frames)for(const auto& c:j.changes){
                    if(rel==L"backup/"+c.path && c.backup){expected=c.backup;allowed=true;}
                    if(rel==L"original/"+c.path && c.before){expected=c.before;allowed=true;}
                    if((rel==L"new/"+c.path || rel==L"failed/"+c.path) && c.after){expected=c.after;allowed=true;}}
                for(const auto& j:frames){MaintenanceFile bootstrap;
                    if(rel==L"bootstrap/OutLoudPrerequisites.exe")bootstrap=j.bootstrap_detector;
                    if(rel==L"bootstrap/prerequisites.ini")bootstrap=j.bootstrap_requirements;
                    if(rel==L"bootstrap/results.ini")bootstrap=j.bootstrap_report;
                    if(bootstrap){allowed=true;expected=bootstrap;}}
                signing_require(allowed && row.is_regular_file(),"uninstall_unknown_archive_file_preserved");add(3,row.path(),expected);
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
        for(const auto& [path,stamp]:inventory.files)add(0,storage().path(path),stamp);
        for(const auto* path:maintenance_metadata)add(3,storage().path(path));
        add(3,storage().root/L"configuration/maintenance.lock");storage().acquire_epoch(false,true);
        blocked_resource=library;
        maintenance_validate_library(storage(),j);library_inventory(library);archives();
        for(const auto& [name,t]:uninstall_tokens(HKEY_LOCAL_MACHINE,KEY_WOW64_64KEY))if(uninstall_dependency(t)){
            uninstall_token_owned(name,t,storage().root,library);plan.tokens.emplace(name,t);}
        // All existing profile locations are inspected, never guessed from C:\Users.
        logs();recovery_reports();installation_inventory();maintenance.maintenance_remove_created_signing(signing_journal(),false,true);
        lock();revalidate();
    }
    MaintenanceJournal signing_journal()const{auto j=plan.owner;j.operation=MaintenanceOperation::fresh;return j;}
    void logs();
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
        signing_require(uninstall.size()>2 && uninstall.front()==L'\"' && uninstall.back()==L'\"',"uninstall_host_command_invalid");
        const fs::path host=installation_path(uninstall.substr(1,uninstall.size()-2));
        signing_require(same_installation_path(host.parent_path(),storage().root) && host.extension()==L".exe" && host.filename().native().starts_with(L"unins"),"uninstall_host_command_invalid");
        signing_require(plan.host_executable.empty() || same_installation_path(plan.host_executable,host),"uninstall_host_command_changed");plan.host_executable=host;}
        allowed.insert(plan.host_executable);auto dat=plan.host_executable;dat.replace_extension(L".dat");allowed.insert(dat);
        auto messages=plan.host_executable;messages.replace_extension(L".msg");allowed.insert(messages); // Inno signed-uninstaller messages; host owns removal
        allowed.insert(receipt);allowed.insert(storage().root/L"configuration/uninstall.failure.ini");
        allowed.insert(storage().root/L"configuration/uninstall.failure.tmp");
        for(const auto& [path,id]:plan.directories){static_cast<void>(id);const auto rel=path.lexically_relative(storage().root);
            if(!rel.empty() && !rel.native().starts_with(L".."))parents.insert(path);}
        for(const auto& e:fs::recursive_directory_iterator(storage().root)){blocked_resource=e.path();safe(e.path());
            signing_require(e.is_directory()?parents.contains(e.path()):(e.is_regular_file() && allowed.contains(e.path())),"uninstall_unknown_installation_resource_preserved");}
        blocked_resource.clear();
    }
    void file_security(){storage().security=[this](const fs::path& p){safe(p);
        if(std::none_of(plan.files.begin(),plan.files.end(),[&](const auto& f){return f.group==2 && f.path==p;}))require_admin_protected(p);};}
    std::wstring encode()const{
        MaintenanceWriter w;w.put(L"OutLoud.Uninstall.1");const auto& j=plan.owner;
        for(const auto* s:{&j.installation_id,&j.root_encoding,&j.library_identity,&j.library_parent_identity,&j.ownership.id,&j.ownership.owner_sid,&j.ownership.key_name,&j.ownership.thumbprint,
            &j.ownership.certificate_sha256,&j.ownership.signed_sha256,&j.registration_after})w.put(*s);
        w.put(maintenance_root_encoding(plan.host_executable.native()));
        w.number(plan.files.size());for(const auto& f:plan.files){w.number(f.group);w.put(maintenance_root_encoding(f.path.native()));w.file(f.stamp);}
        w.number(plan.directories.size());for(const auto& [path,id]:plan.directories){w.put(maintenance_root_encoding(path.native()));w.put(id);}
        w.number(plan.tokens.size());for(const auto& [name,t]:plan.tokens){w.put(name);w.put(encoded_tree(t));}return w.text;
    }
    void persist(){if(!retry){const auto text=encode();signing_require(text.size()<=1024*1024,"uninstall_plan_limit");write_new(receipt,text,true);}}
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
        const auto log_roots=uninstall_log_roots();const auto packaged=maintenance.package();
        const auto log_path=[&](const fs::path& path){return std::any_of(log_roots.begin(),log_roots.end(),[&](const auto& root){return same_installation_path(path,root);});};
        const auto count=r.number();signing_require(count<=8192,"uninstall_plan_limit");std::set<fs::path> unique;
        for(uint64_t n=0;n<count;++n){const auto group=r.number();const fs::path path=uninstall_path_decode(r.get());const auto stamp=r.file();
            signing_require(group<=3 && stamp && unique.insert(path).second,"uninstall_plan_invalid");
            const auto rel=path.lexically_relative(group==1?library:storage().root);
            if(group!=2)signing_require(!rel.empty() && !rel.native().starts_with(L".."),"uninstall_plan_escape");
            if(group==0)signing_require(std::find(packaged.begin(),packaged.end(),rel.generic_wstring())!=packaged.end(),"uninstall_plan_payload_invalid");
            if(group==1)signing_require(uninstall_library_path(path,library,false),"uninstall_plan_library_invalid");
            if(group==2)signing_require(log_path(path.parent_path()) && std::set<std::wstring>{L"engine.log",L"engine.1.log",L"engine.2.log",L".outloud-installation.dat"}.contains(path.filename().native()),"uninstall_plan_logs_invalid");
            if(group==3)signing_require(uninstall_within(path,storage().root/L"configuration") && path!=receipt,"uninstall_plan_metadata_invalid");
            plan.files.push_back({static_cast<unsigned>(group),path,*stamp});}
        const auto dirs=r.number();signing_require(dirs<=8192,"uninstall_plan_limit");
        for(uint64_t n=0;n<dirs;++n){const fs::path path=uninstall_path_decode(r.get());auto id=r.get();signing_require(hexadecimal(id,48) &&
            (uninstall_library_path(path,library,true) || log_path(path) || path==storage().root/L"configuration/maintenance" ||
                uninstall_within(path,storage().root/L"configuration/maintenance")),"uninstall_plan_directory_invalid");plan.directories.emplace_back(path,id);}
        const auto tokens=r.number();signing_require(tokens<=4096,"uninstall_plan_limit");
        for(uint64_t n=0;n<tokens;++n){const auto name=r.get();const auto t=uninstall_decode_tree(r.get());signing_require(t.has_value(),"uninstall_plan_invalid");
            uninstall_token_owned(name,*t,storage().root,library);signing_require(plan.tokens.emplace(name,*t).second,"uninstall_plan_duplicate_token");}
        signing_require(r.cursor==r.text.size(),"uninstall_plan_trailing_data");
        // Final cleanup may have removed the epoch immediately before process death.
        // Recreate only after the protected plan has been completely authenticated.
        if(!fs::exists(storage().root/L"configuration"))maintenance_directory(storage().root/L"configuration",false);
        storage().acquire_epoch(true,true);recovery_reports();file_security();lock();
    }
    void lock(){
        file_security();
        for(const auto& f:plan.files)if(f.group==2)for(auto p=f.path.parent_path().parent_path();p!=p.root_path();p=p.parent_path()){
            safe(p);auto h=std::make_unique<Handle>();h->value=CreateFileW(p.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,
                OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            signing_require(h->value!=INVALID_HANDLE_VALUE,"uninstall_log_ancestor_in_use");log_ancestors.push_back(std::move(h));}
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
        for(const auto& d:directories){signing_require(maintenance_directory_identity(d.path)==d.identity,"uninstall_directory_changed");
            for(const auto& e:fs::directory_iterator(d.path)){
                signing_require(std::any_of(plan.files.begin(),plan.files.end(),[&](const auto& f){return f.path==e.path();}) ||
                    std::any_of(plan.directories.begin(),plan.directories.end(),[&](const auto& p){return p.first==e.path();}),"uninstall_new_resource_preserved");}}
        const auto tokens=uninstall_tokens(HKEY_LOCAL_MACHINE,KEY_WOW64_64KEY);
        for(const auto& [name,t]:tokens)if(uninstall_dependency(t))signing_require(plan.tokens.contains(name) && plan.tokens.at(name)==t,"uninstall_voice_changed");
        for(const auto& [name,t]:plan.tokens){const auto at=tokens.find(name);signing_require(at==tokens.end()?retry:at->second==t,"uninstall_voice_changed");}
        maintenance.maintenance_remove_created_signing(signing_journal(),false,true);
    }
    void remove_registration(){
        revalidate();RegistryTransaction tx;RegistryHandle root;
        const auto current=encoded_tree(registry_capture(HKEY_LOCAL_MACHINE,KEY_WOW64_64KEY,tx.value));
        signing_require(current==plan.owner.registration_after || (retry && current==L"absent"),"uninstall_com_raced");
        const auto tokens=uninstall_tokens(HKEY_LOCAL_MACHINE,KEY_WOW64_64KEY,tx.value);
        for(const auto& [name,t]:tokens)if(uninstall_dependency(t))signing_require(plan.tokens.contains(name) && plan.tokens.at(name)==t,"uninstall_voice_raced");
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
        for(const auto& [name,t]:uninstall_tokens(HKEY_LOCAL_MACHINE,KEY_WOW64_64KEY,tx.value)){static_cast<void>(name);signing_require(!uninstall_dependency(t),"uninstall_voice_dependency_remains");}
        uninstall_no_foreign_dependencies();signing_require(CommitTransaction(tx.value),"uninstall_registry_commit_failed");tx.committed=true;
    }
    void verify_no_dependencies(){signing_require(!registry_capture(),"uninstall_com_remains");uninstall_no_foreign_dependencies();
        for(const auto& [name,t]:uninstall_tokens(HKEY_LOCAL_MACHINE,KEY_WOW64_64KEY)){static_cast<void>(name);signing_require(!uninstall_dependency(t),"uninstall_voice_dependency_remains");}}
    void remove_group(unsigned group,bool final=false){for(const auto& f:plan.files)if(f.group==group && fs::exists(f.path) &&
        (final || f.path!=storage().root/L"bin/OutLoudInstallSigning.exe") && f.path!=storage().root/L"configuration/maintenance.lock"){
        signing_require(storage().stamp(f.path)==MaintenanceFile(f.stamp),"uninstall_file_changed_before_delete");uninstall_delete_held(storage().held(f.stamp));
        // Closing our pin completes delete disposition; never schedule on reboot.
        for(auto& p:storage().pins)if(p.stamp==f.stamp){CloseHandle(p.handle->value);p.handle->value=INVALID_HANDLE_VALUE;}
        signing_require(!fs::exists(f.path),"uninstall_file_delete_pending");}}
    void remove_data(){remove_group(1);remove_group(2);}
    void remove_payload(){verify_no_dependencies();remove_group(0);}
    void remove_signing(){maintenance.owner.reset();maintenance.maintenance_remove_created_signing(signing_journal(),true,true);}
    void verify_native_removed(){verify_no_dependencies();for(const auto& f:plan.files)if(f.group<3 && f.path!=storage().root/L"bin/OutLoudInstallSigning.exe")signing_require(!fs::exists(f.path),"uninstall_components_remain");
        WindowsSigning signer(storage().root,{},L"S-1-5-32-544",true);signer.record=plan.owner.ownership;
        for(const auto* name:{L"My",L"Root"}){Store store;signer.open_store(store,name,true,true);Certificate cert;cert.value=signer.find_owned(store.value);
            signing_require(!cert.value,"uninstall_certificate_remains");}
        Key provider,key;signing_require(NCryptOpenStorageProvider(&provider.value,MS_KEY_STORAGE_PROVIDER,0)==ERROR_SUCCESS,"uninstall_key_verification_failed");
        signing_require(NCryptOpenKey(provider.value,&key.value,plan.owner.ownership.key_name.c_str(),0,NCRYPT_MACHINE_KEY_FLAG|NCRYPT_SILENT_FLAG)==NTE_BAD_KEYSET,"uninstall_key_remains_or_unverified");}
    void await_host(){} // immutable plan remains until checked final cleanup
    void finish(){
        // Inno's host deletion can fail even after its registry record is gone.
        // Keep the helper/plan until those host files are also actually absent.
        auto host_data=plan.host_executable;host_data.replace_extension(L".dat");
        auto host_message=plan.host_executable;host_message.replace_extension(L".msg");
        signing_require(!fs::exists(plan.host_executable) && !fs::exists(host_data) && !fs::exists(host_message),"uninstall_inno_host_files_remain");
        verify_native_removed();maintenance.maintenance_remove_created_signing(signing_journal(),false,true);remove_group(0,true);remove_group(3,true);
        std::sort(directories.begin(),directories.end(),[](const auto& a,const auto& b){return a.path.native().size()>b.path.native().size();});
        for(auto& d:directories){signing_require(fs::is_empty(d.path),"uninstall_directory_not_empty_preserved");uninstall_delete_held(d.pin->value);d.pin.reset();}
        if(!plan.owner.library_parent_identity.empty() && fs::exists(library.parent_path())){
            safe(library.parent_path());require_admin_protected(library.parent_path());
            signing_require(maintenance_directory_identity(library.parent_path())==plan.owner.library_parent_identity,"uninstall_library_parent_changed");
            if(fs::is_empty(library.parent_path()))signing_require(RemoveDirectoryW(library.parent_path().c_str()),"uninstall_library_parent_remains");}
        storage().release();log_ancestors.clear();
        for(const auto* path:{L"bin",L"runtime",L"licenses"}){
            const auto directory_path=storage().root/path;safe(directory_path);
            if(fs::exists(directory_path))signing_require(fs::is_empty(directory_path) && RemoveDirectoryW(directory_path.c_str()),"uninstall_native_directory_remains");}
        if(storage().epoch.value!=INVALID_HANDLE_VALUE){uninstall_delete_held(storage().epoch.value);CloseHandle(storage().epoch.value);storage().epoch.value=INVALID_HANDLE_VALUE;}
        const auto configuration=storage().root/L"configuration";
        if(fs::exists(configuration))signing_require(fs::is_empty(configuration) && RemoveDirectoryW(configuration.c_str()),"uninstall_configuration_directory_remains");
        // The plan is last. A failure before this point retains identities for recovery.
        Handle record;storage().security(receipt);record.value=CreateFileW(receipt.c_str(),GENERIC_READ|DELETE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        signing_require(record.value!=INVALID_HANDLE_VALUE,"uninstall_plan_locked");uninstall_delete_held(record.value);
        CloseHandle(record.value);record.value=INVALID_HANDLE_VALUE;
    }
    std::wstring remaining(){std::wstring report=L"[results]\nschema=1\nuninstall=incomplete\n";
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
