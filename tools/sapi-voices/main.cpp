#include "outloud/sapi_voices.hpp"
#include "voice_library_fs.hpp"
#include <iostream>

namespace {
namespace fs = outloud::detail::library_fs;
void show(const outloud::SapiVoiceToken& token,outloud::SapiVoiceScope scope) {
    std::cout << "id=" << token.library_id << "\nstatus=" << outloud::sapi_token_status_name(token.status)
        << "\ndetail=" << token.detail << "\nsuggested_action=" << token.suggested_action << '\n';
    std::cout << "token_key=" << fs::utf8(token.token_key) << '\n';
    if (!token.library_id.empty()) { std::cout << "token_id=" << fs::utf8(outloud::managed_sapi_token_id(token.library_id,scope)) << '\n'; }
    if (token.tree) {
        for (const auto& [key,value] : token.tree->values) {
            const auto text = outloud::sapi_registry_text(value);
            std::cout << "value[" << fs::utf8(key) << "]=" << (text ? fs::utf8(*text) : "<invalid REG_SZ>") << '\n';
        }
    }
    std::cout << '\n';
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        if (argc<2) {
            std::cerr << "Usage: outloud-sapi-voices list|reconcile|inspect ID|install ID|update ID|remove ID\n"
                "       outloud-sapi-voices verify-engine --runtime-root DEPLOYMENT (read-only)\n"
                "       [--root LIBRARY] [--runtime-root DEPLOYMENT] [--scope machine|user]\n"
                "Default: ProgramData library + machine Registry64 scope. Mutations require manual elevation.\n"
                "Install/update require --runtime-root containing bin/OutLoudPocketSAPI.dll and runtime assets.\n"
                "--allow-development-library-root explicitly permits --root overrides for machine install/update.\n";
            return 2;
        }
        const std::wstring command(argv[1]); int at = 2; std::string id;
        if (command!=L"list" && command!=L"reconcile" && command!=L"verify-engine") {
            if (at>=argc) { throw std::invalid_argument("Missing library ID."); } id = fs::utf8(argv[at++]);
        }
        std::filesystem::path root = outloud::default_voice_library_root(), deployment;
        bool seen_root = false, seen_runtime = false, seen_scope = false;
        auto scope = outloud::SapiVoiceScope::machine;
        auto machine_policy = outloud::MachineVoiceLibraryPolicy::production;
        while (at<argc) {
            const std::wstring flag(argv[at++]);
            if (flag==L"--allow-development-library-root" && machine_policy==outloud::MachineVoiceLibraryPolicy::production) {
                machine_policy = outloud::MachineVoiceLibraryPolicy::development_override; continue;
            }
            if (at>=argc) { throw std::invalid_argument("Incomplete option."); }
            const std::wstring value(argv[at++]);
            if (flag==L"--root" && !seen_root) { root = value; seen_root = true; }
            else if (flag==L"--runtime-root" && !seen_runtime) {
                deployment = std::filesystem::absolute(value).lexically_normal();
                if (deployment!=deployment.root_path() && !deployment.has_filename()) { deployment = deployment.parent_path(); }
                seen_runtime = true;
            }
            else if (flag==L"--scope" && !seen_scope) {
                seen_scope = true;
                if (value==L"machine") { scope = outloud::SapiVoiceScope::machine; }
                else if (value==L"user") { scope = outloud::SapiVoiceScope::user; }
                else { throw outloud::SapiVoiceError(outloud::SapiVoiceErrorCode::unsupported_scope,"Use --scope machine or user; no fallback."); }
            } else { throw std::invalid_argument("Unknown or duplicate option."); }
        }
        if (machine_policy==outloud::MachineVoiceLibraryPolicy::development_override && (!seen_root || scope!=outloud::SapiVoiceScope::machine)) {
            throw std::invalid_argument("--allow-development-library-root requires explicit --root and --scope machine.");
        }
        if ((command==L"install" || command==L"update" || command==L"verify-engine") && deployment.empty()) { throw std::invalid_argument("Install/update/verify-engine require --runtime-root."); }
        // Read operations can report ownership/orphans without a runtime. A full
        // configuration reconciliation needs the intended shared deployment.
        outloud::SapiVoiceRuntime runtime;
        if (!deployment.empty()) { runtime = {deployment,(deployment/L"bin/OutLoudPocketSAPI.dll").lexically_normal(),
            (deployment/L"runtime/pocket-english.olp").lexically_normal(),(deployment/L"runtime/pocket-english-tokenizer.bin").lexically_normal()}; }
        if (command==L"verify-engine") {
            outloud::windows_sapi_voice_registry()->verify_engine(runtime.engine_dll,scope);
            std::cout << "result=engine_verified\nselected_dll=" << fs::utf8(runtime.engine_dll.wstring()) << '\n'; return 0;
        }
        outloud::SapiVoiceManager manager(outloud::VoiceLibrary(root),runtime,outloud::windows_sapi_voice_registry(),machine_policy);
        if (command==L"list" || command==L"reconcile") {
            for (const auto& token : command==L"list" ? manager.list(scope) : manager.reconcile(scope)) { show(token,scope); } return 0;
        }
        if (command==L"inspect") { show(manager.inspect(id,scope),scope); return 0; }
        outloud::SapiTokenChange result;
        if (command==L"install") { result = manager.install(id,scope); }
        else if (command==L"update") { result = manager.update(id,scope); }
        else if (command==L"remove") { result = manager.remove(id,scope); }
        else { throw std::invalid_argument("Unknown command."); }
        static constexpr const char* changes[]{"installed","already_installed","updated","unchanged","removed"};
        std::cout << "result=" << changes[static_cast<std::size_t>(result)] << '\n'; return 0;
    } catch (const outloud::SapiVoiceError& e) {
        std::cerr << "error=" << outloud::sapi_voice_error_name(e.code) << "\nmessage=" << e.what() << "\nsystem_error=" << e.system_error << '\n';
        if (e.library_error) { std::cerr << "library_error=" << outloud::voice_library_error_name(e.library_error->code()) << '\n'; }
        if (e.validation_error) { std::cerr << "validation_error=" << outloud::voice_import_error_name(e.validation_error->code()) << '\n'; }
        return 1;
    } catch (const outloud::VoiceLibraryError& e) {
        std::cerr << "library_error=" << outloud::voice_library_error_name(e.code()) << "\nmessage=" << e.what() << '\n'; return 1;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 2; }
}
