#pragma once
#include "outloud/voice_library.hpp"
#include <map>
#include <memory>

namespace outloud {
enum class SapiVoiceScope { user, machine };
enum class SapiVoiceErrorCode {
    invalid_id, voice_not_found, unhealthy_voice, incompatible_voice, token_not_found,
    token_conflict, malformed_token, registry_access_denied, registry_io,
    unsupported_scope, engine_registration_missing, invalid_runtime, library_failure,
    reconciliation_required, internal_failure,
    engine_registration_malformed, engine_dll_mismatch, engine_dll_inaccessible,
    invalid_machine_library_root, library_access_denied
};
[[nodiscard]] std::string_view sapi_voice_error_name(SapiVoiceErrorCode) noexcept;
class SapiVoiceError : public std::runtime_error {
public:
    SapiVoiceError(SapiVoiceErrorCode, std::string message, std::uint32_t system_error = 0,
        std::optional<VoiceLibraryError> library = {}, std::optional<VoiceImportError> validation = {});
    SapiVoiceErrorCode code;
    std::uint32_t system_error;
    std::optional<VoiceLibraryError> library_error;
    std::optional<VoiceImportError> validation_error;
};
// Raw registry values retain types and bytes so unexpected contents cannot be
// normalized away during ownership checks. Only root + Attributes are writable.
struct SapiRegistryValue {
    std::uint32_t type = 0;
    std::vector<std::uint8_t> bytes;
    bool operator==(const SapiRegistryValue&) const = default;
};
struct SapiTokenTree {
    std::map<std::wstring,SapiRegistryValue> values;
    std::map<std::wstring,SapiTokenTree> children;
    bool operator==(const SapiTokenTree&) const = default;
};
[[nodiscard]] SapiRegistryValue sapi_registry_string(std::wstring_view);
[[nodiscard]] std::optional<std::wstring> sapi_registry_text(const SapiRegistryValue&);
[[nodiscard]] std::wstring managed_sapi_token_key(std::string_view library_id);
[[nodiscard]] std::wstring managed_sapi_token_id(std::string_view library_id, SapiVoiceScope scope = SapiVoiceScope::machine);
// Inject a fake for tests. Production implementation uses Registry64 and native
// transacted APIs: compare snapshot, perform bounded edits, commit or roll back.
class SapiVoiceRegistry {
public:
    virtual ~SapiVoiceRegistry() = default;
    virtual std::vector<std::wstring> keys(SapiVoiceScope) = 0;
    virtual std::optional<SapiTokenTree> read(SapiVoiceScope, std::wstring_view key) = 0;
    virtual void verify_engine(const std::filesystem::path& dll, SapiVoiceScope scope = SapiVoiceScope::machine) = 0;
    virtual void exchange(SapiVoiceScope, std::wstring_view key,
        const std::optional<SapiTokenTree>& expected, const std::optional<SapiTokenTree>& replacement) = 0;
};
[[nodiscard]] std::shared_ptr<SapiVoiceRegistry> windows_sapi_voice_registry();
struct SapiVoiceRuntime {
    std::filesystem::path asset_root, engine_dll, model, tokenizer;
};
enum class SapiTokenStatus { missing, correct, stale_name, orphan, wrong_path, unhealthy_voice, malformed, conflict };
[[nodiscard]] std::string_view sapi_token_status_name(SapiTokenStatus) noexcept;
struct SapiVoiceToken {
    std::wstring token_key;
    std::string library_id;
    SapiTokenStatus status = SapiTokenStatus::malformed;
    std::string detail;
    std::optional<SapiTokenTree> tree;
    std::string suggested_action; // advisory only; reconciliation never mutates
};
enum class SapiTokenChange { installed, already_installed, updated, unchanged, removed };
// Explicit opt-in for isolated tests/developer diagnostics. Never the frontend
// production policy; it does not change scope or elevate permissions.
enum class MachineVoiceLibraryPolicy { production, development_override };
class SapiVoiceManager {
public:
    SapiVoiceManager(VoiceLibrary library, SapiVoiceRuntime runtime,
        std::shared_ptr<SapiVoiceRegistry> registry = windows_sapi_voice_registry(),
        MachineVoiceLibraryPolicy machine_policy = MachineVoiceLibraryPolicy::production);
    [[nodiscard]] std::vector<SapiVoiceToken> list(SapiVoiceScope scope = SapiVoiceScope::machine) const;
    [[nodiscard]] SapiVoiceToken inspect(std::string_view id, SapiVoiceScope scope = SapiVoiceScope::machine) const;
    [[nodiscard]] std::vector<SapiVoiceToken> reconcile(SapiVoiceScope scope = SapiVoiceScope::machine) const;
    [[nodiscard]] SapiTokenChange install(std::string_view id, SapiVoiceScope scope = SapiVoiceScope::machine);
    [[nodiscard]] SapiTokenChange update(std::string_view id, SapiVoiceScope scope = SapiVoiceScope::machine);
    [[nodiscard]] SapiTokenChange remove(std::string_view id, SapiVoiceScope scope = SapiVoiceScope::machine);
private:
    VoiceLibrary library_;
    SapiVoiceRuntime runtime_;
    std::shared_ptr<SapiVoiceRegistry> registry_;
    MachineVoiceLibraryPolicy machine_policy_;
    void validate_machine_library(SapiVoiceScope) const;
    [[nodiscard]] LibraryVoice voice(std::string_view id) const;
    [[nodiscard]] SapiTokenTree desired(const LibraryVoice&) const;
    void validate_runtime(const LibraryVoice&, SapiVoiceScope) const;
};
} // namespace outloud
