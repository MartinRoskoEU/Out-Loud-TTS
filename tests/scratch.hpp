#pragma once
#include "voice_library_fs.hpp"
#include <stdexcept>

namespace outloud::test {
inline void cleanup_owned(const std::filesystem::path& path) noexcept {
    const auto attr = GetFileAttributesW(path.c_str());
    if (attr==INVALID_FILE_ATTRIBUTES) { return; }
    if (!(attr&FILE_ATTRIBUTE_DIRECTORY)) { (void)DeleteFileW(path.c_str()); return; }
    if (!(attr&FILE_ATTRIBUTE_REPARSE_POINT)) {
        try { for (const auto& name : detail::library_fs::children(path)) { cleanup_owned(path/name); } } catch (...) {}
    }
    (void)RemoveDirectoryW(path.c_str()); // Remove a junction itself, never its target.
}
struct Scratch {
    std::filesystem::path path;
    Scratch() {
        namespace fs = detail::library_fs;
        std::wstring temp(32768,L'\0'); const auto count = GetTempPathW(static_cast<DWORD>(temp.size()),temp.data());
        if (!count || count>=temp.size()) { throw std::runtime_error("Cannot locate test temp directory."); }
        temp.resize(count); const fs::Pins parents(temp);
        path = parents.path/(L"outloud-tests-"+fs::wide(fs::random_id()));
        if (!fs::within(path,parents.path) || path==parents.path || !CreateDirectoryW(path.c_str(),nullptr)) {
            throw std::runtime_error("Cannot create safe unique test scratch directory.");
        }
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    ~Scratch() { cleanup_owned(path); }
};
} // namespace outloud::test
