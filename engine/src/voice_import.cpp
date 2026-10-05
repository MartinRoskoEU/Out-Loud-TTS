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
    OUTLOUD_ERROR_NAME(unsupported_dtype); OUTLOUD_ERROR_NAME(incompatible_model);
    OUTLOUD_ERROR_NAME(compatibility_required); OUTLOUD_ERROR_NAME(source_identity_mismatch);
    OUTLOUD_ERROR_NAME(invalid_destination); OUTLOUD_ERROR_NAME(destination_io);
    OUTLOUD_ERROR_NAME(olv_validation); OUTLOUD_ERROR_NAME(internal_failure);
#undef OUTLOUD_ERROR_NAME
    }
    return "internal_failure";
}
namespace {
using Code = VoiceImportErrorCode;
using detail::Bytes;
using Json = detail::voice_json::Value;
constexpr std::uint64_t maximum_payload = 1024ULL * 1024 * 1024;
constexpr std::uint64_t maximum_header = 64 * 1024;
constexpr std::uint64_t bytes_per_position = 6 * 2 * 16 * 64 * 4;

[[noreturn]] void fail(Code code, const char* message, const std::filesystem::path& path,
                       const std::string& tensor = {}, std::uint32_t system_error = 0) {
    throw VoiceImportError(code, message, path, tensor, system_error);
}
template<class Function> auto guarded(const std::filesystem::path& path, Function function) {
    try { return function(); }
    catch (const VoiceImportError&) { throw; }
    catch (const std::exception& error) { throw VoiceImportError(Code::internal_failure, error.what(), path); }
}
std::filesystem::path local_path(const std::filesystem::path& input, Code code) {
    if (input.empty()) { fail(code, "A local file path is required", input); }
    std::error_code error;
    auto path = std::filesystem::absolute(input, error).lexically_normal();
    if (error) { fail(code, "Cannot resolve local path", input, {}, static_cast<std::uint32_t>(error.value())); }
    const auto& native = path.native();
    // Reject UNC, device namespaces, alternate streams and mapped network drives.
    if (native.size() < 3 || native[1] != L':' || native[2] != L'\\' ||
        native.find(L':', 2) != std::wstring::npos || native.find(L'\0') != std::wstring::npos ||
        GetDriveTypeW(path.root_path().c_str()) == DRIVE_REMOTE) {
        fail(code, "Only local drive file paths are supported", input);
    }
    return path;
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
std::array<unsigned char,32> hash_bytes(std::string_view hash, Code code, const std::filesystem::path& path) {
    if (hash.size() != 64) { fail(code, "SHA256/compatibility identity must have 64 hexadecimal digits", path); }
    std::array<unsigned char,32> result{};
    for (std::size_t i = 0; i < hash.size(); ++i) {
        const char ch = hash[i]; unsigned digit = 0;
        if (ch >= '0' && ch <= '9') { digit = static_cast<unsigned>(ch - '0'); }
        else if (ch >= 'a' && ch <= 'f') { digit = static_cast<unsigned>(ch - 'a') + 10; }
        else if (ch >= 'A' && ch <= 'F') { digit = static_cast<unsigned>(ch - 'A') + 10; }
        else { fail(code, "Invalid hexadecimal SHA256/compatibility identity", path); }
        result[i / 2] |= static_cast<unsigned char>(digit << (i % 2 ? 0 : 4));
    }
    return result;
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
struct Source {
    std::unique_ptr<detail::MappedFile> file;
    std::array<Bytes,6> cache;
    PocketVoiceInfo info;
    explicit Source(const std::filesystem::path& path) {
        file = map_source(path, maximum_payload + maximum_header + 8 + 96, Code::corrupt_source);
        const auto bytes = file->bytes();
        if (bytes.size() < 8) { fail(Code::corrupt_source, "Truncated Safetensors length field", path); }
        const auto header_size = detail::u64(bytes, 0);
        if (header_size > maximum_header) { fail(Code::unsupported_format, "Unsupported container or Safetensors header over 64 KiB", path); }
        if (!header_size || header_size > bytes.size() - 8) { fail(Code::corrupt_source, "Truncated Safetensors header", path); }
        const auto header = bytes.subspan(8, static_cast<std::size_t>(header_size));
        if (header.front() != '{') { fail(Code::unsupported_format, "Expected a Safetensors JSON object header", path); }
        Json root;
        try { root = detail::voice_json::Parser({reinterpret_cast<const char*>(header.data()), header.size()}).parse(); }
        catch (const std::invalid_argument& error) { throw VoiceImportError(Code::corrupt_source, error.what(), path); }
        if (root.kind != Json::Kind::object) { fail(Code::unsupported_format, "Expected Safetensors object", path); }
        if (const auto metadata = root.object.find("__metadata__"); metadata != root.object.end()) {
            if (metadata->second.kind != Json::Kind::object) { fail(Code::corrupt_source, "Metadata must be a string map", path); }
            for (const auto& [key,value] : metadata->second.object) {
                if (value.kind != Json::Kind::string) { fail(Code::corrupt_source, "Metadata values must be strings", path); }
                info.metadata.emplace(key, value.text);
            }
            root.object.erase(metadata);
        }
        if (root.object.size() != 18) { fail(Code::malformed_tensor, "Expected exactly six cache/offset/pad groups; legacy current_end is unsupported", path); }
        const auto payload = bytes.subspan(8 + static_cast<std::size_t>(header_size));
        std::vector<std::pair<std::uint64_t,std::uint64_t>> intervals;
        for (std::size_t layer = 0; layer < 6; ++layer) {
            const auto base = "transformer.layers." + std::to_string(layer) + ".self_attn/";
            std::array<Bytes,3> tensors;
            std::uint64_t prefix = 0;
            const std::array<const char*,3> names{"cache", "offset", "pad"};
            for (std::size_t index = 0; index < names.size(); ++index) {
                const auto name = base + names[index];
                const auto found = root.object.find(name);
                if (found == root.object.end()) { fail(Code::malformed_tensor, "Missing Pocket voice tensor", path, name); }
                const auto& record = found->second;
                if (record.kind != Json::Kind::object || record.object.size() != 3) {
                    fail(Code::malformed_tensor, "Expected dtype, shape and data_offsets only", path, name);
                }
                const auto& dtype = field(record, "dtype", path, name);
                if (dtype.kind != Json::Kind::string) { fail(Code::malformed_tensor, "Tensor dtype must be a string", path, name); }
                if (dtype.text != (index == 0 ? "F32" : "I64")) {
                    fail(Code::unsupported_dtype, "Expected F32 cache or I64 offset/pad", path, name);
                }
                const auto shape = integers(field(record, "shape", path, name), Code::invalid_shape, path, name);
                std::uint64_t size = 8;
                if (index == 0) {
                    if (shape.size() != 5 || shape[0] != 2 || shape[1] != 1 || !shape[2] ||
                        shape[2] > maximum_payload / bytes_per_position || shape[3] != 16 || shape[4] != 64) {
                        fail(Code::invalid_shape, "Expected bounded F32 [2,1,N,16,64] with N positive", path, name);
                    }
                    prefix = shape[2]; size = 2 * prefix * 16 * 64 * 4;
                } else if (shape != std::vector<std::uint64_t>{1}) {
                    fail(Code::invalid_shape, "Expected I64 [1] offset/pad", path, name);
                }
                const auto offsets = integers(field(record, "data_offsets", path, name), Code::malformed_tensor, path, name);
                if (offsets.size() != 2 || offsets[0] > offsets[1] || offsets[1] > payload.size() ||
                    offsets[1] - offsets[0] != size) {
                    fail(Code::malformed_tensor, "Tensor byte bounds/length disagree with shape/dtype", path, name);
                }
                intervals.emplace_back(offsets[0], offsets[1]);
                tensors[index] = payload.subspan(static_cast<std::size_t>(offsets[0]), static_cast<std::size_t>(size));
            }
            if (detail::u64(tensors[1], 0) != prefix || detail::u64(tensors[2], 0) != 0 ||
                (layer && info.prefix_length != prefix)) {
                fail(Code::malformed_tensor, "Unequal prefixes, offset != N, or nonzero pad", path, base);
            }
            info.prefix_length = static_cast<std::uint32_t>(prefix);
            cache[layer] = tensors[0];
            for (std::size_t at = 0; at < cache[layer].size(); at += 4) {
                if ((detail::u32(cache[layer], at) & 0x7f800000) == 0x7f800000) {
                    fail(Code::malformed_tensor, "Cache contains infinity, NaN or unused positions", path, base + "cache");
                }
            }
        }
        std::sort(intervals.begin(), intervals.end());
        std::uint64_t end = 0;
        for (const auto& interval : intervals) {
            if (interval.first != end) { fail(Code::corrupt_source, "Overlapping or non-contiguous source tensors", path); }
            end = interval.second;
        }
        if (end != payload.size()) { fail(Code::corrupt_source, "Trailing source payload bytes", path); }
        info.source_bytes = bytes.size(); info.payload_bytes = bytes_per_position * info.prefix_length;
        info.source_sha256 = detail::hex(detail::sha256(bytes));
        detail::Sha256 hash;
        for (auto layer : cache) { hash.update(layer); }
        info.payload_sha256 = detail::hex(hash.finish());
    }
};
void put(std::span<unsigned char> bytes, std::size_t offset, std::uint64_t value, std::size_t width = 4) {
    for (std::size_t i = 0; i < width; ++i) { bytes[offset + i] = static_cast<unsigned char>(value >> (8 * i)); }
}
std::array<unsigned char,256> voice_header(const Source& source, const std::filesystem::path& path) {
    std::array<unsigned char,256> header{};
    constexpr std::string_view magic = "OLVPKT01";
    std::copy(magic.begin(), magic.end(), header.begin());
    put(header,8,1); put(header,12,256); put(header,16,0x01020304);
    put(header,20,1); put(header,24,1); put(header,28,2);
    const auto compatibility = hash_bytes(pocket_model_compatibility_id, Code::internal_failure, path);
    const auto identity = hash_bytes(source.info.source_sha256, Code::internal_failure, path);
    const auto payload_hash = hash_bytes(source.info.payload_sha256, Code::internal_failure, path);
    std::copy(compatibility.begin(),compatibility.end(),header.begin()+32);
    std::copy(identity.begin(),identity.end(),header.begin()+64);
    std::copy(payload_hash.begin(),payload_hash.end(),header.begin()+128);
    put(header,160,256+source.info.payload_bytes,8); put(header,168,256,8);
    put(header,176,256,8); put(header,184,source.info.payload_bytes,8);
    put(header,192,6); put(header,196,source.info.prefix_length);
    put(header,200,16); put(header,204,64); put(header,208,1);
    put(header,212,24000); put(header,216,1920); put(header,220,1);
    put(header,224,source.info.payload_bytes/4,8); put(header,232,1);
    detail::Sha256 container; container.update(header);
    for (auto layer : source.cache) { container.update(layer); }
    const auto digest = container.finish(); std::copy(digest.begin(),digest.end(),header.begin()+96);
    return header;
}
struct Temporary {
    std::filesystem::path path;
    HANDLE handle = INVALID_HANDLE_VALUE;
    explicit Temporary(const std::filesystem::path& destination) {
        std::array<unsigned char,16> random{};
        for (unsigned attempt = 0; attempt < 8; ++attempt) {
            if (BCryptGenRandom(nullptr,random.data(),static_cast<ULONG>(random.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
                fail(Code::destination_io, "Cannot create unique temporary name", destination);
            }
            const auto name = detail::hex(random);
            path = destination.parent_path() / (".outloud-voice-" + name + ".tmp");
            handle = CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            if (handle != INVALID_HANDLE_VALUE) { return; }
            const auto error = GetLastError();
            if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) {
                fail(Code::destination_io, "Cannot create temporary destination file", destination, {}, error);
            }
        }
        fail(Code::destination_io, "Temporary destination name collision", destination);
    }
    ~Temporary() {
        if (handle != INVALID_HANDLE_VALUE) { CloseHandle(handle); }
        if (!path.empty()) { DeleteFileW(path.c_str()); }
    }
    void write(Bytes bytes) {
        while (!bytes.empty()) {
            const auto chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size(),8*1024*1024));
            DWORD written = 0;
            if (!WriteFile(handle,bytes.data(),chunk,&written,nullptr) || written != chunk) {
                fail(Code::destination_io, "Cannot write complete temporary voice", path, {}, GetLastError());
            }
            bytes = bytes.subspan(chunk);
        }
    }
    void close() {
        if (!FlushFileBuffers(handle)) { fail(Code::destination_io, "Cannot flush temporary voice", path, {}, GetLastError()); }
        const auto closing = handle; handle = INVALID_HANDLE_VALUE;
        if (!CloseHandle(closing)) { fail(Code::destination_io, "Cannot close temporary voice", path, {}, GetLastError()); }
    }
};
} // namespace

PocketVoiceInfo inspect_pocket_voice(const std::filesystem::path& source) {
    return guarded(source,[&] { return Source(local_path(source,Code::source_io)).info; });
}
PocketVoiceInfo validate_pocket_voice(const std::filesystem::path& source) { return inspect_pocket_voice(source); }
OlvInfo validate_olv(const std::filesystem::path& input, std::string_view compatibility_id) {
    return guarded(input,[&] {
        const auto path = local_path(input,Code::source_io);
        const auto requested = hash_bytes(compatibility_id,Code::incompatible_model,path);
        const auto file = map_source(path,maximum_payload+256,Code::olv_validation);
        try {
            const OutLoudVoice voice(path);
            if (voice.compatibility_id() != detail::hex(requested)) {
                fail(Code::incompatible_model, "Voice/model compatibility mismatch", path);
            }
            return OlvInfo{voice.version(),voice.prefix_length(),file->bytes().size(),voice.payload_bytes(),
                           voice.source_sha256(),voice.compatibility_id(),detail::hex(detail::sha256(file->bytes()))};
        } catch (const VoiceImportError&) { throw; }
        catch (const std::exception& e) { throw VoiceImportError(Code::olv_validation,e.what(),path); }
    });
}
VoiceImportResult convert_pocket_voice_to_olv(const std::filesystem::path& input,
    const std::filesystem::path& output, const VoiceImportOptions& options) {
    return guarded(input,[&] {
        if (options.declared_model_compatibility_id.empty()) {
            fail(Code::compatibility_required, "Source has no proven model identity; explicitly declare the preparing model compatibility ID", input);
        }
        if (detail::hex(hash_bytes(options.declared_model_compatibility_id,Code::incompatible_model,input)) != pocket_model_compatibility_id) {
            fail(Code::incompatible_model, "Only the pinned Pocket English 3.3.0 synthesis model is supported", input);
        }
        const auto source_path = local_path(input,Code::source_io);
        Source source(source_path);
        if (!options.expected_source_sha256.empty() &&
            detail::hex(hash_bytes(options.expected_source_sha256,Code::source_identity_mismatch,source_path)) != source.info.source_sha256) {
            fail(Code::source_identity_mismatch, "Source SHA256 disagrees with expected identity", source_path);
        }
        const auto destination = local_path(output,Code::invalid_destination);
        auto extension = destination.extension().native();
        std::transform(extension.begin(),extension.end(),extension.begin(),[](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
        if (extension != L".olv") { fail(Code::invalid_destination, "Destination must have .olv extension", output); }
        const auto parent_attributes = GetFileAttributesW(destination.parent_path().c_str());
        if (parent_attributes == INVALID_FILE_ATTRIBUTES || !(parent_attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            fail(Code::invalid_destination, "Destination parent directory must exist", destination);
        }
        std::error_code error;
        if (std::filesystem::equivalent(source_path,destination,error)) {
            fail(Code::invalid_destination, "Destination must not alias source", destination);
        }
        const auto attributes = GetFileAttributesW(destination.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            ((attributes & FILE_ATTRIBUTE_DIRECTORY) || !options.replace_existing)) {
            fail(Code::invalid_destination, "Destination exists; replacement requires explicit option and a file", destination);
        }
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const auto access_error = GetLastError();
            if (access_error != ERROR_FILE_NOT_FOUND && access_error != ERROR_PATH_NOT_FOUND) {
                fail(Code::destination_io, "Cannot access destination", destination, {}, access_error);
            }
        }
        Temporary temporary(destination);
        temporary.write(voice_header(source,source_path));
        for (auto layer : source.cache) { temporary.write(layer); }
        temporary.close();
        // The validator releases its mappings before atomic same-directory finalization.
        auto voice = validate_olv(temporary.path);
        if (!MoveFileExW(temporary.path.c_str(),destination.c_str(),MOVEFILE_WRITE_THROUGH |
                         (options.replace_existing ? MOVEFILE_REPLACE_EXISTING : 0))) {
            fail(Code::destination_io, "Cannot finalize voice; previous destination retained", destination, {}, GetLastError());
        }
        temporary.path.clear();
        return VoiceImportResult{std::move(source.info),std::move(voice)};
    });
}
} // namespace outloud
