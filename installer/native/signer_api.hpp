#pragma once
// ABI declarations required by Microsoft's documented Mssign32 APIs, which have
// no SDK header/import library. Only the file/store/no-attribute variants are used.
// https://learn.microsoft.com/windows/win32/seccrypto/signersignex2
namespace outloud::installer::signer_api {
struct FileInfo { DWORD cbSize; LPCWSTR pwszFileName; HANDLE hFile; };
struct SubjectInfo {
    DWORD cbSize; DWORD* pdwIndex; DWORD dwSubjectChoice;
    union { FileInfo* pSignerFileInfo; void* pSignerBlobInfo; };
};
struct StoreInfo { DWORD cbSize; PCCERT_CONTEXT pSigningCert; DWORD dwCertPolicy; HCERTSTORE hCertStore; };
struct Cert {
    DWORD cbSize; DWORD dwCertChoice;
    union { LPCWSTR pwszSpcFile; StoreInfo* pCertStoreInfo; void* pSpcChainInfo; }; HWND hwnd;
};
struct SignatureInfo {
    DWORD cbSize; ALG_ID algidHash; DWORD dwAttrChoice;
    union { void* pAttrAuthcode; }; PCRYPT_ATTRIBUTES psAuthenticated; PCRYPT_ATTRIBUTES psUnauthenticated;
};
struct ProviderInfo;
struct Context { DWORD cbSize; DWORD cbBlob; BYTE* pbBlob; };
using SignEx2=HRESULT(WINAPI*)(DWORD,SubjectInfo*,Cert*,SignatureInfo*,ProviderInfo*,DWORD,
    PCSTR,PCWSTR,PCRYPT_ATTRIBUTES,PVOID,Context**,PCERT_STRONG_SIGN_PARA,PVOID);
using FreeContext=HRESULT(WINAPI*)(Context*);
static_assert(sizeof(FileInfo)==24 && sizeof(SubjectInfo)==32 && sizeof(StoreInfo)==32 &&
    sizeof(Cert)==24 && sizeof(SignatureInfo)==40 && sizeof(Context)==16);
}
