#include "fixture_checks.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
template<class Function> void rejected(Function function, const char* message) {
    bool threw = false;
    try { function(); } catch (const std::exception&) { threw = true; }
    require(threw, message);
}

void edge_cases(const outloud::Tokenizer& tokenizer) {
    using outloud::prepare_text;
    require(tokenizer.encode("").empty(), "Empty raw encoding");
    rejected([&] { (void)prepare_text(" \t\r\n", tokenizer); }, "Empty prompt must fail");
    for (const auto text : {std::string("\xc0\xaf"), std::string("\xed\xa0\x80"), std::string("\xf4\x90\x80\x80"), std::string("\xe2\x82")}) {
        rejected([&] { (void)tokenizer.encode(text); }, "Invalid UTF-8 accepted");
        rejected([&] { (void)prepare_text(text, tokenizer); }, "Invalid prompt UTF-8 accepted");
    }
    require(prepare_text("go    now", tokenizer).text == "Go now.", "Model whitespace normalization");
    require(prepare_text("The scout\xe2\x80\x99s ready", tokenizer).text == "The scout's ready.", "Model apostrophe replacement");
    require(prepare_text("Wait? , Then go", tokenizer).text == "Wait? Then go.", "Model sentence punctuation cleanup");
    require(prepare_text("Bring ore:", tokenizer).text == "Bring ore.", "Trailing weak punctuation");
    require(prepare_text("'Help?'", tokenizer).text == "'Help?'", "Terminal punctuation behind closers");
    require(prepare_text("Bring ore,;—”", tokenizer).text == "Bring ore.”", "Weak punctuation and closer");
    const std::vector<std::uint32_t> specials = {1,2,3,0};
    require(tokenizer.decode(specials).empty(), "Default special skipping");
    require(tokenizer.decode(specials, false) == "<s></s><pad><unk>", "Explicit special preservation");
    const std::vector<std::uint32_t> invalid_bytes = {4 + 0xc3, 4 + 0x28};
    require(tokenizer.decode(invalid_bytes) == "��", "Invalid byte group replaces each byte");
    const std::vector<std::uint32_t> missing_ids = {9999};
    require(tokenizer.decode(missing_ids).empty(), "Missing decoder IDs must be skipped");
    const std::string with_nul("a\0b", 3);
    require(tokenizer.decode(tokenizer.encode(with_nul)) == with_nul, "Embedded NUL round trip");
    rejected([&] { (void)outloud::process_text("Go", tokenizer, 0); }, "Zero token limit accepted");
}

void asset_checks(const std::filesystem::path& asset) {
    std::ifstream input(asset, std::ios::binary);
    std::vector<char> original((std::istreambuf_iterator<char>(input)), {});
    require(original.size() > 88, "Asset missing");
    // A private test artifact in the current CTest build directory; the original is never written.
    const auto temporary = std::filesystem::current_path() / "corrupt-tokenizer-test.bin";
    require(!std::filesystem::exists(temporary), "Refusing to overwrite an existing test scratch file");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{temporary};
    auto check_bad = [&](std::vector<char> bytes) {
        { std::ofstream output(temporary, std::ios::binary); output.write(bytes.data(), static_cast<std::streamsize>(bytes.size())); }
        rejected([&] { outloud::Tokenizer wrong(temporary); }, "Corrupt asset accepted");
    };
    auto changed = original; changed[24] ^= 1; check_bad(changed); // Original tokenizer SHA.
    changed = original; changed.back() ^= 1; check_bad(changed); // Payload SHA.
    changed = original; changed[8] = 2; check_bad(changed); // Version.
    changed = original; changed.resize(90); check_bad(changed); // Truncation.
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 2) { throw std::invalid_argument("Usage: outloud-text-tests tokenizer-asset"); }
        const std::filesystem::path asset(argv[1]);
        const outloud::Tokenizer tokenizer(asset);
        std::size_t passed = 0;
        for (const auto& test : outloud::test::cases) {
            if (!outloud::test::check(test, tokenizer)) { return 1; }
            ++passed;
        }
        edge_cases(tokenizer);
        asset_checks(asset);
        std::cout << "Canonical + regression cases: " << passed << '/' << outloud::test::cases.size()
                  << " PASS\nUTF-8, preparation, decoder and asset-integrity checks: PASS\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
