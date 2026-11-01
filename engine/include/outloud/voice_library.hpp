#pragma once

#include "outloud/voice_import.hpp"

#include <optional>
#include <functional>

namespace outloud {

enum class VoiceLibraryErrorCode {
    invalid_library_root, library_unavailable, library_busy, unsafe_path,
    invalid_id, voice_not_found, metadata_corrupt, unsupported_metadata_version,
    invalid_display_name, duplicate_content, import_failed, incompatible_voice,
    corrupt_voice, replace_required, source_io, destination_io, remove_failed,
    internal_failure, library_access_denied, library_security_invalid,
    voice_in_use, replacement_conflict, replacement_pending, removal_conflict, removal_partial, removal_pending, export_conflict
};
[[nodiscard]] std::string_view voice_library_error_name(VoiceLibraryErrorCode code) noexcept;
class VoiceLibraryError : public std::runtime_error {
public:
    VoiceLibraryError(VoiceLibraryErrorCode code, std::string message,
        std::filesystem::path path = {}, std::uint32_t system_error = 0,
        std::optional<VoiceImportError> import_error = {}, std::vector<std::string> duplicate_ids = {});
    [[nodiscard]] VoiceLibraryErrorCode code() const noexcept { return code_; }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    [[nodiscard]] std::uint32_t system_error() const noexcept { return system_error_; }
    [[nodiscard]] const std::optional<VoiceImportError>& import_error() const noexcept { return import_error_; }
    [[nodiscard]] const std::vector<std::string>& duplicate_ids() const noexcept { return duplicate_ids_; }
private:
    VoiceLibraryErrorCode code_;
    std::filesystem::path path_;
    std::uint32_t system_error_;
    std::optional<VoiceImportError> import_error_;
    std::vector<std::string> duplicate_ids_;
};
enum class VoiceImportKind { pocket_native };
enum class VoiceHealth {
    valid, missing_voice, corrupt_voice, metadata_corrupt, unsupported_metadata_version,
    unsupported_voice_version, incompatible_model, hash_mismatch, unsafe_entry, unavailable, replacement_pending, removal_pending
};
[[nodiscard]] std::string_view voice_health_name(VoiceHealth health) noexcept;
struct VoiceMetadata {
    std::uint32_t schema_version = 2;
    std::string library_id, display_name, source_sha256, imported_utc, source_filename;
    VoiceImportKind import_kind = VoiceImportKind::pocket_native;
    // Native profiles have no model identity, revision or provenance.
    std::string profile_sha256;
    [[nodiscard]] const std::string& content_sha256() const noexcept { return profile_sha256; }
};
struct LibraryVoice {
    // Directory identity is authoritative even when metadata is corrupt.
    std::string library_id;
    std::optional<VoiceMetadata> metadata;
    VoiceHealth health = VoiceHealth::unavailable;
    std::string detail;
    std::filesystem::path managed_native_path;
};
enum class DuplicateContentPolicy { reject, allow_separate_entry };
struct LibraryImportResult {
    LibraryVoice voice;
    std::vector<std::string> duplicate_ids;
};


struct VoiceExportOptions {
    bool replace_existing = false;
    std::string expected_display_name, expected_content_sha256;
    // "absent" or the native destination fingerprint captured before overwrite confirmation.
    std::optional<std::string> expected_destination;
};
// Read-only, local destination inspection; never creates a file or a library.
[[nodiscard]] std::string inspect_native_export_destination(const std::filesystem::path&);
struct VoiceRemovalResult {
    // Successful removal leaves no active entry. Trash cleanup can be retried.
    bool cleanup_pending = false;
    std::string detail;
};
// Returns "absent" or a strict owned-token SHA-256 identity; errors must throw.
// Called under the library lock: implementations must not re-enter VoiceLibrary.
using RemovalRegistrationIdentity = std::function<std::string(std::string_view)>;
enum class RemovalRecoveryState { none, restored, removed };
struct RemovalRecoveryResult {
    RemovalRecoveryState state = RemovalRecoveryState::none;
    bool cleanup_pending = false;
    std::string detail;
};
struct VoiceRemoveOptions {
    // Name/hash/health assertions are required for coordinated removal. Empty
    // name/hash assert absent metadata; standalone removal remains available.
    std::optional<std::string> expected_display_name, expected_content_sha256, expected_health;
    // Runs under the library lock AFTER reversible detach, BEFORE file deletion.
    // Must not re-enter VoiceLibrary. Failure attempts to restore the directory.
    RemovalRegistrationIdentity registration_identity;
    std::function<void(const LibraryVoice&, std::string_view)> remove_registration;
};
struct LibraryCleanupResult {
    std::size_t cleaned = 0;
    std::vector<std::filesystem::path> pending;
    std::vector<VoiceLibraryError> problems;
};

// SHGetKnownFolderPath(FOLDERID_ProgramData), never creates the real library.
[[nodiscard]] std::filesystem::path default_voice_library_root();
class VoiceLibrary {
public:
    explicit VoiceLibrary(std::filesystem::path root = default_voice_library_root());
    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }
    // True only for the canonical production root. Explicit other roots are
    // isolated test storage; they do not grant machine-token eligibility.
    [[nodiscard]] bool is_machine_storage() const noexcept { return machine_storage_; }
    [[nodiscard]] std::vector<LibraryVoice> list_voices() const;
    [[nodiscard]] LibraryVoice inspect_voice(std::string_view id, std::stop_token cancellation = {}) const;
    [[nodiscard]] LibraryImportResult import_native_profile(const std::filesystem::path& source,
        std::string_view display_name, DuplicateContentPolicy duplicates = DuplicateContentPolicy::reject);
    [[nodiscard]] std::string export_native_profile(std::string_view id, const std::filesystem::path& destination,
        const VoiceExportOptions& options = {}) const;
    [[nodiscard]] LibraryVoice rename_voice(std::string_view id, std::string_view display_name);
    [[nodiscard]] VoiceRemovalResult remove_voice(std::string_view id);
    [[nodiscard]] VoiceRemovalResult remove_voice(std::string_view id, const VoiceRemoveOptions&);
    // Explicitly discards unpublished staging work and completes already committed removals.
    // Unsafe/locked/unknown recovery state is retained and reported.
    [[nodiscard]] LibraryCleanupResult cleanup_pending(const RemovalRegistrationIdentity& registration_identity = {});
    // Explicit mutation/maintenance only; read snapshots never invoke recovery.
    [[nodiscard]] RemovalRecoveryResult recover_removal(std::string_view id,
        const RemovalRegistrationIdentity& registration_identity = {});
private:
    friend class SapiVoiceManager;
    // Only SAPI's read snapshot can borrow validated rows, while Session's shared
    // lock is held. This is not a caller-supplied validation/mutation capability.
    void with_read_snapshot(const std::function<void(std::vector<LibraryVoice>&&)>&) const;
    std::filesystem::path root_;
    bool machine_storage_ = false;
    [[nodiscard]] std::string export_asset(std::string_view, const std::filesystem::path&, const VoiceExportOptions&) const;
};
} // namespace outloud
