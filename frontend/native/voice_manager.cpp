#include "voice_manager_internal.hpp"
#include "voice_library_fs.hpp"
#include <memory>
#include <optional>
#include <tuple>

namespace {
using namespace outloud;
struct Error {
    std::string domain,code,message,path,library_code,import_code,tensor;
    uint32_t system_error = 0;
    outloud_vm_error_v1 view{};
    void bind() noexcept {
        view = {domain.c_str(),code.c_str(),message.c_str(),path.c_str(),library_code.c_str(),
            import_code.c_str(),tensor.c_str(),system_error,0};
    }
};
void library_cause(Error& error,const VoiceLibraryError& cause) {
    error.library_code = voice_library_error_name(cause.code());
    error.path = detail::library_fs::utf8(cause.path().wstring());
    if (cause.import_error()) {
        error.import_code = voice_import_error_name(cause.import_error()->code());
        error.tensor = cause.import_error()->tensor();
    }
}
Error convert(const VoiceLibraryError& cause) {
    Error error; error.domain = "library"; error.code = voice_library_error_name(cause.code());
    error.message = cause.what(); error.system_error = cause.system_error(); library_cause(error,cause); return error;
}
Error convert(const SapiVoiceError& cause) {
    Error error; error.domain = "sapi"; error.code = sapi_voice_error_name(cause.code);
    error.message = cause.what(); error.system_error = cause.system_error;
    if (cause.library_error) { library_cause(error,*cause.library_error); }
    if (cause.validation_error) {
        error.import_code = voice_import_error_name(cause.validation_error->code());
        error.tensor = cause.validation_error->tensor();
        if (error.path.empty()) { error.path = detail::library_fs::utf8(cause.validation_error->path().wstring()); }
    }
    return error;
}
Error internal_error() {
    Error error; error.domain = "bridge"; error.code = "internal_failure";
    error.message = "Unable to read native voice state."; return error;
}
struct Row {
    LibraryVoice voice;
    std::optional<SapiVoiceToken> token;
    std::optional<Error> error;
};
}
struct outloud_vm_result {
    std::string root;
    std::vector<Row> rows;
    std::vector<outloud_vm_voice_v1> records;
    std::vector<outloud_vm_content_v1> contents;
    std::optional<Error> error;
    outloud_vm_snapshot_v1 snapshot{};
    void bind() {
        if (error) { error->bind(); return; }
        records.reserve(rows.size());
        contents.reserve(rows.size());
        for (auto& row : rows) {
            auto& v = row.voice; outloud_vm_voice_v1 record{};
            record.library_id = v.library_id.c_str(); record.health = voice_health_name(v.health).data();
            record.library_detail = v.detail.c_str();
            if (row.token) { record.sapi_status = sapi_token_status_name(row.token->status).data(); record.sapi_detail = row.token->detail.c_str(); }
            if (row.error) { row.error->bind(); record.sapi_error = &row.error->view; }
            if (v.metadata) {
                const auto& m = *v.metadata;
                record.metadata_present = 1; record.display_name = m.display_name.c_str();
                record.olv_version = m.olv_version; record.compatibility_id = m.compatibility_id.c_str();
                record.import_kind = m.import_kind==VoiceImportKind::olv ? "olv" : "pocket_prepared";
                record.provenance_verified = m.model_provenance_verified ? 1U : 0U;
            }
            records.push_back(record);
            contents.push_back(v.metadata ? outloud_vm_content_v1{v.metadata->olv_sha256.c_str(),v.metadata->source_sha256.c_str(),v.metadata->source_filename.c_str(),v.metadata->imported_utc.c_str()} : outloud_vm_content_v1{});
        }
        snapshot = {root.c_str(),static_cast<uint64_t>(records.size()),records.empty() ? nullptr : records.data()};
    }
};
static_assert(sizeof(void*)==8 && sizeof(outloud_vm_error_v1)==64);
static_assert(sizeof(outloud_vm_voice_v1)==88 && sizeof(outloud_vm_snapshot_v1)==24);
static_assert(sizeof(outloud_vm_content_v1)==32);

namespace {
template<class Factory> int32_t read(Factory factory,outloud_vm_result** output) noexcept {
    if (!output) { return OUTLOUD_VM_INVALID_ARGUMENT; } *output = nullptr;
    try {
        auto result = std::make_unique<outloud_vm_result>();
        try {
            auto [library,registry,policy] = factory();
            result->root = detail::library_fs::utf8(library.root().wstring());
            // No intended runtime supplied: preserve the backend's documented
            // identity/path/name-only SAPI diagnostic, including its detail.
            SapiVoiceManager manager(library,{},std::move(registry),policy);
            auto voices = library.list_voices(); result->rows.reserve(voices.size());
            for (auto& voice : voices) {
                Row row; row.voice = std::move(voice);
                try { row.token = manager.inspect(row.voice.library_id,SapiVoiceScope::machine); }
                catch (const SapiVoiceError& e) { row.error = convert(e); }
                result->rows.push_back(std::move(row));
            }
        } catch (const VoiceLibraryError& e) { result->error = convert(e); }
        catch (const SapiVoiceError& e) { result->error = convert(e); }
        catch (const std::bad_alloc&) { throw; }
        catch (...) { result->error = internal_error(); }
        result->bind(); const auto status = result->error ? OUTLOUD_VM_ERROR : OUTLOUD_VM_OK;
        *output = result.release(); return status;
    } catch (const std::bad_alloc&) { return OUTLOUD_VM_OUT_OF_MEMORY; }
    catch (...) { return OUTLOUD_VM_INTERNAL_ERROR; } // no C++ exception crosses the ABI
}
}
namespace outloud::frontend {
int32_t read_injected(VoiceLibrary library,std::shared_ptr<SapiVoiceRegistry> registry,outloud_vm_result** result,
    MachineVoiceLibraryPolicy policy) noexcept {
    return read([&] { return std::tuple{std::move(library),std::move(registry),policy}; },result);
}
}
uint32_t __cdecl outloud_vm_abi_version() noexcept { return 1; }
int32_t __cdecl outloud_vm_read(outloud_vm_result** result) noexcept {
    return read([] { return std::tuple{VoiceLibrary{},windows_sapi_voice_registry(),MachineVoiceLibraryPolicy::production}; },result);
}
const outloud_vm_snapshot_v1* __cdecl outloud_vm_result_snapshot(const outloud_vm_result* result) noexcept {
    return result && !result->error ? &result->snapshot : nullptr;
}
const outloud_vm_error_v1* __cdecl outloud_vm_result_error(const outloud_vm_result* result) noexcept {
    return result && result->error ? &result->error->view : nullptr;
}
void __cdecl outloud_vm_result_free(outloud_vm_result* result) noexcept { delete result; }
const outloud_vm_content_v1* __cdecl outloud_vm_result_content(const outloud_vm_result* result,uint64_t row) noexcept {
    return result && !result->error && row<result->contents.size() ? &result->contents[static_cast<std::size_t>(row)] : nullptr;
}
