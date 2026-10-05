#include "outloud/voice_library.hpp"
#include "voice_library_fs.hpp"
#include "voice_library_security.hpp"

#include <winioctl.h>
#include <aclapi.h>
#include <sddl.h>
#include <shlobj.h>
#include <barrier>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>

namespace {
using namespace outloud;
namespace fs=detail::library_fs;
using Code=VoiceLibraryErrorCode;
unsigned checks=0;
void require(bool condition,const char* message) { ++checks; if (!condition) { throw std::runtime_error(message); } }
template<class Function> VoiceLibraryError rejects(Code code,Function function) {
    try { function(); }
    catch (const VoiceLibraryError& error) {
        if (error.code()!=code) { std::cerr<<"Expected "<<voice_library_error_name(code)<<"; got "<<voice_library_error_name(error.code())<<": "<<error.what()<<'\n'; }
        require(error.code()==code,"Wrong structured library error"); return error;
    }
    throw std::runtime_error("Library accepted invalid operation");
}
std::vector<unsigned char> read(const std::filesystem::path& path) {
    const detail::MappedFile mapping(path,1024ULL*1024*1024+65536+256);
    return {mapping.bytes().begin(),mapping.bytes().end()};
}
void write(const std::filesystem::path& path,detail::Bytes bytes) {
    std::ofstream output(path,std::ios::binary|std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    require(bool(output),"Cannot write private test fixture");
}
void write(const std::filesystem::path& path,std::string_view text) {
    write(path,{reinterpret_cast<const unsigned char*>(text.data()),text.size()});
}
std::string replace(std::string text,std::string_view before,std::string_view after) {
    const auto at=text.find(before); require(at!=std::string::npos,"Fixture replacement not found");
    text.replace(at,before.size(),after); return text;
}
void put(std::vector<unsigned char>& bytes,std::size_t at,std::uint32_t value) {
    for (unsigned i=0;i<4;++i) { bytes[at+i]=static_cast<unsigned char>(value>>(i*8)); }
}
void cleanup_owned(const std::filesystem::path& path) noexcept {
    const auto attributes=GetFileAttributesW(path.c_str());
    if (attributes==INVALID_FILE_ATTRIBUTES) { return; }
    if (!(attributes&FILE_ATTRIBUTE_DIRECTORY)) { DeleteFileW(path.c_str()); return; }
    if (!(attributes&FILE_ATTRIBUTE_REPARSE_POINT)) {
        try { for (const auto& name : fs::children(path)) { cleanup_owned(path/name); } } catch (...) {}
    }
    RemoveDirectoryW(path.c_str()); // Junction itself, never its target.
}
struct Scratch {
    std::filesystem::path path;
    Scratch() {
        std::wstring temp(32768,L'\0'); const auto length=GetTempPathW(static_cast<DWORD>(temp.size()),temp.data());
        require(length && length<temp.size(),"Cannot locate test temporary directory"); temp.resize(length);
        const fs::Pins parents(temp);
        path=parents.path/(L"outloud-library-tests-"+fs::wide(fs::random_id()));
        require(CreateDirectoryW(path.c_str(),nullptr)!=0,"Cannot create unique test scratch root");
        require(fs::within(path,parents.path) && path!=parents.path,"Unsafe test cleanup root");
    }
    ~Scratch() { cleanup_owned(path); }
};
struct Descriptor {
    PSECURITY_DESCRIPTOR value = nullptr;
    explicit Descriptor(const wchar_t* sddl) {
        require(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl,SDDL_REVISION_1,&value,nullptr)!=0,"Cannot build test security descriptor");
    }
    ~Descriptor() { if (value) { LocalFree(value); } }
};
struct ReadOnlyAcl {
    fs::Handle file;
    PSECURITY_DESCRIPTOR original = nullptr;
    PACL original_dacl = nullptr;
    SECURITY_DESCRIPTOR_CONTROL original_control{};
    ReadOnlyAcl(const std::filesystem::path& path,bool directory,const Descriptor& descriptor)
        : file(directory ? fs::directory(path,READ_CONTROL|WRITE_DAC) :
            fs::file(path,READ_CONTROL|WRITE_DAC,FILE_SHARE_READ|FILE_SHARE_WRITE)) {
        require(GetSecurityInfo(file.value,SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&original_dacl,nullptr,&original)==ERROR_SUCCESS,
            "Cannot capture private fixture ACL");
        DWORD revision = 0;
        require(GetSecurityDescriptorControl(original,&original_control,&revision)!=0,"Cannot capture fixture inheritance state");
        PACL dacl = nullptr; BOOL present = FALSE, defaulted = FALSE;
        require(GetSecurityDescriptorDacl(descriptor.value,&present,&dacl,&defaulted)!=0 && present,"Invalid fixture ACL");
        require(SetSecurityInfo(file.value,SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,
            nullptr,nullptr,dacl,nullptr)==ERROR_SUCCESS,"Cannot set private read-only ACL");
    }
    ~ReadOnlyAcl() {
        if (original) {
            (void)SetSecurityInfo(file.value,SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|
                ((original_control&SE_DACL_PROTECTED) ? PROTECTED_DACL_SECURITY_INFORMATION : UNPROTECTED_DACL_SECURITY_INFORMATION),
                nullptr,nullptr,original_dacl,nullptr);
            LocalFree(original);
        }
    }
};
struct OrdinaryToken {
    fs::Handle token;
    OrdinaryToken() {
        fs::Handle process;
        require(OpenProcessToken(GetCurrentProcess(),TOKEN_DUPLICATE|TOKEN_QUERY,&process.value)!=0,"Cannot open own test token");
        BYTE admin_sid[SECURITY_MAX_SID_SIZE]{}; DWORD size = sizeof(admin_sid);
        require(CreateWellKnownSid(WinBuiltinAdministratorsSid,nullptr,admin_sid,&size)!=0,"Cannot build admin SID");
        SID_AND_ATTRIBUTES disabled{admin_sid,0};
        require(CreateRestrictedToken(process.value,DISABLE_MAX_PRIVILEGE,1,&disabled,0,nullptr,0,nullptr,&token.value)!=0,
            "Cannot create ordinary-user test token");
        require(ImpersonateLoggedOnUser(token.value)!=0,"Cannot impersonate own restricted token");
    }
    ~OrdinaryToken() { (void)RevertToSelf(); }
};
void security_checks(Scratch& scratch,VoiceLibrary& library,const LibraryVoice& voice,const std::filesystem::path& source) {
    namespace security = detail::library_security;
    Descriptor safe(security::directory_sddl);
    require(security::valid_descriptor(safe.value,true,true),"Production protected directory descriptor invalid");
    Descriptor file(L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FRFX;;;BU)");
    require(security::valid_descriptor(file.value,false,false),"Production file descriptor invalid");
    security::FileCreationSecurity machine_file(true), developer_file(false);
    require(machine_file.get() && machine_file.get()->lpSecurityDescriptor==machine_file.descriptor &&
        security::valid_descriptor(machine_file.descriptor,false,false),"Machine file creation does not explicitly assign trusted owner/ACL");
    require(!developer_file.get(),"Developer files received machine security implicitly");
    for (const auto* unsafe : {
        L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;BU)",
        L"O:BAG:BAD:(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FRFX;;;BU)",
        L"O:BUG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FRFX;;;BU)",
        L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FRFX;;;WD)",
        L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FRFX;;;BU)(A;OICI;FA;;;CO)",
        L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)",
        L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FRFX;;;BU)"}) {
        Descriptor bad(unsafe); require(!security::valid_descriptor(bad.value,true,true),"Unsafe machine descriptor accepted");
    }
    // Apply the production DACL ONLY to uniquely owned temporary fixtures.
    // Ownership remains the test user; production also requires BA/SYSTEM owner.
    const auto parent = scratch.path/L"acl-parent";
    require(CreateDirectoryW(parent.c_str(),nullptr)!=0,"Cannot create private ACL parent");
    {
        ReadOnlyAcl acl(parent,true,safe);
        const auto dir = fs::directory(parent,READ_CONTROL);
        if (!security::valid_descriptor(safe.value,true,true)) { throw std::runtime_error("Descriptor changed"); }
        // The ordinary token must be able to list but cannot create or delete children.
        OrdinaryToken ordinary;
        require(fs::children(parent).empty(),"Ordinary user could not enumerate read-only directory");
        const auto denied = rejects(Code::library_access_denied,[&] { security::require_machine_authority(parent); });
        require(denied.system_error()==ERROR_ACCESS_DENIED,"Elevation denial lost native cause");
        rejects(Code::library_access_denied,[&] { (void)security::create_directory(parent/L"must-not-exist"); });
        rejects(Code::library_access_denied,[&] { security::protect_created_file(parent/L"must-not-exist"); });
        require(!std::filesystem::exists(parent/L"must-not-exist"),"Denied operation created a directory");
        require(!CreateDirectoryW((parent/L"raw-denied").c_str(),nullptr) && GetLastError()==ERROR_ACCESS_DENIED,"Users could write machine DACL");
    }
    const auto child = parent/L"inherited";
    // Create the inheritance fixture using a held, authorized directory handle.
    // Temporarily grant the test owner write while creating, then install exact
    // inheritable ACL on parent and let Windows propagate to this private child.
    require(CreateDirectoryW(child.c_str(),nullptr)!=0,"Cannot create inheritance child");
    write(child/L"data.bin","private fixture");
    {
        ReadOnlyAcl acl(parent,true,safe);
        for (const auto& entry : {child,child/L"data.bin"}) {
            const bool directory = entry==child;
            const auto handle = directory ? fs::directory(entry,READ_CONTROL) : fs::file(entry);
            PSECURITY_DESCRIPTOR raw = nullptr; PACL dacl = nullptr;
            const auto status = GetSecurityInfo(handle.value,SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&dacl,nullptr,&raw);
            require(status==ERROR_SUCCESS,"Cannot inspect inherited private ACL");
            const bool valid = security::valid_dacl(dacl,directory); LocalFree(raw);
            require(valid,"Windows inheritance did not produce expected child ACL");
        }
    }
    {
        ReadOnlyAcl lock(library.root()/L".library.lock",false,file);
        ReadOnlyAcl asset(voice.managed_olv_path,false,file);
        ReadOnlyAcl record(voice.managed_olv_path.parent_path()/L"metadata.json",false,file);
        OrdinaryToken ordinary;
        require(library.inspect_voice(voice.library_id).health==VoiceHealth::valid && !library.list_voices().empty(),
            "Read-only lock/metadata/voice require write permission");
        require(validate_olv(voice.managed_olv_path).file_sha256==voice.metadata->olv_sha256,"Ordinary asset read/validation failed");
        rejects(Code::library_access_denied,[&] { (void)library.rename_voice(voice.library_id,"Denied rename"); });
        rejects(Code::library_access_denied,[&] { (void)library.remove_voice(voice.library_id); });
        rejects(Code::library_access_denied,[&] { (void)library.import_olv(source,"Denied import",DuplicateContentPolicy::allow_separate_entry); });
        rejects(Code::library_access_denied,[&] { (void)library.cleanup_pending(); });
    }
    require(library.inspect_voice(voice.library_id).health==VoiceHealth::valid,"Permission failures damaged entry");
}
void junction(const std::filesystem::path& link,const std::filesystem::path& target) {
    require(CreateDirectoryW(link.c_str(),nullptr)!=0,"Cannot create junction test directory");
    auto handle=fs::directory(link,GENERIC_WRITE);
    const auto substitute=L"\\??\\"+target.native(); const auto print=target.native();
    const auto path_bytes=(substitute.size()+print.size()+2)*sizeof(wchar_t);
    std::vector<std::uint64_t> storage((16+path_bytes+7)/8,0);
    auto raw=reinterpret_cast<unsigned char*>(storage.data());
    const DWORD tag=IO_REPARSE_TAG_MOUNT_POINT; const auto data_length=static_cast<WORD>(8+path_bytes);
    const WORD sub_offset=0,sub_length=static_cast<WORD>(substitute.size()*2);
    const WORD print_offset=static_cast<WORD>((substitute.size()+1)*2),print_length=static_cast<WORD>(print.size()*2);
    std::memcpy(raw,&tag,4); std::memcpy(raw+4,&data_length,2);
    std::memcpy(raw+8,&sub_offset,2); std::memcpy(raw+10,&sub_length,2);
    std::memcpy(raw+12,&print_offset,2); std::memcpy(raw+14,&print_length,2);
    std::memcpy(raw+16,substitute.data(),sub_length); std::memcpy(raw+16+print_offset,print.data(),print_length);
    DWORD received=0;
    require(DeviceIoControl(handle.value,FSCTL_SET_REPARSE_POINT,raw,static_cast<DWORD>(16+path_bytes),nullptr,0,&received,nullptr)!=0,
            "Cannot create private junction fixture");
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        require(argc==3,"Usage: outloud-voice-library-tests Gnome.safetensors Gnome.olv");
        const auto original_source=read(argv[1]),original_olv=read(argv[2]);
        const auto real_root=default_voice_library_root(); const auto real_before=GetFileAttributesW(real_root.c_str());
        PWSTR program_data = nullptr;
        require(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData,KF_FLAG_DONT_VERIFY,nullptr,&program_data)),"Cannot resolve ProgramData for default-root check");
        const auto expected_root = std::filesystem::path(program_data)/L"OutLoud"/L"VoiceLibrary"; CoTaskMemFree(program_data);
        require(real_root==expected_root,"Default root is not ProgramData known-folder result");
        Scratch scratch; const auto root=scratch.path/L"kni\u017enica";
        VoiceLibrary library(root);
        require(!library.is_machine_storage(),"Explicit temporary root gained production status");
        require(library.list_voices().empty() && !std::filesystem::exists(root),"Empty enumeration created a library");
        rejects(Code::invalid_library_root,[&] { VoiceLibrary bad(L"C:\\OutLoud\\forbidden"); });
        rejects(Code::invalid_library_root,[&] { VoiceLibrary bad(L"\\\\server\\share\\voices"); });
        rejects(Code::invalid_library_root,[&] { VoiceLibrary bad(root/L".."/L"escaped"); });
        rejects(Code::invalid_library_root,[&] { VoiceLibrary bad(scratch.path/L"NUL.olv"); });
        VoiceImportOptions options; options.declared_model_compatibility_id=std::string(pocket_model_compatibility_id);
        auto first=library.import_pocket_voice(argv[1],"Gnome Male",options).voice;
        require(first.health==VoiceHealth::valid && first.metadata && !first.metadata->model_provenance_verified &&
                first.metadata->import_kind==VoiceImportKind::pocket_prepared,"Pocket import metadata is invalid");
        require(read(first.managed_olv_path)==original_olv,"Pocket library import changed .olv bytes");
        require(first.metadata->schema_version==1 && fs::id(first.library_id),"Schema or library ID format changed");
        security_checks(scratch,library,first,argv[2]);
        require(validate_olv(first.managed_olv_path).file_sha256==first.metadata->olv_sha256,"Production loader identity mismatch");
        auto duplicate=rejects(Code::duplicate_content,[&] { (void)library.import_olv(argv[2],"Other",DuplicateContentPolicy::reject); });
        require(duplicate.duplicate_ids()==std::vector<std::string>{first.library_id},"Duplicate result has no stable IDs");
        auto second=library.import_olv(argv[2],"Gnome Male",DuplicateContentPolicy::allow_separate_entry);
        require(second.duplicate_ids==std::vector<std::string>{first.library_id} && second.voice.library_id!=first.library_id,
                "Explicit duplicate did not create separate identity");
        require(second.voice.metadata->import_kind==VoiceImportKind::olv && library.list_voices().size()==2,"Duplicate display names were not preserved");
        const auto export_path=scratch.path/L"export-\u017e.olv";
        (void)library.export_olv(first.library_id,export_path);
        require(read(export_path)==original_olv,"Export is not byte exact");
        rejects(Code::replace_required,[&] { (void)library.export_olv(first.library_id,export_path); });
        write(export_path,"previous destination");
        const auto old_export=read(export_path);
        {
            const auto locked=fs::file(export_path);
            rejects(Code::destination_io,[&] { (void)library.export_olv(first.library_id,export_path,true); });
        }
        require(read(export_path)==old_export,"Locked export destination was overwritten");
        (void)library.export_olv(first.library_id,export_path,true);
        require(read(export_path)==original_olv,"Explicit export replacement failed");
        for (const auto& name : fs::children(scratch.path)) { require(!name.starts_with(L".outloud-export-"),"Export failure left temporary file"); }
        rejects(Code::destination_io,[&] { (void)library.export_olv(first.library_id,root/L"voices"/L"bad.olv"); });
        rejects(Code::destination_io,[&] { (void)library.export_olv(first.library_id,scratch.path/L"COM1.olv"); });
        rejects(Code::destination_io,[&] { (void)library.export_olv(first.library_id,scratch.path/L"export:stream.olv"); });
        rejects(Code::invalid_id,[&] { (void)library.inspect_voice("../outside"); });
        rejects(Code::voice_not_found,[&] { (void)library.inspect_voice(std::string(32,'f')); });
        const auto renamed=library.rename_voice(first.library_id,"  \xc5\xbd" "aba caf\xc3\xa9 \xf0\x9f\x90\xb8  ");
        require(renamed.library_id==first.library_id && renamed.metadata->display_name=="\xc5\xbd" "aba caf\xc3\xa9 \xf0\x9f\x90\xb8" &&
                read(first.managed_olv_path)==original_olv,"Unicode rename changed ID/asset bytes or failed trimming");
        for (auto name : {std::string(""),std::string(" \xc2\xa0 "),std::string("line\nname"),std::string("\xc0\xaf"),std::string(257,'a'),std::string("nul\0name",8)}) {
            rejects(Code::invalid_display_name,[&] { (void)library.rename_voice(first.library_id,name); });
        }
        const auto first_metadata=first.managed_olv_path.parent_path()/L"metadata.json";
        const auto before_rename=read(first_metadata);
        {
            const auto locked=fs::file(first_metadata);
            rejects(Code::destination_io,[&] { (void)library.rename_voice(first.library_id,"Cannot replace locked metadata"); });
        }
        require(read(first_metadata)==before_rename,"Failed rename changed metadata");
        require(library.cleanup_pending().pending.empty(),"Failed rename left unsafe staging work");
        {
            auto locked=fs::file(root/L".library.lock",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE);
            OVERLAPPED overlap{}; require(LockFileEx(locked.value,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&overlap)!=0,"Cannot acquire library lock fixture");
            rejects(Code::library_busy,[&] { (void)library.list_voices(); });
            rejects(Code::library_busy,[&] { (void)library.rename_voice(first.library_id,"Busy"); });
        }
        {
            auto shared=fs::file(root/L".library.lock",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE);
            OVERLAPPED overlap{};
            require(LockFileEx(shared.value,LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&overlap)!=0,"Cannot acquire shared library lock fixture");
            require(library.inspect_voice(first.library_id).health==VoiceHealth::valid && !library.list_voices().empty(),"Concurrent readers conflict");
            rejects(Code::library_busy,[&] { (void)library.rename_voice(first.library_id,"Blocked by reader"); });
            rejects(Code::library_busy,[&] { (void)library.cleanup_pending(); });
        }
        auto no_declaration=options; no_declaration.declared_model_compatibility_id.clear();
        const auto error=rejects(Code::import_failed,[&] { (void)library.import_pocket_voice(argv[1],"Missing declaration",no_declaration); });
        require(error.import_error() && error.import_error()->code()==VoiceImportErrorCode::compatibility_required,"Underlying importer error was flattened");
        require(fs::children(root/L".staging").empty(),"Failed/duplicate import left ordinary staging work");
        const auto second_path=second.voice.managed_olv_path,meta=second_path.parent_path()/L"metadata.json";
        const auto metadata_bytes=read(meta); const std::string original_metadata(metadata_bytes.begin(),metadata_bytes.end());
        auto health=[&](VoiceHealth expected) {
            const auto actual=library.inspect_voice(second.voice.library_id);
            if (actual.health!=expected) { std::cerr<<"Expected health "<<voice_health_name(expected)<<" got "<<voice_health_name(actual.health)<<": "<<actual.detail<<'\n'; }
            require(actual.health==expected,"Wrong voice health");
            const auto entries=library.list_voices();
            require(entries.size()==2 && std::any_of(entries.begin(),entries.end(),[&](const auto& v) { return v.library_id==first.library_id && v.health==VoiceHealth::valid; }),
                    "One corrupt entry broke enumeration");
        };
        write(meta,"{invalid JSON"); health(VoiceHealth::metadata_corrupt);
        const auto corrupt_duplicate=rejects(Code::duplicate_content,[&] { (void)library.import_olv(argv[2],"Duplicate with corrupt metadata"); });
        require(std::find(corrupt_duplicate.duplicate_ids().begin(),corrupt_duplicate.duplicate_ids().end(),second.voice.library_id)!=corrupt_duplicate.duplicate_ids().end(),
                "Duplicate detection trusted corrupt display metadata");
        write(meta,std::string(16385,' ')); health(VoiceHealth::metadata_corrupt);
        write(meta,replace(original_metadata,"\"schema_version\":1","\"schema_version\":2")); health(VoiceHealth::unsupported_metadata_version);
        rejects(Code::unsupported_metadata_version,[&] { (void)library.rename_voice(second.voice.library_id,"Unsupported"); });
        write(meta,replace(original_metadata,"{","{\"asset_path\":\"C:\\\\outside.olv\",")); health(VoiceHealth::metadata_corrupt);
        write(meta,replace(original_metadata,"gnome_male.olv","../outside.olv")); health(VoiceHealth::metadata_corrupt);
        write(meta,replace(original_metadata,second.voice.library_id,"../outside")); health(VoiceHealth::metadata_corrupt);
        write(meta,replace(original_metadata,"\"model_provenance_verified\":false","\"model_provenance_verified\":true")); health(VoiceHealth::metadata_corrupt);
        write(meta,replace(original_metadata,"\"schema_version\":1","\"schema_version\":1,\"schema_version\":1")); health(VoiceHealth::metadata_corrupt);
        write(meta,replace(original_metadata,second.voice.metadata->olv_sha256,std::string(64,'0'))); health(VoiceHealth::hash_mismatch);
        rejects(Code::corrupt_voice,[&] { (void)library.export_olv(second.voice.library_id,scratch.path/L"bad-export.olv"); });
        require(!std::filesystem::exists(scratch.path/L"bad-export.olv"),"Invalid managed source published an export");
        write(meta,metadata_bytes);
        std::filesystem::remove(second_path); health(VoiceHealth::missing_voice); write(second_path,original_olv);
        auto changed=original_olv; changed[256]^=1; write(second_path,changed); health(VoiceHealth::corrupt_voice);
        changed=original_olv; put(changed,8,2); write(second_path,changed); health(VoiceHealth::unsupported_voice_version);
        const auto invalid_source=scratch.path/L"unsupported.olv"; write(invalid_source,changed);
        const auto bad_version=rejects(Code::import_failed,[&] { (void)library.import_olv(invalid_source,"Unsupported voice"); });
        require(bad_version.import_error() && bad_version.import_error()->code()==VoiceImportErrorCode::olv_validation,"Unsupported voice lost production validation reason");
        changed=original_olv; changed[32]^=1;
        const auto digest=detail::asset_integrity(changed); std::copy(digest.begin(),digest.end(),changed.begin()+96);
        write(second_path,changed); health(VoiceHealth::incompatible_model);
        write(invalid_source,changed);
        const auto bad_model=rejects(Code::import_failed,[&] { (void)library.import_olv(invalid_source,"Incompatible voice"); });
        require(bad_model.import_error() && bad_model.import_error()->code()==VoiceImportErrorCode::incompatible_model,"Incompatible voice lost model error");
        write(second_path,original_olv);
        const auto foreign=scratch.path/L"external-copy.olv"; write(foreign,original_olv);
        std::filesystem::remove(second_path); std::filesystem::create_hard_link(foreign,second_path);
        health(VoiceHealth::unsafe_entry);
        DWORD handles_before=0,handles_after=0; GetProcessHandleCount(GetCurrentProcess(),&handles_before);
        rejects(Code::unsafe_path,[&] { (void)library.remove_voice(second.voice.library_id); });
        GetProcessHandleCount(GetCurrentProcess(),&handles_after);
        require(handles_before==handles_after,"Failed removal leaked a preflight handle");
        require(read(foreign)==original_olv,"Hard-link rejection modified external target");
        std::filesystem::remove(second_path); write(second_path,original_olv);
        health(VoiceHealth::valid);
        std::filesystem::remove(export_path); std::filesystem::create_hard_link(foreign,export_path);
        rejects(Code::unsafe_path,[&] { (void)library.export_olv(first.library_id,export_path,true); });
        std::filesystem::remove(export_path);
        health(VoiceHealth::valid);
        const auto outside=scratch.path/L"outside"; std::filesystem::create_directory(outside); write(outside/L"sentinel.txt","untouched");
        const auto link=root/L"voices"/L"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"; junction(link,outside);
        require(library.inspect_voice(std::string(32,'a')).health==VoiceHealth::unsafe_entry,"Junction entry was followed");
        rejects(Code::unsafe_path,[&] { (void)library.remove_voice(std::string(32,'a')); });
        require(fs::children(outside)==std::vector<std::wstring>{L"sentinel.txt"},"Junction remove escaped managed storage");
        require(RemoveDirectoryW(link.c_str())!=0,"Cannot remove junction fixture itself");
        health(VoiceHealth::valid);
        const auto export_link=scratch.path/L"export-junction"; junction(export_link,outside);
        rejects(Code::unsafe_path,[&] { (void)library.export_olv(first.library_id,export_link/L"voice.olv"); });
        rejects(Code::unsafe_path,[&] { VoiceLibrary bad(export_link/L"library"); });
        const auto staged_link=root/L".staging"; require(RemoveDirectoryW(staged_link.c_str())!=0,"Staging was not empty"); junction(staged_link,outside);
        rejects(Code::unsafe_path,[&] { (void)library.import_olv(argv[2],"Unsafe stage",DuplicateContentPolicy::allow_separate_entry); });
        require(RemoveDirectoryW(staged_link.c_str())!=0,"Cannot remove stage junction fixture"); std::filesystem::create_directory(staged_link);
        health(VoiceHealth::valid);
        {
            const auto locked=fs::file(second_path);
            rejects(Code::remove_failed,[&] { (void)library.remove_voice(second.voice.library_id); });
        }
        const auto after_locked_remove=library.inspect_voice(second.voice.library_id);
        if (after_locked_remove.health!=VoiceHealth::valid) {
            std::cerr<<"After locked remove: "<<voice_health_name(after_locked_remove.health)<<": "<<after_locked_remove.detail<<'\n';
        }
        require(after_locked_remove.health==VoiceHealth::valid,"Locked removal partially removed active entry");
        const auto staged=root/L".staging"/L"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"; std::filesystem::create_directory(staged);
        write(staged/L"voice.olv",original_olv); write(staged/L"metadata.json","interrupted metadata");
        {
            const auto locked=fs::file(staged/L"voice.olv"); const auto result=library.cleanup_pending();
            require(result.pending==std::vector<std::filesystem::path>{staged} && std::filesystem::exists(staged/L"metadata.json"),
                    "Locked recovery did not preflight before deletion");
        }
        require(library.cleanup_pending().cleaned==1 && !std::filesystem::exists(staged),"Interrupted import cleanup failed");
        const auto unexpected=root/L".trash"/L"cccccccccccccccccccccccccccccccc"; std::filesystem::create_directory(unexpected);
        write(unexpected/L"unknown.txt","retained");
        require(library.cleanup_pending().pending==std::vector<std::filesystem::path>{unexpected},"Recovery deleted unknown files");
        cleanup_owned(unexpected);
        // Simulate a crash after removal committed and the voice was deleted, but before metadata cleanup.
        const auto partial_trash=root/L".trash"/L"dddddddddddddddddddddddddddddddd";
        std::filesystem::create_directory(partial_trash); write(partial_trash/L"metadata.json",metadata_bytes);
        const auto recovered_removal=library.cleanup_pending();
        require(recovered_removal.cleaned==1 && recovered_removal.pending.empty() && !std::filesystem::exists(partial_trash) &&
                library.inspect_voice(first.library_id).health==VoiceHealth::valid,"Partial committed removal recovery failed");
        {
            const auto held=fs::file(second_path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE);
            try {
                const auto result=library.remove_voice(second.voice.library_id);
                require(!std::filesystem::exists(second_path.parent_path()),"Removal did not atomically depublish entry");
                std::cout<<"post-commit cleanup_pending="<<(result.cleanup_pending?"true":"false")<<'\n';
            } catch (const VoiceLibraryError& e) {
                require(e.code()==Code::remove_failed && library.inspect_voice(second.voice.library_id).health==VoiceHealth::valid,
                        "Directory rename denial damaged the active entry");
            }
        }
        if (std::filesystem::exists(second_path.parent_path())) { require(!library.remove_voice(second.voice.library_id).cleanup_pending,"Unlocked removal failed"); }
        require(library.cleanup_pending().pending.empty() && fs::children(root/L".trash").empty(),"Committed removal cleanup failed");
        require(library.inspect_voice(first.library_id).health==VoiceHealth::valid,"Removal affected another entry");
        // Separate API instances contend on the same OS lock. Winners get distinct, never overwritten IDs.
        const auto before_concurrent=library.list_voices().size(); std::barrier start(2);
        std::array<std::optional<LibraryImportResult>,2> imported; std::array<std::exception_ptr,2> errors;
        auto run=[&](std::size_t i) {
            start.arrive_and_wait();
            try { VoiceLibrary peer(root); imported[i]=peer.import_olv(argv[2],"Concurrent",DuplicateContentPolicy::allow_separate_entry); }
            catch (...) { errors[i]=std::current_exception(); }
        };
        std::thread one(run,0),two(run,1); one.join(); two.join(); std::size_t successes=0;
        for (std::size_t i=0;i<2;++i) {
            if (imported[i]) { ++successes; }
            else {
                try { std::rethrow_exception(errors[i]); }
                catch (const VoiceLibraryError& e) { require(e.code()==Code::library_busy,"Unexpected concurrent operation error"); }
            }
        }
        require(successes>=1 && library.list_voices().size()==before_concurrent+successes,"Concurrent import lost/overwrote an entry");
        if (successes==2) { require(imported[0]->voice.library_id!=imported[1]->voice.library_id,"Concurrent IDs collided"); }
        const auto removed=library.remove_voice(first.library_id); require(!removed.cleanup_pending,"Ordinary removal failed");
        rejects(Code::voice_not_found,[&] { (void)library.inspect_voice(first.library_id); });
        require(read(argv[1])==original_source && read(argv[2])==original_olv,"Import/remove changed original source assets");
        require(GetFileAttributesW(real_root.c_str())==real_before,"Test touched the real machine library");
        require(fs::children(outside)==std::vector<std::wstring>{L"sentinel.txt"},"Operation changed external sentinel directory");
        std::cout<<"PASS: "<<checks<<" voice library assertions; imports/exports exact Gnome bytes; IDs/rename; metadata/health; duplicates; locks; junctions/hard links/traversal; recovery/concurrency\n"
            "Real machine/user libraries, source assets, deployment and SAPI untouched\nRESULT: PASS\n";
        return 0;
    } catch (const VoiceLibraryError& error) {
        std::cerr<<voice_library_error_name(error.code())<<": "<<error.what()<<" system="<<error.system_error()<<'\n';
        std::wcerr<<error.path().native()<<L'\n'; return 1;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
