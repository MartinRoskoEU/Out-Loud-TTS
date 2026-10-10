#include "mutation.hpp"
#include "../error_conversion.hpp"
#include "engine_registration.hpp"
#include "identity.hpp"
#include <cstring>

namespace outloud::frontend::mutation {
namespace {
namespace fs = detail::library_fs;
[[noreturn]] void malformed() { throw std::invalid_argument("Malformed or unsupported voice mutation request."); }
struct Reader {
    std::span<const std::uint8_t> remaining;
    std::uint32_t number() {
        if (remaining.size()<4) { malformed(); }
        std::uint32_t value; std::memcpy(&value,remaining.data(),4); remaining = remaining.subspan(4); return value;
    }
    std::string string() {
        const auto length = number(); if (length>32768 || length>remaining.size()) { malformed(); }
        std::string value(reinterpret_cast<const char*>(remaining.data()),length); remaining = remaining.subspan(length);
        if (value.find('\0')!=value.npos || !detail::valid_utf8(value)) { malformed(); } return value;
    }
};
void number(std::vector<std::uint8_t>& bytes,std::uint32_t value) {
    const auto* first = reinterpret_cast<const std::uint8_t*>(&value); bytes.insert(bytes.end(),first,first+4);
}
void string(std::vector<std::uint8_t>& bytes,const std::string& value) {
    if (value.size()>32768) { throw std::length_error("Result string exceeds protocol bound."); }
    number(bytes,static_cast<std::uint32_t>(value.size())); bytes.insert(bytes.end(),value.begin(),value.end());
}
}
Request decode_request(std::span<const std::uint8_t> bytes) {
    if (bytes.size()>limit || bytes.size()<16) { malformed(); }
    Reader reader{bytes};
    if (reader.number()!=magic || reader.number()!=version) { malformed(); }
    const auto kind=reader.number(); if ((kind<1 || kind>5) || reader.number()!=bytes.size()-16) { malformed(); }
    if (kind==3) {
        auto id=reader.string(), name=reader.string(), expected_hash=reader.string(), health=reader.string();
        if (!fs::id(id) || !reader.remaining.empty() || health.empty() || health.size()>32 ||
            (expected_hash.empty() ? !name.empty() : expected_hash.size()!=64 || expected_hash.find_first_not_of("0123456789abcdef")!=expected_hash.npos)) { malformed(); }
        return {{},std::move(name),Operation::remove,std::move(id),std::move(expected_hash),std::move(health)};
    }
    const auto source = reader.string(), name = reader.string();
    std::string id, expected_hash;
    if (kind==2 || kind==5) { id=reader.string(); expected_hash=reader.string();
        if (!fs::id(id) || expected_hash.size()!=64 || expected_hash.find_first_not_of("0123456789abcdef")!=expected_hash.npos) { malformed(); } }
    if (!reader.remaining.empty() || source.empty() || name.empty()) { malformed(); }
    // Transport constraints only. Native VoiceLibrary/importer own all file/name validation.
    const std::filesystem::path supplied = fs::wide(source);
    if (!supplied.is_absolute()) { malformed(); }
    auto path = fs::local(supplied,VoiceLibraryErrorCode::source_io);
    if (_wcsicmp(path.extension().c_str(),kind==4 || kind==5 ? L".olv" : L".safetensors")!=0) { malformed(); }
    return {std::move(path),name,static_cast<Operation>(kind),id,expected_hash};
}
std::vector<std::uint8_t> encode_result(const Result& result) {
    std::vector<std::uint8_t> bytes; number(bytes,magic); number(bytes,version); number(bytes,static_cast<std::uint32_t>(result.outcome)); number(bytes,0);
    if (result.outcome!=Outcome::started) {
        string(bytes,result.library_id);
        number(bytes,static_cast<std::uint32_t>(result.kind));
        for (const auto* value : {&result.error.domain,&result.error.code,&result.error.message,&result.error.path,&result.error.library_code,&result.error.import_code,&result.error.tensor}) { string(bytes,*value); }
        number(bytes,result.error.system_error); number(bytes,static_cast<std::uint32_t>(result.error.duplicate_ids.size()));
        for (const auto& id : result.error.duplicate_ids) { string(bytes,id); }
    }
    if (bytes.size()>limit) { throw std::length_error("Result exceeds protocol bound."); }
    const auto length = static_cast<std::uint32_t>(bytes.size()-16); std::memcpy(bytes.data()+12,&length,4); return bytes;
}
SapiVoiceRuntime runtime_from_registration(const std::optional<SapiRegistryValue>& registered) {
    const auto text = registered ? sapi_registry_text(*registered) : std::nullopt;
    if (!text || text->empty()) { throw SapiVoiceError(SapiVoiceErrorCode::engine_registration_malformed,"Machine engine registration must contain a nonempty REG_SZ DLL path."); }
    const auto dll = fs::local(*text,VoiceLibraryErrorCode::unsafe_path);
    if (_wcsicmp(dll.filename().c_str(),L"OutLoudPocketSAPI.dll") || _wcsicmp(dll.parent_path().filename().c_str(),L"bin")) {
        throw SapiVoiceError(SapiVoiceErrorCode::invalid_runtime,"Registered engine must use the documented deployment bin/OutLoudPocketSAPI.dll layout; no runtime fallback.");
    }
    detail::validate_engine_registration(registered,dll);
    const auto root = dll.parent_path().parent_path();
    return {root,dll,(root/L"runtime/pocket-english.olp").lexically_normal(),(root/L"runtime/pocket-english-tokenizer.bin").lexically_normal()};
}
SapiVoiceRuntime resolve_machine_runtime() {
    // Read-only, fixed machine Registry64 registration. Never HKCR/HKCU or UI paths.
    struct Keys { std::vector<HKEY> values; ~Keys() { for (const auto key : values) { RegCloseKey(key); } } } keys;
    HKEY raw = HKEY_LOCAL_MACHINE;
    for (const auto* component : {L"SOFTWARE",L"Classes",L"CLSID",sapi::clsid_text,L"InprocServer32"}) {
        HKEY next = nullptr;
        const auto status = RegOpenKeyExW(raw,component,REG_OPTION_OPEN_LINK,KEY_READ|KEY_WOW64_64KEY,&next);
        if (status!=ERROR_SUCCESS) { throw SapiVoiceError(status==ERROR_FILE_NOT_FOUND ? SapiVoiceErrorCode::engine_registration_missing :
            status==ERROR_ACCESS_DENIED ? SapiVoiceErrorCode::registry_access_denied : SapiVoiceErrorCode::registry_io,
            "Cannot resolve the registered machine engine runtime.",static_cast<std::uint32_t>(status)); }
        // No link-following through intermediate registry ancestors either.
        try { keys.values.push_back(next); } catch (...) { RegCloseKey(next); throw; } raw = next;
        DWORD link_type = 0, link_size = 0;
        const auto link_status = RegQueryValueExW(raw,L"SymbolicLinkValue",nullptr,&link_type,nullptr,&link_size);
        if (link_status!=ERROR_FILE_NOT_FOUND) { throw SapiVoiceError(SapiVoiceErrorCode::engine_registration_malformed,"Engine registry links or unreadable link state are not supported."); }
    }
    SapiRegistryValue value; DWORD size = 0, type = 0;
    if (RegQueryValueExW(raw,nullptr,nullptr,&type,nullptr,&size)!=ERROR_SUCCESS || size>32768) {
        throw SapiVoiceError(SapiVoiceErrorCode::engine_registration_malformed,"Cannot read bounded machine engine registration.");
    }
    value.bytes.resize(size);
    if (RegQueryValueExW(raw,nullptr,nullptr,&type,value.bytes.data(),&size)!=ERROR_SUCCESS || size>value.bytes.size()) {
        throw SapiVoiceError(SapiVoiceErrorCode::engine_registration_malformed,"Machine engine registration changed during read.");
    }
    value.type = type; value.bytes.resize(size); return runtime_from_registration(value);
}
Result import_and_install(const Request& request,VoiceLibrary library,std::shared_ptr<SapiVoiceRegistry> registry,
    const std::function<SapiVoiceRuntime()>& resolve,MachineVoiceLibraryPolicy policy) {
    Result result;
    result.kind=request.kind;
    try {
        if (request.kind==Operation::remove) {
            result.library_id=request.target_id;
            SapiVoiceManager manager(library,{},registry,policy); // Removal needs no runtime assets or engine-registration edit.
            const RemovalRegistrationIdentity query=[&](std::string_view id) {
                try { return manager.removal_registration_identity(id); }
                catch (const SapiVoiceError& error) { result.error=frontend::error_from<Error>(error); throw; }
            };
            const auto recovery=library.recover_removal(request.target_id,query);
            if (recovery.state==RemovalRecoveryState::removed) {
                result.outcome=recovery.cleanup_pending ? Outcome::removal_warning : Outcome::removed;
                if (recovery.cleanup_pending) { result.error.domain="library"; result.error.code="cleanup_pending"; result.error.message=recovery.detail; }
                return result;
            }
            VoiceRemoveOptions options; options.expected_display_name=request.display_name; options.expected_olv_sha256=request.expected_hash;
            options.expected_health=request.expected_health;
            options.registration_identity=query;
            options.remove_registration=[&](const LibraryVoice&,std::string_view identity) {
                try { (void)manager.remove(request.target_id,identity); }
                catch (const SapiVoiceError& error) {
                    if (error.code!=SapiVoiceErrorCode::token_not_found) { result.error=frontend::error_from<Error>(error); throw; }
                }
            };
            const auto removed=library.remove_voice(request.target_id,options);
            result.outcome=removed.cleanup_pending ? Outcome::removal_warning : Outcome::removed;
            if (removed.cleanup_pending) { result.error.domain="library"; result.error.code="cleanup_pending"; result.error.message="Voice and token removed; trash cleanup pending: "+removed.detail; }
            return result;
        }
        VoiceImportOptions options; options.declared_model_compatibility_id = pocket_model_compatibility_id;
        if (request.kind==Operation::replace || request.kind==Operation::replace_olv) {
            result.library_id=request.target_id;
            VoiceReplaceOptions replacement; replacement.import=options; replacement.expected_display_name=request.display_name; replacement.expected_olv_sha256=request.expected_hash;
            SapiVoiceManager manager(library,resolve(),registry,policy);
            replacement.validate_before_publish=[&](const LibraryVoice& current,const std::filesystem::path& staged) { manager.validate_replacement(current,staged); };
            const auto replaced=request.kind==Operation::replace_olv ? library.replace_olv(request.target_id,request.source,replacement) : library.replace_pocket_voice(request.target_id,request.source,replacement);
            result.outcome=replaced.changed ? Outcome::replaced : Outcome::unchanged;
            if (replaced.cleanup_pending) { result.outcome=Outcome::replacement_warning; result.error.domain="library"; result.error.code="cleanup_pending"; result.error.message=replaced.detail; }
            return result; // Correct owned token remains byte-for-byte unchanged.
        }
        if (request.kind!=Operation::import && request.kind!=Operation::import_olv) { malformed(); }
        const auto imported = request.kind==Operation::import_olv ? library.import_olv(request.source,request.display_name,DuplicateContentPolicy::reject) : library.import_pocket_voice(request.source,request.display_name,options,DuplicateContentPolicy::reject);
        result.library_id = imported.voice.library_id;
        result.outcome = Outcome::partial;
        SapiVoiceManager manager(std::move(library),resolve(),std::move(registry),policy);
        (void)manager.install(result.library_id);
        result.outcome = Outcome::success;
    } catch (const VoiceLibraryError& error) {
        if (request.kind==Operation::remove && (error.code()==VoiceLibraryErrorCode::removal_partial || error.code()==VoiceLibraryErrorCode::removal_pending)) {
            const auto registration_error=result.error; result.error=frontend::error_from<Error>(error);
            if (!registration_error.code.empty()) { result.error.message+=" Registration failure: "+registration_error.code+": "+registration_error.message; }
            result.outcome=Outcome::removal_warning;
        } else { result.error = frontend::error_from<Error>(error); }
    }
    catch (const SapiVoiceError& error) { result.error = frontend::error_from<Error>(error); }
    catch (...) { result.error.domain = "helper"; result.error.code = "internal_failure"; result.error.message = "Native voice mutation failed."; }
    return result;
}
}
