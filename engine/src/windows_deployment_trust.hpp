#pragma once
#include <windows.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <sddl.h>
#include <algorithm>
#include <iterator>
#include <filesystem>
#include <string>
#include <string_view>

// Existing production deployment policy, shared without changing its trust or ACL rules.
namespace outloud::detail::deployment_trust {
inline constexpr wchar_t trusted_installer_sid[]=L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464";
struct Signature { LONG status{};std::wstring thumbprint,certificate_sha256,publisher; };
template<class Policy>
Signature verify_signature(const std::filesystem::path& path,HANDLE file,Policy& policy) {
    WINTRUST_FILE_INFO info{};info.cbStruct=sizeof(info);info.pcwszFilePath=path.c_str();info.hFile=file;
    WINTRUST_DATA data{};data.cbStruct=sizeof(data);data.dwUIChoice=WTD_UI_NONE;data.fdwRevocationChecks=WTD_REVOKE_WHOLECHAIN;
    data.dwUnionChoice=WTD_CHOICE_FILE;data.pFile=&info;data.dwStateAction=WTD_STATEACTION_VERIFY;
    data.dwProvFlags=WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT|WTD_DISABLE_MD2_MD4|WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID action=WINTRUST_ACTION_GENERIC_VERIFY_V2;Signature result;result.status=WinVerifyTrust(nullptr,&action,&data);
    try {
        if(result.status==ERROR_SUCCESS){auto provider=WTHelperProvDataFromStateData(data.hWVTStateData);
            auto signer=provider?WTHelperGetProvSignerFromChain(provider,0,FALSE,0):nullptr;
            policy.require(signer && signer->csCertChain && signer->pasCertChain[0].pCert,"signer_identity_missing");
            wchar_t publisher[256]{};CertGetNameStringW(signer->pasCertChain[0].pCert,CERT_NAME_SIMPLE_DISPLAY_TYPE,0,nullptr,publisher,256);result.publisher=publisher;
            result.thumbprint=policy.certificate_hash(signer->pasCertChain[0].pCert,CALG_SHA1);
            result.certificate_sha256=policy.certificate_hash(signer->pasCertChain[0].pCert,CALG_SHA_256);}
    }catch(...){data.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust(nullptr,&action,&data);throw;}
    data.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust(nullptr,&action,&data);return result;
}
inline bool actual_volume_root(HANDLE directory,DWORD& error){
    error=ERROR_SUCCESS;
    FILE_ATTRIBUTE_TAG_INFO attributes{};
    if(!GetFileInformationByHandleEx(directory,FileAttributeTagInfo,&attributes,sizeof(attributes))){error=GetLastError();return false;}
    if(!(attributes.FileAttributes&FILE_ATTRIBUTE_DIRECTORY) || (attributes.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
    wchar_t final_path[50]{};
    const auto length=GetFinalPathNameByHandleW(directory,final_path,50,FILE_NAME_NORMALIZED|VOLUME_NAME_GUID);
    if(!length){error=GetLastError();return false;}
    if(length!=49 || std::wstring_view(final_path,11)!=L"\\\\?\\Volume{" || final_path[47]!=L'}' || final_path[48]!=L'\\')return false;
    const auto opened=CreateFileW(final_path,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,
        OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(opened==INVALID_HANDLE_VALUE){error=GetLastError();return false;}
    struct Close { HANDLE value;~Close(){CloseHandle(value);} } root{opened};
    FILE_ID_INFO candidate_id{},root_id{};FILE_ATTRIBUTE_TAG_INFO root_attributes{};
    if(!GetFileInformationByHandleEx(directory,FileIdInfo,&candidate_id,sizeof(candidate_id)) ||
        !GetFileInformationByHandleEx(root.value,FileIdInfo,&root_id,sizeof(root_id)) ||
        !GetFileInformationByHandleEx(root.value,FileAttributeTagInfo,&root_attributes,sizeof(root_attributes))){error=GetLastError();return false;}
    return (root_attributes.FileAttributes&FILE_ATTRIBUTE_DIRECTORY) && !(root_attributes.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) &&
        candidate_id.VolumeSerialNumber==root_id.VolumeSerialNumber &&
        std::equal(std::begin(candidate_id.FileId.Identifier),std::end(candidate_id.FileId.Identifier),std::begin(root_id.FileId.Identifier));
}
template<class Policy>
void require_admin_descriptor(PSECURITY_DESCRIPTOR descriptor,bool ancestor,bool replacement_only,bool verified_root,Policy& policy){
    PSID owner{};PACL dacl{};BOOL defaulted{},present{};
    policy.require(policy.api(GetSecurityDescriptorOwner(descriptor,&owner,&defaulted),"GetSecurityDescriptorOwner") && owner &&
        policy.api(GetSecurityDescriptorDacl(descriptor,&present,&dacl,&defaulted),"GetSecurityDescriptorDacl") && present && dacl,"state_security_unreadable");
    policy.security(descriptor);
    struct Local { void* value{};~Local(){if(value)LocalFree(value);} } value;
    policy.require(policy.api(ConvertSidToStringSidW(owner,reinterpret_cast<LPWSTR*>(&value.value)),"ConvertSidToStringSidW"),"state_security_unreadable");
    const std::wstring name=static_cast<wchar_t*>(value.value);policy.owner(name);
    policy.require(name==L"S-1-5-18" || name==L"S-1-5-32-544" || (ancestor && name==trusted_installer_sid),"state_owner_invalid");
    const DWORD replacement=FILE_DELETE_CHILD|WRITE_DAC|WRITE_OWNER|GENERIC_ALL|((ancestor && replacement_only && verified_root)?0:DELETE);
    const DWORD writes=replacement|(replacement_only?0:FILE_WRITE_DATA|FILE_APPEND_DATA|FILE_WRITE_EA|FILE_WRITE_ATTRIBUTES|GENERIC_WRITE);
    for(DWORD i=0;i<dacl->AceCount;++i){void* entry{};policy.require(policy.api(GetAce(dacl,i,&entry),"GetAce"),"state_acl_invalid");
        auto header=static_cast<ACE_HEADER*>(entry);policy.ace(i,header);if(header->AceFlags&INHERIT_ONLY_ACE)continue;
        if(header->AceType==ACCESS_DENIED_ACE_TYPE)continue;
        policy.require(header->AceType==ACCESS_ALLOWED_ACE_TYPE,"state_acl_invalid");auto ace=static_cast<ACCESS_ALLOWED_ACE*>(entry);
        if(ace->Mask&writes){Local principal;policy.require(policy.api(ConvertSidToStringSidW(&ace->SidStart,reinterpret_cast<LPWSTR*>(&principal.value)),"ConvertSidToStringSidW"),"state_acl_invalid");
            const std::wstring identity=static_cast<wchar_t*>(principal.value);policy.principal(identity);
            policy.require(identity==L"S-1-5-18" || identity==L"S-1-5-32-544" || (ancestor && identity==trusted_installer_sid),"state_acl_writable_by_user");}
    }
}
}
