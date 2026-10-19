#include "outloud/voice_import.hpp"

#include <iostream>

namespace {
std::string ascii(std::wstring_view value) {
    std::string result;
    for (auto ch : value) {
        if (ch > 127) { throw std::invalid_argument("Option identity must be ASCII hexadecimal"); }
        result += static_cast<char>(ch);
    }
    return result;
}
void show(const outloud::PocketVoiceInfo& info) {
    std::cout << "format=Safetensors/Pocket-cache-offset-pad\nprefix=" << info.prefix_length
              << "\nsource_bytes=" << info.source_bytes << "\npayload_bytes=" << info.payload_bytes
              << "\nsource_sha256=" << info.source_sha256 << "\npayload_sha256=" << info.payload_sha256
              << "\nmetadata_entries=" << info.metadata.size() << "\nmodel_provenance_verified=false\n"
              << "voice_state_format=Pocket-TTS-6L" << (info.legacy_current_end ? "-legacy-current_end" : "") << '\n';
}
void show(const outloud::OlvInfo& info) {
    std::cout << "olv_version=" << info.format_version << "\nprefix=" << info.prefix_length
              << "\nfile_bytes=" << info.file_bytes << "\nfile_sha256=" << info.file_sha256
              << "\ncompatibility_id=" << info.compatibility_id << '\n';
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 3 && std::wstring_view(argv[1]) == L"inspect") {
            show(outloud::inspect_pocket_voice(argv[2])); return 0;
        }
        if (argc == 3 && std::wstring_view(argv[1]) == L"validate-olv") {
            show(outloud::validate_olv(argv[2])); return 0;
        }
        if (argc < 3) {
            std::cerr << "Usage: outloud-voice-import <source> <destination.olv> --model-compatibility <pinned|ID> "
                         "[--expected-source-sha256 SHA256] [--replace]\n"
                         "       outloud-voice-import inspect <source>\n"
                         "       outloud-voice-import validate-olv <voice.olv>\n"
                         "pinned explicitly declares that the source was prepared with this project's pinned model.\n";
            return 2;
        }
        outloud::VoiceImportOptions options;
        for (int i = 3; i < argc; ++i) {
            const std::wstring_view option(argv[i]);
            if (option == L"--replace" && !options.replace_existing) { options.replace_existing = true; }
            else if (option == L"--model-compatibility" && options.declared_model_compatibility_id.empty() && i + 1 < argc) {
                const std::wstring_view value(argv[++i]);
                options.declared_model_compatibility_id = value == L"pinned" ? std::string(outloud::pocket_model_compatibility_id) : ascii(value);
            } else if (option == L"--expected-source-sha256" && options.expected_source_sha256.empty() && i + 1 < argc) {
                options.expected_source_sha256 = ascii(argv[++i]);
            } else { throw std::invalid_argument("Unknown, duplicate or incomplete CLI option"); }
        }
        const auto result = outloud::convert_pocket_voice_to_olv(argv[1],argv[2],options);
        show(result.source); show(result.voice);
        std::cout << "RESULT: PASS\n";
        return 0;
    } catch (const outloud::VoiceImportError& error) {
        std::cerr << "error=" << outloud::voice_import_error_name(error.code()) << "\nmessage=" << error.what()
                  << "\ntensor=" << error.tensor() << "\nsystem_error=" << error.system_error() << '\n';
        std::wcerr << L"path=" << error.path().native() << L'\n'; return 1;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}
