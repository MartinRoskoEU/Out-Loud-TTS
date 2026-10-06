#pragma once
#include "outloud/sapi_voices.hpp"
#include <functional>
#include <span>

namespace outloud::frontend::mutation {
inline constexpr std::uint32_t magic = 0x314d4c4f, version = 2, operation = 1, limit = 65536;
enum class Operation : std::uint32_t { import = 1, replace = 2 };
enum class Outcome : std::uint32_t { success = 1, failed = 2, partial = 3, started = 4, replaced = 5, unchanged = 6, replacement_warning = 7 };
struct Request { std::filesystem::path source; std::string display_name; Operation kind=Operation::import; std::string target_id, expected_hash; };
struct Error {
    std::string domain, code, message, path, library_code, import_code, tensor;
    std::uint32_t system_error = 0;
    std::vector<std::string> duplicate_ids;
};
struct Result { Outcome outcome = Outcome::failed; std::string library_id; Error error; Operation kind=Operation::import; };
// Private, statically linked seams. No injection/root/scope options in the helper.
Request decode_request(std::span<const std::uint8_t>);
std::vector<std::uint8_t> encode_result(const Result&);
SapiVoiceRuntime runtime_from_registration(const std::optional<SapiRegistryValue>&);
SapiVoiceRuntime resolve_machine_runtime();
Result import_and_install(const Request&, VoiceLibrary, std::shared_ptr<SapiVoiceRegistry>,
    const std::function<SapiVoiceRuntime()>&,
    MachineVoiceLibraryPolicy = MachineVoiceLibraryPolicy::production);
}
