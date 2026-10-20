#include "outloud/voice_library.hpp"
#include "voice_library_fs.hpp"

#include <iostream>

namespace {
namespace fs=outloud::detail::library_fs;
void show(const outloud::LibraryVoice& voice) {
    std::cout<<"id="<<voice.library_id<<"\nhealth="<<outloud::voice_health_name(voice.health)
             <<'\n';
    if (voice.metadata) {
        const auto& m=*voice.metadata;
        std::cout<<"name="<<m.display_name<<"\nolv_version="<<m.olv_version<<"\nolv_sha256="<<m.olv_sha256
            <<"\nsource_sha256="<<m.source_sha256<<"\ncompatibility_id="<<m.compatibility_id
            <<"\nimport_kind="<<(m.import_kind==outloud::VoiceImportKind::pocket_prepared?"pocket_prepared":"olv")
            <<"\nimported_utc="<<m.imported_utc<<'\n';
    }
    std::wcout<<L"managed_olv="<<voice.managed_olv_path.native()<<L'\n';
    if (!voice.detail.empty()) { std::cout<<"detail="<<voice.detail<<'\n'; }
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        int at=1;
        if (at>=argc) {
            std::cerr<<"Usage: outloud-voice-library list|inspect ID|rename ID NAME|remove ID|cleanup\n"
                "       ... import-pocket SOURCE --name NAME --model-compatibility pinned|ID [--expected-source-sha256 SHA] [--allow-duplicate]\n"
                "       ... import-olv SOURCE --name NAME [--allow-duplicate]\n"
                "       ... export ID DESTINATION.olv [--replace]\n"
                "Default root: ProgramData/OutLoud/VoiceLibrary; machine mutations require manual elevation.\n"
                "No alternate library root or automatic migration/UAC.\n"; return 2;
        }
        const std::wstring_view command(argv[at++]);
        outloud::VoiceLibrary library;
        if (command==L"list" && at==argc) {
            for (const auto& voice : library.list_voices()) { show(voice); std::cout<<'\n'; } return 0;
        }
        if (command==L"inspect" && at+1==argc) { show(library.inspect_voice(fs::utf8(argv[at]))); return 0; }
        if (command==L"rename" && at+2==argc) { show(library.rename_voice(fs::utf8(argv[at]),fs::utf8(argv[at+1]))); return 0; }
        if (command==L"remove" && at+1==argc) {
            const auto result=library.remove_voice(fs::utf8(argv[at]));
            std::cout<<"removed=true\ncleanup_pending="<<(result.cleanup_pending?"true":"false")<<"\ndetail="<<result.detail<<'\n'; return 0;
        }
        if (command==L"cleanup" && at==argc) {
            const auto result=library.cleanup_pending(); std::cout<<"cleaned="<<result.cleaned<<"\npending="<<result.pending.size()<<'\n';
            for (const auto& path : result.pending) { std::wcout<<path.native()<<L'\n'; }
            for (const auto& error : result.problems) { std::cout<<"error="<<outloud::voice_library_error_name(error.code())<<"\nmessage="<<error.what()<<'\n'; } return result.pending.empty()?0:1;
        }
        if (command==L"export" && (at+2==argc || (at+3==argc && std::wstring_view(argv[at+2])==L"--replace"))) {
            const auto result=library.export_olv(fs::utf8(argv[at]),argv[at+1],at+3==argc);
            std::cout<<"exported_sha256="<<result.file_sha256<<'\n'; return 0;
        }
        if ((command==L"import-pocket" || command==L"import-olv") && at<argc) {
            const std::filesystem::path source(argv[at++]); std::string name; outloud::VoiceImportOptions options;
            auto policy=outloud::DuplicateContentPolicy::reject;
            while (at<argc) {
                const std::wstring_view flag(argv[at++]);
                if (flag==L"--allow-duplicate" && policy==outloud::DuplicateContentPolicy::reject) {
                    policy=outloud::DuplicateContentPolicy::allow_separate_entry;
                } else if (flag==L"--name" && name.empty() && at<argc) { name=fs::utf8(argv[at++]); }
                else if (flag==L"--model-compatibility" && command==L"import-pocket" && options.declared_model_compatibility_id.empty() && at<argc) {
                    const std::wstring_view value(argv[at++]);
                    options.declared_model_compatibility_id=value==L"pinned"?std::string(outloud::pocket_model_compatibility_id):fs::utf8(value);
                } else if (flag==L"--expected-source-sha256" && command==L"import-pocket" && options.expected_source_sha256.empty() && at<argc) {
                    options.expected_source_sha256=fs::utf8(argv[at++]);
                } else { throw std::invalid_argument("Unknown, duplicate or incomplete option"); }
            }
            const auto result=command==L"import-pocket"?library.import_pocket_voice(source,name,options,policy):library.import_olv(source,name,policy);
            show(result.voice);
            for (const auto& id : result.duplicate_ids) { std::cout<<"duplicate_id="<<id<<'\n'; } return 0;
        }
        throw std::invalid_argument("Invalid command/argument count; run without arguments for usage");
    } catch (const outloud::VoiceLibraryError& error) {
        std::cerr<<"error="<<outloud::voice_library_error_name(error.code())<<"\nmessage="<<error.what()<<"\nsystem_error="<<error.system_error()<<'\n';
        std::wcerr<<L"path="<<error.path().native()<<L'\n';
        if (error.import_error()) { std::cerr<<"import_error="<<outloud::voice_import_error_name(error.import_error()->code())<<"\ntensor="<<error.import_error()->tensor()<<'\n'; }
        for (const auto& id : error.duplicate_ids()) { std::cerr<<"duplicate_id="<<id<<'\n'; } return 1;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 2; }
}
