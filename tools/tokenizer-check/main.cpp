#include "fixture_checks.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <optional>

namespace {
std::string utf8(std::wstring_view wide) {
    if (wide.empty()) { return {}; }
    const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()),
                                        nullptr, 0, nullptr, nullptr);
    if (size <= 0) { throw std::invalid_argument("Invalid command-line Unicode"); }
    std::string result(static_cast<std::size_t>(size), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()),
                            result.data(), size, nullptr, nullptr)) { throw std::runtime_error("UTF-8 conversion failed"); }
    return result;
}
std::filesystem::path default_asset() {
    std::wstring executable(32768, L'\0');
    const auto size = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (size == 0 || size >= executable.size()) { throw std::runtime_error("Cannot resolve executable location"); }
    executable.resize(size);
    return std::filesystem::path(executable).parent_path() / "pocket-english-tokenizer.bin";
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        auto asset = default_asset();
        std::optional<std::string> text;
        for (int i = 1; i < argc; ++i) {
            const std::wstring_view arg(argv[i]);
            if (arg == L"--asset" || arg == L"--text-file" || arg == L"--text") {
                if (++i == argc) { throw std::invalid_argument("Missing option value"); }
                if (arg == L"--asset") { asset = argv[i]; }
                else if (arg == L"--text") { text = utf8(argv[i]); }
                else {
                    std::ifstream input(std::filesystem::path(argv[i]), std::ios::binary);
                    if (!input) { throw std::runtime_error("Cannot open UTF-8 text file"); }
                    text = std::string((std::istreambuf_iterator<char>(input)), {});
                }
            } else if (arg == L"--help") {
                std::cout << "outloud-tokenizer-check [--asset path] [--text text | --text-file utf8-file]\n"
                             "No text runs the embedded canonical Pocket parity fixture.\n";
                return 0;
            } else if (arg.starts_with(L"--")) { throw std::invalid_argument("Unknown option"); }
            else { if (text) { *text += ' '; *text += utf8(arg); } else { text = utf8(arg); } }
        }
        const outloud::Tokenizer tokenizer(asset);
        std::cout << "Out Loud Pocket tokenizer " << (text ? "check\n" : "parity\n")
                  << "Tokenizer SHA256: " << outloud::Tokenizer::source_sha256 << '\n';
        bool passed = true;
        const auto start = std::chrono::steady_clock::now();
        // Time exactly text preparation/splitting/tokenization, excluding load, comparisons and output.
        const auto result = outloud::process_text(text ? *text : outloud::test::cases.front().text, tokenizer);
        const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (!text) { passed = outloud::test::check(outloud::test::cases.front(), tokenizer, true); }
        else {
            std::cout << "Prepared: " << result.prepared_text << "\nFull token IDs:";
            for (auto id : result.full_token_ids) { std::cout << ' ' << id; }
            std::cout << "\nSegments: " << result.segments.size() << '\n';
            for (std::size_t i = 0; i < result.segments.size(); ++i) {
                const auto& s = result.segments[i];
                std::cout << '[' << i + 1 << "] tokens=" << s.token_ids.size() << " tail=" << s.frames_after_eos
                          << ' ' << s.text << "\nIDs:";
                for (auto id : s.token_ids) { std::cout << ' ' << id; }
                std::cout << '\n';
            }
        }
        std::cout << "Text pipeline: " << std::fixed << std::setprecision(3) << elapsed << " ms\n"
                  << (text ? "RESULT: OK\n" : passed ? "RESULT: PASS\n" : "RESULT: FAIL\n");
        return passed ? 0 : 1;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
