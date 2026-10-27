#include <windows.h>
#include <wincrypt.h>
#include <ncrypt.h>
#include <bcrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <mssip.h>
#include <mscat.h>
#include <set>
#include <cwctype>
#include <sddl.h>
#include <aclapi.h>
#include <shlobj.h>
#include <filesystem>
#include <vector>
#include <array>
#include <iostream>
#include <algorithm>
#include <memory>
#include <functional>
#include <sstream>
#include <string_view>
#include <RestartManager.h>
#include <winver.h>
#include "maintenance_codec.hpp"
#include "uninstall.hpp"
#include "signing_policy.hpp"
#include "signer_api.hpp"
#include "registration_policy.hpp"
#include "installation_path.hpp"
#include <ktmw32.h>
namespace {
namespace fs=std::filesystem;
using namespace outloud::installer;
__declspec(noinline) bool mutation_gate(){return signing_lifecycle_ready;}
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    ~Handle(){if(value!=INVALID_HANDLE_VALUE) CloseHandle(value);}
    Handle()=default; Handle(const Handle&)=delete; Handle& operator=(const Handle&)=delete;
};
struct Store { HCERTSTORE value{}; ~Store(){if(value) CertCloseStore(value,0);} };
struct Certificate { PCCERT_CONTEXT value{}; ~Certificate(){if(value) CertFreeCertificateContext(value);} };
struct Key { NCRYPT_HANDLE value{}; ~Key(){if(value) NCryptFreeObject(value);} };
struct Local { void* value{}; ~Local(){if(value) LocalFree(value);} };
#include "installation_diagnostics.inc"
void safe(const fs::path& p) {
    inspection_path(p);
    const auto text=p.native();
    signing_require(p.is_absolute() && !text.starts_with(L"\\\\") && text.size()>=3 &&
        text.substr(2).find(L':')==std::wstring::npos && text.find_first_of(L"\r\n\"")==std::wstring::npos && text.find(L'\0')==text.npos,"unsafe_path");
    for(auto current=p;!current.empty();) {
        inspection_path(current);
        const auto a=GetFileAttributesW(current.c_str());
        if(a==INVALID_FILE_ATTRIBUTES){const auto error=GetLastError();
            if(error!=ERROR_FILE_NOT_FOUND && error!=ERROR_PATH_NOT_FOUND)inspection_error("GetFileAttributesW",error);
            signing_require(error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND,"path_unreadable");}
        else if(a&FILE_ATTRIBUTE_REPARSE_POINT)inspection_reparse(current,a);
        signing_require(a==INVALID_FILE_ATTRIBUTES || !(a&FILE_ATTRIBUTE_REPARSE_POINT),"unsafe_reparse_path");
        const auto parent=current.parent_path(); if(parent==current) break; current=parent;
    }
    inspection_path(p);
}
void pin(const fs::path& p,Handle& h,bool writable=false) {
    safe(p); h.value=CreateFileW(p.c_str(),GENERIC_READ|(writable?GENERIC_WRITE:0),FILE_SHARE_READ,nullptr,
        OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(h.value==INVALID_HANDLE_VALUE)inspection_error("CreateFileW",GetLastError());
    signing_require(h.value!=INVALID_HANDLE_VALUE,"file_inaccessible_or_locked");
    BY_HANDLE_FILE_INFORMATION info{};
    signing_require(inspection_api(GetFileInformationByHandle(h.value,&info),"GetFileInformationByHandle") && info.nNumberOfLinks==1 &&
        !(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)),"unsafe_file_identity");
}
std::wstring hex(const BYTE* p,size_t count) {
    std::wstring result;
    for(size_t i=0;i<count;++i){result+=L"0123456789abcdef"[p[i]>>4];result+=L"0123456789abcdef"[p[i]&15];}
    return result;
}
std::wstring file_hash(HANDLE file) {
    BCRYPT_ALG_HANDLE algorithm{}; BCRYPT_HASH_HANDLE hash{};
    signing_require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"hash_provider_failed");
    try {
        signing_require(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0,"hash_failed");
        LARGE_INTEGER zero{}; signing_require(SetFilePointerEx(file,zero,nullptr,FILE_BEGIN)!=0,"file_read_failed");
        std::array<BYTE,65536> bytes{}; DWORD read{};
        for(;;){signing_require(ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr)!=0,"file_read_failed");
            if(!read)break; signing_require(BCryptHashData(hash,bytes.data(),read,0)>=0,"hash_failed");}
        std::array<BYTE,32> digest{};signing_require(BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)>=0,"hash_failed");
        BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);return hex(digest.data(),digest.size());
    }catch(...){if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);throw;}
}
std::wstring cert_hash(PCCERT_CONTEXT c,ALG_ID algorithm) {
    std::array<BYTE,32> hash{};DWORD size=static_cast<DWORD>(hash.size());
    signing_require(CryptHashCertificate(0,algorithm,0,c->pbCertEncoded,c->cbCertEncoded,hash.data(),&size)!=0,"certificate_hash_failed");
    return hex(hash.data(),size);
}
void write_new(const fs::path& path,const std::wstring& text,bool protected_state=false) {
    safe(path);signing_require(fs::is_directory(path.parent_path()),"report_parent_missing");
    Local descriptor;SECURITY_ATTRIBUTES attributes{};attributes.nLength=sizeof(attributes);
    if(protected_state){signing_require(ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FR;;;BU)",SDDL_REVISION_1,
        reinterpret_cast<PSECURITY_DESCRIPTOR*>(&descriptor.value),nullptr),"state_acl_failed");attributes.lpSecurityDescriptor=descriptor.value;}
    Handle file;file.value=CreateFileW(path.c_str(),GENERIC_WRITE,0,protected_state?&attributes:nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file.value==INVALID_HANDLE_VALUE)inspection_error("CreateFileW",GetLastError());
    signing_require(file.value!=INVALID_HANDLE_VALUE,"report_already_exists_or_denied");
    // Reports contain ASCII identifiers only; no keys, user names or secret material.
    std::string bytes;for(auto ch:text){signing_require(ch<128,"report_encoding_failed");bytes+=static_cast<char>(ch);}
    DWORD written{};signing_require(inspection_api(WriteFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr),"WriteFile") &&
        written==bytes.size() && inspection_api(FlushFileBuffers(file.value),"FlushFileBuffers"),"report_write_failed");
}
std::wstring ini(const fs::path& path,const wchar_t* section,const wchar_t* key,bool optional=false) {
    inspection_path(path);
    wchar_t text[256]{};const auto n=GetPrivateProfileStringW(section,key,L"",text,256,path.c_str());
    signing_require(n<255 && (optional || n>0),"metadata_missing_or_invalid");return text;
}
std::wstring sip_digest(const fs::path& path,HANDLE file) {
    GUID guid{};signing_require(CryptSIPRetrieveSubjectGuid(path.c_str(),file,&guid),"sip_subject_failed");
    SIP_SUBJECTINFO subject{};subject.cbSize=sizeof(subject);subject.pgSubjectType=&guid;subject.hFile=file;
    subject.pwsFileName=path.c_str();subject.DigestAlgorithm.pszObjId=const_cast<char*>(szOID_NIST_sha256);
    subject.dwEncodingType=X509_ASN_ENCODING|PKCS_7_ASN_ENCODING;subject.dwFlags=SPC_EXC_PE_PAGE_HASHES_FLAG;
    DWORD size{};signing_require(CryptSIPCreateIndirectData(&subject,&size,nullptr)!=0 && size<65536,"sip_digest_failed");
    std::vector<BYTE> bytes(size);auto data=reinterpret_cast<SIP_INDIRECT_DATA*>(bytes.data());
    signing_require(CryptSIPCreateIndirectData(&subject,&size,data)!=0 && data->Digest.cbData==32 &&
        std::string(data->DigestAlgorithm.pszObjId)==szOID_NIST_sha256,"sip_digest_failed");
    return hex(data->Digest.pbData,data->Digest.cbData);
}
struct Signature { LONG status{};std::wstring thumbprint,certificate_sha256,publisher; };
Signature verify_signature(const fs::path& path,HANDLE file) {
    WINTRUST_FILE_INFO info{};info.cbStruct=sizeof(info);info.pcwszFilePath=path.c_str();info.hFile=file;
    WINTRUST_DATA data{};data.cbStruct=sizeof(data);data.dwUIChoice=WTD_UI_NONE;data.fdwRevocationChecks=WTD_REVOKE_WHOLECHAIN;
    data.dwUnionChoice=WTD_CHOICE_FILE;data.pFile=&info;data.dwStateAction=WTD_STATEACTION_VERIFY;
    data.dwProvFlags=WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT|WTD_DISABLE_MD2_MD4|WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID action=WINTRUST_ACTION_GENERIC_VERIFY_V2;Signature result;result.status=WinVerifyTrust(nullptr,&action,&data);
    try {
        if(result.status==ERROR_SUCCESS){auto provider=WTHelperProvDataFromStateData(data.hWVTStateData);
            auto signer=provider?WTHelperGetProvSignerFromChain(provider,0,FALSE,0):nullptr;
            signing_require(signer && signer->csCertChain && signer->pasCertChain[0].pCert,"signer_identity_missing");
            wchar_t publisher[256]{};CertGetNameStringW(signer->pasCertChain[0].pCert,CERT_NAME_SIMPLE_DISPLAY_TYPE,0,nullptr,publisher,256);result.publisher=publisher;
            result.thumbprint=cert_hash(signer->pasCertChain[0].pCert,CALG_SHA1);
            result.certificate_sha256=cert_hash(signer->pasCertChain[0].pCert,CALG_SHA_256);}
    }catch(...){data.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust(nullptr,&action,&data);throw;}
    data.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust(nullptr,&action,&data);return result;
}
#include "signing_dependencies.inc"
bool system_admin_dacl(PSECURITY_DESCRIPTOR descriptor,const std::wstring& sid) {
    PACL acl{};BOOL present{},defaulted{};SECURITY_DESCRIPTOR_CONTROL control{};DWORD revision{};
    if(!GetSecurityDescriptorDacl(descriptor,&present,&acl,&defaulted) || !present || !acl || acl->AceCount!=2 ||
        !GetSecurityDescriptorControl(descriptor,&control,&revision) || !(control&SE_DACL_PROTECTED))return false;
    PSID owner{};BOOL owner_defaulted{};Local owner_text;
    if(!GetSecurityDescriptorOwner(descriptor,&owner,&owner_defaulted) || !owner ||
        !ConvertSidToStringSidW(owner,reinterpret_cast<LPWSTR*>(&owner_text.value)))return false;
    const std::wstring owner_identity=static_cast<wchar_t*>(owner_text.value);
    if(owner_identity!=sid && owner_identity!=L"S-1-5-18")return false;
    bool user=false,system=false;
    for(DWORD i=0;i<acl->AceCount;++i){void* entry{};if(!GetAce(acl,i,&entry))return false;
        auto ace=static_cast<ACCESS_ALLOWED_ACE*>(entry);if(ace->Header.AceType!=ACCESS_ALLOWED_ACE_TYPE || ace->Header.AceFlags)return false;
        if((ace->Mask&GENERIC_ALL)!=GENERIC_ALL && (ace->Mask&FILE_ALL_ACCESS)!=FILE_ALL_ACCESS)return false;
        Local value;if(!ConvertSidToStringSidW(&ace->SidStart,reinterpret_cast<LPWSTR*>(&value.value)))return false;
        const std::wstring identity=static_cast<wchar_t*>(value.value);
        if(identity==sid)user=true;else if(identity==L"S-1-5-18")system=true;else return false;
    }return user && system;
}
inline constexpr wchar_t trusted_installer_sid[]=L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464";
// Resolve an opened object through the kernel, then independently open the
// returned volume GUID root and compare physical identities. A drive alias to
// an ordinary directory must never receive the volume-root DELETE exception.
bool actual_volume_root(HANDLE directory,DWORD& error){
    error=ERROR_SUCCESS;
    FILE_ATTRIBUTE_TAG_INFO attributes{};
    if(!GetFileInformationByHandleEx(directory,FileAttributeTagInfo,&attributes,sizeof(attributes))){error=GetLastError();return false;}
    if(!(attributes.FileAttributes&FILE_ATTRIBUTE_DIRECTORY) || (attributes.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
    wchar_t final_path[50]{};
    const auto length=GetFinalPathNameByHandleW(directory,final_path,50,FILE_NAME_NORMALIZED|VOLUME_NAME_GUID);
    if(!length){error=GetLastError();return false;}
    // A GUID root is exactly \\?\Volume{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}\.
    // Longer resolved names (including SUBST targets) are ordinary directories.
    if(length!=49 || std::wstring_view(final_path,11)!=L"\\\\?\\Volume{" || final_path[47]!=L'}' || final_path[48]!=L'\\')return false;
    Handle root;root.value=CreateFileW(final_path,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,
        OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(root.value==INVALID_HANDLE_VALUE){error=GetLastError();return false;}
    FILE_ID_INFO candidate_id{},root_id{};FILE_ATTRIBUTE_TAG_INFO root_attributes{};
    if(!GetFileInformationByHandleEx(directory,FileIdInfo,&candidate_id,sizeof(candidate_id)) ||
        !GetFileInformationByHandleEx(root.value,FileIdInfo,&root_id,sizeof(root_id)) ||
        !GetFileInformationByHandleEx(root.value,FileAttributeTagInfo,&root_attributes,sizeof(root_attributes))){error=GetLastError();return false;}
    return (root_attributes.FileAttributes&FILE_ATTRIBUTE_DIRECTORY) && !(root_attributes.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) &&
        candidate_id.VolumeSerialNumber==root_id.VolumeSerialNumber &&
        std::equal(std::begin(candidate_id.FileId.Identifier),std::end(candidate_id.FileId.Identifier),std::begin(root_id.FileId.Identifier));
}
void require_admin_descriptor(PSECURITY_DESCRIPTOR descriptor,bool ancestor,bool replacement_only,bool verified_root){
    PSID owner{};PACL dacl{};BOOL defaulted{},present{};
    signing_require(inspection_api(GetSecurityDescriptorOwner(descriptor,&owner,&defaulted),"GetSecurityDescriptorOwner") && owner &&
        inspection_api(GetSecurityDescriptorDacl(descriptor,&present,&dacl,&defaulted),"GetSecurityDescriptorDacl") && present && dacl,"state_security_unreadable");
    inspection_security(descriptor);
    Local value;signing_require(inspection_api(ConvertSidToStringSidW(owner,reinterpret_cast<LPWSTR*>(&value.value)),"ConvertSidToStringSidW"),"state_security_unreadable");
    const std::wstring name=static_cast<wchar_t*>(value.value);
    if(installation_diagnostic)installation_diagnostic->owner=name;
    signing_require(name==L"S-1-5-18" || name==L"S-1-5-32-544" || (ancestor && name==trusted_installer_sid),"state_owner_invalid");
    const DWORD replacement=FILE_DELETE_CHILD|WRITE_DAC|WRITE_OWNER|GENERIC_ALL|
        ((ancestor && replacement_only && verified_root)?0:DELETE);
    const DWORD writes=replacement|(replacement_only?0:FILE_WRITE_DATA|FILE_APPEND_DATA|FILE_WRITE_EA|FILE_WRITE_ATTRIBUTES|GENERIC_WRITE);
    for(DWORD i=0;i<dacl->AceCount;++i){void* entry{};signing_require(inspection_api(GetAce(dacl,i,&entry),"GetAce"),"state_acl_invalid");
        auto header=static_cast<ACE_HEADER*>(entry);inspection_ace(i,header);if(header->AceFlags&INHERIT_ONLY_ACE)continue;
        if(header->AceType==ACCESS_DENIED_ACE_TYPE)continue;
        signing_require(header->AceType==ACCESS_ALLOWED_ACE_TYPE,"state_acl_invalid");auto ace=static_cast<ACCESS_ALLOWED_ACE*>(entry);
        if(ace->Mask&writes){Local principal;signing_require(inspection_api(ConvertSidToStringSidW(&ace->SidStart,reinterpret_cast<LPWSTR*>(&principal.value)),"ConvertSidToStringSidW"),"state_acl_invalid");
            const std::wstring identity=static_cast<wchar_t*>(principal.value);
            if(installation_diagnostic)installation_diagnostic->ace_sid=identity;
            signing_require(identity==L"S-1-5-18" || identity==L"S-1-5-32-544" || (ancestor && identity==trusted_installer_sid),"state_acl_writable_by_user");}
    }
}
void require_admin_protected(const fs::path& path,bool ancestor=false,bool replacement_only=false) {
    if(installation_diagnostic){const auto phase=installation_diagnostic->phase;inspection_stage(phase.c_str(),path);}
    safe(path);Local descriptor;Handle directory;bool verified_root=false;
    if(ancestor && replacement_only){
        directory.value=CreateFileW(path.c_str(),READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,
            OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        DWORD error=directory.value==INVALID_HANDLE_VALUE?GetLastError():ERROR_SUCCESS;
        if(directory.value!=INVALID_HANDLE_VALUE)verified_root=actual_volume_root(directory.value,error);
        if(installation_diagnostic){installation_diagnostic->volume_root_verified=verified_root?1:0;
            installation_diagnostic->volume_root_error=error;}
    }
    // Bind the ACL to the exact object whose root identity was verified. If any
    // proof step fails, keep the original named-object policy including DELETE.
    const auto status=verified_root?
        inspection_status(GetSecurityInfo(directory.value,SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,
            nullptr,nullptr,nullptr,nullptr,reinterpret_cast<PSECURITY_DESCRIPTOR*>(&descriptor.value)),"GetSecurityInfo"):
        inspection_status(GetNamedSecurityInfoW(path.c_str(),SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,
            nullptr,nullptr,nullptr,nullptr,reinterpret_cast<PSECURITY_DESCRIPTOR*>(&descriptor.value)),"GetNamedSecurityInfoW");
    signing_require(status==ERROR_SUCCESS && descriptor.value,"state_security_unreadable");
    require_admin_descriptor(descriptor.value,ancestor,replacement_only,verified_root);
}
void require_installation_location(const fs::path& root) {
    inspection_stage("path_attributes",root);
    safe(root);
    const auto volume=root.root_path();
    inspection_stage("volume",volume);
    signing_require(GetDriveTypeW(volume.c_str())==DRIVE_FIXED,"installation_requires_fixed_local_drive");
    wchar_t filesystem[MAX_PATH]{};
    signing_require(inspection_api(GetVolumeInformationW(volume.c_str(),nullptr,0,nullptr,nullptr,nullptr,filesystem,MAX_PATH),"GetVolumeInformationW") &&
        _wcsicmp(filesystem,L"NTFS")==0,"installation_requires_ntfs");
    auto existing=root;
    inspection_stage("parent_search",existing);
    while(!fs::exists(existing)){const auto parent=existing.parent_path();signing_require(parent!=existing,"installation_parent_missing");existing=parent;inspection_path(existing);}
    signing_require(fs::is_directory(existing),"installation_parent_not_directory");
    inspection_stage("directory_security",existing);
    require_admin_protected(existing,true);
    // New directories must not inherit ordinary-user write rights.
    Local descriptor;PACL dacl{};
    const auto inspected_owner=installation_diagnostic?installation_diagnostic->owner:std::wstring{};
    inspection_stage("inherited_acl",existing);
    if(installation_diagnostic)installation_diagnostic->owner=inspected_owner;
    signing_require(inspection_status(GetNamedSecurityInfoW(existing.c_str(),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,
        nullptr,nullptr,&dacl,nullptr,reinterpret_cast<PSECURITY_DESCRIPTOR*>(&descriptor.value)),"GetNamedSecurityInfoW")==ERROR_SUCCESS && dacl,"state_security_unreadable");
    inspection_security(descriptor.value);
    {
        constexpr DWORD writes=FILE_WRITE_DATA|FILE_APPEND_DATA|FILE_DELETE_CHILD|FILE_WRITE_EA|FILE_WRITE_ATTRIBUTES|DELETE|WRITE_DAC|WRITE_OWNER|GENERIC_WRITE|GENERIC_ALL;
        for(DWORD i=0;i<dacl->AceCount;++i){void* entry{};signing_require(inspection_api(GetAce(dacl,i,&entry),"GetAce"),"state_acl_invalid");
            auto ace=static_cast<ACCESS_ALLOWED_ACE*>(entry);
            inspection_ace(i,&ace->Header);
            if(ace->Header.AceType==ACCESS_DENIED_ACE_TYPE)continue;
            signing_require(ace->Header.AceType==ACCESS_ALLOWED_ACE_TYPE,"state_acl_invalid");
            if((ace->Header.AceFlags&(OBJECT_INHERIT_ACE|CONTAINER_INHERIT_ACE)) && (ace->Mask&writes)){
                Local principal;signing_require(inspection_api(ConvertSidToStringSidW(&ace->SidStart,reinterpret_cast<LPWSTR*>(&principal.value)),"ConvertSidToStringSidW"),"state_acl_invalid");
                const std::wstring who=static_cast<wchar_t*>(principal.value);
                if(installation_diagnostic)installation_diagnostic->ace_sid=who;
                signing_require(who==L"S-1-5-18" || who==L"S-1-5-32-544" || who==L"S-1-3-0" || who==trusted_installer_sid,"installation_inheritance_writable_by_user");
            }
        }
    }
    for(auto current=existing.parent_path();!current.empty();){
        inspection_stage("ancestor_security",current);
        require_admin_protected(current,true,true);
        const auto parent=current.parent_path();if(parent==current)break;current=parent;
    }
}
std::vector<BYTE> encode(LPCSTR type,const void* value) {
    DWORD size{};signing_require(CryptEncodeObjectEx(X509_ASN_ENCODING,type,value,0,nullptr,nullptr,&size),"certificate_extension_failed");
    std::vector<BYTE> bytes(size);signing_require(CryptEncodeObjectEx(X509_ASN_ENCODING,type,value,0,nullptr,bytes.data(),&size),"certificate_extension_failed");return bytes;
}
std::vector<BYTE> key_property(NCRYPT_KEY_HANDLE key,const wchar_t* name,DWORD flags=0) {
    DWORD size{};signing_require(NCryptGetProperty(key,name,nullptr,0,&size,flags)==ERROR_SUCCESS && size>0 && size<65536,"private_key_property_failed");
    std::vector<BYTE> bytes(size);signing_require(NCryptGetProperty(key,name,bytes.data(),size,&size,flags)==ERROR_SUCCESS,"private_key_property_failed");return bytes;
}
DWORD key_number(NCRYPT_KEY_HANDLE key,const wchar_t* name) {
    const auto bytes=key_property(key,name);signing_require(bytes.size()==sizeof(DWORD),"private_key_property_failed");DWORD value{};memcpy(&value,bytes.data(),sizeof(value));return value;
}
struct WindowsSigning {
    fs::path root,policy,target,state,pending;std::wstring owner,expected_hash,expected_digest;
    SigningOwnership record;Handle file,policy_pin;Key provider,key;Certificate certificate;Store personal,trust;
    bool fresh{},already_signed{};
    explicit WindowsSigning(const fs::path& app,const fs::path& manifest,const std::wstring& sid,bool is_fresh):
        root(installation_path(app)),policy(manifest),target((root/signing_target).lexically_normal()),state(root/L"configuration/signing.ini"),
        pending(root/L"configuration/signing.pending.ini"),owner(sid),fresh(is_fresh) {}
    const SigningOwnership& ownership()const{return record;} const std::wstring& sid()const{return owner;}
    void preflight(bool writable=true){
        signing_require(IsUserAnAdmin() && owner==L"S-1-5-32-544","elevated_machine_authority_required");
        OSVERSIONINFOEXW version{};version.dwOSVersionInfoSize=sizeof(version);
        using RtlVersion=LONG(WINAPI*)(OSVERSIONINFOEXW*);auto rtl=reinterpret_cast<RtlVersion>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion"));
        SYSTEM_INFO system{};GetNativeSystemInfo(&system);
        signing_require(rtl && rtl(&version)==0 && version.dwMajorVersion==10 && version.dwMinorVersion==0 && version.dwBuildNumber>=22000 &&
            version.wProductType==VER_NT_WORKSTATION && system.wProcessorArchitecture==PROCESSOR_ARCHITECTURE_AMD64,"unsupported_windows");
        require_installation_location(root);
        require_admin_protected(root);require_admin_protected(target.parent_path());require_admin_protected(target);
        const auto directory=state.parent_path();require_admin_protected(directory);
        signing_require(!fs::exists(pending) && !fs::exists(root/L"configuration/signing.previous.ini"),"recovery_required");
        signing_require(fresh?!fs::exists(state):fs::exists(state),"ownership_state_missing_or_conflicting");
        pin(policy,policy_pin);signing_require(ini(policy,L"signing",L"schema")==L"1" &&
            ini(policy,L"signing",L"target")==signing_target && ini(policy,L"signing",L"scope")==L"LocalMachine" &&
            ini(policy,L"signing",L"stores")==L"My,Root","signing_policy_invalid");
        expected_hash=ini(policy,L"signing",L"unsigned_sha256");expected_digest=ini(policy,L"signing",L"sip_sha256");
        signing_require(hexadecimal(expected_hash,64) && hexadecimal(expected_digest,64),"signing_policy_invalid");
        pin(target,file,writable);IMAGE_DOS_HEADER dos{};DWORD read{};LARGE_INTEGER offset{};
        signing_require(ReadFile(file.value,&dos,sizeof(dos),&read,nullptr) && read==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE,"target_not_pe");
        offset.QuadPart=dos.e_lfanew;signing_require(offset.QuadPart>0 && SetFilePointerEx(file.value,offset,nullptr,FILE_BEGIN),"target_not_pe");
        IMAGE_NT_HEADERS64 pe{};signing_require(ReadFile(file.value,&pe,sizeof(pe),&read,nullptr) && read==sizeof(pe) && pe.Signature==IMAGE_NT_SIGNATURE &&
            pe.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64 && pe.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC,"target_not_amd64");
        signing_require(sip_digest(target,file.value)==expected_digest,"target_code_identity_mismatch");
        const auto signature=verify_signature(target,file.value);
        already_signed=signature.status==ERROR_SUCCESS;
        signing_require(!already_signed || !fresh,"fresh_target_already_signed");
        if(!already_signed) signing_require(signature.status==TRUST_E_NOSIGNATURE && file_hash(file.value)==expected_hash,"target_unsigned_identity_mismatch");
        signing_require(NCryptOpenStorageProvider(&provider.value,MS_KEY_STORAGE_PROVIDER,0)==ERROR_SUCCESS,"key_provider_failed");
    }
    void verify_dependencies(){static_cast<void>(dependency_inventory(target,file.value));}
    void new_ownership(){
        std::array<BYTE,16> random{};signing_require(BCryptGenRandom(nullptr,random.data(),static_cast<ULONG>(random.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,"identity_generation_failed");
        record.id=hex(random.data(),random.size());record.owner_sid=owner;record.key_name=std::wstring(key_prefix)+record.id;
    }
    std::wstring metadata(const char* phase)const{
        std::wstring step;while(*phase)step+=static_cast<wchar_t>(*phase++);
        return L"[ownership]\nschema=1\nproduct=OutLoud\nscope=LocalMachine\nstores=My,Root\nprovider=Microsoft Software Key Storage Provider\nphase="+step+
            L"\ninstallation_id="+record.id+L"\nowner_sid="+record.owner_sid+L"\nkey_name="+record.key_name+
            L"\nthumbprint="+record.thumbprint+L"\ncertificate_sha256="+record.certificate_sha256+L"\nsigned_sha256="+record.signed_sha256+L"\n";
    }
    void checkpoint(const char* phase){
        const auto next=root/L"configuration/signing.next.ini";signing_require(!fs::exists(next),"recovery_required");
        write_new(next,metadata(phase),true);require_admin_protected(next);
        signing_require(MoveFileExW(next.c_str(),pending.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0,"journal_publication_failed");
    }
    void generate_key(){
        signing_require(NCryptCreatePersistedKey(provider.value,&key.value,BCRYPT_RSA_ALGORITHM,record.key_name.c_str(),0,NCRYPT_MACHINE_KEY_FLAG)==ERROR_SUCCESS,"key_generation_failed");
        DWORD bits=3072,export_policy=0,usage=NCRYPT_ALLOW_SIGNING_FLAG;
        signing_require(NCryptSetProperty(key.value,NCRYPT_LENGTH_PROPERTY,reinterpret_cast<BYTE*>(&bits),sizeof(bits),0)==ERROR_SUCCESS &&
            NCryptSetProperty(key.value,NCRYPT_EXPORT_POLICY_PROPERTY,reinterpret_cast<BYTE*>(&export_policy),sizeof(export_policy),0)==ERROR_SUCCESS &&
            NCryptSetProperty(key.value,NCRYPT_KEY_USAGE_PROPERTY,reinterpret_cast<BYTE*>(&usage),sizeof(usage),0)==ERROR_SUCCESS,"key_configuration_failed");
        Local descriptor;const auto sddl=L"O:BAG:BAD:P(A;;GA;;;SY)(A;;GA;;;BA)";
        signing_require(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl,SDDL_REVISION_1,
            reinterpret_cast<PSECURITY_DESCRIPTOR*>(&descriptor.value),nullptr),"key_acl_failed");
        signing_require(NCryptFinalizeKey(key.value,NCRYPT_SILENT_FLAG)==ERROR_SUCCESS &&
            NCryptSetProperty(key.value,NCRYPT_SECURITY_DESCR_PROPERTY,static_cast<BYTE*>(descriptor.value),
                GetSecurityDescriptorLength(descriptor.value),OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION|NCRYPT_PERSIST_FLAG)==ERROR_SUCCESS,"key_generation_or_acl_failed");
    }
    void generate_certificate(){
        const auto subject=L"CN=Out Loud Local Signing "+record.id;DWORD size{};
        signing_require(CertStrToNameW(X509_ASN_ENCODING,subject.c_str(),CERT_X500_NAME_STR,nullptr,nullptr,&size,nullptr),"certificate_subject_failed");
        std::vector<BYTE> name(size);signing_require(CertStrToNameW(X509_ASN_ENCODING,subject.c_str(),CERT_X500_NAME_STR,nullptr,name.data(),&size,nullptr),"certificate_subject_failed");
        CERT_NAME_BLOB blob{size,name.data()};CRYPT_KEY_PROV_INFO key_info{};key_info.pwszContainerName=record.key_name.data();
        key_info.pwszProvName=const_cast<wchar_t*>(MS_KEY_STORAGE_PROVIDER);key_info.dwKeySpec=CERT_NCRYPT_KEY_SPEC;key_info.dwFlags=CRYPT_MACHINE_KEYSET;
        auto oid=const_cast<char*>(szOID_PKIX_KP_CODE_SIGNING);CERT_ENHKEY_USAGE eku{1,&oid};BYTE flag=CERT_DIGITAL_SIGNATURE_KEY_USAGE;
        CRYPT_BIT_BLOB usage{1,&flag,0};CERT_BASIC_CONSTRAINTS2_INFO constraints{};
        const auto e=encode(X509_ENHANCED_KEY_USAGE,&eku),u=encode(X509_KEY_USAGE,&usage),c=encode(X509_BASIC_CONSTRAINTS2,&constraints);
        std::array<CERT_EXTENSION,3> extensions{{
            {const_cast<char*>(szOID_ENHANCED_KEY_USAGE),FALSE,{static_cast<DWORD>(e.size()),const_cast<BYTE*>(e.data())}},
            {const_cast<char*>(szOID_KEY_USAGE),TRUE,{static_cast<DWORD>(u.size()),const_cast<BYTE*>(u.data())}},
            {const_cast<char*>(szOID_BASIC_CONSTRAINTS2),TRUE,{static_cast<DWORD>(c.size()),const_cast<BYTE*>(c.data())}}}};
        CERT_EXTENSIONS ext{static_cast<DWORD>(extensions.size()),extensions.data()};CRYPT_ALGORITHM_IDENTIFIER algorithm{const_cast<char*>(szOID_RSA_SHA256RSA),{}};
        SYSTEMTIME start{},end{};GetSystemTime(&start);end=start;end.wYear=static_cast<WORD>(end.wYear+5);
        if(end.wMonth==2 && end.wDay==29)end.wDay=28;
        // Use the opened persisted CNG key, not an implicit/default provider key.
        Key reopened;signing_require(NCryptOpenKey(provider.value,&reopened.value,record.key_name.c_str(),0,NCRYPT_MACHINE_KEY_FLAG|NCRYPT_SILENT_FLAG)==ERROR_SUCCESS,"private_key_inaccessible");
        certificate.value=CertCreateSelfSignCertificate(reopened.value,&blob,0,&key_info,&algorithm,&start,&end,&ext);
        signing_require(certificate.value!=nullptr,"certificate_generation_failed");
        record.thumbprint=cert_hash(certificate.value,CALG_SHA1);record.certificate_sha256=cert_hash(certificate.value,CALG_SHA_256);
    }
    void open_store(Store& store,const wchar_t* name,bool read_only=false,bool archived=false){
        store.value=CertOpenStore(CERT_STORE_PROV_SYSTEM_W,0,0,CERT_SYSTEM_STORE_LOCAL_MACHINE|
            (read_only?(CERT_STORE_OPEN_EXISTING_FLAG|CERT_STORE_READONLY_FLAG):0)|(archived?CERT_STORE_ENUM_ARCHIVED_FLAG:0),name);
        signing_require(store.value!=nullptr,"certificate_store_inaccessible");
    }
    void add_personal(){open_store(personal,L"My");signing_require(CertAddCertificateContextToStore(personal.value,certificate.value,CERT_STORE_ADD_NEW,nullptr),"personal_store_add_failed");}
    void add_trust(){open_store(trust,L"Root");
        Certificate public_only;public_only.value=CertCreateCertificateContext(X509_ASN_ENCODING,certificate.value->pbCertEncoded,certificate.value->cbCertEncoded);
        signing_require(public_only.value && CertAddCertificateContextToStore(trust.value,public_only.value,CERT_STORE_ADD_NEW,nullptr),"trust_configuration_failed");}
    PCCERT_CONTEXT find_owned(HCERTSTORE store){
        std::array<BYTE,20> bytes{};for(size_t i=0;i<bytes.size();++i)bytes[i]=static_cast<BYTE>(std::stoul(record.thumbprint.substr(i*2,2),nullptr,16));
        CRYPT_HASH_BLOB hash{static_cast<DWORD>(bytes.size()),bytes.data()};return CertFindCertificateInStore(store,X509_ASN_ENCODING,0,CERT_FIND_SHA1_HASH,&hash,nullptr);
    }
    void load_ownership(bool archived=false){
        require_admin_protected(state);Handle state_pin;pin(state,state_pin);
        signing_require(ini(state,L"ownership",L"schema")==L"1" && ini(state,L"ownership",L"product")==L"OutLoud" &&
            ini(state,L"ownership",L"scope")==L"LocalMachine" && ini(state,L"ownership",L"stores")==L"My,Root" &&
            ini(state,L"ownership",L"provider")==MS_KEY_STORAGE_PROVIDER && ini(state,L"ownership",L"phase")==L"verified","ownership_mismatch");
        record.id=ini(state,L"ownership",L"installation_id");record.owner_sid=ini(state,L"ownership",L"owner_sid");
        record.key_name=ini(state,L"ownership",L"key_name");record.thumbprint=ini(state,L"ownership",L"thumbprint");
        record.certificate_sha256=ini(state,L"ownership",L"certificate_sha256");record.signed_sha256=ini(state,L"ownership",L"signed_sha256");
        signing_require(hexadecimal(record.signed_sha256,64),"ownership_signed_hash_invalid");
        validate_ownership(record,owner);open_store(personal,L"My",true,archived);certificate.value=find_owned(personal.value);
        signing_require(certificate.value!=nullptr,"owned_certificate_missing");
        signing_require(NCryptOpenKey(provider.value,&key.value,record.key_name.c_str(),0,NCRYPT_MACHINE_KEY_FLAG|NCRYPT_SILENT_FLAG)==ERROR_SUCCESS,"private_key_inaccessible");
    }
    CertificateFacts certificate_facts(){
        CertificateFacts facts;auto cert=certificate.value;signing_require(cert!=nullptr,"owned_certificate_missing");
        wchar_t name[128]{};CertGetNameStringW(cert,CERT_NAME_SIMPLE_DISPLAY_TYPE,0,nullptr,name,128);
        facts.identity=cert_hash(cert,CALG_SHA1)==record.thumbprint && cert_hash(cert,CALG_SHA_256)==record.certificate_sha256 &&
            std::wstring(name)==L"Out Loud Local Signing "+record.id &&
            CertCompareCertificateName(X509_ASN_ENCODING,&cert->pCertInfo->Subject,&cert->pCertInfo->Issuer);
        facts.validity=CertVerifyTimeValidity(nullptr,cert->pCertInfo)==0 &&
            CryptVerifyCertificateSignatureEx(0,X509_ASN_ENCODING,CRYPT_VERIFY_CERT_SIGN_SUBJECT_CERT,const_cast<CERT_CONTEXT*>(cert),
                CRYPT_VERIFY_CERT_SIGN_ISSUER_CERT,const_cast<CERT_CONTEXT*>(cert),0,nullptr);
        DWORD size{};if(CertGetEnhancedKeyUsage(cert,CERT_FIND_EXT_ONLY_ENHKEY_USAGE_FLAG,nullptr,&size)){
            std::vector<BYTE> buffer(size);auto eku=reinterpret_cast<CERT_ENHKEY_USAGE*>(buffer.data());
            if(CertGetEnhancedKeyUsage(cert,CERT_FIND_EXT_ONLY_ENHKEY_USAGE_FLAG,eku,&size))facts.code_signing=eku->cUsageIdentifier==1 && std::string(eku->rgpszUsageIdentifier[0])==szOID_PKIX_KP_CODE_SIGNING;}
        BYTE usage{};facts.digital_signature=CertGetIntendedKeyUsage(X509_ASN_ENCODING,cert->pCertInfo,&usage,1) && usage==CERT_DIGITAL_SIGNATURE_KEY_USAGE;
        facts.rsa3072_sha256=std::string(cert->pCertInfo->SubjectPublicKeyInfo.Algorithm.pszObjId)==szOID_RSA_RSA &&
            std::string(cert->pCertInfo->SignatureAlgorithm.pszObjId)==szOID_RSA_SHA256RSA && key_number(key.value,NCRYPT_LENGTH_PROPERTY)==3072;
        HCRYPTPROV_OR_NCRYPT_KEY_HANDLE acquired{};DWORD spec{};BOOL free_key{};
        facts.key_access=CryptAcquireCertificatePrivateKey(cert,CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG|CRYPT_ACQUIRE_COMPARE_KEY_FLAG|CRYPT_ACQUIRE_SILENT_FLAG,
            nullptr,&acquired,&spec,&free_key)!=FALSE && spec==CERT_NCRYPT_KEY_SPEC;
        if(free_key && acquired)NCryptFreeObject(acquired);
        DWORD property_size{};
        if(CertGetCertificateContextProperty(cert,CERT_KEY_PROV_INFO_PROP_ID,nullptr,&property_size)){
            std::vector<BYTE> buffer(property_size);if(CertGetCertificateContextProperty(cert,CERT_KEY_PROV_INFO_PROP_ID,buffer.data(),&property_size)){
                const auto info=reinterpret_cast<CRYPT_KEY_PROV_INFO*>(buffer.data());facts.key_access=facts.key_access &&
                    info->pwszContainerName && info->pwszProvName && info->pwszContainerName==record.key_name &&
                    std::wstring(info->pwszProvName)==MS_KEY_STORAGE_PROVIDER && info->dwKeySpec==CERT_NCRYPT_KEY_SPEC && (info->dwFlags&CRYPT_MACHINE_KEYSET)!=0;}}
        else facts.key_access=false;
        facts.nonexportable=key_number(key.value,NCRYPT_EXPORT_POLICY_PROPERTY)==0;
        facts.signing_only=key_number(key.value,NCRYPT_KEY_USAGE_PROPERTY)==NCRYPT_ALLOW_SIGNING_FLAG;
        facts.machine_key=(key_number(key.value,NCRYPT_KEY_TYPE_PROPERTY)&NCRYPT_MACHINE_KEY_FLAG)!=0;
        const auto security=key_property(key.value,NCRYPT_SECURITY_DESCR_PROPERTY,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION);
        facts.admin_system_dacl=system_admin_dacl(reinterpret_cast<PSECURITY_DESCRIPTOR>(const_cast<BYTE*>(security.data())),owner);
        if(!trust.value && fresh)return facts; // Fresh trust is checked after the owned Root addition.
        if(!trust.value)open_store(trust,L"Root",true);Certificate root_cert;root_cert.value=find_owned(trust.value);
        if(root_cert.value && cert_hash(root_cert.value,CALG_SHA_256)==record.certificate_sha256){
            CERT_CHAIN_PARA para{};para.cbSize=sizeof(para);auto oid=const_cast<char*>(szOID_PKIX_KP_CODE_SIGNING);para.RequestedUsage.Usage={1,&oid};
            PCCERT_CHAIN_CONTEXT chain{};if(CertGetCertificateChain(HCCE_LOCAL_MACHINE,cert,nullptr,personal.value,&para,CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT|CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL|CERT_CHAIN_DISABLE_AUTH_ROOT_AUTO_UPDATE,nullptr,&chain)){
                CERT_CHAIN_POLICY_PARA p{};p.cbSize=sizeof(p);CERT_CHAIN_POLICY_STATUS status{};status.cbSize=sizeof(status);
                facts.trusted=CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_AUTHENTICODE,chain,&p,&status) && status.dwError==0 && chain->TrustStatus.dwErrorStatus==0;
                CertFreeCertificateChain(chain);}}
        return facts;
    }
    void sign_required(){
        if(already_signed){const auto signature=verify_signature(target,file.value);
            signing_require(signature.status==ERROR_SUCCESS && signature.thumbprint==record.thumbprint && signature.certificate_sha256==record.certificate_sha256 &&
                file_hash(file.value)==record.signed_sha256,"existing_signature_ownership_mismatch");return;}
        const auto module=LoadLibraryExW(L"mssign32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);signing_require(module!=nullptr,"native_signing_unavailable");
        try {
            namespace api=signer_api;auto sign=reinterpret_cast<api::SignEx2>(GetProcAddress(module,"SignerSignEx2"));
            auto release=reinterpret_cast<api::FreeContext>(GetProcAddress(module,"SignerFreeSignerContext"));signing_require(sign && release,"native_signing_unavailable");
            DWORD index{};api::FileInfo info{sizeof(info),target.c_str(),file.value};api::SubjectInfo subject{};
            subject.cbSize=sizeof(subject);subject.pdwIndex=&index;subject.dwSubjectChoice=1;subject.pSignerFileInfo=&info;
            api::StoreInfo store{sizeof(store),certificate.value,2,personal.value};api::Cert cert{};cert.cbSize=sizeof(cert);cert.dwCertChoice=2;cert.pCertStoreInfo=&store;
            api::SignatureInfo signature{};signature.cbSize=sizeof(signature);signature.algidHash=CALG_SHA_256;
            api::Context* context{};const auto result=sign(SPC_EXC_PE_PAGE_HASHES_FLAG,&subject,&cert,&signature,nullptr,0,nullptr,nullptr,nullptr,nullptr,&context,nullptr,nullptr);
            if(context)release(context);signing_require(SUCCEEDED(result) && FlushFileBuffers(file.value),"dll_signing_failed");
        }catch(...){FreeLibrary(module);throw;}FreeLibrary(module);
    }
    void verify_required(){
        const auto signature=verify_signature(target,file.value);
        signing_require(signature.status==ERROR_SUCCESS,"dll_signature_verification_failed");
        signing_require(signature.thumbprint==record.thumbprint && signature.certificate_sha256==record.certificate_sha256,"dll_signer_identity_mismatch");
        signing_require(sip_digest(target,file.value)==expected_digest,"signed_code_identity_mismatch");
        verify_dependencies();record.signed_sha256=file_hash(file.value);
    }
    void commit(){
        if(fresh)signing_require(MoveFileExW(pending.c_str(),state.c_str(),MOVEFILE_WRITE_THROUGH)!=0,"ownership_commit_failed");
        else signing_require(ReplaceFileW(state.c_str(),pending.c_str(),(root/L"configuration/signing.previous.ini").c_str(),0,nullptr,nullptr)!=0,"ownership_commit_failed");
        require_admin_protected(state);
    }
};
#include "registration.inc"
#include "maintenance_files.inc"
#include "data_binding.inc"
#include "maintenance_windows.inc"
#include "uninstall_windows.inc"
#include "uninstall_logs.inc"
std::wstring maintenance_failed_fresh_audit(WindowsUninstall& audit,const std::vector<std::wstring>& package){
    const auto& root=audit.storage().root;
    if(!fs::exists(root) || fs::is_empty(root))return {};
    const auto base=root/L"configuration/maintenance";
    std::wstring id;
    if(fs::exists(base))for(const auto& e:fs::directory_iterator(base)){
        safe(e.path());signing_require(e.is_directory() && hexadecimal(e.path().filename().native(),32),"maintenance_fresh_unknown_archive");
        const auto pointer=audit.storage().read(e.path()/L"terminal.pointer");
        signing_require(pointer==e.path().filename().native()+L"\n","maintenance_fresh_archive_pending");
        MaintenanceJournal last;unsigned frames{};
        for(unsigned n=0;n<256 && fs::exists(e.path()/(L"state-"+std::to_wstring(n)+L".dat"));++n){
            last=maintenance_decode(audit.storage().read(e.path()/(L"state-"+std::to_wstring(n)+L".dat")),package);++frames;}
        signing_require(frames && last.phase==MaintenancePhase::rolled_back && last.operation==MaintenanceOperation::fresh &&
            last.root_encoding==maintenance_root_encoding(root.native()) && last.transaction_id==e.path().filename().native() &&
            (id.empty() || id==last.installation_id),"maintenance_fresh_archive_not_rolled_back");
        id=last.installation_id;audit.plan.owner=last;
    }
    if(!id.empty())audit.archives();
    std::set<fs::path> allowed_files,allowed_directories{root/L"configuration",base,root/L"bin",root/L"runtime",root/L"licenses"};
    for(const auto& f:audit.plan.files)allowed_files.insert(f.path);
    for(const auto& d:audit.plan.directories)allowed_directories.insert(d.first);
    const auto lock=root/L"configuration/maintenance.lock";
    if(fs::exists(lock)){
        signing_require(!id.empty() && audit.storage().read(lock)==L"OutLoud.Maintenance.Lock.1\n","maintenance_fresh_unbound_epoch");allowed_files.insert(lock);}
    for(const auto& e:fs::recursive_directory_iterator(root)){
        safe(e.path());audit.storage().security(e.path());
        signing_require(e.is_directory()?allowed_directories.contains(e.path()):(e.is_regular_file() && allowed_files.contains(e.path())),"maintenance_fresh_directory_not_empty");}
    return id;
}
std::wstring maintenance_failed_fresh_identity(const fs::path& root,const std::vector<std::wstring>& package){
    WindowsUninstall audit(root);return maintenance_failed_fresh_audit(audit,package);
}
#include "maintenance_fresh_cleanup.inc"
#include "installation_inspection.inc"
}
#ifndef OUTLOUD_MAINTENANCE_CHECK
int wmain(int argc,wchar_t** argv){
    try {
        signing_require(argc>=2,"invalid_arguments");const std::wstring command=argv[1];
        if(command==L"--uninstall-status"){
            signing_require(argc==3,"invalid_arguments");write_new(argv[2],L"[results]\nschema=1\nuninstall="+std::wstring((uninstall_lifecycle_ready && maintenance_lifecycle_ready)?L"enabled":L"blocked")+L"\n");return 0;
        }
        if(command==L"--uninstall-check" || command==L"--uninstall" || command==L"--uninstall-finalize" || command==L"--uninstall-helper-check"){
            // Gates precede parsing, profile discovery, elevation and all resource access.
            signing_require(uninstall_lifecycle_ready && maintenance_lifecycle_ready,"foundation_uninstall_blocked");
            if(command==L"--uninstall-helper-check"){
                signing_require(argc==5,"invalid_arguments");WindowsUninstall op(argv[2]);op.maintenance.authority();
                const auto host=maintenance_host_record();signing_require(host && same_installation_path(host->first,op.storage().root),"uninstall_host_identity_invalid");
                MaintenanceFile expected;
                if(fs::exists(op.receipt)){op.load();for(const auto& f:op.plan.files)if(f.path==op.storage().root/L"bin/OutLoudInstallSigning.exe")expected=f.stamp;}
                else{const auto inventory=maintenance_inventory_decode(op.storage().read(op.storage().root/L"configuration/inventory.dat"));
                    expected=inventory.files.at(L"bin/OutLoudInstallSigning.exe");}
                Handle candidate;pin(argv[3],candidate);const auto actual=maintenance_file_stamp(candidate.value);
                signing_require(expected && actual.hash==expected->hash && actual.size==expected->size && op.storage().current(L"bin/OutLoudInstallSigning.exe")==expected,"uninstall_helper_identity_changed");
                write_new(argv[4],L"[results]\nschema=1\nuninstall=helper_verified\n");return 0;
            }
            signing_require(argc==(command==L"--uninstall"?5:4),"invalid_arguments");
            if(command==L"--uninstall")signing_require(std::wstring(argv[3])==L"--confirmed-complete","uninstall_confirmation_required");
            WindowsUninstall op(argv[2]);const auto report=argv[argc-1];
            try{
                if(command==L"--uninstall-finalize"){
                    op.maintenance.authority();require_admin_protected(op.storage().root);op.retry=true;op.load();
                    signing_require(!maintenance_host_record(),"uninstall_inno_host_remains");op.installation_inventory(false);op.revalidate();op.finish();
                    write_new(report,L"[results]\nschema=1\nuninstall=complete\n");
                }else if(command==L"--uninstall-check"){
                    op.preflight();write_new(report,L"[results]\nschema=1\nuninstall=ready\n");
                }else{uninstall_installation(op);write_new(report,L"[results]\nschema=1\nuninstall=awaiting_host\n");}
                return 0;
            }catch(const SigningError& e){
                auto text=op.remaining();text+=L"reason=";for(const auto c:std::string(e.what()))text+=static_cast<wchar_t>(c);text+=L"\n";
                write_new(report,text);try{op.durable_failure(text);}catch(...){std::cerr<<"Uninstall report could not be retained; preserve the temporary report.\n";}throw;
            }catch(...){const auto text=op.remaining()+L"reason=uninstall_unverified_failure\n";write_new(report,text);try{op.durable_failure(text);}catch(...){}throw;}
        }
        if(command==L"--maintenance-status"){
            signing_require(argc==3,"invalid_arguments");write_new(argv[2],maintenance_lifecycle_ready?
                L"[results]\nschema=1\nmaintenance=enabled\n":
                L"[results]\nschema=1\nmaintenance=blocked\nreason=lifecycle_live_validation_and_release_pending\n");return 0;
        }
        if(command==L"--maintenance-fresh-cleanup"){
            signing_require(maintenance_lifecycle_ready,"foundation_maintenance_blocked");signing_require(argc==4,"invalid_arguments");
            WindowsFreshCleanup cleanup(argv[2]);cleanup.run();
            write_new(argv[3],L"[results]\nschema=1\nmaintenance=fresh_removed\n");return 0;
        }
        if(command==L"--maintain" || command==L"--maintenance-recover" || command==L"--maintenance-finalize" || command==L"--maintenance-reconcile"){
            // Closed before parsing arguments, elevation, file, registry or key access.
            signing_require(maintenance_lifecycle_ready,"foundation_maintenance_blocked");
            if(command==L"--maintain"){
                signing_require(argc==9 && std::wstring(argv[7])==L"--consented","invalid_arguments");
                WindowsMaintenance op(argv[4],argv[5],argv[6],maintenance_operation(argv[2]),maintenance_components(argv[3]));
                const auto outcome=maintain_installation(op);op.finish(outcome);write_new(argv[8],maintenance_result(op,outcome));return 0;
            }
            signing_require(argc==4,"invalid_arguments");WindowsMaintenance op(argv[2]);auto j=op.load();
            MaintenanceOutcome outcome;
            if(command==L"--maintenance-reconcile" && j.host==MaintenanceHost::inno && j.phase==MaintenancePhase::awaiting_host){
                signing_require(j.new_version==maintenance_version(),"maintenance_resume_matching_setup_required");
                outcome=reconcile_maintenance(op,j);
            }else outcome=recover_maintenance(op,j,command==L"--maintenance-finalize");
            op.finish(outcome);
            write_new(argv[3],maintenance_result(op,outcome));return 0;
        }
        if(command==L"--installation-inspect")return run_installation_inspection(argc,argv);
        if(command==L"--registration-inspect"){
            signing_require(argc==4,"invalid_arguments");
            const auto report=registration_inspection(argv[2]);std::wcout<<report;write_new(argv[3],report);return 0;
        }
        if(command==L"--register"){
            // Gate before argument parsing, registry reads or any installation operation.
            signing_require(registration_lifecycle_ready,"foundation_registration_blocked");
            signing_require(argc==8 && std::wstring(argv[6])==L"--consented","invalid_arguments");
            WindowsRegistration operations(argv[2],argv[3],argv[4],argv[5]);register_engine(operations);
            write_new(argv[7],L"[results]\nschema=1\nregistration=verified\nhive=HKLM\nview=64\nclsid="+
                std::wstring(outloud::sapi::clsid_text)+L"\ninstallation_id="+operations.identity().installation_id+
                L"\ncertificate_sha256="+operations.identity().certificate_sha256+L"\nsigned_sha256="+operations.signing.record.signed_sha256+L"\n");return 0;
        }
        if(command==L"--identity"){
            signing_require(argc==3,"invalid_arguments");write_new(argv[2],L"[results]\nschema=1\nscope=LocalMachine\nowner_sid=S-1-5-32-544\nelevated="+std::wstring(IsUserAnAdmin()?L"true":L"false")+L"\n");return 0;
        }
        if(command==L"--describe"){
            signing_require(argc==3 || argc==5,"invalid_arguments");if(argc==5)signing_require(std::wstring(argv[3])==L"--report","invalid_arguments");
            const fs::path path=argv[2];Handle file;pin(path,file);const auto signature=verify_signature(path,file.value);
            const auto report=L"[results]\nschema=1\nsha256="+file_hash(file.value)+L"\nsip_sha256="+sip_digest(path,file.value)+
                L"\nsignature="+(signature.status==ERROR_SUCCESS?L"valid":(signature.status==TRUST_E_NOSIGNATURE?L"unsigned":L"invalid"))+
                L"\nthumbprint="+signature.thumbprint+L"\ncertificate_sha256="+signature.certificate_sha256+L"\n";
            std::wcout<<report;if(argc==5)write_new(argv[4],report);return 0;
        }
        if(command==L"--dependencies"){
            signing_require(argc==3 || argc==5,"invalid_arguments");if(argc==5)signing_require(std::wstring(argv[3])==L"--report","invalid_arguments");
            const auto report=dependency_inventory(argv[2]);std::wcout<<report;if(argc==5)write_new(argv[4],report);return 0;
        }
        if(command==L"--prepare"){
            // Defense in depth: even direct invocation cannot mutate this foundation.
            signing_require(mutation_gate(),"foundation_signing_blocked");
            signing_require(argc==8 && std::wstring(argv[6])==L"--consented","invalid_arguments");
            const std::wstring mode=argv[2];signing_require(mode==L"fresh" || mode==L"upgrade","invalid_arguments");
            WindowsSigning operations(argv[3],argv[4],argv[5],mode==L"fresh");prepare_signing(mode==L"fresh",operations);
            write_new(argv[7],L"[results]\nschema=1\nsigning=verified\nowner_sid="+operations.owner+L"\nthumbprint="+operations.record.thumbprint+
                L"\ncertificate_sha256="+operations.record.certificate_sha256+L"\nsigned_sha256="+operations.record.signed_sha256+L"\n");return 0;
        }
        throw SigningError("unknown_command");
    }catch(const SigningError& error){
        if(argc>=2){const std::wstring command=argv[1];
            if((command==L"--maintain" && argc==9) || ((command==L"--maintenance-recover" || command==L"--maintenance-finalize" || command==L"--maintenance-reconcile" || command==L"--maintenance-fresh-cleanup") && argc==4)){
                try{std::wstring reason;for(const char c:std::string(error.what()))reason+=static_cast<wchar_t>(c);
                    write_new(argv[argc-1],L"[results]\nschema=1\nmaintenance=incomplete\nreason="+reason+L"\n");}
                catch(...){std::cerr<<"Maintenance failure report unavailable; preserve protected recovery records.\n";}}}
        std::cerr<<"Out Loud signing refused: "<<error.what()<<"\n";return 2;}
    catch(...){std::cerr<<"Out Loud signing failed; completion is not verified.\n";return 2;}
}
#endif

