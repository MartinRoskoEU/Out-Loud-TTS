#include "outloud/sapi_voices.hpp"
#include "scratch.hpp"
#include "engine_registration.hpp"
#include "voice_registry_location.hpp"
#include "identity.hpp"
#include <winioctl.h>
#include <shlobj.h>
#include <iostream>
#include <atomic>
#include <fstream>
#include <mutex>
#include <thread>

namespace {
using namespace outloud;
using Code = SapiVoiceErrorCode;
unsigned checks = 0;
void check(bool value,const char* message) { ++checks; if (!value) { throw std::runtime_error(message); } }
template<class F> SapiVoiceError rejects(Code code,F fn) {
    try { fn(); } catch (const SapiVoiceError& e) {
        if (e.code!=code) { std::cerr << "Expected " << sapi_voice_error_name(code) << "; got " << sapi_voice_error_name(e.code) << ": " << e.what() << '\n'; }
        check(e.code==code,"Wrong structured SAPI error."); return e;
    }
    throw std::runtime_error("Invalid operation accepted.");
}
class FakeRegistry final : public SapiVoiceRegistry {
public:
    std::map<std::wstring,SapiTokenTree> tokens;
    std::map<std::wstring,SapiTokenTree> user_tokens;
    std::mutex mutex;
    bool engine = true, fail_commit = false, race = false;
    std::optional<SapiRegistryValue> engine_registration;
    std::wstring engine_clsid = sapi::clsid_text;
    std::optional<Code> failure;
    unsigned writes = 0;
    void fail() { if (failure) { throw SapiVoiceError(*failure,"Injected native registry error.",*failure==Code::registry_access_denied ? ERROR_ACCESS_DENIED : ERROR_WRITE_FAULT); } }
    std::vector<std::wstring> keys(SapiVoiceScope scope) override {
        std::lock_guard lock(mutex); fail(); std::vector<std::wstring> names;
        for (const auto& [key,tree] : scope==SapiVoiceScope::machine ? tokens : user_tokens) { names.push_back(key); (void)tree; } return names;
    }
    std::optional<SapiTokenTree> read(SapiVoiceScope scope,std::wstring_view key) override {
        std::lock_guard lock(mutex); fail(); const auto& entries = scope==SapiVoiceScope::machine ? tokens : user_tokens;
        const auto it = entries.find(std::wstring(key));
        return it==entries.end() ? std::nullopt : std::optional(it->second);
    }
    void verify_engine(const std::filesystem::path& dll,SapiVoiceScope scope) override {
        (void)scope;
        if (!engine || engine_clsid!=sapi::clsid_text) { throw SapiVoiceError(Code::engine_registration_missing,"Injected missing engine/expected CLSID."); }
        // Only the registry lookup is fake. The decoder, path policy, file
        // pinning and identity validation are the production implementation.
        detail::validate_engine_registration(engine_registration ? engine_registration : std::optional(sapi_registry_string(dll.wstring())),dll);
    }
    void exchange(SapiVoiceScope scope,std::wstring_view name,const std::optional<SapiTokenTree>& expected,
        const std::optional<SapiTokenTree>& replacement) override {
        std::lock_guard lock(mutex); fail(); const std::wstring key(name);
        auto& entries = scope==SapiVoiceScope::machine ? tokens : user_tokens;
        if (race) { entries[key].values[L"Unexpected"] = sapi_registry_string(L"external writer"); race = false; }
        const auto it = entries.find(key); const auto old = it==entries.end() ? std::nullopt : std::optional(it->second);
        if (old!=expected) { throw SapiVoiceError(Code::token_conflict,"Concurrent modification."); }
        if (fail_commit) { throw SapiVoiceError(Code::registry_io,"Injected rollback.",ERROR_WRITE_FAULT); }
        if (replacement) { entries[key] = *replacement; } else { entries.erase(key); } ++writes;
    }
};
void junction(const std::filesystem::path& link,const std::filesystem::path& target) {
    check(CreateDirectoryW(link.c_str(),nullptr)!=0,"Cannot create private junction directory.");
    auto handle = detail::library_fs::directory(link,GENERIC_WRITE);
    const auto substitute = L"\\??\\"+target.native(), print = target.native();
    const auto bytes = (substitute.size()+print.size()+2)*sizeof(wchar_t);
    std::vector<std::uint64_t> storage((16+bytes+7)/8,0);
    auto* raw = reinterpret_cast<unsigned char*>(storage.data());
    const DWORD tag = IO_REPARSE_TAG_MOUNT_POINT; const auto length = static_cast<WORD>(8+bytes);
    const WORD sub_offset = 0, sub_length = static_cast<WORD>(substitute.size()*2);
    const WORD print_offset = static_cast<WORD>((substitute.size()+1)*2), print_length = static_cast<WORD>(print.size()*2);
    std::memcpy(raw,&tag,4); std::memcpy(raw+4,&length,2);
    std::memcpy(raw+8,&sub_offset,2); std::memcpy(raw+10,&sub_length,2);
    std::memcpy(raw+12,&print_offset,2); std::memcpy(raw+14,&print_length,2);
    std::memcpy(raw+16,substitute.data(),sub_length); std::memcpy(raw+16+print_offset,print.data(),print_length);
    DWORD received = 0;
    check(DeviceIoControl(handle.value,FSCTL_SET_REPARSE_POINT,raw,static_cast<DWORD>(16+bytes),nullptr,0,&received,nullptr)!=0,
        "Cannot set private junction fixture.");
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        check(argc==5,"Usage: token-tests DLL MODEL TOKENIZER GNOME.olv");
        sapi_test::Scratch scratch; VoiceLibrary library(scratch.path/L"kni\u017enica");
        // Exercise every alignment of the Win32 variable-size rename buffer.
        // In particular, an exact eight-byte allocation must still hold NUL.
        for (std::size_t alignment = 0; alignment<4; ++alignment) {
            const auto source = scratch.path/L"rename-source";
            { std::ofstream output(source,std::ios::binary); output << "rename sentinel"; }
            auto destination = scratch.path/L"renamed";
            while (destination.native().size()%4!=alignment) { destination += L"x"; }
            auto handle = detail::library_fs::file(source,GENERIC_READ|DELETE);
            detail::library_fs::rename(handle.value,destination,false,VoiceLibraryErrorCode::destination_io);
            check(std::filesystem::is_regular_file(destination) && !std::filesystem::exists(source),"Win32 rename appended characters.");
            detail::library_fs::discard(handle.value,destination);
        }
        const auto imported = library.import_olv(argv[4],"Hlas \xc5\xbe \xf0\x9f\x8e\xb5"); const auto id = imported.voice.library_id;
        const auto body = validate_olv(imported.voice.managed_olv_path).file_sha256;
        const auto registry = std::make_shared<FakeRegistry>();
        const auto runtime_root = scratch.path/L"shared-runtime";
        std::filesystem::create_directories(runtime_root/L"bin");
        const auto selected = runtime_root/L"bin/OutLoudPocketSAPI.dll"; // exact former CLI construction
        std::filesystem::copy_file(argv[1],selected);
        registry->engine_registration = sapi_registry_string(selected.lexically_normal().wstring());
        const auto registered = sapi_registry_text(*registry->engine_registration);
        check(registered && CompareStringOrdinal(registered->c_str(),-1,selected.c_str(),-1,TRUE)!=CSTR_EQUAL,
            "Mixed-slash fixture does not reproduce the original raw-string mismatch.");
        detail::validate_engine_registration(registry->engine_registration,selected);
        check(true,"Mixed-slash engine DLL was not accepted.");
        auto mixed_registered = sapi_registry_string(selected.wstring());
        detail::validate_engine_registration(mixed_registered,selected.lexically_normal());
        check(true,"Mixed registered path was not accepted.");
        auto case_variant = registered.value();
        for (auto& c : case_variant) { if (c>=L'a' && c<=L'z') { c = static_cast<wchar_t>(c-L'a'+L'A'); } }
        detail::validate_engine_registration(sapi_registry_string(case_variant),selected);
        check(true,"Case variant rejected the same file.");
        {
            const auto pinned = detail::library_fs::file(selected.lexically_normal(),FILE_READ_ATTRIBUTES);
            const auto final = detail::library_fs::final_path(pinned.value,selected);
            detail::validate_engine_registration(sapi_registry_string(final.wstring()),selected);
            check(true,"Canonical handle-resolved path rejected the same file.");
            // The final-path helper removes Windows' trusted API prefix. Raw
            // device prefixes supplied by registry/caller remain prohibited.
            rejects(Code::engine_registration_malformed,[&] { detail::validate_engine_registration(sapi_registry_string(L"\\\\?\\"+final.wstring()),selected); });
            rejects(Code::invalid_runtime,[&] { detail::validate_engine_registration(registry->engine_registration,L"\\\\?\\"+final.wstring()); });
        }
        {
            struct Loaded {
                HMODULE module;
                ~Loaded() { if (module) { FreeLibrary(module); } }
            } loaded{LoadLibraryExW(selected.lexically_normal().c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32)};
            check(loaded.module!=nullptr,"Cannot load private DLL for sharing regression.");
            detail::validate_engine_registration(registry->engine_registration,selected);
            check(true,"Loaded DLL could not be validated read-only.");
        }
        // Short-name aliases are resolved by pinned file identity when the
        // filesystem provides them. No reparse or hard-link alias is admitted.
        std::wstring short_path(32768,L'\0');
        const auto short_size = GetShortPathNameW(selected.lexically_normal().c_str(),short_path.data(),static_cast<DWORD>(short_path.size()));
        check(short_size && short_size<short_path.size(),"Cannot inspect short-path representation."); short_path.resize(short_size);
        detail::validate_engine_registration(sapi_registry_string(short_path),selected);
        check(true,"Short/long representation rejected the same file.");
        const auto different = runtime_root/L"bin/different.dll";
        std::filesystem::copy_file(argv[1],different);
        rejects(Code::engine_dll_mismatch,[&] { detail::validate_engine_registration(sapi_registry_string(different.wstring()),selected); });
        rejects(Code::engine_registration_malformed,[&] { detail::validate_engine_registration({},selected); });
        rejects(Code::engine_registration_malformed,[&] { detail::validate_engine_registration(sapi_registry_string(L""),selected); });
        auto invalid_registration = *registry->engine_registration; invalid_registration.type = REG_EXPAND_SZ;
        rejects(Code::engine_registration_malformed,[&] { detail::validate_engine_registration(invalid_registration,selected); });
        invalid_registration = *registry->engine_registration; invalid_registration.bytes.resize(invalid_registration.bytes.size()-2);
        rejects(Code::engine_registration_malformed,[&] { detail::validate_engine_registration(invalid_registration,selected); });
        invalid_registration = *registry->engine_registration; invalid_registration.bytes[2] = invalid_registration.bytes[3] = 0;
        rejects(Code::engine_registration_malformed,[&] { detail::validate_engine_registration(invalid_registration,selected); });
        for (const auto* unsafe : {L"relative.dll",L"C:relative.dll",L"\\\\server\\share\\engine.dll",L"\\\\.\\engine.dll",
            L"C:\\engine.dll:stream",L"C:\\parent\\..\\engine.dll",L"C:\\engine.dll.",L"C:\\engine.dll ",L"C:\\CON.dll",L"C:\\engine.dll\\"}) {
            rejects(Code::engine_registration_malformed,[&] { detail::validate_engine_registration(sapi_registry_string(unsafe),selected); });
        }
        const auto missing_dll = runtime_root/L"bin/missing.dll";
        const auto missing_engine = rejects(Code::engine_dll_inaccessible,[&] { detail::validate_engine_registration(sapi_registry_string(missing_dll.wstring()),selected); });
        check(missing_engine.system_error==ERROR_FILE_NOT_FOUND && missing_engine.library_error.has_value(),"DLL failure lost its Win32/underlying cause.");
        const auto directory_dll = runtime_root/L"bin/directory.dll"; std::filesystem::create_directory(directory_dll);
        rejects(Code::engine_dll_inaccessible,[&] { detail::validate_engine_registration(sapi_registry_string(directory_dll.wstring()),selected); });
        const auto hard_link = runtime_root/L"bin/hardlink.dll";
        check(CreateHardLinkW(hard_link.c_str(),different.c_str(),nullptr)!=0,"Cannot create private hard-link fixture.");
        rejects(Code::engine_registration_malformed,[&] { detail::validate_engine_registration(sapi_registry_string(hard_link.wstring()),selected); });
        check(DeleteFileW(hard_link.c_str())!=0,"Cannot remove private hard-link fixture.");
        {
            auto locked = detail::library_fs::file(different,GENERIC_READ,0);
            // FILE_READ_ATTRIBUTES can inspect identity while file-data reads
            // are exclusively held. That still must reject this different DLL.
            rejects(Code::engine_dll_mismatch,[&] { detail::validate_engine_registration(sapi_registry_string(different.wstring()),selected); });
        }
        const auto alias = scratch.path/L"runtime-alias";
        junction(alias,runtime_root/L"bin");
        rejects(Code::engine_registration_malformed,[&] { detail::validate_engine_registration(sapi_registry_string((alias/L"OutLoudPocketSAPI.dll").wstring()),selected); });
        const SapiVoiceRuntime runtime{runtime_root,selected,
            std::filesystem::absolute(argv[2]),std::filesystem::absolute(argv[3])};
        SapiVoiceManager production(library,runtime,registry);
        rejects(Code::invalid_machine_library_root,[&] { (void)production.install(id); });
        rejects(Code::invalid_machine_library_root,[&] { (void)production.update(id); });
        check(registry->tokens.empty() && !registry->writes,"Production root rejection mutated registry.");
        check(!library.is_machine_storage(),"Temporary/user-profile storage classified as production.");
        const auto machine_location = detail::voice_registry_location(SapiVoiceScope::machine);
        check(machine_location.hive==HKEY_LOCAL_MACHINE && machine_location.view==KEY_WOW64_64KEY,"Machine registry hive/view changed.");
        const auto user_location = detail::voice_registry_location(SapiVoiceScope::user);
        check(user_location.hive==HKEY_CURRENT_USER && user_location.view==KEY_WOW64_64KEY,"Legacy registry hive/view changed.");
        const std::string known_id = "a4fb27ae6305b2c563711fdb512bcbb2";
        check(managed_sapi_token_key(known_id)==L"OutLoud_Pocket_a4fb27ae6305b2c563711fdb512bcbb2","Stable token key changed.");
        check(managed_sapi_token_id(known_id)==L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Speech\\Voices\\Tokens\\OutLoud_Pocket_a4fb27ae6305b2c563711fdb512bcbb2","Machine token ID path wrong.");
        check(managed_sapi_token_id(known_id,SapiVoiceScope::user)==L"HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Speech\\Voices\\Tokens\\OutLoud_Pocket_a4fb27ae6305b2c563711fdb512bcbb2","Legacy token ID path changed.");
        SapiVoiceManager manager(library,runtime,registry,MachineVoiceLibraryPolicy::development_override);
        check(manager.inspect(id).status==SapiTokenStatus::missing,"Missing token not reported.");
        check(manager.reconcile().size()==1 && !registry->writes,"Reconcile mutated registry.");
        rejects(Code::unsupported_scope,[&] { (void)manager.install(id,static_cast<SapiVoiceScope>(99)); });
        rejects(Code::invalid_id,[&] { (void)manager.remove("../bad"); });
        const std::string absent(32,'0');
        auto missing = rejects(Code::voice_not_found,[&] { (void)manager.install(absent); });
        check(missing.library_error.has_value(),"Underlying library cause lost.");
        rejects(Code::token_not_found,[&] { (void)manager.remove(id); });
        registry->engine = false;
        rejects(Code::engine_registration_missing,[&] { (void)manager.install(id); }); registry->engine = true;
        registry->engine_clsid = L"{00000000-0000-0000-0000-000000000000}";
        rejects(Code::engine_registration_missing,[&] { (void)manager.install(id); }); registry->engine_clsid = sapi::clsid_text;
        registry->engine_registration = sapi_registry_string(different.wstring());
        rejects(Code::engine_dll_mismatch,[&] { (void)manager.install(id); });
        check(registry->tokens.empty() && !registry->writes,"DLL mismatch mutated registry.");
        registry->engine_registration = SapiRegistryValue{REG_DWORD,{0,0,0,0}};
        rejects(Code::engine_registration_malformed,[&] { (void)manager.install(id); });
        registry->engine_registration = sapi_registry_string(selected.lexically_normal().wstring());
        registry->fail_commit = true;
        auto failed = rejects(Code::registry_io,[&] { (void)manager.install(id); });
        check(failed.system_error==ERROR_WRITE_FAULT && registry->tokens.empty(),"Failed install published token."); registry->fail_commit = false;
        registry->failure = Code::registry_access_denied;
        auto denied = rejects(Code::registry_access_denied,[&] { (void)manager.install(id); });
        check(denied.system_error==ERROR_ACCESS_DENIED,"Access denied flattened."); registry->failure.reset();
        check(manager.install(id)==SapiTokenChange::installed,"Install failed.");
        const auto key = managed_sapi_token_key(id); const auto original = registry->tokens.at(key);
        check(production.inspect(id).status==SapiTokenStatus::wrong_path,"Production inspection accepted machine token tied to user/test storage.");
        check(original.values.size()==12 && original.children.size()==1,"Machine token snapshot schema changed.");
        check(sapi_registry_text(original.values.at(L"LibraryId"))==detail::library_fs::wide(id) &&
            sapi_registry_text(original.values.at(L"CLSID"))==sapi::clsid_text &&
            sapi_registry_text(original.children.at(L"Attributes").values.at(L"Vendor"))==L"Out Loud" &&
            sapi_registry_text(original.children.at(L"Attributes").values.at(L"Language"))==L"409","Machine snapshot identity/attributes changed.");
        // Legacy mutation/cleanup is retained, but machine calls never see it.
        check(production.install(id,SapiVoiceScope::user)==SapiTokenChange::installed,"Legacy explicit user install compatibility failed.");
        check(production.inspect(id,SapiVoiceScope::user).status==SapiTokenStatus::correct,"Legacy inspect failed.");
        const auto legacy_snapshot = registry->user_tokens;
        const auto machine_before = registry->tokens;
        registry->failure = Code::registry_access_denied;
        rejects(Code::registry_access_denied,[&] { (void)manager.list(); });
        rejects(Code::registry_access_denied,[&] { (void)manager.inspect(id); });
        rejects(Code::registry_access_denied,[&] { (void)manager.reconcile(); });
        rejects(Code::registry_access_denied,[&] { (void)manager.update(id); });
        rejects(Code::registry_access_denied,[&] { (void)manager.remove(id); });
        check(registry->tokens==machine_before && registry->user_tokens==legacy_snapshot,"Permission failures damaged either scope.");
        registry->failure.reset();
        check(production.reconcile(SapiVoiceScope::user).size()==1 && registry->tokens==machine_before,"User reconciliation touched machine tokens.");
        check(original.children.at(L"Attributes").values.size()==3,"Invented gender/age attributes.");
        check(manager.install(id)==SapiTokenChange::already_installed && manager.inspect(id).status==SapiTokenStatus::correct,"Idempotence failed.");
        check(sapi_registry_text(original.values.at(L"VoicePath"))==imported.voice.managed_olv_path.wstring(),"Unicode managed path lost.");
        (void)library.rename_voice(id,"Renamed voice");
        check(manager.inspect(id).status==SapiTokenStatus::stale_name,"Stale name not reported.");
        check(production.update(id,SapiVoiceScope::user)==SapiTokenChange::updated,"Legacy rename sync failed.");
        check(registry->user_tokens.at(key).values.at(L"VoicePath")==legacy_snapshot.at(key).values.at(L"VoicePath"),"Legacy rename changed path.");
        const auto legacy_renamed = registry->user_tokens;
        rejects(Code::token_conflict,[&] { (void)manager.install(id); });
        registry->fail_commit = true;
        rejects(Code::registry_io,[&] { (void)manager.update(id); });
        check(registry->tokens.at(key)==original,"Failed update published partial metadata."); registry->fail_commit = false;
        check(manager.update(id)==SapiTokenChange::updated && manager.update(id)==SapiTokenChange::unchanged,"Rename sync failed.");
        check(registry->tokens.at(key).values.at(L"VoicePath")==original.values.at(L"VoicePath"),"Rename changed voice path.");
        const auto renamed = registry->tokens.at(key);
        for (const auto* folder : {&FOLDERID_LocalAppData,&FOLDERID_Profile}) {
            PWSTR user_folder = nullptr;
            check(SUCCEEDED(SHGetKnownFolderPath(*folder,KF_FLAG_DONT_VERIFY,nullptr,&user_folder)),"Cannot resolve user-folder rejection fixture.");
            const auto wrong_path = std::filesystem::path(user_folder)/L"OutLoud"/L"VoiceLibrary"/L"voices"/detail::library_fs::wide(id)/L"voice.olv";
            CoTaskMemFree(user_folder);
            // Fake token data only: no user folder/asset is opened or changed.
            registry->tokens.at(key).values[L"VoicePath"] = sapi_registry_string(wrong_path.wstring());
            check(manager.inspect(id).status==SapiTokenStatus::wrong_path,"User-folder VoicePath accepted for machine token.");
            rejects(Code::reconciliation_required,[&] { (void)manager.update(id); });
            registry->tokens[key] = renamed;
        }
        registry->tokens.at(key).values[L"VoicePath"] = sapi_registry_string(L"C:\\wrong.olv");
        check(manager.inspect(id).status==SapiTokenStatus::wrong_path,"Wrong path not reported.");
        rejects(Code::reconciliation_required,[&] { (void)manager.update(id); }); registry->tokens[key] = renamed;
        for (const auto& field : {L"OutLoudManagedOwner",L"LibraryId",L"CLSID"}) {
            registry->tokens.at(key).values[field] = sapi_registry_string(L"wrong");
            rejects(Code::token_conflict,[&] { (void)manager.remove(id); }); registry->tokens[key] = renamed;
        }
        registry->tokens.at(key).children[L"Unexpected"].children[L"NeverDelete"] = {};
        check(manager.inspect(id).status==SapiTokenStatus::malformed,"Unexpected tree not reported.");
        rejects(Code::malformed_token,[&] { (void)manager.remove(id); }); registry->tokens[key] = renamed;
        registry->tokens.at(key).values[L"Seed"].type = REG_DWORD;
        rejects(Code::malformed_token,[&] { (void)manager.remove(id); }); registry->tokens[key] = renamed;
        registry->race = true;
        rejects(Code::token_conflict,[&] { (void)manager.remove(id); });
        check(registry->tokens.at(key).values.contains(L"Unexpected"),"Concurrent registry writer overwritten."); registry->tokens[key] = renamed;
        registry->tokens[L"OutLoud_Pocket_GnomeMale_Development"] = {};
        registry->tokens[L"Microsoft_Unrelated"] = {};
        registry->tokens[L"OutLoud_Pocket_malformed"].values[L"OutLoudManagedOwner"] = sapi_registry_string(L"OutLoud.ManagedVoice.1");
        check(manager.list().size()==2,"Legacy/unrelated not ignored or malformed candidate lost.");
        check(registry->user_tokens==legacy_renamed,"Machine enumeration migrated/modified legacy HKCU data.");
        const auto second = library.import_olv(argv[4],"Duplicate content distinct ID",DuplicateContentPolicy::allow_separate_entry).voice;
        check(manager.install(second.library_id)==SapiTokenChange::installed,"Independent second token failed.");
        const auto peer = registry->tokens.at(managed_sapi_token_key(second.library_id));
        check(manager.reconcile().size()==3 && registry->user_tokens==legacy_renamed,"Machine reconciliation merged scopes or lost candidates.");
        std::atomic<unsigned> removed{0}, rejected{0}; std::exception_ptr concurrent_error;
        std::mutex errors;
        const auto remove_concurrently = [&] {
            try { if (manager.remove(id)==SapiTokenChange::removed) { ++removed; } }
            catch (const SapiVoiceError& e) {
                if (e.code==Code::token_conflict || e.code==Code::token_not_found) { ++rejected; }
                else { std::lock_guard lock(errors); concurrent_error = std::current_exception(); }
            } catch (...) { std::lock_guard lock(errors); concurrent_error = std::current_exception(); }
        };
        std::thread first(remove_concurrently), next(remove_concurrently); first.join(); next.join();
        if (concurrent_error) { std::rethrow_exception(concurrent_error); }
        check(removed==1 && rejected==1,"Concurrent removals did not preserve snapshot preconditions.");
        (void)manager.install(id);
        registry->fail_commit = true;
        rejects(Code::registry_io,[&] { (void)manager.remove(id); });
        check(registry->tokens.at(key)==renamed,"Failed removal damaged token."); registry->fail_commit = false;
        check(manager.remove(id)==SapiTokenChange::removed && library.inspect_voice(id).health==VoiceHealth::valid,"Token removal removed library voice.");
        check(registry->tokens.at(managed_sapi_token_key(second.library_id))==peer,"Removal changed peer.");
        check(validate_olv(imported.voice.managed_olv_path).file_sha256==body,"Operations changed .olv.");
        (void)library.remove_voice(second.library_id);
        check(manager.inspect(second.library_id).status==SapiTokenStatus::orphan,"Orphan token not reported.");
        check(manager.reconcile().size()==3,"Reconciliation lost orphan/missing/managed candidates.");
        check(manager.remove(second.library_id)==SapiTokenChange::removed,"Owned orphan removal failed.");
        (void)manager.install(id);
        { std::ofstream damage(imported.voice.managed_olv_path,std::ios::binary|std::ios::trunc); damage << "broken"; }
        check(manager.inspect(id).status==SapiTokenStatus::unhealthy_voice,"Corrupt voice not reported.");
        rejects(Code::unhealthy_voice,[&] { (void)manager.install(id); });
        (void)manager.remove(id);
        check(registry->user_tokens==legacy_renamed,"Machine removal changed legacy HKCU token.");
        const auto before_legacy_remove = registry->tokens;
        check(production.remove(id,SapiVoiceScope::user)==SapiTokenChange::removed,"Legacy explicit cleanup failed.");
        check(registry->user_tokens.empty() && registry->tokens==before_legacy_remove && registry->engine && registry->engine_clsid==sapi::clsid_text,
            "Legacy cleanup changed machine tokens/shared COM engine.");
        check(registry->tokens.contains(L"OutLoud_Pocket_GnomeMale_Development") && registry->tokens.contains(L"Microsoft_Unrelated"),"Unrelated tokens changed.");
        SapiRegistryValue invalid{REG_SZ,{0,0,0,0}}; check(!sapi_registry_text(invalid),"Embedded NUL accepted.");
        std::cout << "PASS: " << checks << " managed-token assertions; fake registry only, private temporary library.\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
