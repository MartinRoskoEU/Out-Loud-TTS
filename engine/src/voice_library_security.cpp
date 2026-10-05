#include "voice_library_security.hpp"
#include <aclapi.h>
#include <sddl.h>

namespace outloud::detail::library_security {
namespace {
namespace fs = library_fs;
using Code = VoiceLibraryErrorCode;
struct Local {
    PSECURITY_DESCRIPTOR value = nullptr;
    ~Local() { if (value) { LocalFree(value); } }
};
struct Sid {
    BYTE bytes[SECURITY_MAX_SID_SIZE]{};
    explicit Sid(WELL_KNOWN_SID_TYPE type) {
        DWORD length = sizeof(bytes);
        if (!CreateWellKnownSid(type,nullptr,bytes,&length)) {
            fs::fail(Code::library_unavailable,"Cannot build machine-library security SID",{},GetLastError());
        }
    }
};
}
FileCreationSecurity::FileCreationSecurity(bool machine) {
    if (!machine) { return; }
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(file_sddl,SDDL_REVISION_1,&descriptor,nullptr)) {
        fs::fail(Code::library_unavailable,"Cannot build machine file creation security",{},GetLastError());
    }
    attributes = {sizeof(attributes),descriptor,FALSE};
}
FileCreationSecurity::~FileCreationSecurity() { if (descriptor) { LocalFree(descriptor); } }
void protect_created_file(const std::filesystem::path& path) {
    require_machine_authority(path);
    FileCreationSecurity security(true);
    PSID owner = nullptr; BOOL defaulted = FALSE, present = FALSE; PACL dacl = nullptr;
    if (!GetSecurityDescriptorOwner(security.descriptor,&owner,&defaulted) ||
        !GetSecurityDescriptorDacl(security.descriptor,&present,&dacl,&defaulted) || !present) {
        fs::fail(Code::internal_failure,"Cannot decode staged file security",path,GetLastError());
    }
    const auto file = fs::file(path,READ_CONTROL|WRITE_DAC|WRITE_OWNER);
    const auto status = SetSecurityInfo(file.value,SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION|
        PROTECTED_DACL_SECURITY_INFORMATION,owner,nullptr,dacl,nullptr);
    if (status!=ERROR_SUCCESS) { fs::fail_access(Code::library_unavailable,"Cannot secure newly converted staged file",path,status); }
    validate(file.value,path,false);
}
bool valid_descriptor(PSECURITY_DESCRIPTOR descriptor,bool directory,bool anchor) {
    if (!descriptor || !IsValidSecurityDescriptor(descriptor)) { return false; }
    Sid system(WinLocalSystemSid), admins(WinBuiltinAdministratorsSid), users(WinBuiltinUsersSid);
    PSID owner = nullptr; BOOL defaulted = FALSE;
    if (!GetSecurityDescriptorOwner(descriptor,&owner,&defaulted) || !owner ||
        (!EqualSid(owner,system.bytes) && !EqualSid(owner,admins.bytes))) { return false; }
    SECURITY_DESCRIPTOR_CONTROL control{}; DWORD revision = 0;
    if (!GetSecurityDescriptorControl(descriptor,&control,&revision) || (anchor && !(control&SE_DACL_PROTECTED))) { return false; }
    PACL acl = nullptr; BOOL present = FALSE;
    if (!GetSecurityDescriptorDacl(descriptor,&present,&acl,&defaulted) || !present) { return false; }
    return valid_dacl(acl,directory);
}
bool valid_dacl(PACL acl,bool directory) {
    if (!acl || !IsValidAcl(acl) || acl->AceCount!=3) { return false; }
    Sid system(WinLocalSystemSid), admins(WinBuiltinAdministratorsSid), users(WinBuiltinUsersSid);
    bool found_system = false, found_admins = false, found_users = false;
    for (DWORD i = 0; i<acl->AceCount; ++i) {
        void* raw = nullptr; if (!GetAce(acl,i,&raw)) { return false; }
        const auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw);
        if (ace->Header.AceType!=ACCESS_ALLOWED_ACE_TYPE) { return false; }
        const auto expected_flags = directory ? OBJECT_INHERIT_ACE|CONTAINER_INHERIT_ACE : 0;
        if ((ace->Header.AceFlags&~INHERITED_ACE)!=expected_flags) { return false; }
        const auto sid = const_cast<DWORD*>(&ace->SidStart);
        if (!IsValidSid(sid)) { return false; }
        bool* found = nullptr; DWORD mask = 0;
        if (EqualSid(sid,system.bytes)) { found = &found_system; mask = FILE_ALL_ACCESS; }
        else if (EqualSid(sid,admins.bytes)) { found = &found_admins; mask = FILE_ALL_ACCESS; }
        else if (EqualSid(sid,users.bytes)) { found = &found_users; mask = FILE_GENERIC_READ|FILE_GENERIC_EXECUTE; }
        else { return false; }
        if (*found || ace->Mask!=mask) { return false; } *found = true;
    }
    return found_system && found_admins && found_users;
}
void validate(HANDLE handle,const std::filesystem::path& path,bool directory,bool anchor) {
    Local security;
    const auto status = GetSecurityInfo(handle,SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,
        nullptr,nullptr,nullptr,nullptr,&security.value);
    if (status!=ERROR_SUCCESS) { fs::fail_access(Code::library_unavailable,"Cannot read machine-library security",path,status); }
    if (!valid_descriptor(security.value,directory,anchor)) {
        fs::fail(Code::library_security_invalid,"Machine library requires SYSTEM/Administrators ownership and full control, Users read/execute only; review existing ACLs manually",path);
    }
}
void require_machine_authority(const std::filesystem::path& path) {
    Sid admins(WinBuiltinAdministratorsSid); BOOL member = FALSE;
    if (!CheckTokenMembership(nullptr,admins.bytes,&member)) {
        fs::fail(Code::library_access_denied,"Cannot determine machine-library authority",path,GetLastError());
    }
    // A filtered administrator token does not have the Administrators SID enabled.
    // SYSTEM is also accepted (normally already a member of Administrators).
    if (!member) {
        Sid system(WinLocalSystemSid);
        if (!CheckTokenMembership(nullptr,system.bytes,&member)) {
            fs::fail(Code::library_access_denied,"Cannot determine SYSTEM authority",path,GetLastError());
        }
    }
    if (!member) { fs::fail(Code::library_access_denied,"Machine-library mutation requires an elevated administrator; no automatic UAC",path,ERROR_ACCESS_DENIED); }
}
bool create_directory(const std::filesystem::path& path) {
    require_machine_authority(path);
    Local security;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(directory_sddl,SDDL_REVISION_1,&security.value,nullptr)) {
        fs::fail(Code::library_unavailable,"Cannot build machine-library ACL",path,GetLastError());
    }
    SECURITY_ATTRIBUTES attributes{sizeof(attributes),security.value,FALSE};
    const bool created = CreateDirectoryW(path.c_str(),&attributes)!=FALSE;
    if (!created && GetLastError()!=ERROR_ALREADY_EXISTS) {
        fs::fail_access(Code::library_unavailable,"Cannot create protected machine-library directory",path,GetLastError());
    }
    const auto pinned = fs::directory(path,READ_CONTROL); validate(pinned.value,path,true,true);
    return created;
}
fs::Pins prepare_root(const std::filesystem::path& path,bool mutate,bool create) {
    if (mutate) { require_machine_authority(path); }
    const auto parent = path.parent_path();
    // Production callers pass the known-folder-resolved OutLoud/VoiceLibrary.
    fs::Pins base(parent.parent_path());
    std::vector<fs::Handle> anchors;
    for (const auto& next : {parent,path}) {
        if (!fs::exists(next)) {
            if (!create) { return fs::Pins(path,false,true); }
            library_security::create_directory(next);
        }
        anchors.push_back(fs::directory(next,READ_CONTROL));
        validate(anchors.back().value,next,true,true);
    }
    return fs::Pins(path);
}
} // namespace outloud::detail::library_security
