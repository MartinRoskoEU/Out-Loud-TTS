// Included in the existing installer helper, reusing pins, hashes and ACL checks.
#include "maintenance_codec.hpp"
MaintenanceStamp maintenance_file_stamp(HANDLE file){
    FILE_ID_INFO id{};LARGE_INTEGER size{};BY_HANDLE_FILE_INFORMATION info{};
    signing_require(GetFileInformationByHandleEx(file,FileIdInfo,&id,sizeof(id)) && GetFileSizeEx(file,&size) && size.QuadPart>=0 &&
        GetFileInformationByHandle(file,&info) && info.nNumberOfLinks==1 && !(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)),"maintenance_file_identity_invalid");
    return {file_hash(file),hex(reinterpret_cast<const BYTE*>(&id.VolumeSerialNumber),8)+hex(id.FileId.Identifier,16),static_cast<uint64_t>(size.QuadPart)};
}
void maintenance_directory(const fs::path& path,bool private_directory){
    safe(path);if(fs::exists(path)){require_admin_protected(path);return;}
    Local descriptor;SECURITY_ATTRIBUTES attributes{sizeof(attributes),nullptr,FALSE};
    const auto acl=private_directory?L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)":L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;GRGX;;;BU)";
    signing_require(ConvertStringSecurityDescriptorToSecurityDescriptorW(acl,SDDL_REVISION_1,reinterpret_cast<PSECURITY_DESCRIPTOR*>(&descriptor.value),nullptr),"maintenance_acl_failed");
    attributes.lpSecurityDescriptor=descriptor.value;signing_require(CreateDirectoryW(path.c_str(),&attributes),"maintenance_directory_failed");require_admin_protected(path);
}
struct MaintenanceFiles {
    fs::path root,vault;std::function<void(const fs::path&)> security;
    std::function<void(const fs::path&,bool)> directory;
    struct Pinned {std::unique_ptr<Handle> handle;MaintenanceStamp stamp;};
    std::vector<Pinned> pins;Handle epoch;unsigned sequence{};bool protected_state=true;
    explicit MaintenanceFiles(const fs::path& r):root(installation_path(r)),security([](const fs::path& p){require_admin_protected(p);}),directory(maintenance_directory){}
    fs::path path(const std::wstring& relative)const{
        signing_require(!relative.empty() && relative.find_first_of(L"\\:\r\n\"")==std::wstring::npos && relative.front()!=L'/' &&
            relative.find(L"..") == std::wstring::npos,"maintenance_relative_path_invalid");
        const auto p=(root/fs::path(relative)).lexically_normal();safe(p);return p;
    }
    void parents(const fs::path& target,bool private_directory){
        std::vector<fs::path> missing;for(auto current=target.parent_path();!fs::exists(current);current=current.parent_path()){
            signing_require(current!=current.parent_path(),"maintenance_parent_missing");missing.push_back(current);}
        for(auto it=missing.rbegin();it!=missing.rend();++it)directory(*it,private_directory);
    }
    MaintenanceFile stamp(const fs::path& p){
        safe(p);if(!fs::exists(p))return {};security(p);
        // Attributes-only open permits namespace identity validation while our
        // no-read/no-write epoch pins prohibit clients loading the current set.
        Handle attributes;attributes.value=CreateFileW(p.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        signing_require(attributes.value!=INVALID_HANDLE_VALUE,"maintenance_namespace_inaccessible");FILE_ID_INFO id{};
        signing_require(GetFileInformationByHandleEx(attributes.value,FileIdInfo,&id,sizeof(id)),"maintenance_namespace_identity_failed");
        const auto identity=hex(reinterpret_cast<const BYTE*>(&id.VolumeSerialNumber),8)+hex(id.FileId.Identifier,16);
        BY_HANDLE_FILE_INFORMATION attributes_info{};signing_require(GetFileInformationByHandle(attributes.value,&attributes_info) && attributes_info.nNumberOfLinks==1 &&
            !(attributes_info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)),"maintenance_namespace_identity_failed");
        for(const auto& h:pins)if(h.stamp.identity==identity)return h.stamp;
        Handle file;pin(p,file);return maintenance_file_stamp(file.value);
    }
    void acquire(const fs::path& p){
        if(!fs::exists(p))return;safe(p);security(p);
        const auto present=stamp(p);for(const auto& existing:pins)if(existing.stamp==present)return;
        auto h=std::make_unique<Handle>();
        h->value=CreateFileW(p.c_str(),GENERIC_READ|DELETE,FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        signing_require(h->value!=INVALID_HANDLE_VALUE,"maintenance_resource_in_use_or_denied");const auto observed=maintenance_file_stamp(h->value);pins.push_back({std::move(h),observed});
    }
    HANDLE held(const MaintenanceStamp& expected){for(const auto& h:pins)if(h.stamp==expected)return h.handle->value;
        throw SigningError("maintenance_pin_identity_changed");}
    void copy(const fs::path& source,const fs::path& target,const std::wstring& expected){
        safe(source);safe(target);Handle input;pin(source,input);const auto before=maintenance_file_stamp(input.value);
        signing_require(before.hash==expected,"maintenance_candidate_hash_changed");parents(target,true);
        Handle output;output.value=CreateFileW(target.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        signing_require(output.value!=INVALID_HANDLE_VALUE,"maintenance_copy_destination_exists_or_denied");
        LARGE_INTEGER zero{};signing_require(SetFilePointerEx(input.value,zero,nullptr,FILE_BEGIN),"maintenance_copy_read_failed");
        std::array<BYTE,65536> bytes{};DWORD count{},written{};
        for(;;){signing_require(ReadFile(input.value,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr),"maintenance_copy_read_failed");if(!count)break;
            signing_require(WriteFile(output.value,bytes.data(),count,&written,nullptr) && written==count,"maintenance_copy_write_failed");}
        signing_require(count==0 && FlushFileBuffers(output.value),"maintenance_copy_flush_failed");
        signing_require(maintenance_file_stamp(input.value)==before,"maintenance_candidate_identity_changed");
        CloseHandle(output.value);output.value=INVALID_HANDLE_VALUE;
        signing_require(stamp(target)->hash==expected,"maintenance_copy_hash_invalid");
    }
    void rename(HANDLE h,const fs::path& target){
        safe(target);parents(target,target.native().find(vault.native())==0);
        const auto name=target.native();const size_t size=sizeof(FILE_RENAME_INFO)+(name.size()+1)*sizeof(wchar_t);
        std::vector<BYTE> storage(size);auto info=reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
        info->ReplaceIfExists=FALSE;info->FileNameLength=static_cast<DWORD>(name.size()*sizeof(wchar_t));memcpy(info->FileName,name.data(),info->FileNameLength);
        if(!SetFileInformationByHandle(h,FileRenameInfo,info,static_cast<DWORD>(size)))
            throw SigningError(("maintenance_checked_rename_failed_win32_"+std::to_string(GetLastError())).c_str());
    }
    void destination_security(const MaintenanceChange& c){
        const auto staged=vault/L"new"/c.path;
        // Preserve original permissions. Fresh/repair-absent files receive the
        // protected application read/execute ACL, never stage-only admin access.
        Local descriptor;
        if(c.before){security(path(c.path));signing_require(GetNamedSecurityInfoW(path(c.path).c_str(),SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,
            nullptr,nullptr,nullptr,nullptr,reinterpret_cast<PSECURITY_DESCRIPTOR*>(&descriptor.value))==ERROR_SUCCESS,"maintenance_original_acl_unreadable");}
        else signing_require(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;GRGX;;;BU)",SDDL_REVISION_1,
            reinterpret_cast<PSECURITY_DESCRIPTOR*>(&descriptor.value),nullptr),"maintenance_acl_failed");
        signing_require(SetFileSecurityW(staged.c_str(),OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,
            static_cast<PSECURITY_DESCRIPTOR>(descriptor.value)),"maintenance_staged_acl_failed");security(staged);
    }
    void acquire_epoch(bool create,bool removable=false){
        if(epoch.value!=INVALID_HANDLE_VALUE)return;
        const auto lock=root/L"configuration/maintenance.lock";safe(lock);
        if(fs::exists(lock))security(lock);
        else {signing_require(create,"maintenance_epoch_missing");write_new(lock,L"OutLoud.Maintenance.Lock.1\n",protected_state);}
        // Uninstall must delete through this same exclusive, ownership-checked
        // handle. Ordinary maintenance keeps its existing read/write access.
        epoch.value=CreateFileW(lock.c_str(),GENERIC_READ|GENERIC_WRITE|(removable?DELETE:0),0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        signing_require(epoch.value!=INVALID_HANDLE_VALUE,"maintenance_already_active");security(lock);
        std::array<char,64> marker{};DWORD count{};
        signing_require(ReadFile(epoch.value,marker.data(),static_cast<DWORD>(marker.size()),&count,nullptr) &&
            std::string(marker.data(),count)=="OutLoud.Maintenance.Lock.1\n","maintenance_epoch_ownership_invalid");
    }
    void begin(const MaintenanceJournal& j){
        parents(root/L".outloud-bootstrap.dat",false);
        directory(root,false);
        // Write-ahead authority before creating transaction infrastructure. A
        // truncated record is retained and refused; it never authorizes adoption.
        write_new(root/L".outloud-bootstrap.dat",maintenance_encode(j),protected_state);
        directory(root/L"configuration",false);
        acquire_epoch(true);
        signing_require(!fs::exists(root/L"configuration/maintenance.pending"),"maintenance_recovery_required");
        directory(root/L"configuration/maintenance",true);vault=root/L"configuration/maintenance"/j.transaction_id;
        signing_require(!fs::exists(vault),"maintenance_transaction_collision");directory(vault,true);
        save(j);publish_pointer(j);
        remove_bootstrap(j);
    }
    void publish_pointer(const MaintenanceJournal& j){
        const auto temporary=root/L"configuration/maintenance.pending.tmp";
        if(fs::exists(temporary))signing_require(read(temporary)==j.transaction_id+L"\n","maintenance_bootstrap_pointer_invalid");
        else write_new(temporary,j.transaction_id+L"\n",protected_state);
        signing_require(MoveFileExW(temporary.c_str(),(root/L"configuration/maintenance.pending").c_str(),MOVEFILE_WRITE_THROUGH),"maintenance_pointer_publication_failed");
    }
    void remove_bootstrap(const MaintenanceJournal& j){
        const auto bootstrap=root/L".outloud-bootstrap.dat";
        if(!fs::exists(bootstrap))return;
        signing_require(read(bootstrap)==maintenance_encode(j),"maintenance_bootstrap_changed");
        signing_require(DeleteFileW(bootstrap.c_str()),"maintenance_bootstrap_retirement_failed");
    }
    void resume_bootstrap(const std::vector<std::wstring>& package){
        const auto bootstrap=root/L".outloud-bootstrap.dat";
        if(!fs::exists(bootstrap) || fs::exists(root/L"configuration/maintenance.pending"))return;
        const auto j=maintenance_decode(read(bootstrap),package);
        signing_require(j.root_encoding==maintenance_root_encoding(root.native()) && j.phase==MaintenancePhase::preparing &&
            j.resource_phase.empty() && j.library_identity.empty()==(j.operation==MaintenanceOperation::fresh) &&
            !j.bootstrap_detector && !j.bootstrap_requirements && !j.bootstrap_report &&
            std::all_of(j.changes.begin(),j.changes.end(),[](const auto& c){return !c.after && !c.backup;}),"maintenance_bootstrap_not_initial");
        for(const auto& c:j.changes)signing_require(current(c.path)==c.before,"maintenance_bootstrap_baseline_changed");
        directory(root/L"configuration",false);acquire_epoch(true);
        directory(root/L"configuration/maintenance",true);vault=root/L"configuration/maintenance"/j.transaction_id;
        directory(vault,true);const auto initial=vault/L"state-0.dat";
        fs::path partial;
        // Only a complete initial frame can be promoted. Unknown/partial bytes
        // remain evidence, without starting signing or publishing any payload.
        for(const auto& e:fs::directory_iterator(vault)){
            safe(e.path());signing_require(e.is_regular_file(),"maintenance_bootstrap_unknown_content");
            if(e.path()==initial)signing_require(read(initial)==maintenance_encode(j),"maintenance_bootstrap_frame_changed");
            else {const auto name=e.path().filename().native();
                signing_require(partial.empty() && name.starts_with(L"frame-") && name.ends_with(L"-0.tmp") &&
                    name.size()<40 && name.substr(6,name.size()-12).find_first_not_of(L"0123456789")==std::wstring::npos &&
                    read(e.path())==maintenance_encode(j),"maintenance_bootstrap_unknown_content");partial=e.path();}}
        if(!partial.empty())signing_require(!fs::exists(initial) && MoveFileExW(partial.c_str(),initial.c_str(),MOVEFILE_WRITE_THROUGH),"maintenance_bootstrap_frame_conflict");
        if(!fs::exists(initial)){sequence=0;save(j);}
        publish_pointer(j);remove_bootstrap(j);
    }
    void save(const MaintenanceJournal& j){
        signing_require(sequence<256,"maintenance_journal_limit");const auto final=vault/(L"state-"+std::to_wstring(sequence)+L".dat");
        // Partial temporary snapshots are evidence, never authoritative frames.
        const auto temp=vault/(L"frame-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(sequence)+L".tmp");
        write_new(temp,maintenance_encode(j),protected_state);
        signing_require(MoveFileExW(temp.c_str(),final.c_str(),MOVEFILE_WRITE_THROUGH),"maintenance_journal_publication_failed");++sequence;
    }
    std::wstring read(const fs::path& p){
        security(p);Handle h;pin(p,h);LARGE_INTEGER size{};signing_require(GetFileSizeEx(h.value,&size) && size.QuadPart>=0 && size.QuadPart<=1024*1024,"maintenance_journal_size_invalid");
        std::vector<char> bytes(static_cast<size_t>(size.QuadPart));DWORD n{};
        signing_require(ReadFile(h.value,bytes.data(),static_cast<DWORD>(bytes.size()),&n,nullptr) && n==bytes.size(),"maintenance_journal_read_failed");
        std::wstring text;for(auto b:bytes){signing_require(static_cast<unsigned char>(b)<128,"maintenance_journal_encoding_invalid");text+=static_cast<wchar_t>(b);}return text;
    }
    std::wstring read_held(const fs::path& p){
        const auto present=stamp(p);signing_require(present && present->size<=1024*1024,"maintenance_journal_size_invalid");
        HANDLE input=INVALID_HANDLE_VALUE;for(const auto& h:pins)if(h.stamp==present)input=h.handle->value;
        if(input==INVALID_HANDLE_VALUE)return read(p);
        LARGE_INTEGER zero{};signing_require(SetFilePointerEx(input,zero,nullptr,FILE_BEGIN),"maintenance_journal_read_failed");
        std::vector<char> bytes(static_cast<size_t>(present->size));DWORD n{};
        signing_require(ReadFile(input,bytes.data(),static_cast<DWORD>(bytes.size()),&n,nullptr) && n==bytes.size(),"maintenance_journal_read_failed");
        std::wstring text;for(auto b:bytes){signing_require(static_cast<unsigned char>(b)<128,"maintenance_journal_encoding_invalid");text+=static_cast<wchar_t>(b);}return text;
    }
    MaintenanceJournal load(const std::vector<std::wstring>& package){
        resume_bootstrap(package);
        acquire_epoch(false);
        const auto pointer=read(root/L"configuration/maintenance.pending");
        signing_require(pointer.size()==33 && pointer.back()==L'\n' && hexadecimal(pointer.substr(0,32),32),"maintenance_pointer_invalid");
        vault=root/L"configuration/maintenance"/pointer.substr(0,32);safe(vault);security(vault);
        unsigned count{};std::set<unsigned> indices;
        for(const auto& entry:fs::directory_iterator(vault)){safe(entry.path());if(!entry.is_regular_file())continue;const auto name=entry.path().filename().native();
            if(name.starts_with(L"state-") && name.ends_with(L".dat")){const auto number=name.substr(6,name.size()-10);
                signing_require(!number.empty() && number.size()<4 && number.find_first_not_of(L"0123456789")==number.npos,"maintenance_journal_name_invalid");
                signing_require(indices.insert(static_cast<unsigned>(std::stoul(number))).second,"maintenance_journal_duplicate");++count;}}
        signing_require(count>0 && count<=256,"maintenance_journal_missing");MaintenanceJournal last;
        for(unsigned i=0;i<count;++i){signing_require(indices.contains(i),"maintenance_journal_gap");auto j=maintenance_decode(read(vault/(L"state-"+std::to_wstring(i)+L".dat")),package);
            signing_require(j.transaction_id==pointer.substr(0,32) && j.root_encoding==maintenance_root_encoding(root.native()),"maintenance_journal_wrong_installation");
            if(i){signing_require(j.installation_id==last.installation_id && j.operation==last.operation && j.components==last.components && j.host==last.host &&
                    j.old_version==last.old_version && j.new_version==last.new_version && j.registration_before==last.registration_before &&
                    j.host_before==last.host_before && j.host_after==last.host_after &&
                    (last.library_identity.empty() || last.library_identity==j.library_identity) &&
                    (last.library_parent_identity.empty() || last.library_parent_identity==j.library_parent_identity) &&
                    (!last.bootstrap_detector || last.bootstrap_detector==j.bootstrap_detector) &&
                    (!last.bootstrap_requirements || last.bootstrap_requirements==j.bootstrap_requirements) &&
                    (!last.bootstrap_report || last.bootstrap_report==j.bootstrap_report) &&
                    j.changes.size()==last.changes.size(),"maintenance_journal_identity_changed");
                for(size_t k=0;k<j.changes.size();++k)signing_require(j.changes[k].path==last.changes[k].path && j.changes[k].before==last.changes[k].before &&
                    j.changes[k].input_hash==last.changes[k].input_hash,"maintenance_journal_baseline_changed");}
            last=std::move(j);}
        sequence=count;
        if(fs::exists(root/L".outloud-bootstrap.dat"))remove_bootstrap(maintenance_decode(read(vault/L"state-0.dat"),package));
        return last;
    }
    MaintenanceFile current(const std::wstring& p){return stamp(path(p));}
    MaintenanceFile backup(const std::wstring& p){return stamp(vault/L"backup"/p);}
    MaintenanceFile original(const std::wstring& p){return stamp(vault/L"original"/p);}
    void backup_all(MaintenanceJournal& j){for(auto& c:j.changes){if(c.after)destination_security(c);
        if(c.before){copy(path(c.path),vault/L"backup"/c.path,c.before->hash);c.backup=backup(c.path);}}}
    void lock_files(const MaintenanceJournal& j){
        for(const auto& c:j.changes){acquire(path(c.path));if(c.after)acquire(vault/L"new"/c.path);
            acquire(vault/L"original"/c.path);acquire(vault/L"backup"/c.path);}
    }
    void publish(const MaintenanceChange& c){
        signing_require(current(c.path)==c.before && stamp(vault/L"new"/c.path)==c.after,"maintenance_publication_identity_changed");
        if(c.before)rename(held(*c.before),vault/L"original"/c.path);
        if(c.after)rename(held(*c.after),path(c.path));signing_require(current(c.path)==c.after,"maintenance_publication_verification_failed");
    }
    void restore(const MaintenanceChange& c){
        const auto now=current(c.path);signing_require(now==c.before || now==c.after || (!now && c.before && original(c.path)==c.before),"maintenance_restore_foreign_file");
        if(now==c.before)return;
        if(now){acquire(path(c.path));rename(held(*now),vault/L"failed"/c.path);}
        if(c.before){signing_require(original(c.path)==c.before,"maintenance_original_identity_changed");acquire(vault/L"original"/c.path);rename(held(*c.before),path(c.path));}
        signing_require(current(c.path)==c.before,"maintenance_restore_verification_failed");
    }
    void release(){pins.clear();}
    void retire(const MaintenanceJournal& j){
        signing_require(j.phase==MaintenancePhase::committed || j.phase==MaintenancePhase::rolled_back,"maintenance_not_terminal");
        release();const auto pointer=root/L"configuration/maintenance.pending";
        signing_require(read(pointer)==j.transaction_id+L"\n","maintenance_pointer_changed");
        signing_require(MoveFileExW(pointer.c_str(),(vault/L"terminal.pointer").c_str(),MOVEFILE_WRITE_THROUGH),"maintenance_retirement_failed");
        // A03.11 inventories these terminal archives before final removal.
    }
};
// Stay within ordinary Win32 filename/directory limits. Derive the budget from
// the actual package and vault layout, rather than imposing a 100-character root.
void maintenance_path_budget(const fs::path& root,const std::vector<std::wstring>& package){
    std::vector<fs::path> paths{root/L"configuration/maintenance.pending.tmp",root/L".outloud-bootstrap.dat"};
    const auto vault=root/L"configuration/maintenance"/std::wstring(32,L'a');
    paths.push_back(vault/L"frame-18446744073709551615-255.tmp");
    paths.push_back(vault/L"bootstrap/OutLoudPrerequisites.exe");
    for(const auto& relative:package)for(const auto* part:{L"new",L"backup",L"original",L"failed"})paths.push_back(vault/part/relative);
    for(const auto& path:paths)signing_require(path.native().size()<MAX_PATH && path.parent_path().native().size()<MAX_PATH-12,"maintenance_path_budget_exceeded");
}
