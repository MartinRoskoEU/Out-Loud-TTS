#include "error_logger.hpp"
#include <shlobj.h>
#include <oleauto.h>
#include <array>
#include <cstdio>
#include <filesystem>
#include <string>
#ifdef OUTLOUD_ERROR_LOGGER_TEST
#include <atomic>
#include <mutex>
#endif

namespace outloud::sapi::logging {
namespace {
struct Handle {
    HANDLE value;
    ~Handle() { if (value && value!=INVALID_HANDLE_VALUE) { CloseHandle(value); } }
};
struct PreserveErrors {
    DWORD system = GetLastError();
    IErrorInfo* info = nullptr;
    HRESULT captured = GetErrorInfo(0,&info);
    ~PreserveErrors() {
        if (SUCCEEDED(captured)) { (void)SetErrorInfo(0,info); }
        if (info) { info->Release(); }
        SetLastError(system);
    }
};
const char* event_name(Event event) noexcept {
    switch (event) {
    case Event::configuration: return "TOKEN_CONFIGURATION";
    case Event::token_access: return "TOKEN_ACCESS";
    case Event::output_format: return "OUTPUT_FORMAT";
    case Event::synthesis: return "SYNTHESIS";
    case Event::write: return "SITE_WRITE";
    case Event::bookmark_interest: return "BOOKMARK_INTEREST";
    case Event::bookmark_delivery: return "BOOKMARK_DELIVERY";
    case Event::rate: return "SITE_RATE";
    case Event::volume: return "SITE_VOLUME";
    case Event::skip_info: return "SITE_SKIP_INFO";
    case Event::skip_complete: return "SITE_SKIP_COMPLETE";
    case Event::create_engine: return "ENGINE_CREATE";
    case Event::create_factory: return "FACTORY_CREATE";
    }
    return "INTERNAL";
}
const char* component_name(Component component) noexcept {
    switch (component) {
    case Component::sapi: return "SAPI";
    case Component::native: return "Native";
    case Component::assets: return "Assets";
    case Component::model: return "Model";
    case Component::tokenizer: return "Tokenizer";
    case Component::voice: return "Voice";
    }
    return "Native";
}
const char* message(Kind kind) noexcept {
    switch (kind) {
    case Kind::operation: return "SAPI operation failed.";
    case Kind::asset_io: return "Runtime asset I/O failed.";
    case Kind::asset_invalid: return "Runtime asset is invalid or corrupted.";
    case Kind::asset_compatibility: return "Voice/model compatibility mismatch.";
    case Kind::allocation: return "Resource allocation failed.";
    case Kind::invalid_argument: return "Request or configuration is invalid.";
    case Kind::filesystem: return "Filesystem operation failed.";
    case Kind::unexpected: return "Unexpected native exception.";
    case Kind::unexpected_cancellation: return "Native cancellation without SAPI abort.";
    }
    return "Native operation failed.";
}
// A path-derived, opaque name shares one lock across processes and sessions.
// Hash collisions can only serialize unrelated logs; paths never enter a record.
std::wstring mutex_name(const std::filesystem::path& directory) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (auto ch : directory.native()) {
        if (ch>=L'A' && ch<=L'Z') { ch = static_cast<wchar_t>(ch+L'a'-L'A'); }
        if (ch==L'/') { ch=L'\\'; }
        hash ^= static_cast<std::uint16_t>(ch); hash *= 1099511628211ULL;
    }
    wchar_t name[80]{};
    swprintf_s(name,L"Global\\OutLoud.Pocket.ErrorLog.%016llX",static_cast<unsigned long long>(hash));
    return name;
}
#ifdef OUTLOUD_ERROR_LOGGER_TEST
std::mutex test_mutex;
std::filesystem::path test_directory;
Fault test_fault = Fault::none;
std::atomic<std::uint64_t> test_attempts{0};
#endif
bool directory_path(std::filesystem::path& path) {
#ifdef OUTLOUD_ERROR_LOGGER_TEST
    std::lock_guard lock(test_mutex);
    path = test_directory;
    return !path.empty();
#else
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DONT_VERIFY,nullptr,&raw))) { return false; }
    struct Free { PWSTR value; ~Free() { CoTaskMemFree(value); } } free{raw};
    path = std::filesystem::path(raw)/L"OutLoud"/L"Logs";
    return true;
