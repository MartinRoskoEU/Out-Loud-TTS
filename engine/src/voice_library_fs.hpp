#pragma once

#include "outloud/voice_library.hpp"
#include "asset_io.hpp"
#include "utf8.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <utility>

namespace outloud::detail::library_fs {
using Code = VoiceLibraryErrorCode;
[[noreturn]] inline void fail(Code code,const char* message,const std::filesystem::path& path,DWORD error=0) {
    throw VoiceLibraryError(code,message,path,error);
}
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    Handle()=default;
    explicit Handle(HANDLE raw) : value(raw) {}
    Handle(Handle&& other) noexcept : value(std::exchange(other.value,INVALID_HANDLE_VALUE)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this!=&other) { close(); value=std::exchange(other.value,INVALID_HANDLE_VALUE); } return *this;
    }
    Handle(const Handle&)=delete;
    ~Handle() { close(); }
    void close() noexcept { if (value!=INVALID_HANDLE_VALUE) { CloseHandle(value); value=INVALID_HANDLE_VALUE; } }
};
inline bool absent(DWORD error) { return error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND; }
inline bool id(std::string_view value) {
    return value.size()==32 && std::all_of(value.begin(),value.end(),[](char ch) { return (ch>='0'&&ch<='9')||(ch>='a'&&ch<='f'); });
}
inline std::wstring wide(std::string_view text) {
    if (!valid_utf8(text) || text.size()>32768) { throw std::invalid_argument("Invalid or oversized UTF-8"); }
    if (text.empty()) { return {}; }
    const auto length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
    std::wstring result(static_cast<std::size_t>(length),L'\0');
    if (!length || !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),result.data(),length)) {
        throw std::invalid_argument("Cannot decode UTF-8");
    }
    return result;
}
inline std::string utf8(std::wstring_view text) {
    if (text.empty()) { return {}; }
    if (text.size()>32768) { throw std::invalid_argument("Oversized UTF-16"); }
    const auto length=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    if (!length) { throw std::invalid_argument("Invalid UTF-16"); }
    std::string result(static_cast<std::size_t>(length),'\0');
    if (!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),result.data(),length,nullptr,nullptr)) {
        throw std::invalid_argument("Cannot encode UTF-16");
    }
    return result;
}
inline std::filesystem::path local(const std::filesystem::path& input,Code code) {
    if (input.empty()) { fail(code,"Local path is required",input); }
    for (const auto& component : input) {
        if (component==L"..") { fail(code,"Parent traversal is forbidden",input); }
    }
    std::error_code error;
    auto path=std::filesystem::absolute(input,error).lexically_normal();
    if (path!=path.root_path() && !path.has_filename()) { path=path.parent_path(); }
    const auto& name=path.native();
    if (error || name.size()<3 || name.size()>32000 || name[1]!=L':' || name[2]!=L'\\' ||
        name.find(L':',2)!=std::wstring::npos || name.find(L'\0')!=std::wstring::npos ||
        GetDriveTypeW(path.root_path().c_str())==DRIVE_REMOTE) { fail(code,"Only local drive paths are supported",input); }
    (void)utf8(name);
    for (const auto& component : path.relative_path()) {
        const auto& part=component.native();
        if (part.empty() || part.back()==L'.' || part.back()==L' ' || part.find_first_of(L"<>\"|?*")!=std::wstring::npos) {
            fail(code,"Ambiguous/invalid Windows path component",input);
        }
        auto stem=part.substr(0,part.find(L'.'));
        while (!stem.empty() && stem.back()==L' ') { stem.pop_back(); }
        for (auto& ch : stem) { if (ch>=L'a' && ch<=L'z') { ch=static_cast<wchar_t>(ch-L'a'+L'A'); } }
        const bool numbered=stem.size()==4 && (stem.starts_with(L"COM") || stem.starts_with(L"LPT")) &&
            ((stem[3]>=L'1' && stem[3]<=L'9') || stem[3]==L'\u00b9' || stem[3]==L'\u00b2' || stem[3]==L'\u00b3');
        if (stem==L"CON" || stem==L"PRN" || stem==L"AUX" || stem==L"NUL" || stem==L"CONIN$" || stem==L"CONOUT$" || numbered) {
            fail(code,"DOS device path aliases are forbidden",input);
        }
    }
    return path;
}
inline BY_HANDLE_FILE_INFORMATION info(HANDLE handle,const std::filesystem::path& path,bool directory) {
    BY_HANDLE_FILE_INFORMATION result{};
    if (!GetFileInformationByHandle(handle,&result)) { fail(Code::library_unavailable,"Cannot inspect file handle",path,GetLastError()); }
    if ((result.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) ||
        bool(result.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=directory || (!directory && result.nNumberOfLinks!=1)) {
        fail(Code::unsafe_path,"Reparse points, hard-linked files or unexpected file types are forbidden",path);
    }
    return result;
}
inline Handle directory(const std::filesystem::path& path,DWORD extra_access=0) {
    Handle result(CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES|extra_access,FILE_SHARE_READ|FILE_SHARE_WRITE,
        nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if (result.value==INVALID_HANDLE_VALUE) { fail(Code::library_unavailable,"Cannot pin local directory",path,GetLastError()); }
    (void)info(result.value,path,true); return result;
}
// Ancestors are opened one component at a time without following reparse points.
// Denying delete sharing pins their identities until the operation releases them.
struct Pins {
    std::filesystem::path path;
    std::vector<Handle> handles;
    bool exists=true;
    Pins(const std::filesystem::path& input,bool create=false,bool missing_ok=false) : path(local(input,Code::unsafe_path)) {
        auto current=path.root_path(); handles.push_back(directory(current));
        for (const auto& component : path.relative_path()) {
            current/=component;
            auto attributes=GetFileAttributesW(current.c_str());
            if (attributes==INVALID_FILE_ATTRIBUTES) {
                const auto error=GetLastError();
                if (absent(error) && create) {
                    if (!CreateDirectoryW(current.c_str(),nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS) {
                        fail(Code::library_unavailable,"Cannot create library directory",current,GetLastError());
                    }
                } else if (absent(error) && missing_ok) { exists=false; return; }
                else { fail(Code::library_unavailable,"Cannot access local directory",current,error); }
            }
            handles.push_back(directory(current));
        }
    }
};
inline Handle file(const std::filesystem::path& path,DWORD access=GENERIC_READ,DWORD sharing=FILE_SHARE_READ,
                   DWORD creation=OPEN_EXISTING,Code code=Code::source_io) {
    Handle result(CreateFileW(path.c_str(),access,sharing,nullptr,creation,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if (result.value==INVALID_HANDLE_VALUE) { fail(code,"Cannot open regular local file",path,GetLastError()); }
    (void)info(result.value,path,false); return result;
}
inline std::uint64_t size(HANDLE handle,const std::filesystem::path& path) {
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(handle,&length) || length.QuadPart<0) { fail(Code::source_io,"Cannot read file size",path,GetLastError()); }
    return static_cast<std::uint64_t>(length.QuadPart);
}
inline std::vector<unsigned char> read(HANDLE handle,const std::filesystem::path& path,std::uint64_t limit) {
    const auto count=size(handle,path);
    if (count>limit) { fail(Code::metadata_corrupt,"Metadata file exceeds size limit",path); }
    std::vector<unsigned char> bytes(static_cast<std::size_t>(count)); DWORD received=0;
    if (count && (!ReadFile(handle,bytes.data(),static_cast<DWORD>(count),&received,nullptr) || received!=count)) {
        fail(Code::source_io,"Cannot read complete file",path,GetLastError());
    }
    return bytes;
}
inline void write(HANDLE handle,Bytes bytes,const std::filesystem::path& path) {
    while (!bytes.empty()) {
        const auto count=static_cast<DWORD>(std::min<std::size_t>(bytes.size(),8*1024*1024)); DWORD written=0;
        if (!WriteFile(handle,bytes.data(),count,&written,nullptr) || written!=count) {
            fail(Code::destination_io,"Cannot write complete library file",path,GetLastError());
        }
        bytes=bytes.subspan(count);
    }
    if (!FlushFileBuffers(handle)) { fail(Code::destination_io,"Cannot flush library file",path,GetLastError()); }
}
inline std::string random_id() {
    std::array<unsigned char,16> bytes{};
    if (BCryptGenRandom(nullptr,bytes.data(),static_cast<ULONG>(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0) {
        fail(Code::internal_failure,"Cannot generate secure library identity",{});
    }
    return hex(bytes);
}
inline bool exists(const std::filesystem::path& path) {
    if (GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES) { return true; }
    const auto error=GetLastError();
    if (!absent(error)) { fail(Code::library_unavailable,"Cannot determine path existence",path,error); }
    return false;
}
inline std::vector<std::wstring> children(const std::filesystem::path& path) {
    WIN32_FIND_DATAW data{}; std::vector<std::wstring> result;
    const auto search=FindFirstFileW((path/L"*").c_str(),&data);
    if (search==INVALID_HANDLE_VALUE) {
        if (GetLastError()==ERROR_FILE_NOT_FOUND) { return result; }
        fail(Code::library_unavailable,"Cannot enumerate directory",path,GetLastError());
    }
    DWORD error=0;
    try {
        do {
            const std::wstring_view name(data.cFileName);
            if (name!=L"." && name!=L"..") { result.emplace_back(name); }
        } while (FindNextFileW(search,&data));
        error=GetLastError();
    } catch (...) { FindClose(search); throw; }
    FindClose(search);
    if (error!=ERROR_NO_MORE_FILES) { fail(Code::library_unavailable,"Directory enumeration failed",path,error); }
    std::sort(result.begin(),result.end()); return result;
}
inline std::filesystem::path final_path(HANDLE handle,const std::filesystem::path& path) {
    std::wstring text(32768,L'\0');
    const auto length=GetFinalPathNameByHandleW(handle,text.data(),static_cast<DWORD>(text.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    if (!length || length>=text.size()) { fail(Code::unsafe_path,"Cannot resolve pinned directory identity",path,GetLastError()); }
    text.resize(length);
    if (text.starts_with(L"\\\\?\\")) { text.erase(0,4); }
    return text;
}
inline bool within(const std::filesystem::path& path,const std::filesystem::path& root) {
    const auto& name=path.native(); const auto& base=root.native();
    return name.size()>=base.size() && CompareStringOrdinal(name.data(),static_cast<int>(base.size()),base.data(),
        static_cast<int>(base.size()),TRUE)==CSTR_EQUAL && (name.size()==base.size() || name[base.size()]==L'\\');
}
inline void rename(HANDLE handle,const std::filesystem::path& destination,bool replace,Code code) {
    const auto& name=destination.native();
    const auto bytes=offsetof(FILE_RENAME_INFO,FileName)+name.size()*sizeof(wchar_t);
    std::vector<std::uint64_t> storage((bytes+7)/8,0);
    auto record=reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
    record->ReplaceIfExists=replace?TRUE:FALSE;
    record->FileNameLength=static_cast<DWORD>(name.size()*sizeof(wchar_t));
    std::copy(name.begin(),name.end(),record->FileName);
    if (!SetFileInformationByHandle(handle,FileRenameInfo,record,static_cast<DWORD>(bytes))) {
        fail(code,"Cannot atomically finalize file/directory",destination,GetLastError());
    }
}
inline void discard(HANDLE handle,const std::filesystem::path& path) {
    FILE_DISPOSITION_INFO disposition{TRUE};
    if (!SetFileInformationByHandle(handle,FileDispositionInfo,&disposition,sizeof(disposition))) {
        fail(Code::remove_failed,"Cannot remove owned library file/directory",path,GetLastError());
    }
}
} // namespace outloud::detail::library_fs
