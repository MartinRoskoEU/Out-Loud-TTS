#include "voice_manager_internal.hpp"
#include "scratch.hpp"
#include <fstream>
#include <iostream>
#include <iterator>

namespace {
using namespace outloud;
unsigned checks = 0;
void check(bool value,const char* message) { ++checks; if (!value) { throw std::runtime_error(message); } }
bool text(const char* value,std::string_view expected) { return value && value==expected; }
class FakeRegistry final : public SapiVoiceRegistry {
public:
    std::map<std::wstring,SapiTokenTree> tokens;
    bool fixture_setup = false, deny = false, unexpected = false;
    unsigned writes = 0, verifies = 0, reads = 0;
    void scope(SapiVoiceScope value) { check(value==SapiVoiceScope::machine,"Bridge used non-machine scope."); }
    std::vector<std::wstring> keys(SapiVoiceScope value) override {
        scope(value); throw std::runtime_error("Bridge must inspect library rows, not enumerate unrelated registry tokens.");
    }
    std::optional<SapiTokenTree> read(SapiVoiceScope value,std::wstring_view key) override {
        scope(value); ++reads;
        if (deny) { throw SapiVoiceError(SapiVoiceErrorCode::registry_access_denied,"Injected registry denial.",5); }
        if (unexpected) { throw std::runtime_error("private C++ exception details"); }
        const auto it = tokens.find(std::wstring(key)); return it==tokens.end() ? std::nullopt : std::optional(it->second);
    }
    void verify_engine(const std::filesystem::path&,SapiVoiceScope value) override {
        scope(value); check(fixture_setup,"Read-only bridge attempted engine verification."); ++verifies;
    }
    void exchange(SapiVoiceScope value,std::wstring_view key,const std::optional<SapiTokenTree>& expected,
        const std::optional<SapiTokenTree>& replacement) override {
        scope(value); check(fixture_setup,"Read-only bridge attempted token mutation.");
        const auto name = std::wstring(key); const auto it = tokens.find(name);
        const auto current = it==tokens.end() ? std::nullopt : std::optional(it->second);
        check(current==expected,"Fixture token compare mismatch.");
        if (replacement) { tokens[name] = *replacement; } else { tokens.erase(name); } ++writes;
    }
};
using Result = std::unique_ptr<outloud_vm_result,decltype(&outloud_vm_result_free)>;
Result read(const VoiceLibrary& library,const std::shared_ptr<FakeRegistry>& registry,int32_t expected = OUTLOUD_VM_OK,
    MachineVoiceLibraryPolicy policy = MachineVoiceLibraryPolicy::development_override) {
    outloud_vm_result* pointer = nullptr;
    const auto status = frontend::read_injected(library,registry,&pointer,policy);
    Result result(pointer,outloud_vm_result_free);
    check(status==expected && pointer,"Bridge read status/ownership mismatch."); return result;
}
const outloud_vm_voice_v1& row(const Result& result,std::string_view id) {
    const auto* snapshot = outloud_vm_result_snapshot(result.get()); check(snapshot!=nullptr,"Missing snapshot.");
    for (uint64_t i = 0; i<snapshot->voice_count; ++i) {
        if (text(snapshot->voices[i].library_id,id)) { return snapshot->voices[i]; }
    }
    throw std::runtime_error("Missing expected voice row.");
}
using Inventory = std::map<std::filesystem::path,std::string>;
Inventory inventory(const std::filesystem::path& root) {
    Inventory result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file()) {
            std::ifstream input(entry.path(),std::ios::binary);
            result.emplace(entry.path(),std::string(std::istreambuf_iterator<char>(input),{}));
        }
    }
    return result;
}
struct Module {
    HMODULE handle;
    ~Module() { if (handle) { FreeLibrary(handle); } }
};
}
#include "../mutation/tests.hpp"
#include "../mutation/replace_tests.hpp"
int wmain(int argc,wchar_t** argv) {
    try {
        check(argc==7,"Usage: bridge-tests BRIDGE SAPI MODEL TOKENIZER VOICE.olv PREPARED.safetensors");
        Module dll{LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32)};
        check(dll.handle!=nullptr,"Cannot load production bridge DLL.");
        for (const auto* name : {"outloud_vm_abi_version","outloud_vm_read","outloud_vm_result_snapshot",
            "outloud_vm_result_error","outloud_vm_result_free","outloud_vm_result_content"}) {
            check(GetProcAddress(dll.handle,name)!=nullptr,"Missing stable C export.");
        }
        const auto version = reinterpret_cast<decltype(&outloud_vm_abi_version)>(GetProcAddress(dll.handle,"outloud_vm_abi_version"));
        check(version()==1,"Exported ABI version."); // Never invoke production read.
        check(outloud_vm_result_snapshot(nullptr)==nullptr && outloud_vm_result_error(nullptr)==nullptr,"NULL getters.");
        check(outloud_vm_result_content(nullptr,0)==nullptr,"NULL content extension getter.");
        outloud_vm_result_free(nullptr);
        sapi_test::Scratch scratch;
        VoiceLibrary absent(scratch.path/L"absent"); auto registry = std::make_shared<FakeRegistry>();
        check(frontend::read_injected(absent,registry,nullptr)==OUTLOUD_VM_INVALID_ARGUMENT,"NULL output accepted.");
        auto empty = read(absent,registry);
        check(outloud_vm_result_snapshot(empty.get())->voice_count==0 && outloud_vm_result_snapshot(empty.get())->voices==nullptr,"Empty array contract.");
        check(!std::filesystem::exists(absent.root()),"Reading absent library created it.");
        VoiceLibrary library(scratch.path/L"kni\u017enica");
        const auto imported = library.import_olv(argv[5],"Hlas \xc5\xbe \xf0\x9f\x8e\xb5"); const auto id = imported.voice.library_id;
        const auto second = library.import_olv(argv[5],"Second",DuplicateContentPolicy::allow_separate_entry).voice.library_id;
        const auto files_before = inventory(library.root()); const auto registry_before = registry->tokens;
        auto missing = read(library,registry);
        check(outloud_vm_result_snapshot(missing.get())->voice_count==2,"Multiple voices missing.");
        const auto& first = row(missing,id);
        const auto* content_snapshot=outloud_vm_result_snapshot(missing.get());
        for (uint64_t at=0;at<content_snapshot->voice_count;++at) {
            const auto* content=outloud_vm_result_content(missing.get(),at);
            check(content!=nullptr,"Missing additive content record.");
            if (text(content_snapshot->voices[at].library_id,id)) {
                check(text(content->olv_sha256,imported.voice.metadata->olv_sha256) && text(content->source_sha256,imported.voice.metadata->source_sha256) &&
                    text(content->source_filename,imported.voice.metadata->source_filename) && text(content->imported_utc,imported.voice.metadata->imported_utc),"Native metadata extension changed fields.");
            }
        }
        check(outloud_vm_result_content(missing.get(),content_snapshot->voice_count)==nullptr,"Out-of-range content getter.");
        check(text(first.display_name,imported.voice.metadata->display_name) && text(first.health,voice_health_name(imported.voice.health)),"UTF-8 name/health not preserved.");
        check(text(first.sapi_status,sapi_token_status_name(SapiTokenStatus::missing)) && !first.sapi_error,"Missing SAPI status.");
        check(first.metadata_present==1 && first.olv_version==imported.voice.metadata->olv_version && first.provenance_verified==0,"Metadata flags.");
        check(text(first.compatibility_id,imported.voice.metadata->compatibility_id) && text(first.import_kind,"olv"),"Metadata IDs/kind.");
        check(inventory(library.root())==files_before && registry->tokens==registry_before && !registry->writes && !registry->verifies,"Bridge changed library/registry.");
        // Prepare valid tokens using the real backend, temporary library and fake
        // registry. Mutation allowance ends before any tested snapshot call.
        const SapiVoiceRuntime runtime{scratch.path,std::filesystem::absolute(argv[2]).lexically_normal(),
            std::filesystem::absolute(argv[3]).lexically_normal(),std::filesystem::absolute(argv[4]).lexically_normal()};
        mutation_checks(argv[6],argv[2],runtime,scratch.path);
        replacement_checks(argv[6],runtime,scratch.path);
        SapiVoiceManager setup(library,runtime,registry,MachineVoiceLibraryPolicy::development_override);
        registry->fixture_setup = true; (void)setup.install(id); registry->fixture_setup = false;
        const auto fixture_writes = registry->writes, fixture_verifies = registry->verifies;
        auto correct = read(library,registry);
        check(text(row(correct,id).sapi_status,sapi_token_status_name(SapiTokenStatus::correct)),"Correct native status changed.");
        check(std::string_view(row(correct,id).sapi_detail).find("shared runtime configuration was not supplied")!=std::string_view::npos,"Runtime diagnostic lost.");
        auto production_policy = read(library,registry,OUTLOUD_VM_OK,MachineVoiceLibraryPolicy::production);
        check(text(row(production_policy,id).sapi_status,sapi_token_status_name(SapiTokenStatus::wrong_path)),"Production policy weakened for temp root.");
        (void)library.rename_voice(id,"Renamed fixture");
        auto stale = read(library,registry);
        check(text(row(stale,id).sapi_status,sapi_token_status_name(SapiTokenStatus::stale_name)),"Stale name reduced to bool.");
        check(text(row(correct,id).display_name,imported.voice.metadata->display_name),"Previous result storage invalidated.");
        auto& tree = registry->tokens.at(managed_sapi_token_key(id)); const auto saved = tree;
        tree.values[L"VoicePath"] = sapi_registry_string(L"C:\\invalid-fixture.olv");
        check(text(row(read(library,registry),id).sapi_status,sapi_token_status_name(SapiTokenStatus::wrong_path)),"Wrong path status.");
        tree = saved; tree.values.erase(L"Seed");
        check(text(row(read(library,registry),id).sapi_status,sapi_token_status_name(SapiTokenStatus::malformed)),"Malformed status.");
        tree = saved; tree.values[L"OutLoudManagedOwner"] = sapi_registry_string(L"not owned");
        check(text(row(read(library,registry),id).sapi_status,sapi_token_status_name(SapiTokenStatus::conflict)),"Conflict status.");
        tree = saved;
        registry->deny = true; auto denied = read(library,registry);
        const auto* cause = row(denied,id).sapi_error;
        check(cause && text(cause->domain,"sapi") && text(cause->code,sapi_voice_error_name(SapiVoiceErrorCode::registry_access_denied)) && cause->system_error==5,
            "Registry error/category/Win32 code lost.");
        check(row(denied,id).sapi_status==nullptr && outloud_vm_result_error(denied.get())==nullptr,"Partial failure fabricated status/top failure.");
        registry->deny = false; registry->unexpected = true; auto internal = read(library,registry,OUTLOUD_VM_ERROR);
        check(outloud_vm_result_snapshot(internal.get())==nullptr,"Failed read exposed partial snapshot.");
        check(text(outloud_vm_result_error(internal.get())->code,"internal_failure") &&
            std::string_view(outloud_vm_result_error(internal.get())->message).find("private")==std::string_view::npos,"Raw C++ exception crossed ABI.");
        registry->unexpected = false;
        { std::ofstream metadata(library.inspect_voice(second).managed_olv_path.parent_path()/L"metadata.json",std::ios::binary|std::ios::trunc);
          metadata << "invalid JSON"; check(metadata.good(),"Cannot create damaged metadata fixture."); }
        auto damaged = read(library,registry); const auto& bad = row(damaged,second);
        check(bad.metadata_present==0 && !bad.display_name && !bad.compatibility_id && !bad.import_kind,"Missing metadata invented.");
        check(text(bad.health,voice_health_name(VoiceHealth::metadata_corrupt)) && text(bad.sapi_status,sapi_token_status_name(SapiTokenStatus::unhealthy_voice)),"Damaged health/status lost.");
        VoiceLibrary blocked(scratch.path/L"blocked"); { std::ofstream file(blocked.root()); file << "not a directory"; }
        auto failed = read(blocked,registry,OUTLOUD_VM_ERROR); const auto* error = outloud_vm_result_error(failed.get());
        check(error && text(error->domain,"library") && *error->code && *error->library_code && *error->path,"Structured library error/path missing.");
        check(outloud_vm_result_snapshot(failed.get())==nullptr,"Library failure exposed snapshot.");
        const auto final_files = inventory(library.root()); const auto final_tokens = registry->tokens;
        auto repeat = read(library,registry); (void)repeat;
        check(inventory(library.root())==final_files && registry->tokens==final_tokens,"Repeated read wrote state.");
        check(registry->writes==fixture_writes && registry->verifies==fixture_verifies,"Bridge used mutation/engine-validation API.");
        std::cout << "PASS: " << checks << " bridge checks; temporary library, fake machine registry, no production read.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
