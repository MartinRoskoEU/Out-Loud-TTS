#pragma once
#include "voice_library_fs.hpp"

namespace outloud::detail::library_security {
// Protected inheritable DACL; no creator-owner or ordinary-user write grants.
inline constexpr auto directory_sddl = L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FRFX;;;BU)";
inline constexpr auto file_sddl = L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FRFX;;;BU)";
struct FileCreationSecurity {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    SECURITY_ATTRIBUTES attributes{};
    explicit FileCreationSecurity(bool machine);
    ~FileCreationSecurity();
    FileCreationSecurity(const FileCreationSecurity&) = delete;
    [[nodiscard]] SECURITY_ATTRIBUTES* get() noexcept { return descriptor ? &attributes : nullptr; }
};
// Only for this operation's newly converted, unpublished staged output.
void protect_created_file(const std::filesystem::path&);
[[nodiscard]] bool valid_descriptor(PSECURITY_DESCRIPTOR, bool directory, bool anchor);
[[nodiscard]] bool valid_dacl(PACL, bool directory);
void validate(HANDLE, const std::filesystem::path&, bool directory, bool anchor = false);
void require_machine_authority(const std::filesystem::path&);
bool create_directory(const std::filesystem::path&);
// Pins ProgramData/OutLoud and the library before examining/creating children.
// Existing ACLs are verified, never silently repaired.
[[nodiscard]] library_fs::Pins prepare_root(const std::filesystem::path&, bool mutate, bool create);
} // namespace outloud::detail::library_security
