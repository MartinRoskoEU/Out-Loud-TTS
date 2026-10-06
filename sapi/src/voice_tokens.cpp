#include "outloud/sapi_voices.hpp"
#include "outloud/assets.hpp"
#include "outloud/tokenizer.hpp"
#include "identity.hpp"
#include "voice_library_fs.hpp"
#include <cstring>
#include <set>

namespace outloud {
namespace {
using Code = SapiVoiceErrorCode;
constexpr std::wstring_view marker = L"OutLoud.ManagedVoice.1";
void scope_check(SapiVoiceScope scope) {
    if (scope!=SapiVoiceScope::user && scope!=SapiVoiceScope::machine) { throw SapiVoiceError(Code::unsupported_scope,"Unknown SAPI voice scope; no fallback."); }
}
Code library_code(const VoiceLibraryError& error) {
    return error.code()==VoiceLibraryErrorCode::library_access_denied ? Code::library_access_denied : Code::library_failure;
}
std::wstring wide(std::string_view value) { return detail::library_fs::wide(value); }
std::string utf8(std::wstring_view value) { return detail::library_fs::utf8(value); }
std::optional<std::wstring> text(const SapiTokenTree& tree, const wchar_t* name) {
    const auto it = tree.values.find(name);
    return it==tree.values.end() ? std::nullopt : sapi_registry_text(it->second);
}
bool owned(const SapiTokenTree& tree,std::string_view id) {
    return text(tree,L"OutLoudManagedOwner")==marker && text(tree,L"LibraryId")==wide(id) &&
        text(tree,L"CLSID")==sapi::clsid_text;
}
bool shape(const SapiTokenTree& tree) {
    static const std::set<std::wstring> root{L"",L"409",L"CLSID",L"OutLoudManagedOwner",L"LibraryId",L"AssetRoot",
        L"ModelPath",L"TokenizerPath",L"VoicePath",L"Seed",L"MaximumSegmentTokens",L"KernelPath"};
    static const std::set<std::wstring> attributes{L"Name",L"Language",L"Vendor"};
    if (tree.values.size()!=root.size() || tree.children.size()!=1 || !tree.children.contains(L"Attributes")) { return false; }
    for (const auto& [key,value] : tree.values) { if (!root.contains(key) || !sapi_registry_text(value)) { return false; } }
    const auto& attr = tree.children.at(L"Attributes");
    if (!attr.children.empty() || attr.values.size()!=attributes.size()) { return false; }
    for (const auto& [key,value] : attr.values) { if (!attributes.contains(key) || !sapi_registry_text(value)) { return false; } }
    return true;
}
void set(SapiTokenTree& tree,const wchar_t* key,std::wstring_view value) { tree.values[key] = sapi_registry_string(value); }
SapiTokenTree without_name(SapiTokenTree tree) {
    tree.values.erase(L""); tree.values.erase(L"409");
    if (tree.children.contains(L"Attributes")) { tree.children.at(L"Attributes").values.erase(L"Name"); }
    return tree;
}
bool candidate(std::wstring_view key,const SapiTokenTree& tree) {
    return key.starts_with(L"OutLoud_Pocket_") && key!=sapi::token_name &&
        (key.size()==47 || tree.values.contains(L"OutLoudManagedOwner") || tree.values.contains(L"LibraryId"));
}
}
SapiVoiceError::SapiVoiceError(Code c,std::string message,std::uint32_t error,
    std::optional<VoiceLibraryError> library,std::optional<VoiceImportError> validation)
    : std::runtime_error(std::move(message)),code(c),system_error(error),library_error(std::move(library)),validation_error(std::move(validation)) {}
std::string_view sapi_voice_error_name(Code code) noexcept {
    switch (code) {
#define CASE(x) case Code::x: return #x
    CASE(invalid_id); CASE(voice_not_found); CASE(unhealthy_voice); CASE(incompatible_voice); CASE(token_not_found);
    CASE(token_conflict); CASE(malformed_token); CASE(registry_access_denied); CASE(registry_io); CASE(unsupported_scope);
    CASE(engine_registration_missing); CASE(invalid_runtime); CASE(library_failure); CASE(reconciliation_required); CASE(internal_failure);
    CASE(engine_registration_malformed); CASE(engine_dll_mismatch); CASE(engine_dll_inaccessible);
    CASE(invalid_machine_library_root); CASE(library_access_denied);
#undef CASE
    } return "internal_failure";
}
std::string_view sapi_token_status_name(SapiTokenStatus status) noexcept {
    switch (status) {
#define CASE(x) case SapiTokenStatus::x: return #x
    CASE(missing); CASE(correct); CASE(stale_name); CASE(orphan); CASE(wrong_path); CASE(unhealthy_voice); CASE(malformed); CASE(conflict);
#undef CASE
    } return "malformed";
}
SapiRegistryValue sapi_registry_string(std::wstring_view value) {
    if (value.size()>16383 || value.find(L'\0')!=value.npos) { throw SapiVoiceError(Code::malformed_token,"Invalid registry string."); }
    SapiRegistryValue result; result.type = REG_SZ; result.bytes.resize((value.size()+1)*sizeof(wchar_t));
    std::memcpy(result.bytes.data(),value.data(),value.size()*sizeof(wchar_t)); return result;
}
std::optional<std::wstring> sapi_registry_text(const SapiRegistryValue& value) {
    if (value.type!=REG_SZ || value.bytes.empty() || value.bytes.size()%sizeof(wchar_t) || value.bytes.size()>32768) { return {}; }
    std::wstring result(value.bytes.size()/sizeof(wchar_t),L'\0'); std::memcpy(result.data(),value.bytes.data(),value.bytes.size());
    if (result.back()!=L'\0') { return {}; } result.pop_back();
    if (result.find(L'\0')!=result.npos) { return {}; }
    try { (void)utf8(result); } catch (...) { return {}; }
    return result;
}
std::wstring managed_sapi_token_key(std::string_view id) {
    if (!detail::library_fs::id(id)) { throw SapiVoiceError(Code::invalid_id,"Expected 32 lowercase hexadecimal library ID."); }
    return L"OutLoud_Pocket_"+wide(id);
}
std::wstring managed_sapi_token_id(std::string_view id,SapiVoiceScope scope) {
    scope_check(scope);
    return std::wstring(scope==SapiVoiceScope::machine ? L"HKEY_LOCAL_MACHINE" : L"HKEY_CURRENT_USER")+
        L"\\SOFTWARE\\Microsoft\\Speech\\Voices\\Tokens\\"+managed_sapi_token_key(id);
}
SapiVoiceManager::SapiVoiceManager(VoiceLibrary library,SapiVoiceRuntime runtime,std::shared_ptr<SapiVoiceRegistry> registry,
    MachineVoiceLibraryPolicy machine_policy)
    : library_(std::move(library)),runtime_(std::move(runtime)),registry_(std::move(registry)),machine_policy_(machine_policy) {
    if (!registry_) { throw SapiVoiceError(Code::internal_failure,"Missing registry backend."); }
}
void SapiVoiceManager::validate_machine_library(SapiVoiceScope scope) const {
    if (scope==SapiVoiceScope::machine && machine_policy_!=MachineVoiceLibraryPolicy::development_override && !library_.is_machine_storage()) {
        throw SapiVoiceError(Code::invalid_machine_library_root,"Production HKLM tokens require the ProgramData OutLoud/VoiceLibrary; other roots require an explicit development override.");
    }
}
LibraryVoice SapiVoiceManager::voice(std::string_view id) const {
    (void)managed_sapi_token_key(id);
    try { return library_.inspect_voice(id); }
    catch (const VoiceLibraryError& e) {
        throw SapiVoiceError(e.code()==VoiceLibraryErrorCode::voice_not_found ? Code::voice_not_found : library_code(e),e.what(),e.system_error(),e);
    }
}
SapiTokenTree SapiVoiceManager::desired(const LibraryVoice& v) const {
    if (v.health!=VoiceHealth::valid || !v.metadata) { throw SapiVoiceError(Code::unhealthy_voice,v.detail); }
    SapiTokenTree tree;
    const auto name = L"Out Loud - "+wide(v.metadata->display_name);
    set(tree,L"",name); set(tree,L"409",name); set(tree,L"CLSID",sapi::clsid_text);
    set(tree,L"OutLoudManagedOwner",marker); set(tree,L"LibraryId",wide(v.library_id));
    set(tree,L"AssetRoot",runtime_.asset_root.wstring()); set(tree,L"ModelPath",runtime_.model.wstring());
    set(tree,L"TokenizerPath",runtime_.tokenizer.wstring()); set(tree,L"VoicePath",v.managed_olv_path.wstring());
    set(tree,L"Seed",L"73"); set(tree,L"MaximumSegmentTokens",L"128"); set(tree,L"KernelPath",L"Auto");
    auto& attr = tree.children[L"Attributes"];
    set(attr,L"Name",name); set(attr,L"Language",L"409"); set(attr,L"Vendor",L"Out Loud"); return tree;
}
void SapiVoiceManager::validate_runtime(const LibraryVoice& v,SapiVoiceScope scope) const {
    if (v.health!=VoiceHealth::valid) { throw SapiVoiceError(Code::unhealthy_voice,v.detail); }
    try {
        detail::library_fs::Pins root(runtime_.asset_root);
        for (const auto& path : {runtime_.asset_root,runtime_.engine_dll,runtime_.model,runtime_.tokenizer,v.managed_olv_path}) {
            const auto safe = detail::library_fs::local(path,VoiceLibraryErrorCode::unsafe_path);
            if (safe!=path || !path.is_absolute()) { throw SapiVoiceError(Code::invalid_runtime,"Runtime paths must be normalized absolute local paths."); }
            detail::library_fs::Pins pins(path.parent_path());
            if (path!=runtime_.asset_root) { auto pinned = detail::library_fs::file(path); }
        }
        (void)validate_olv(v.managed_olv_path);
        PocketModelAsset model(runtime_.model); OutLoudVoice asset(v.managed_olv_path); asset.validate_against(model);
        Tokenizer tokenizer(runtime_.tokenizer);
    } catch (const VoiceImportError& e) { throw SapiVoiceError(Code::incompatible_voice,e.what(),e.system_error(),{},e); }
    catch (const VoiceLibraryError& e) { throw SapiVoiceError(e.code()==VoiceLibraryErrorCode::library_access_denied ? Code::library_access_denied : Code::invalid_runtime,e.what(),e.system_error(),e); }
    catch (const SapiVoiceError&) { throw; }
    catch (const std::exception& e) { throw SapiVoiceError(Code::invalid_runtime,e.what()); }
    registry_->verify_engine(runtime_.engine_dll,scope);
}
SapiVoiceToken SapiVoiceManager::inspect(std::string_view id,SapiVoiceScope scope) const {
    scope_check(scope);
    SapiVoiceToken result; result.library_id = id; result.token_key = managed_sapi_token_key(id);
    try { result.tree = registry_->read(scope,result.token_key); }
    catch (const SapiVoiceError& e) {
        if (e.code!=Code::malformed_token) { throw; }
        result.status = SapiTokenStatus::malformed; result.detail = e.what(); result.suggested_action = "review_registry"; return result;
    }
    std::optional<LibraryVoice> v;
    try { v = voice(id); } catch (const SapiVoiceError& e) { if (e.code!=Code::voice_not_found) { throw; } }
    if (!result.tree) { result.status = v && v->health!=VoiceHealth::valid ? SapiTokenStatus::unhealthy_voice : SapiTokenStatus::missing; }
    else if (!owned(*result.tree,id)) { result.status = SapiTokenStatus::conflict; }
    else if (!shape(*result.tree)) { result.status = SapiTokenStatus::malformed; }
    else if (!v) { result.status = SapiTokenStatus::orphan; }
    else if (v->health!=VoiceHealth::valid) { result.status = SapiTokenStatus::unhealthy_voice; result.detail = v->detail; }
    else if (scope==SapiVoiceScope::machine && machine_policy_!=MachineVoiceLibraryPolicy::development_override && !library_.is_machine_storage()) {
        result.status = SapiTokenStatus::wrong_path;
        result.detail = "This diagnostic library root is not production machine storage; HKLM install/update are prohibited.";
    }
    else {
        auto expected = desired(*v);
        if (runtime_.asset_root.empty() && runtime_.model.empty() && runtime_.tokenizer.empty()) {
            for (const auto* key : {L"AssetRoot",L"ModelPath",L"TokenizerPath"}) { expected.values[key] = result.tree->values.at(key); }
            result.detail = "Library identity/path/name checked; shared runtime configuration was not supplied.";
        }
        if (text(*result.tree,L"VoicePath")!=text(expected,L"VoicePath")) { result.status = SapiTokenStatus::wrong_path; }
        else if (*result.tree==expected) { result.status = SapiTokenStatus::correct; }
        else if (without_name(*result.tree)==without_name(expected)) { result.status = SapiTokenStatus::stale_name; }
        else { result.status = SapiTokenStatus::conflict; }
    }
    if (result.detail.empty()) { result.detail = std::string(sapi_token_status_name(result.status)); }
    switch (result.status) {
    case SapiTokenStatus::missing: result.suggested_action = v ? "install" : "none"; break;
    case SapiTokenStatus::correct: result.suggested_action = "none"; break;
    case SapiTokenStatus::stale_name: result.suggested_action = "update"; break;
    case SapiTokenStatus::orphan: result.suggested_action = "remove_token_explicitly"; break;
    case SapiTokenStatus::unhealthy_voice: result.suggested_action = "repair_library"; break;
    default: result.suggested_action = "review_registry"; break;
    }
    return result;
}
std::vector<SapiVoiceToken> SapiVoiceManager::list(SapiVoiceScope scope) const {
    scope_check(scope); std::vector<SapiVoiceToken> rows;
    for (const auto& key : registry_->keys(scope)) {
        // Ignore peers and legacy tokens without reading their contents.
        if (!key.starts_with(L"OutLoud_Pocket_") || key==sapi::token_name) { continue; }
        std::optional<SapiTokenTree> tree;
        try { tree = registry_->read(scope,key); }
        catch (const SapiVoiceError& e) {
            if (e.code!=Code::malformed_token) { throw; }
            rows.push_back({key,{},SapiTokenStatus::malformed,e.what(),{},"review_registry"}); continue;
        }
        if (!tree || !candidate(key,*tree)) { continue; }
        const auto id = utf8(key.substr(15));
        if (detail::library_fs::id(id)) { rows.push_back(inspect(id,scope)); }
        else { rows.push_back({key,{},SapiTokenStatus::malformed,"Unexpected managed candidate key; manual review required.",tree,"review_registry"}); }
    } return rows;
}
std::vector<SapiVoiceToken> SapiVoiceManager::reconcile(SapiVoiceScope scope) const {
    auto rows = list(scope);
    std::vector<LibraryVoice> voices;
    try { voices = library_.list_voices(); }
    catch (const VoiceLibraryError& e) { throw SapiVoiceError(library_code(e),e.what(),e.system_error(),e); }
    for (const auto& v : voices) {
        if (std::none_of(rows.begin(),rows.end(),[&](const auto& row) { return row.library_id==v.library_id; })) { rows.push_back(inspect(v.library_id,scope)); }
    } return rows;
}
SapiTokenChange SapiVoiceManager::install(std::string_view id,SapiVoiceScope scope) {
    scope_check(scope); validate_machine_library(scope); const auto v = voice(id); validate_runtime(v,scope); const auto next = desired(v);
    const auto key = managed_sapi_token_key(id); const auto old = registry_->read(scope,key);
    if (old==std::optional(next)) { return SapiTokenChange::already_installed; }
    if (old) { throw SapiVoiceError(Code::token_conflict,"Token exists with different contents; reconcile before changing it."); }
    registry_->exchange(scope,key,{},next); return SapiTokenChange::installed;
}
void SapiVoiceManager::validate_replacement(const LibraryVoice& current,const std::filesystem::path& staged,SapiVoiceScope scope) const {
    scope_check(scope); validate_machine_library(scope);
    if (!current.metadata || current.health!=VoiceHealth::valid || current.managed_olv_path!=library_.root()/L"voices"/wide(current.library_id)/L"voice.olv") {
        throw SapiVoiceError(Code::unhealthy_voice,"Replacement target must be a healthy managed entry.");
    }
    auto candidate=current; candidate.managed_olv_path=staged; validate_runtime(candidate,scope);
    const auto expected=desired(current); const auto actual=registry_->read(scope,managed_sapi_token_key(current.library_id));
    if (actual!=std::optional(expected)) { throw SapiVoiceError(Code::reconciliation_required,"Replacement requires the existing owned machine token to be correct. Reconcile it first; no content was published."); }
}
SapiTokenChange SapiVoiceManager::update(std::string_view id,SapiVoiceScope scope) {
    scope_check(scope); validate_machine_library(scope); const auto v = voice(id); validate_runtime(v,scope); const auto next = desired(v);
    const auto key = managed_sapi_token_key(id); const auto old = registry_->read(scope,key);
    if (!old) { throw SapiVoiceError(Code::token_not_found,"Token is not installed."); }
    if (!owned(*old,id) || !shape(*old) || without_name(*old)!=without_name(next)) {
        throw SapiVoiceError(Code::reconciliation_required,"Only an owned token with exclusively stale name fields can be updated.");
    }
    if (*old==next) { return SapiTokenChange::unchanged; }
    registry_->exchange(scope,key,old,next); return SapiTokenChange::updated;
}
SapiTokenChange SapiVoiceManager::remove(std::string_view id,SapiVoiceScope scope) {
    scope_check(scope); const auto key = managed_sapi_token_key(id); const auto old = registry_->read(scope,key);
    if (!old) { throw SapiVoiceError(Code::token_not_found,"Token is not installed."); }
    if (!owned(*old,id)) { throw SapiVoiceError(Code::token_conflict,"Ownership, library ID or CLSID differs."); }
    if (!shape(*old)) { throw SapiVoiceError(Code::malformed_token,"Unexpected registry values/subkeys; refusing removal."); }
    registry_->exchange(scope,key,old,{}); return SapiTokenChange::removed;
}
} // namespace outloud
