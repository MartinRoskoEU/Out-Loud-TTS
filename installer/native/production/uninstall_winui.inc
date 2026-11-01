// Current machine-wide storage only. User profiles are neither discovered nor adopted.
bool uninstall_download_path(const fs::path& path,const fs::path& root,bool directory){
    if(!uninstall_within(path,root))return false;
    std::vector<std::wstring> p;for(const auto& part:path.lexically_relative(root))p.push_back(part.native());
    const auto n=p.size();if(n<3 || (p[0]!=L"models" && p[0]!=L"runtimes") || p[1]!=L".staging")return false;
    if(n==3 && !directory && (path.extension()==L".json" || path.extension()==L".tmp"))return hexadecimal(path.stem().native(),32);
    if(!hexadecimal(p[2],32))return false;
    if(directory)return n==3 || (n==4 && p[0]==L"runtimes" && p[3]==L"payload");
    if(n==4 && p[3]==L"value.json")return true;
    if(p[0]==L"models")return n==4 && path.extension()==L".partial" && hexadecimal(path.stem().native(),32);
    return (n==4 && p[3]==L"runtime.zip") || (n==5 && p[3]==L"payload" &&
        (p[4]==L"pocket-tts.exe" || p[4]==L"LICENSE" || p[4]==L"runtime-installation.json"));
}
bool uninstall_in_download(const fs::path& path,const fs::path& root){
    const auto models=root/L"models/.staging",runtimes=root/L"runtimes/.staging";
    return (path!=models && uninstall_within(path,models)) || (path!=runtimes && uninstall_within(path,runtimes));
}
bool uninstall_winui_path(const fs::path& path,const fs::path& root,bool directory){
    if(path==root)return directory;if(!uninstall_within(path,root))return false;
    if(uninstall_download_path(path,root,directory))return true;
    std::vector<std::wstring> p;for(const auto& part:path.lexically_relative(root))p.push_back(part.native());
    const auto n=p.size();
    if(directory){
        if(n==1)return p[0]==L"models" || p[0]==L"runtimes" || p[0]==L"update-checks";
        if(p[0]==L"models")return (n==2 && (p[1]==L"blobs" || p[1]==L".staging" || p[1]==L".native-operations")) ||
            (n==3 && p[1]==L"blobs" && (p[2]==L"sha256" || p[2]==L"git-blob-sha1"));
        if(p[0]==L"runtimes")return (n==2 && (p[1]==L".staging" || (p[1].size()<129 && !p[1].empty() &&
            (p[1][0]==L'v' || (p[1][0]>=L'0' && p[1][0]<=L'9')) &&
            p[1].find_first_not_of(L"vABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.+-")==p[1].npos))) ||
            (n==3 && p[1]!=L".staging" && uninstall_winui_path(path.parent_path(),root,true) && hexadecimal(maintenance_path_key(p[2]),64));
        return false;
    }
    if(n==2 && p[0]==L"update-checks")return std::set<std::wstring>{L"model-update-checks.json",L"model-update-checks.previous.json",L"model-update-checks.lock"}.contains(p[1]);
    if(n==1)return std::set<std::wstring>{L".outloud-installation.dat",L"model-inventory.json",L"model-inventory.previous.json",
        L"model-selection.json",L"model-selection.lock",L"model-update-checks.json",L"model-update-checks.previous.json",L"runtime-identity.json",
        L"runtime-identity.previous.json",L"model-install.lock",L"model-inventory.lock",L"model-update-checks.lock",L"runtime-update.lock",L"metadata-write.lock"}.contains(p[0]);
    if(p[0]==L"models")return (n==2 && p[1]==L".operations.lock") ||
        (n==4 && p[1]==L"blobs" && ((p[2]==L"sha256" && hexadecimal(p[3],64)) || (p[2]==L"git-blob-sha1" && hexadecimal(p[3],40))));
    return n==4 && p[0]==L"runtimes" && uninstall_winui_path(path.parent_path(),root,true) &&
        (p[3]==L"pocket-tts.exe" || p[3]==L"LICENSE" || p[3]==L"runtime-installation.json");
}
outloud::detail::voice_json::Value uninstall_data_json(const fs::path& path){
    Handle file;pin(path,file);LARGE_INTEGER size{};
    signing_require(GetFileSizeEx(file.value,&size) && size.QuadPart>0 && size.QuadPart<=16*1024*1024,"uninstall_winui_metadata_size_invalid");
    std::string text(static_cast<size_t>(size.QuadPart),'\0');DWORD read{};
    signing_require(ReadFile(file.value,text.data(),static_cast<DWORD>(text.size()),&read,nullptr) && read==text.size(),"uninstall_winui_metadata_unreadable");
    auto json=outloud::detail::voice_json::Parser(text,1048576,4096,65536).parse();
    signing_require(json.kind==outloud::detail::voice_json::Value::Kind::object,"uninstall_winui_metadata_invalid");return json;
}
std::wstring uninstall_json_text(const outloud::detail::voice_json::Value& object,const char* key){
    const auto& v=object.object.at(key);signing_require(v.kind==outloud::detail::voice_json::Value::Kind::string,"uninstall_winui_metadata_invalid");
    signing_require(v.text.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_./:+-")==v.text.npos,"uninstall_winui_identity_invalid");
    return {v.text.begin(),v.text.end()};
}
void uninstall_json_keys(const outloud::detail::voice_json::Value& object,const std::set<std::string>& keys){
    signing_require(object.object.size()==keys.size(),"uninstall_unknown_winui_metadata_preserved");
    for(const auto& [key,value]:object.object){static_cast<void>(value);signing_require(keys.contains(key),"uninstall_unknown_winui_metadata_preserved");}
}
std::wstring uninstall_git_blob_hash(HANDLE file,uint64_t size){
    BCRYPT_ALG_HANDLE algorithm{};BCRYPT_HASH_HANDLE hash{};
    signing_require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA1_ALGORITHM,nullptr,0)>=0,"hash_provider_failed");
    try{
        signing_require(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0,"hash_failed");
        const auto prefix=std::string("blob ")+std::to_string(size)+'\0';
        signing_require(BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<char*>(prefix.data())),static_cast<ULONG>(prefix.size()),0)>=0,"hash_failed");
        LARGE_INTEGER start{};signing_require(SetFilePointerEx(file,start,nullptr,FILE_BEGIN),"file_read_failed");std::array<BYTE,65536> buffer{};DWORD read{};
        for(;;){signing_require(ReadFile(file,buffer.data(),static_cast<DWORD>(buffer.size()),&read,nullptr),"file_read_failed");if(!read)break;
            signing_require(BCryptHashData(hash,buffer.data(),read,0)>=0,"hash_failed");}
        std::array<BYTE,20> digest{};signing_require(BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)>=0,"hash_failed");
        BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);return hex(digest.data(),digest.size());
    }catch(...){if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);throw;}
}
bool uninstall_winui_binding_valid(const std::vector<std::wstring>& fields,const std::wstring& installation,
    const fs::path& app,const std::wstring& identity){
    const bool shape=fields.size()==4 && fields[0]==L"OutLoud.WinUI.Data.2" && fields[3]==identity;
    return shape && fields[1]==installation && same_installation_path(uninstall_path_decode(fields[2]),app);
}
std::set<fs::path> uninstall_download_resources(const fs::path& root){
    std::set<fs::path> resources;
    const auto identity=maintenance_directory_identity(root);
    for(const auto& kind:{L"model",L"runtime"}){
        const bool model=std::wstring_view(kind)==L"model";
        const auto staging=root/(model?L"models/.staging":L"runtimes/.staging");
        safe(staging);if(!fs::exists(staging))continue;
        std::set<std::wstring> ids;
        for(const auto& entry:fs::directory_iterator(staging)){
            const auto journal=entry.path();
            if(!uninstall_download_path(journal,root,false))continue;
            try{
            safe(journal);if(!entry.is_regular_file())continue;
            if(fs::exists(staging/(journal.stem().native()+L".json")) && fs::exists(staging/(journal.stem().native()+L".tmp")))continue;
            const auto record=uninstall_data_json(journal);
            uninstall_json_keys(record,{"SchemaVersion","OperationId","Kind","StoreIdentity","Files"});
            const auto id=uninstall_json_text(record,"OperationId");
            const auto operation_kind=uninstall_json_text(record,"Kind");
            const bool metadata=model?std::set<std::wstring>{L"model-inventory",L"model-selection",L"model-update-checks"}.contains(operation_kind):operation_kind==L"runtime-identity";
            const auto& schema=record.object.at("SchemaVersion");
            signing_require(schema.kind==outloud::detail::voice_json::Value::Kind::number && schema.text=="1" &&
                hexadecimal(id,32) && id==journal.stem().native() && ids.insert(id).second &&
                (operation_kind==kind || metadata) && uninstall_json_text(record,"StoreIdentity")==identity,
                "uninstall_unverified_download_record_preserved");
            const auto operation=staging/id;
            std::set<fs::path> verified{journal,operation};
            if(!model && !metadata)verified.insert(operation/L"payload");
            const auto& files=record.object.at("Files");
            signing_require(files.kind==outloud::detail::voice_json::Value::Kind::array && !files.array.empty() && files.array.size()<=256,
                "uninstall_unverified_download_files_preserved");
            std::set<std::wstring> names;
            for(const auto& file:files.array){
                signing_require(file.kind==outloud::detail::voice_json::Value::Kind::string,"uninstall_unverified_download_files_preserved");
                signing_require(file.text.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789./-")==file.text.npos,
                    "uninstall_unverified_download_files_preserved");
                const std::wstring name(file.text.begin(),file.text.end());
                const auto path=operation/name;
                signing_require(uninstall_download_path(path,root,false) && names.insert(name).second,
                    "uninstall_unverified_download_files_preserved");
                verified.insert(path);
            }
            if(metadata)signing_require(names==std::set<std::wstring>{L"value.json"},"uninstall_unverified_download_files_preserved");
            else if(!model)signing_require(names==std::set<std::wstring>{L"runtime.zip",L"payload/pocket-tts.exe",L"payload/LICENSE",L"payload/runtime-installation.json"},
                "uninstall_unverified_download_files_preserved");
            else for(const auto& name:names)signing_require(name.ends_with(L".partial") && hexadecimal(fs::path(name).stem().native(),32),"uninstall_unverified_download_files_preserved");
            resources.insert(verified.begin(),verified.end());
            }catch(const std::exception&){/* Unverified metadata grants no deletion authority. */}
        }
    }
    return resources;
}
void WindowsUninstall::winui(){
    const auto root=maintenance_application_data();blocked_resource=root;safe(root);
    if(!fs::exists(root))return;
    maintenance_library_security(root);winui_store(root);
}
void WindowsUninstall::winui_store(const fs::path& root){
        blocked_resource=root;safe(root);if(!fs::exists(root))return;
        MaintenanceFiles user(root);user.security=[](const fs::path& p){safe(p);};
        const auto marker=root/L".outloud-installation.dat";const auto before=user.stamp(marker);
        const auto identity=maintenance_directory_identity(root);
        signing_require(before && before->size<=8192,"uninstall_unbound_winui_data_preserved");
        std::wistringstream binding(user.read(marker));std::vector<std::wstring> fields;std::wstring line;
        while(std::getline(binding,line))fields.push_back(line);
        signing_require(uninstall_winui_binding_valid(fields,plan.owner.installation_id,storage().root,identity) &&
            user.stamp(marker)==before,"uninstall_unbound_winui_data_preserved");
        plan.directories.emplace_back(root,identity);
        const auto downloads=uninstall_download_resources(root);
        for(auto it=fs::recursive_directory_iterator(root);it!=fs::recursive_directory_iterator{};++it){const auto& entry=*it;
            const auto path=entry.path();blocked_resource=path;
            const bool download=uninstall_in_download(path,root);
            if(!uninstall_winui_path(path,root,true) && !uninstall_winui_path(path,root,false)){
                it.disable_recursion_pending();continue;}
            if((download && !downloads.contains(path)) || !uninstall_winui_path(path,root,entry.is_directory())){
                it.disable_recursion_pending();continue;}
            safe(path);
            if(entry.is_directory()){plan.directories.emplace_back(path,maintenance_directory_identity(path));continue;}
            const auto stamp=user.stamp(path);signing_require(stamp.has_value(),"uninstall_winui_data_raced");
            const auto relative=path.lexically_relative(root).generic_wstring();
            if(path.extension()==L".lock")signing_require(stamp->size==0,"uninstall_unverified_winui_lock_preserved");
            if(relative.starts_with(L"models/blobs/")){
                Handle file;pin(path,file);const bool sha=path.parent_path().filename()==L"sha256";
                signing_require((sha?stamp->hash:uninstall_git_blob_hash(file.value,stamp->size))==path.filename().native(),"uninstall_unverified_model_blob_preserved");
            }
            if(path.extension()==L".json" && !download){
                const auto json=uninstall_data_json(path);
                if((relative.find(L'/')==relative.npos || relative.starts_with(L"update-checks/")) && !relative.starts_with(L"runtime-identity")){
                    const auto& schema=json.object.at("SchemaVersion");
                    signing_require(schema.kind==outloud::detail::voice_json::Value::Kind::number &&
                        schema.text==(relative.starts_with(L"model-inventory")?"2":"1"),"uninstall_unverified_winui_metadata_preserved");
                    if(relative.starts_with(L"model-inventory")){
                        auto keys=std::set<std::string>{"SchemaVersion","Models"};if(json.object.contains("PendingCleanups"))keys.insert("PendingCleanups");
                        uninstall_json_keys(json,keys);
                    }else if(relative==L"model-selection.json")uninstall_json_keys(json,{"SchemaVersion","SelectedModelAlias"});
                    else uninstall_json_keys(json,{"SchemaVersion","LastSuccessfulCheckUtc","Models"});
                }else if(relative.starts_with(L"runtime-identity")){
                    uninstall_json_keys(json,{"ReleaseVersion","ExecutableSha256"});
                    signing_require(hexadecimal(maintenance_path_key(uninstall_json_text(json,"ExecutableSha256")),64),"uninstall_unverified_runtime_identity_preserved");
                }
            }
            if(relative.starts_with(L"runtimes/") && !download){
                const auto runtime_receipt=uninstall_data_json(path.parent_path()/L"runtime-installation.json");
                uninstall_json_keys(runtime_receipt,{"ReleaseVersion","ExecutableSha256","ArchiveSha256","AssetId","AssetName","AssetUrl","LicenseSha256"});
                const auto executable=maintenance_path_key(uninstall_json_text(runtime_receipt,"ExecutableSha256"));
                const auto license=maintenance_path_key(uninstall_json_text(runtime_receipt,"LicenseSha256"));
                const auto tag=uninstall_json_text(runtime_receipt,"ReleaseVersion"),asset=uninstall_json_text(runtime_receipt,"AssetName");
                uint64_t asset_id{};
                signing_require(outloud::detail::voice_json::unsigned_integer(runtime_receipt.object.at("AssetId"),asset_id) && asset_id>0 &&
                    hexadecimal(maintenance_path_key(uninstall_json_text(runtime_receipt,"ArchiveSha256")),64) && asset.starts_with(L"pocket-tts-") &&
                    asset.ends_with(L"-x86_64-pc-windows-msvc.zip") &&
                    uninstall_json_text(runtime_receipt,"AssetUrl")==L"https://github.com/kyutai-labs/pocket-tts/releases/download/"+tag+L"/"+asset,
                    "uninstall_unverified_runtime_source_preserved");
                signing_require(executable==maintenance_path_key(path.parent_path().filename().native()) && hexadecimal(executable,64) && hexadecimal(license,64) &&
                    uninstall_json_text(runtime_receipt,"ReleaseVersion")==path.parent_path().parent_path().filename().native(),"uninstall_unverified_runtime_receipt_preserved");
                if(path.filename()==L"pocket-tts.exe")signing_require(stamp->hash==executable,"uninstall_unverified_runtime_preserved");
                if(path.filename()==L"LICENSE")signing_require(stamp->hash==license,"uninstall_unverified_runtime_license_preserved");
            }
            signing_require(user.stamp(path)==stamp,"uninstall_winui_data_raced");
            signing_require(plan.files.size()<uninstall_inventory_limit,"uninstall_file_inventory_limit");plan.files.push_back({4,path,*stamp});
        }
}
