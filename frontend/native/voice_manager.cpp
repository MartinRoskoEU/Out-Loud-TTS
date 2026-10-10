#include "voice_manager_internal.hpp"
#include "../error_conversion.hpp"
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
    bool is_snapshot=true;
    std::string destination_identity;
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
            auto voices = manager.read_snapshot(); result->rows.reserve(voices.size());
            for (auto& voice : voices) {
                Row row; row.voice = std::move(voice.voice); row.token=std::move(voice.token);
                if (voice.error) { row.error = frontend::error_from<Error>(*voice.error); }
                result->rows.push_back(std::move(row));
            }
        } catch (const VoiceLibraryError& e) { result->error = frontend::error_from<Error>(e); }
        catch (const SapiVoiceError& e) { result->error = frontend::error_from<Error>(e); }
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
    return result && result->is_snapshot && !result->error ? &result->snapshot : nullptr;
}
const outloud_vm_error_v1* __cdecl outloud_vm_result_error(const outloud_vm_result* result) noexcept {
    return result && result->error ? &result->error->view : nullptr;
}
void __cdecl outloud_vm_result_free(outloud_vm_result* result) noexcept { delete result; }
const outloud_vm_content_v1* __cdecl outloud_vm_result_content(const outloud_vm_result* result,uint64_t row) noexcept {
    return result && !result->error && row<result->contents.size() ? &result->contents[static_cast<std::size_t>(row)] : nullptr;
}

namespace {
template<class Function> int32_t export_operation(Function function,outloud_vm_result** output) noexcept {
    if (!output) { return OUTLOUD_VM_INVALID_ARGUMENT; } *output=nullptr;
    try {
        auto result=std::make_unique<outloud_vm_result>(); result->is_snapshot=false;
        try { function(*result); }
        catch (const VoiceLibraryError& error) { result->error=frontend::error_from<Error>(error); }
        catch (const std::bad_alloc&) { throw; }
        catch (...) { result->error=internal_error(); }
        result->bind(); const auto status=result->error ? OUTLOUD_VM_ERROR : OUTLOUD_VM_OK;
        *output=result.release(); return status;
    } catch (const std::bad_alloc&) { return OUTLOUD_VM_OUT_OF_MEMORY; }
    catch (...) { return OUTLOUD_VM_INTERNAL_ERROR; }
}
void checked_export(VoiceLibrary library,std::string_view id,std::string_view name,std::string_view hash,
    const std::filesystem::path& destination,std::string_view identity) {
    if (!detail::library_fs::id(id) || hash.size()!=64 || hash.find_first_not_of("0123456789abcdef")!=hash.npos || name.empty() ||
        (identity!="absent" && (identity.size()!=64 || identity.find_first_not_of("0123456789abcdef")!=identity.npos))) {
        throw VoiceLibraryError(VoiceLibraryErrorCode::export_conflict,"Export requires a valid selected voice and destination snapshot.");
    }
    VoiceExportOptions options; options.expected_display_name=name; options.expected_olv_sha256=hash;
    options.expected_destination=std::string(identity); options.replace_existing=identity!="absent";
    (void)library.export_olv(id,destination,options);
}
bool bounded(const char* text) { return text && strnlen_s(text,32769)<=32768 && detail::valid_utf8(text); }
}
namespace outloud::frontend {
int32_t export_injected(VoiceLibrary library,std::string_view id,std::string_view name,std::string_view hash,
    const std::filesystem::path& destination,std::string_view identity,outloud_vm_result** output) noexcept {
    return export_operation([&](auto&) { checked_export(std::move(library),id,name,hash,destination,identity); },output);
}
}
uint32_t __cdecl outloud_vm_capabilities() noexcept { return OUTLOUD_VM_CAP_OLV_EXPORT_V1; }
int32_t __cdecl outloud_vm_export_destination(const char* destination,outloud_vm_result** output) noexcept {
    if (!bounded(destination)) { if (output) { *output=nullptr; } return OUTLOUD_VM_INVALID_ARGUMENT; }
    return export_operation([&](auto& result) { result.destination_identity=inspect_olv_export_destination(detail::library_fs::wide(destination)); },output);
}
const char* __cdecl outloud_vm_result_destination(const outloud_vm_result* result) noexcept {
    return result && !result->is_snapshot && !result->error && !result->destination_identity.empty() ? result->destination_identity.c_str() : nullptr;
}
int32_t __cdecl outloud_vm_export(const char* id,const char* name,const char* hash,const char* destination,
    const char* identity,outloud_vm_result** output) noexcept {
    if (!bounded(id) || !bounded(name) || !bounded(hash) || !bounded(destination) || !bounded(identity)) {
        if (output) { *output=nullptr; } return OUTLOUD_VM_INVALID_ARGUMENT;
    }
    return export_operation([&](auto&) { checked_export(VoiceLibrary{},id,name,hash,detail::library_fs::wide(destination),identity); },output);
}
