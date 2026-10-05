#pragma once
#include "voice_library_fs.hpp"
#include <stdexcept>
namespace sapi_test {
namespace fs = outloud::detail::library_fs;
inline void cleanup(const std::filesystem::path& path) noexcept {
    const auto attr = GetFileAttributesW(path.c_str());
    if (attr==INVALID_FILE_ATTRIBUTES) { return; }
    if (!(attr&FILE_ATTRIBUTE_DIRECTORY)) { (void)DeleteFileW(path.c_str()); return; }
    if (!(attr&FILE_ATTRIBUTE_REPARSE_POINT)) {
        try { for (const auto& name : fs::children(path)) { cleanup(path/name); } } catch (...) {}
    }
    (void)RemoveDirectoryW(path.c_str());
}
struct Scratch {
    std::filesystem::path path;
    Scratch() {
        std::wstring temp(32768,L'\0'); const auto count = GetTempPathW(static_cast<DWORD>(temp.size()),temp.data());
        if (!count || count>=temp.size()) { throw std::runtime_error("Cannot locate test temp directory."); }
        temp.resize(count); fs::Pins parents(temp);
        path = parents.path/(L"outloud-sapi-tests-"+fs::wide(fs::random_id()));
        if (!fs::within(path,parents.path) || path==parents.path || !CreateDirectoryW(path.c_str(),nullptr)) {
            throw std::runtime_error("Cannot create safe unique test scratch directory.");
        }
    }
    ~Scratch() { cleanup(path); }
};
}
