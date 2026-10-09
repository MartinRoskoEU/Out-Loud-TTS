#pragma once
#include "outloud/voice_library.hpp"
#include <map>
#include <memory>

namespace outloud {
enum class SapiVoiceErrorCode {
    invalid_id, voice_not_found, unhealthy_voice, incompatible_voice, token_not_found,
    token_conflict, malformed_token, registry_access_denied, registry_io,
    engine_registration_missing = 10, invalid_runtime, library_failure, // Preserve remaining error ordinals.
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
[[nodiscard]] std::wstring managed_sapi_token_id(std::string_view library_id);
// Inject a fake for tests. Production implementation uses Registry64 and native
// transacted APIs: compare snapshot, perform bounded edits, commit or roll back.
class SapiVoiceRegistry {
public:
    virtual ~SapiVoiceRegistry() = default;
    virtual std::vector<std::wstring> keys() = 0;
    virtual std::optional<SapiTokenTree> read(std::wstring_view key) = 0;
    virtual void verify_engine(const std::filesystem::path& dll) = 0;
    virtual void exchange(std::wstring_view key,
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
struct SapiVoiceSnapshotRow {
    LibraryVoice voice;
    std::optional<SapiVoiceToken> token;
    std::optional<SapiVoiceError> error;
};
enum class SapiTokenChange { installed, already_installed, updated, unchanged, removed };
// Explicit opt-in for isolated fixture libraries. Never the frontend production
// policy or a CLI option; it does not change the fixed registry location or elevate.
enum class MachineVoiceLibraryPolicy { production, development_override };
class SapiVoiceManager {
public:
    SapiVoiceManager(VoiceLibrary library, SapiVoiceRuntime runtime,
        std::shared_ptr<SapiVoiceRegistry> registry = windows_sapi_voice_registry(),
        MachineVoiceLibraryPolicy machine_policy = MachineVoiceLibraryPolicy::production);
    [[nodiscard]] std::vector<SapiVoiceToken> list() const;
    [[nodiscard]] SapiVoiceToken inspect(std::string_view id) const;
    // One library read epoch, fresh per-row token reads, no reusable mutation trust.
    [[nodiscard]] std::vector<SapiVoiceSnapshotRow> read_snapshot() const;
    [[nodiscard]] std::vector<SapiVoiceToken> reconcile() const;
    [[nodiscard]] SapiTokenChange install(std::string_view id);
    // Read-only preflight under VoiceLibrary's replace lock: do not re-enter it.
    // Requires an already-correct owned token; validates staged content/runtime.
    void validate_replacement(const LibraryVoice& current, const std::filesystem::path& staged) const;
    [[nodiscard]] SapiTokenChange update(std::string_view id);
    // Exact owned-token snapshot for coordinated remove/recovery; never reads library/runtime.
    [[nodiscard]] std::string removal_registration_identity(std::string_view id) const;
    [[nodiscard]] SapiTokenChange remove(std::string_view id, std::string_view expected_identity = {});
private:
    VoiceLibrary library_;
    SapiVoiceRuntime runtime_;
    std::shared_ptr<SapiVoiceRegistry> registry_;
    MachineVoiceLibraryPolicy machine_policy_;
    void validate_machine_library() const;
    [[nodiscard]] SapiVoiceToken inspect_impl(std::string_view id, const LibraryVoice* snapshot) const;
    [[nodiscard]] LibraryVoice voice(std::string_view id) const;
    [[nodiscard]] SapiTokenTree desired(const LibraryVoice&) const;
    void validate_runtime(const LibraryVoice&) const;
};
} // namespace outloud
