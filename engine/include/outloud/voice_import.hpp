#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <map>
#include <stdexcept>
#include <stop_token>

namespace outloud {

enum class VoiceImportErrorCode {
    file_not_found, source_io, unsupported_format, corrupt_source,
    malformed_tensor, invalid_shape, unsupported_dtype, invalid_destination = 10,
    destination_io, internal_failure = 13
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

// Native container validation; model-dependent checks remain in the Rust runtime.
[[nodiscard]] std::string validate_native_profile(const std::filesystem::path& source, std::stop_token cancellation = {});

} // namespace outloud
