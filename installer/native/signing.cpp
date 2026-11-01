#include <windows.h>
#include <wincrypt.h>
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
#include "signing_policy.hpp"
#include "installation_path.hpp"
namespace {
namespace fs=std::filesystem;
using namespace outloud::installer;
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    ~Handle(){if(value!=INVALID_HANDLE_VALUE) CloseHandle(value);}
    Handle()=default; Handle(const Handle&)=delete; Handle& operator=(const Handle&)=delete;
};



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
#include "installation_inspection.inc"
} // namespace
#ifndef OUTLOUD_MAINTENANCE_CHECK
int wmain(int argc,wchar_t** argv) {
    try {
        signing_require(argc>=2,"invalid_arguments"); const std::wstring command=argv[1];
        if(command==L"--installation-inspect") return run_installation_inspection(argc,argv);
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
        throw SigningError("unsupported_operation_legacy_installer_retired");
    } catch(const SigningError& error) { std::cerr << "Out Loud inspection refused: " << error.what() << "\n"; return 2; }
    catch(...) { std::cerr << "Out Loud inspection failed; completion is not verified.\n"; return 2; }
}
#endif
