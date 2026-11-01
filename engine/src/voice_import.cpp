#include "outloud/voice_import.hpp"
#include "asset_io.hpp"
#include "voice_json.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cwctype>

namespace outloud {
VoiceImportError::VoiceImportError(VoiceImportErrorCode code, std::string message,
    std::filesystem::path path, std::string tensor, std::uint32_t system_error)
    : std::runtime_error(std::move(message)), code_(code), path_(std::move(path)),
      tensor_(std::move(tensor)), system_error_(system_error) {}
std::string_view voice_import_error_name(VoiceImportErrorCode code) noexcept {
    switch (code) {
#define OUTLOUD_ERROR_NAME(name) case VoiceImportErrorCode::name: return #name
    OUTLOUD_ERROR_NAME(file_not_found); OUTLOUD_ERROR_NAME(source_io);
    OUTLOUD_ERROR_NAME(unsupported_format); OUTLOUD_ERROR_NAME(corrupt_source);
    OUTLOUD_ERROR_NAME(malformed_tensor); OUTLOUD_ERROR_NAME(invalid_shape);
    OUTLOUD_ERROR_NAME(unsupported_dtype);
    OUTLOUD_ERROR_NAME(invalid_destination); OUTLOUD_ERROR_NAME(destination_io);
    OUTLOUD_ERROR_NAME(internal_failure);
#undef OUTLOUD_ERROR_NAME
    }
    return "internal_failure";
}
namespace {
using Code = VoiceImportErrorCode;
using Json = detail::voice_json::Value;
[[noreturn]] void fail(Code code, const char* message, const std::filesystem::path& path,
                       const std::string& tensor = {}, std::uint32_t system_error = 0) {
    throw VoiceImportError(code, message, path, tensor, system_error);
}
template<class Function> auto guarded(const std::filesystem::path& path, Function function) {
    try { return function(); }
    catch (const VoiceImportError&) { throw; }
    catch (const std::exception& error) { throw VoiceImportError(Code::internal_failure, error.what(), path); }
}
std::unique_ptr<detail::MappedFile> map_source(const std::filesystem::path& path, std::uint64_t maximum,
                                            Code invalid_code) {
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const auto error = GetLastError();
        fail(error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? Code::file_not_found : Code::source_io,
             "Cannot access source file", path, {}, error);
    }
    if (attributes & FILE_ATTRIBUTE_DIRECTORY) { fail(Code::source_io, "Source is a directory", path); }
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) { fail(Code::source_io, "Cannot determine source size", path, {}, static_cast<std::uint32_t>(error.value())); }
    if (!size || size > maximum) { fail(invalid_code, "Empty or oversized source file", path); }
    try { return std::make_unique<detail::MappedFile>(path, maximum); }
    catch (const std::exception& e) { throw VoiceImportError(Code::source_io, e.what(), path); }
}
const Json& field(const Json& value, const char* key, const std::filesystem::path& path, const std::string& tensor) {
    const auto found = value.object.find(key);
    if (value.kind != Json::Kind::object || found == value.object.end()) {
        fail(Code::malformed_tensor, "Missing tensor record field", path, tensor);
    }
    return found->second;
}
std::vector<std::uint64_t> integers(const Json& value, Code code, const std::filesystem::path& path,
                                   const std::string& tensor) {
    if (value.kind != Json::Kind::array) { fail(code, "Tensor field must be an integer array", path, tensor); }
    std::vector<std::uint64_t> result;
    for (const auto& item : value.array) {
        std::uint64_t number = 0;
        if (!detail::voice_json::unsigned_integer(item, number)) {
            fail(code, "Tensor field must contain unsigned integers", path, tensor);
        }
        result.push_back(number);
    }
    return result;
}
}
std::string validate_native_profile(const std::filesystem::path& path,std::stop_token cancellation) {
    return guarded(path, [&] {
        const auto file = map_source(path, 1024ULL * 1024 * 1024, Code::corrupt_source);
        const auto bytes = file->bytes();
        if (bytes.size() < 8) { fail(Code::corrupt_source, "Truncated native Safetensors profile", path); }
        const auto length = detail::u64(bytes, 0);
        if (!length || length > 16 * 1024 * 1024 || length > bytes.size() - 8) {
            fail(Code::corrupt_source, "Invalid native Safetensors header length", path);
        }
        Json root;
        try { root = detail::voice_json::Parser({reinterpret_cast<const char*>(bytes.data() + 8),
            static_cast<std::size_t>(length)}, 131072, 8192, 32).parse(); }
        catch (const std::exception& error) { fail(Code::corrupt_source, error.what(), path); }
        if (root.kind != Json::Kind::object) { fail(Code::corrupt_source, "Expected a native Safetensors object", path); }
        const auto payload = bytes.size() - 8 - length;
        std::vector<std::pair<std::uint64_t,std::uint64_t>> regions;
        for (const auto& [name, tensor] : root.object) {
            if (name == "__metadata__") {
                if (tensor.kind != Json::Kind::object) { fail(Code::corrupt_source, "Invalid Safetensors metadata", path); }
                for (const auto& [key, value] : tensor.object) {
                    (void)key;
                    if (value.kind != Json::Kind::string) { fail(Code::corrupt_source, "Invalid Safetensors metadata value", path); }
                }
                continue;
            }
            const auto& dtype = field(tensor, "dtype", path, name);
            const auto shape = integers(field(tensor, "shape", path, name), Code::invalid_shape, path, name);
            const auto offsets = integers(field(tensor, "data_offsets", path, name), Code::malformed_tensor, path, name);
            const auto type = dtype.text;
            const std::uint64_t width = type == "F64" || type == "I64" || type == "U64" ? 8 :
                type == "F32" || type == "I32" || type == "U32" ? 4 :
                type == "F16" || type == "BF16" || type == "I16" || type == "U16" ? 2 :
                type == "I8" || type == "U8" || type == "BOOL" ? 1 : 0;
            if (!width || dtype.kind != Json::Kind::string || tensor.object.size() != 3) {
                fail(Code::corrupt_source, "Invalid native tensor descriptor", path, name);
            }
            std::uint64_t size = width;
            for (const auto dimension : shape) {
                if (dimension && size > UINT64_MAX / dimension) { fail(Code::corrupt_source, "Tensor size overflow", path, name); }
                size *= dimension;
            }
            if (offsets.size() != 2 || offsets[0] > offsets[1] || offsets[1] > payload || offsets[1] - offsets[0] != size) {
                fail(Code::corrupt_source, "Native tensor bounds disagree with shape", path, name);
            }
            regions.emplace_back(offsets[0], offsets[1]);
        }
        std::sort(regions.begin(), regions.end());
        std::uint64_t end = 0;
        if (regions.empty()) { fail(Code::corrupt_source, "Native profile has no tensors", path); }
        for (const auto& region : regions) {
            if (region.first != end) { fail(Code::corrupt_source, "Overlapping or incomplete native tensor payload", path); }
            end = region.second;
        }
        if (end != payload) { fail(Code::corrupt_source, "Trailing native tensor data", path); }
        detail::Sha256 hash;
        for (std::size_t at=0;at<bytes.size();) {
            if (cancellation.stop_requested()) { fail(Code::source_io,"Native profile verification cancelled",path); }
            const auto count=(std::min)(bytes.size()-at,std::size_t{131072}); hash.update(bytes.subspan(at,count)); at+=count;
        }
        return detail::hex(hash.finish());
    });
}
} // namespace outloud
