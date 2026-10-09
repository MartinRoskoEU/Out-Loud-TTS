#pragma once
#include "outloud/asset_error.hpp"
#include <filesystem>
#include <windows.h>
#include <oleauto.h>

namespace outloud::sapi {
inline HRESULT asset_hresult(const AssetError& error) noexcept {
    if (error.system_error) { return HRESULT_FROM_WIN32(error.system_error); }
    return HRESULT_FROM_WIN32(error.code==AssetErrorCode::compatibility ? ERROR_REVISION_MISMATCH :
        error.code==AssetErrorCode::invalid_data ? ERROR_INVALID_DATA : ERROR_READ_FAULT);
}
// Error info is published on the COM caller, once per failure, without paths,
// token names or supplied speech text. Some SAPI hosts expose only the HRESULT.
inline HRESULT report_asset_error(const AssetError& error) noexcept {
    ICreateErrorInfo* create=nullptr;
    if (SUCCEEDED(CreateErrorInfo(&create))) {
        const wchar_t* description=error.code==AssetErrorCode::compatibility ? L"Pocket voice/model compatibility mismatch." :
            error.code==AssetErrorCode::invalid_data ? L"Pocket runtime asset is invalid or corrupted." : L"Pocket runtime asset I/O failed.";
        // Role/operation are stable implementation messages, never user input.
        wchar_t message[512]{};
        const auto count=MultiByteToWideChar(CP_UTF8,0,error.what(),-1,message,512);
        (void)create->SetSource(const_cast<wchar_t*>(L"Out Loud Pocket SAPI"));
        (void)create->SetDescription(const_cast<wchar_t*>(count ? message : description));
        IErrorInfo* info=nullptr;
        if (SUCCEEDED(create->QueryInterface(IID_IErrorInfo,reinterpret_cast<void**>(&info)))) {
            (void)SetErrorInfo(0,info); info->Release();
        }
        create->Release();
    }
    return asset_hresult(error);
}
template<class T> T load_asset(const std::filesystem::path& path,const char* role) {
    try { return T(path); }
    catch (const AssetError& e) { throw AssetError(e.code,std::string(role)+": "+e.what(),e.system_error); }
    catch (const std::runtime_error&) { throw AssetError(AssetErrorCode::invalid_data,std::string(role)+": invalid or corrupted asset"); }
    catch (const std::invalid_argument&) { throw AssetError(AssetErrorCode::invalid_data,std::string(role)+": invalid or corrupted asset"); }
}
inline void readable_asset(const std::filesystem::path& path,const char* role) {
    const auto file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file==INVALID_HANDLE_VALUE) { const auto error=GetLastError(); throw AssetError(AssetErrorCode::io,std::string(role)+": cannot open runtime asset",error); }
    CloseHandle(file);
}
}
