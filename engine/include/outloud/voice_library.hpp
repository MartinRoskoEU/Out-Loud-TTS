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
enum class VoiceImportKind { pocket_prepared, olv };
enum class VoiceHealth {
    valid, missing_voice, corrupt_voice, metadata_corrupt, unsupported_metadata_version,
    unsupported_voice_version, incompatible_model, hash_mismatch, unsafe_entry, unavailable, replacement_pending, removal_pending
};
[[nodiscard]] std::string_view voice_health_name(VoiceHealth health) noexcept;
struct VoiceMetadata {
    std::uint32_t schema_version = 1;
    std::string library_id, display_name;
    std::uint32_t olv_version = 1;
    std::string olv_sha256, source_sha256, compatibility_id, imported_utc, source_filename;
    VoiceImportKind import_kind = VoiceImportKind::olv;
    bool model_provenance_verified = false;
};
struct LibraryVoice {
    // Directory identity is authoritative even when metadata is corrupt.
    std::string library_id;
    std::optional<VoiceMetadata> metadata;
    std::filesystem::path managed_olv_path;
    VoiceHealth health = VoiceHealth::unavailable;
    std::string detail;
};
enum class DuplicateContentPolicy { reject, allow_separate_entry };
struct LibraryImportResult {
    LibraryVoice voice;
    std::vector<std::string> duplicate_ids;
};
struct VoiceReplaceOptions {
    VoiceImportOptions import;
    // Required optimistic concurrency assertions from a native read snapshot.
    std::string expected_display_name, expected_olv_sha256;
    // Called under the library lock with validated target and staged asset.
    // Must not re-enter VoiceLibrary. Exceptions propagate without publication.
    std::function<void(const LibraryVoice&, const std::filesystem::path&)> validate_before_publish;
};
struct LibraryReplaceResult {
    LibraryVoice voice;
    bool changed = false, cleanup_pending = false;
    std::string detail;
};
struct VoiceExportOptions {
    bool replace_existing = false;
    std::string expected_display_name, expected_olv_sha256;
    // "absent" or the native destination fingerprint captured before overwrite confirmation.
    std::optional<std::string> expected_destination;
};
// Read-only, local destination inspection; never creates a file or a library.
[[nodiscard]] std::string inspect_olv_export_destination(const std::filesystem::path&);
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
    std::optional<std::string> expected_display_name, expected_olv_sha256, expected_health;
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
    [[nodiscard]] LibraryVoice inspect_voice(std::string_view id) const;
    [[nodiscard]] LibraryImportResult import_pocket_voice(const std::filesystem::path& source,
        std::string_view display_name, const VoiceImportOptions& options,
        DuplicateContentPolicy duplicates = DuplicateContentPolicy::reject);
    [[nodiscard]] LibraryImportResult import_olv(const std::filesystem::path& source,
        std::string_view display_name, DuplicateContentPolicy duplicates = DuplicateContentPolicy::reject);
    [[nodiscard]] LibraryReplaceResult replace_pocket_voice(std::string_view id,
        const std::filesystem::path& source, const VoiceReplaceOptions& options);
    [[nodiscard]] LibraryReplaceResult replace_olv(std::string_view id,
        const std::filesystem::path& source, const VoiceReplaceOptions& options);
    [[nodiscard]] OlvInfo export_olv(std::string_view id, const std::filesystem::path& destination,
                                   bool replace_existing = false) const;
    [[nodiscard]] OlvInfo export_olv(std::string_view id, const std::filesystem::path& destination,
                                   const VoiceExportOptions&) const;
    [[nodiscard]] LibraryVoice rename_voice(std::string_view id, std::string_view display_name);
    [[nodiscard]] VoiceRemovalResult remove_voice(std::string_view id);
    [[nodiscard]] VoiceRemovalResult remove_voice(std::string_view id, const VoiceRemoveOptions&);
    // Explicitly discards unpublished staging work and completes already committed removals.
    // Also recovers journalled replacements in place; never deletes an active entry.
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
};
} // namespace outloud
