#pragma once

#include "outloud/assets.hpp"

#include <map>
#include <stdexcept>

namespace outloud {

enum class VoiceImportErrorCode {
    file_not_found, source_io, unsupported_format, corrupt_source,
    malformed_tensor, invalid_shape, unsupported_dtype, incompatible_model,
    compatibility_required, source_identity_mismatch, invalid_destination,
    destination_io, olv_validation, internal_failure
};
[[nodiscard]] std::string_view voice_import_error_name(VoiceImportErrorCode code) noexcept;

class VoiceImportError : public std::runtime_error {
public:
    VoiceImportError(VoiceImportErrorCode code, std::string message,
                     std::filesystem::path path = {}, std::string tensor = {},
                     std::uint32_t system_error = 0);
    [[nodiscard]] VoiceImportErrorCode code() const noexcept { return code_; }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    [[nodiscard]] const std::string& tensor() const noexcept { return tensor_; }
    [[nodiscard]] std::uint32_t system_error() const noexcept { return system_error_; }
private:
    VoiceImportErrorCode code_;
    std::filesystem::path path_;
    std::string tensor_;
    std::uint32_t system_error_;
};

// Inspection includes full schema/bounds/finite-value validation, not just a header peek.
// Safetensors metadata is opaque: the pinned exporter defines no model/version fields.
struct PocketVoiceInfo {
    std::uint32_t prefix_length = 0;
    std::uint64_t source_bytes = 0, payload_bytes = 0;
    std::string source_sha256, payload_sha256;
    std::map<std::string, std::string> metadata;
    bool model_provenance_verified = false;
};
struct OlvInfo {
    std::uint32_t format_version = 1, prefix_length = 0;
    std::uint64_t file_bytes = 0, payload_bytes = 0;
    std::string source_sha256, compatibility_id, file_sha256;
};
struct VoiceImportOptions {
    // Required assertion by the caller about the model that prepared the prefix.
    // Only pocket_model_compatibility_id is currently supported. This is not proof.
    std::string declared_model_compatibility_id;
    std::string expected_source_sha256; // optional independently known source identity
    bool replace_existing = false;
};
struct VoiceImportResult {
    PocketVoiceInfo source;
    OlvInfo voice;
};

// Local files only; no neural execution, Python, downloads or process helpers.
// Paths support Windows Unicode. Parent directories must already exist.
// Calls own their state and may run independently; destination collisions fail by default.
[[nodiscard]] PocketVoiceInfo inspect_pocket_voice(const std::filesystem::path& source);
[[nodiscard]] PocketVoiceInfo validate_pocket_voice(const std::filesystem::path& source);
[[nodiscard]] VoiceImportResult convert_pocket_voice_to_olv(
    const std::filesystem::path& source, const std::filesystem::path& destination,
    const VoiceImportOptions& options);
// Validates through the production OutLoudVoice loader, plus the requested pairing.
[[nodiscard]] OlvInfo validate_olv(const std::filesystem::path& path,
    std::string_view compatibility_id = pocket_model_compatibility_id);

} // namespace outloud
