#pragma once
#include <aclapi.h>
#include <sddl.h>
namespace outloud::sapi::logging {
// Best effort, invoked under the existing log mutex. Development/unbound logs
// retain their existing behavior and are never adopted by a production install.
inline void bind_installed_logs(const std::filesystem::path& directory){
    const auto safe_path=[](std::filesystem::path p){for(;;){const auto a=GetFileAttributesW(p.c_str());
        if(a!=INVALID_FILE_ATTRIBUTES && (a&FILE_ATTRIBUTE_REPARSE_POINT))return false;
        const auto parent=p.parent_path();if(parent==p || parent.empty())break;p=parent;}return true;};
    if(!safe_path(directory))return;
    HMODULE module{};if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&bind_installed_logs),&module))return;
    wchar_t file[32768]{};const auto count=GetModuleFileNameW(module,file,32768);if(!count || count>=32768)return;
    const auto root=std::filesystem::path(file).parent_path().parent_path();const auto receipt=root/L"configuration/signing.ini";
    if(!safe_path(receipt))return;
    PSECURITY_DESCRIPTOR descriptor{};PSID owner{};PACL dacl{};
    const auto status=GetNamedSecurityInfoW(receipt.c_str(),SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,&owner,nullptr,&dacl,nullptr,&descriptor);
    if(status!=ERROR_SUCCESS)return;
    struct Free {PSECURITY_DESCRIPTOR p;~Free(){LocalFree(p);}} free{descriptor};
    BYTE admins[SECURITY_MAX_SID_SIZE]{},system[SECURITY_MAX_SID_SIZE]{};DWORD n=sizeof(admins),m=sizeof(system);
    if(!CreateWellKnownSid(WinBuiltinAdministratorsSid,nullptr,admins,&n) || !CreateWellKnownSid(WinLocalSystemSid,nullptr,system,&m) ||
        !owner || (!EqualSid(owner,admins) && !EqualSid(owner,system)) || !dacl)return;
    for(DWORD i=0;i<dacl->AceCount;++i){void* raw{};if(!GetAce(dacl,i,&raw))return;const auto* ace=static_cast<ACCESS_ALLOWED_ACE*>(raw);
        if(ace->Header.AceType!=ACCESS_ALLOWED_ACE_TYPE)return;const auto sid=const_cast<DWORD*>(&ace->SidStart);
        if(!EqualSid(sid,admins) && !EqualSid(sid,system) && (ace->Mask&(FILE_GENERIC_WRITE|DELETE|WRITE_DAC|WRITE_OWNER)&~(READ_CONTROL|SYNCHRONIZE)))return;}
    wchar_t id[64]{},product[64]{},phase[64]{};
    GetPrivateProfileStringW(L"ownership",L"product",L"",product,64,receipt.c_str());GetPrivateProfileStringW(L"ownership",L"phase",L"",phase,64,receipt.c_str());
    GetPrivateProfileStringW(L"ownership",L"installation_id",L"",id,64,receipt.c_str());const std::wstring installation(id);
    if(std::wstring(product)!=L"OutLoud" || std::wstring(phase)!=L"verified" || installation.size()!=32 || installation.find_first_not_of(L"0123456789abcdef")!=installation.npos)return;
    const auto marker=directory/L".outloud-installation.dat";if(GetFileAttributesW(marker.c_str())!=INVALID_FILE_ATTRIBUTES)return;
    // Existing logs might come from another deployment. Never add ownership retroactively.
    for(const auto* name:{L"engine.log",L"engine.1.log",L"engine.2.log"})if(GetFileAttributesW((directory/name).c_str())!=INVALID_FILE_ATTRIBUTES)return;
    HANDLE dir=CreateFileW(directory.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(dir==INVALID_HANDLE_VALUE)return;struct Close {HANDLE h;~Close(){CloseHandle(h);}} close_dir{dir};FILE_ID_INFO identity{};
    if(!GetFileInformationByHandleEx(dir,FileIdInfo,&identity,sizeof(identity)))return;
    const auto hex=[](const unsigned char* bytes,size_t size){std::string s;for(size_t i=0;i<size;++i){s+="0123456789abcdef"[bytes[i]>>4];s+="0123456789abcdef"[bytes[i]&15];}return s;};
    std::string root_hex;for(const auto c:root.native()){const unsigned char bytes[]{static_cast<unsigned char>(c),static_cast<unsigned char>(c>>8)};root_hex+=hex(bytes,2);}
    std::string ascii_id;for(const auto c:installation)ascii_id+=static_cast<char>(c);
    const auto text="OutLoud.Logs.1\n"+ascii_id+"\n"+root_hex+"\n"+
        hex(reinterpret_cast<const unsigned char*>(&identity.VolumeSerialNumber),8)+hex(identity.FileId.Identifier,16)+"\n";
    HANDLE output=CreateFileW(marker.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);if(output==INVALID_HANDLE_VALUE)return;
    Close close_file{output};DWORD written{};(void)WriteFile(output,text.data(),static_cast<DWORD>(text.size()),&written,nullptr);
}
}