#endif
}
bool regular_file(HANDLE file, std::uint64_t& length) noexcept {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file,&info) ||
        (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) || info.nNumberOfLinks!=1) { return false; }
    length = (static_cast<std::uint64_t>(info.nFileSizeHigh)<<32)|info.nFileSizeLow;
    return true;
}
bool bounded_archive(const std::filesystem::path& path) {
    Handle file{CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,
        OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr)};
    if (file.value==INVALID_HANDLE_VALUE) { return GetLastError()==ERROR_FILE_NOT_FOUND; }
    std::uint64_t length=0;
    if (!regular_file(file.value,length)) { return false; }
    return length<=file_limit || DeleteFileW(path.c_str());
}
bool move_if_present(const std::filesystem::path& from,const std::filesystem::path& to) {
    return MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING) || GetLastError()==ERROR_FILE_NOT_FOUND;
}
void append(const std::filesystem::path& directory,const char* record,DWORD bytes) {
    const auto name=mutex_name(directory);
    Handle mutex{CreateMutexExW(nullptr,name.c_str(),0,SYNCHRONIZE|MUTEX_MODIFY_STATE)};
    if (!mutex.value) { return; }
    const auto wait=WaitForSingleObject(mutex.value,mutex_wait_ms);
    if (wait!=WAIT_OBJECT_0 && wait!=WAIT_ABANDONED) { return; }
    struct Release { HANDLE value; ~Release() { ReleaseMutex(value); } } release{mutex.value};
#ifdef OUTLOUD_ERROR_LOGGER_TEST
    Fault fault;
    { std::lock_guard lock(test_mutex); fault=test_fault; }
    if (fault==Fault::directory_access) { SetLastError(ERROR_ACCESS_DENIED); return; }
#endif
    // Only known directories/files are visited; no enumeration or archive scan.
    std::filesystem::create_directories(directory);
    const auto current=directory/L"engine.log", one=directory/L"engine.1.log", two=directory/L"engine.2.log";
    if (!bounded_archive(one) || !bounded_archive(two)) { return; }
    Handle file{CreateFileW(current.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,
        OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr)};
    if (file.value==INVALID_HANDLE_VALUE) { return; }
    std::uint64_t length=0;
    if (!regular_file(file.value,length)) { return; }
    if (length>file_limit) { // An externally oversized current log is discarded.
        LARGE_INTEGER zero{};
        if (!SetFilePointerEx(file.value,zero,nullptr,FILE_BEGIN) || !SetEndOfFile(file.value)) { return; }
        length=0;
    }
    if (length+bytes>file_limit) {
        CloseHandle(file.value); file.value=INVALID_HANDLE_VALUE;
        if (!DeleteFileW(two.c_str()) && GetLastError()!=ERROR_FILE_NOT_FOUND) { return; }
        if (!move_if_present(one,two) || !move_if_present(current,one)) { return; }
        file.value=CreateFileW(current.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,
            CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if (file.value==INVALID_HANDLE_VALUE) { return; }
    }
    LARGE_INTEGER zero{};
    LARGE_INTEGER before{};
    if (!SetFilePointerEx(file.value,zero,&before,FILE_END)) { return; }
    DWORD written=0;
    BOOL success=FALSE;
#ifdef OUTLOUD_ERROR_LOGGER_TEST
    if (fault==Fault::disk_write) {
        // Private test seam simulates a short write followed by a disk error.
        (void)WriteFile(file.value,record,bytes/2,&written,nullptr); SetLastError(ERROR_DISK_FULL);
    } else
#endif
    { success=WriteFile(file.value,record,bytes,&written,nullptr); }
    if (!success || written!=bytes) {
        // Best-effort removal of a partial record; never retry the write.
        if (SetFilePointerEx(file.value,before,nullptr,FILE_BEGIN)) { (void)SetEndOfFile(file.value); }
    }
    // No recursive reporting or synchronous disk flush.
}
}
void error(const Error& record) noexcept {
    PreserveErrors preserve;
    try {
#ifdef OUTLOUD_ERROR_LOGGER_TEST
        ++test_attempts;
#endif
        std::filesystem::path directory;
        if (!directory_path(directory)) { return; }
        directory=std::filesystem::absolute(directory).lexically_normal();
        SYSTEMTIME now{}; GetSystemTime(&now);
        std::array<char,record_limit> text{};
        const auto size=sprintf_s(text.data(),text.size(),
            "[%04u-%02u-%02uT%02u:%02u:%02u.%03uZ] [ERROR] [%s] [%s] [HRESULT=0x%08lX Win32=%lu Kind=%u PID=%lu TID=%lu] %s\n",
            static_cast<unsigned>(now.wYear),static_cast<unsigned>(now.wMonth),static_cast<unsigned>(now.wDay),
            static_cast<unsigned>(now.wHour),static_cast<unsigned>(now.wMinute),static_cast<unsigned>(now.wSecond),static_cast<unsigned>(now.wMilliseconds),
            component_name(record.component),event_name(record.event),static_cast<unsigned long>(record.hresult),
            record.system_error,static_cast<unsigned>(record.kind),GetCurrentProcessId(),GetCurrentThreadId(),message(record.kind));
        if (size>0 && static_cast<std::size_t>(size)<text.size()) { append(directory,text.data(),static_cast<DWORD>(size)); }
    } catch (...) {} // Logging cannot replace a TTS result or log its own failures.
}
#ifdef OUTLOUD_ERROR_LOGGER_TEST
void configure(const wchar_t* directory,Fault fault) noexcept {
    try { std::lock_guard lock(test_mutex); test_directory=directory ? directory : L""; test_fault=fault; test_attempts=0; }
    catch (...) {} // Tests set configuration only while no writer is active.
}
std::uint64_t attempts() noexcept { return test_attempts.load(); }
Fault fault() noexcept { std::lock_guard lock(test_mutex); return test_fault; }
HANDLE test_mutex_handle() {
    std::filesystem::path directory;
    if (!directory_path(directory)) { return nullptr; }
    const auto name=mutex_name(std::filesystem::absolute(directory).lexically_normal());
    return CreateMutexExW(nullptr,name.c_str(),0,SYNCHRONIZE|MUTEX_MODIFY_STATE);
}
#ifdef OUTLOUD_ERROR_LOGGER_TEST_DLL
extern "C" __declspec(dllexport) void __stdcall OutLoudLoggerTestConfigure(const wchar_t* directory,unsigned fault) {
    configure(directory,static_cast<Fault>(fault));
}
extern "C" __declspec(dllexport) std::uint64_t __stdcall OutLoudLoggerTestAttempts() { return attempts(); }
#endif
#endif
}
