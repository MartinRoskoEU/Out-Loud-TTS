#include "outloud/sapi_voices.hpp"
#include "engine_registration.hpp"
#include "voice_library_fs.hpp"
#include "identity.hpp"
#include <KtmW32.h>
#include <array>
#include <set>
#include <cstring>

namespace outloud {
namespace {
using Code = SapiVoiceErrorCode;
void ok(LSTATUS result,const char* message) {
    if (result!=ERROR_SUCCESS) { throw SapiVoiceError(result==ERROR_ACCESS_DENIED ? Code::registry_access_denied : Code::registry_io,message,static_cast<DWORD>(result)); }
}
struct Key {
    HKEY value = nullptr;
    ~Key() { if (value) { RegCloseKey(value); } }
    Key() = default; Key(const Key&) = delete; Key& operator=(const Key&) = delete;
};
struct Transaction {
    HANDLE value = CreateTransaction(nullptr,nullptr,0,0,0,0,nullptr);
    bool committed = false;
    Transaction() { if (value==INVALID_HANDLE_VALUE) { ok(static_cast<LSTATUS>(GetLastError()),"Cannot start registry transaction."); } }
    ~Transaction() { if (!committed) { (void)RollbackTransaction(value); } CloseHandle(value); }
    void commit() { if (!CommitTransaction(value)) { ok(static_cast<LSTATUS>(GetLastError()),"Registry commit failed; no fallback writes."); } committed = true; }
};
bool open(HKEY parent,const wchar_t* name,Key& out,REGSAM access,HANDLE tx = nullptr,bool create = false) {
    LSTATUS result;
    if (create) {
        DWORD disposition = 0;
        result = RegCreateKeyTransactedW(parent,name,0,nullptr,REG_OPTION_NON_VOLATILE,
            access|KEY_WOW64_64KEY,nullptr,&out.value,&disposition,tx,nullptr);
        if (result==ERROR_SUCCESS && disposition!=REG_CREATED_NEW_KEY) {
            throw SapiVoiceError(Code::token_conflict,"Key appeared during creation; transaction rolled back.");
        }
    } else if (tx) {
        result = RegOpenKeyTransactedW(parent,name,REG_OPTION_OPEN_LINK,access|KEY_WOW64_64KEY,&out.value,tx,nullptr);
    } else { result = RegOpenKeyExW(parent,name,REG_OPTION_OPEN_LINK,access|KEY_WOW64_64KEY,&out.value); }
    if (result==ERROR_FILE_NOT_FOUND || result==ERROR_PATH_NOT_FOUND) { return false; }
    ok(result,"Cannot open registry key.");
    DWORD type = 0, bytes = 0;
    const auto link = RegQueryValueExW(out.value,L"SymbolicLinkValue",nullptr,&type,nullptr,&bytes);
    if (link==ERROR_SUCCESS && type==REG_LINK) { throw SapiVoiceError(Code::malformed_token,"Registry links are not supported."); }
    if (link!=ERROR_SUCCESS && link!=ERROR_FILE_NOT_FOUND) { ok(link,"Cannot inspect registry link state."); }
    return true;
}
// Walk every known ancestor without following registry links. No configurable
// registry root is accepted by the production implementation.
bool root(Key& result,HANDLE tx = nullptr,bool create = false) {
    constexpr std::array names{L"SOFTWARE",L"Microsoft",L"Speech",L"Voices",L"Tokens"};
    std::array<Key,5> ancestors;
    HKEY parent = HKEY_LOCAL_MACHINE;
    for (std::size_t i = 0; i < names.size(); ++i) {
        auto& next = i==names.size()-1 ? result : ancestors[i];
        if (!open(parent,names[i],next,create ? KEY_READ|KEY_WRITE : KEY_READ,tx)) {
            if (!create || !open(parent,names[i],next,KEY_READ|KEY_WRITE,tx,true)) { return false; }
        }
        parent = next.value;
    } return true;
}
std::vector<std::wstring> names(HKEY key,bool values) {
    std::vector<std::wstring> result;
    for (DWORD i = 0; ; ++i) {
        wchar_t buffer[16384]{}; DWORD length = static_cast<DWORD>(std::size(buffer));
        const auto status = values ? RegEnumValueW(key,i,buffer,&length,nullptr,nullptr,nullptr,nullptr) :
            RegEnumKeyExW(key,i,buffer,&length,nullptr,nullptr,nullptr,nullptr);
        if (status==ERROR_NO_MORE_ITEMS) { break; }
        ok(status,"Cannot enumerate registry contents.");
        if (i>=(values ? 64U : 4096U)) { throw SapiVoiceError(Code::malformed_token,"Registry enumeration exceeds safety bound."); }
        result.emplace_back(buffer,length);
    } return result;
}
SapiTokenTree snapshot(HKEY key,HANDLE tx = nullptr,bool attributes = false) {
    SapiTokenTree tree;
    for (const auto& name : names(key,true)) {
        SapiRegistryValue value; DWORD bytes = 0, type = 0;
        ok(RegQueryValueExW(key,name.c_str(),nullptr,&type,nullptr,&bytes),"Cannot size registry value.");
        if (bytes>32768) { throw SapiVoiceError(Code::malformed_token,"Registry value exceeds safety bound."); }
        value.type = type; value.bytes.resize(bytes);
        ok(RegQueryValueExW(key,name.c_str(),nullptr,&type,value.bytes.data(),&bytes),"Cannot read registry value.");
        value.type = type; value.bytes.resize(bytes); tree.values.emplace(name,std::move(value));
    }
    for (const auto& name : names(key,false)) {
        // Unknown subkeys are represented, never recursively opened/deleted.
        if (!attributes && name==L"Attributes") {
            Key child; if (!open(key,name.c_str(),child,KEY_READ,tx)) { throw SapiVoiceError(Code::registry_io,"Registry changed during inspection."); }
            tree.children.emplace(name,snapshot(child.value,tx,true));
        } else { tree.children.emplace(name,SapiTokenTree{}); }
    } return tree;
}
void key_check(std::wstring_view key) {
    if (key.size()!=47 || !key.starts_with(L"OutLoud_Pocket_")) { throw SapiVoiceError(Code::invalid_id,"Unexpected managed token key."); }
    for (const auto c : key.substr(15)) { if (!((c>=L'0' && c<=L'9') || (c>=L'a' && c<=L'f'))) { throw SapiVoiceError(Code::invalid_id,"Unexpected managed token key."); } }
}
void writable(const SapiTokenTree& tree,std::wstring_view key) {
    const auto get = [&](const wchar_t* name) -> std::optional<std::wstring> {
        const auto it = tree.values.find(name); return it==tree.values.end() ? std::nullopt : sapi_registry_text(it->second);
    };
    if (get(L"OutLoudManagedOwner")!=L"OutLoud.ManagedVoice.1" || get(L"CLSID")!=sapi::clsid_text ||
        get(L"LibraryId")!=key.substr(15) || tree.children.size()!=1 || !tree.children.contains(L"Attributes") ||
        !tree.children.at(L"Attributes").children.empty()) { throw SapiVoiceError(Code::malformed_token,"Backend refuses unowned or unexpected token tree."); }
    // Managers define the full allowed schema; backend additionally bounds edits.
    static const std::set<std::wstring> allowed{L"",L"409",L"CLSID",L"OutLoudManagedOwner",L"LibraryId",L"AssetRoot",L"ModelPath",
        L"TokenizerPath",L"VoicePath",L"Seed",L"MaximumSegmentTokens",L"KernelPath"};
    static const std::set<std::wstring> attributes{L"Name",L"Language",L"Vendor"};
    if (tree.values.size()!=allowed.size() || tree.children.at(L"Attributes").values.size()!=attributes.size()) { throw SapiVoiceError(Code::malformed_token,"Unexpected token schema."); }
    for (const auto& [name,value] : tree.values) {
        if (!allowed.contains(name) || !sapi_registry_text(value)) { throw SapiVoiceError(Code::malformed_token,"Unexpected token value."); }
    }
    for (const auto& [name,value] : tree.children.at(L"Attributes").values) {
        if (!attributes.contains(name) || !sapi_registry_text(value)) { throw SapiVoiceError(Code::malformed_token,"Unexpected token attribute."); }
    }
}
void write(HKEY key,const SapiTokenTree& tree,HANDLE tx) {
    for (const auto& [name,value] : tree.values) {
        ok(RegSetValueExW(key,name.c_str(),0,value.type,value.bytes.data(),static_cast<DWORD>(value.bytes.size())),"Cannot write token value.");
    }
    Key attr;
    if (!open(key,L"Attributes",attr,KEY_READ|KEY_WRITE,tx)) { (void)open(key,L"Attributes",attr,KEY_READ|KEY_WRITE,tx,true); }
    for (const auto& [name,value] : tree.children.at(L"Attributes").values) {
        ok(RegSetValueExW(attr.value,name.c_str(),0,value.type,value.bytes.data(),static_cast<DWORD>(value.bytes.size())),"Cannot write token attribute.");
    }
}
class WindowsRegistry final : public SapiVoiceRegistry {
public:
    std::vector<std::wstring> keys() override {
        Key parent; if (!root(parent)) { return {}; } return names(parent.value,false);
    }
    std::optional<SapiTokenTree> read(std::wstring_view name) override {
        if (name.find_first_of(L"\\/\0",0,3)!=name.npos || name.empty()) { throw SapiVoiceError(Code::invalid_id,"Invalid registry key."); }
        Key parent; if (!root(parent)) { return {}; }
        Key token; const std::wstring key(name); if (!open(parent.value,key.c_str(),token,KEY_READ)) { return {}; }
        return snapshot(token.value);
    }
    void verify_engine(const std::filesystem::path& dll) override {
        // Machine voices require the shared HKLM Registry64 COM registration.
        // No effective HKCR lookup or CLSID writes.
        try {
            Key server;
            const auto path = std::wstring(L"SOFTWARE\\Classes\\CLSID\\")+
                sapi::clsid_text+L"\\InprocServer32";
            if (!open(HKEY_LOCAL_MACHINE,path.c_str(),server,KEY_READ)) {
                throw SapiVoiceError(Code::engine_registration_missing,"Register the engine manually before installing managed tokens.");
            }
            const auto tree = snapshot(server.value);
            const auto it = tree.values.find(L"");
            detail::validate_engine_registration(it==tree.values.end() ? std::nullopt : std::optional(it->second),dll);
        } catch (const SapiVoiceError& e) {
            if (e.code!=Code::malformed_token) { throw; }
            throw SapiVoiceError(Code::engine_registration_malformed,e.what(),e.system_error);
        }
    }
    void exchange(std::wstring_view name,const std::optional<SapiTokenTree>& expected,
        const std::optional<SapiTokenTree>& replacement) override {
        key_check(name); if (expected) { writable(*expected,name); } if (replacement) { writable(*replacement,name); }
        Transaction tx; Key parent;
        if (!root(parent,tx.value,true)) { throw SapiVoiceError(Code::registry_io,"Cannot open token root."); }
        const std::wstring key(name); Key token;
        const bool present = open(parent.value,key.c_str(),token,KEY_READ|KEY_WRITE,tx.value);
        const auto actual = present ? std::optional(snapshot(token.value,tx.value)) : std::nullopt;
        if (actual!=expected) { throw SapiVoiceError(Code::token_conflict,"Token changed since inspection; transaction rolled back."); }
        if (replacement) {
            if (!present) { (void)open(parent.value,key.c_str(),token,KEY_READ|KEY_WRITE,tx.value,true); }
            write(token.value,*replacement,tx.value);
        } else if (present) {
            ok(RegDeleteKeyTransactedW(token.value,L"Attributes",KEY_WOW64_64KEY,0,tx.value,nullptr),"Cannot delete owned Attributes key.");
            ok(RegDeleteKeyTransactedW(parent.value,key.c_str(),KEY_WOW64_64KEY,0,tx.value,nullptr),"Cannot delete owned token key.");
        }
        tx.commit();
    }
};
}
namespace detail {
namespace {
namespace fs = library_fs;
std::filesystem::path engine_path(const std::filesystem::path& input,Code unsafe) {
    try {
        if (!input.is_absolute() || !input.has_filename() || _wcsicmp(input.extension().c_str(),L".dll")!=0) {
            throw SapiVoiceError(unsafe,"Engine DLL must be an absolute regular local .dll path.");
        }
        // Normalize separators, retaining the existing alias/reparse policy.
        // In particular no untrusted \\?\ prefix is stripped or admitted.
        return fs::local(input,VoiceLibraryErrorCode::unsafe_path);
    } catch (const VoiceLibraryError& e) { throw SapiVoiceError(unsafe,e.what(),e.system_error(),e); }
}
struct EngineFile {
    fs::Pins parents;
    fs::Handle file;
    FILE_ID_INFO identity{};
    explicit EngineFile(const std::filesystem::path& path)
        : parents(path.parent_path()),file(fs::file(path,FILE_READ_ATTRIBUTES,FILE_SHARE_READ)) {
        if (!GetFileInformationByHandleEx(file.value,FileIdInfo,&identity,sizeof(identity))) {
            const auto error = GetLastError();
            throw SapiVoiceError(Code::engine_dll_inaccessible,"Cannot read engine DLL file identity.",error);
        }
    }
};
}
void validate_engine_registration(const std::optional<SapiRegistryValue>& registered,const std::filesystem::path& selected_dll) {
    const auto decoded = registered ? sapi_registry_text(*registered) : std::nullopt;
    if (!decoded || decoded->empty()) {
        throw SapiVoiceError(Code::engine_registration_malformed,"InprocServer32 default value must be a nonempty, well-formed REG_SZ DLL path.");
    }
    const auto selected = engine_path(selected_dll,Code::invalid_runtime);
    const auto actual = engine_path(*decoded,Code::engine_registration_malformed);
    // Pin both ancestor chains and files concurrently. Compare volume + 128-bit
    // file ID, never the lexical spelling, a workspace path or a final-path
    // string. Loaded DLLs remain readable; no write/delete access is requested.
    std::optional<EngineFile> expected_file,actual_file;
    try { expected_file.emplace(selected); }
    catch (const VoiceLibraryError& e) {
        throw SapiVoiceError(e.code()==VoiceLibraryErrorCode::unsafe_path ? Code::invalid_runtime : Code::engine_dll_inaccessible,
            e.what(),e.system_error(),e);
    }
    try { actual_file.emplace(actual); }
    catch (const VoiceLibraryError& e) {
        throw SapiVoiceError(e.code()==VoiceLibraryErrorCode::unsafe_path ? Code::engine_registration_malformed : Code::engine_dll_inaccessible,
            e.what(),e.system_error(),e);
    }
    const auto& expected = expected_file->identity; const auto& found = actual_file->identity;
    if (expected.VolumeSerialNumber!=found.VolumeSerialNumber || std::memcmp(expected.FileId.Identifier,found.FileId.Identifier,sizeof(expected.FileId.Identifier))!=0) {
        throw SapiVoiceError(Code::engine_dll_mismatch,"Effective COM engine DLL is a different file from the selected shared runtime.");
    }
}
}
std::shared_ptr<SapiVoiceRegistry> windows_sapi_voice_registry() { return std::make_shared<WindowsRegistry>(); }
} // namespace outloud
