#pragma once
#include "outloud/sapi_voices.hpp"
#include <functional>
#include <span>

namespace outloud::frontend::mutation {
inline constexpr std::uint32_t magic = 0x314d4c4f, version = 4, limit = 65536;
enum class Operation : std::uint32_t { remove = 3, import_native = 7, replace_native = 10 };
enum class Outcome : std::uint32_t { success = 1, failed = 2, partial = 3, started = 4, removed = 8, removal_warning = 9 };
struct Request { std::filesystem::path source; std::string display_name; Operation kind=Operation::import_native; std::string target_id, expected_hash, expected_health, expected_source_hash; };
struct Error {
    std::string domain, code, message, path, library_code, import_code, tensor;
    std::uint32_t system_error = 0;
    std::vector<std::string> duplicate_ids;
};
struct Result { Outcome outcome = Outcome::failed; std::string library_id; Error error; Operation kind=Operation::import_native; };
// Private, statically linked seams. No injection/root/scope options in the helper.
Request decode_request(std::span<const std::uint8_t>);
std::vector<std::uint8_t> encode_result(const Result&);
SapiVoiceRuntime runtime_from_registration(const std::optional<SapiRegistryValue>&);
SapiVoiceRuntime resolve_machine_runtime();
Result import_and_install(const Request&, VoiceLibrary, std::shared_ptr<SapiVoiceRegistry>,
    const std::function<SapiVoiceRuntime()>&,
    MachineVoiceLibraryPolicy = MachineVoiceLibraryPolicy::production);
}
