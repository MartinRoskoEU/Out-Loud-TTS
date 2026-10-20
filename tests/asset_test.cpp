#include "asset_fixture_checks.hpp"

#include <fstream>
#include <iostream>
#include <limits>

namespace {
using outloud::test::asset_require;
using Bytes = outloud::detail::Bytes;
template<class Function> void rejected(Function function, std::string_view expected_reason) {
    try { function(); }
    catch (const std::exception& error) {
        asset_require(std::string_view(error.what()).find(expected_reason) != std::string_view::npos,
                      "Asset rejected for a different reason than expected");
        return;
    }
    throw std::runtime_error("Invalid asset accepted");
}
void write(const std::filesystem::path& path, Bytes bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    asset_require(bool(output), "Cannot write private corruption fixture");
}
void set32(std::vector<unsigned char>& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) { bytes[offset + i] = static_cast<unsigned char>(value >> (8 * i)); }
}
void seal(std::vector<unsigned char>& bytes, bool payload = false) {
    if (payload) {
        const auto digest = outloud::detail::sha256(Bytes(bytes).subspan(256));
        std::copy(digest.begin(), digest.end(), bytes.begin() + 128);
    }
    const auto digest = outloud::detail::asset_integrity(bytes);
    std::copy(digest.begin(), digest.end(), bytes.begin() + 96);
}
struct Scratch {
    std::filesystem::path model = std::filesystem::current_path() / "asset-test-model.olp";
    std::filesystem::path voice = std::filesystem::current_path() / "asset-test-voice.olv";
    Scratch() {
        asset_require(!std::filesystem::exists(model) && !std::filesystem::exists(voice), "Existing asset test scratch file");
    }
    ~Scratch() {
        std::error_code ec; std::filesystem::remove(model, ec); std::filesystem::remove(voice, ec);
    }
};

void corrupt_voice(const std::filesystem::path& source, const std::filesystem::path& scratch, const outloud::PocketModelAsset& model) {
    const outloud::detail::MappedFile file(source, 1024 * 1024 * 1024ULL);
    const std::vector<unsigned char> original(file.bytes().begin(), file.bytes().end());
    auto check = [&](std::vector<unsigned char> bytes, std::string_view reason, bool reseal = false) {
        if (reseal) { seal(bytes); }
        write(scratch, bytes);
        rejected([&] { outloud::OutLoudVoice bad(scratch); bad.validate_against(model); }, reason);
    };
    auto changed = original; changed[0] ^= 1; check(changed, "magic");
    changed = original; set32(changed, 8, 2); check(changed, "version");
    changed = original; set32(changed, 16, 0x04030201); check(changed, "byte order");
    changed = original; set32(changed, 28, 1); check(changed, "dtype");
    changed = original; set32(changed, 196, 0); check(changed, "prefix");
    changed = original; set32(changed, 192, 5); check(changed, "dimensions");
    changed = original; set32(changed, 200, 15); check(changed, "dimensions");
    changed = original; set32(changed, 208, 2); check(changed, "dimensions");
    changed = original; set32(changed, 220, 0); check(changed, "post-RoPE");
    changed = original; changed[32] ^= 1; seal(changed); write(scratch,changed);
    outloud::OutLoudVoice(scratch).validate_against(model); // Different preparation identity is informational.
    changed = original; changed[64] ^= 1; check(changed, "container SHA256");
    changed = original; changed[128] ^= 1; check(changed, "payload SHA256", true);
    changed = original; changed[96] ^= 1; check(changed, "container SHA256");
    changed = original; changed.back() ^= 1; check(changed, "container SHA256");
    changed = original; changed.pop_back(); check(changed, "truncation");
    changed = original; changed.push_back(0); check(changed, "trailing");
    changed = original; changed.resize(100); check(changed, "Truncated");
    changed = original; set32(changed, 256, 0x7f800000); seal(changed, true); check(changed, "Non-finite");
    changed = original; changed[236] = 1; check(changed, "reserved");
}

void corrupt_model(const std::filesystem::path& source, const std::filesystem::path& scratch) {
    std::filesystem::copy_file(source, scratch);
    const outloud::detail::MappedFile file(source, 1024 * 1024 * 1024ULL);
    const auto original = file.bytes();
    auto change = [&](std::uint64_t offset, std::vector<unsigned char> value, std::string_view reason) {
        {
            std::fstream output(scratch, std::ios::binary | std::ios::in | std::ios::out);
            output.seekp(static_cast<std::streamoff>(offset));
            output.write(reinterpret_cast<const char*>(value.data()), static_cast<std::streamsize>(value.size()));
            asset_require(bool(output), "Cannot alter corruption fixture");
        }
        rejected([&] { outloud::PocketModelAsset bad(scratch); }, reason);
        {
            std::fstream output(scratch, std::ios::binary | std::ios::in | std::ios::out);
            output.seekp(static_cast<std::streamoff>(offset));
            output.write(reinterpret_cast<const char*>(original.data() + offset), static_cast<std::streamsize>(value.size()));
            asset_require(bool(output), "Cannot restore corruption fixture");
        }
    };
    auto u32 = [&](std::size_t offset, std::uint32_t value, std::string_view reason) {
        std::vector<unsigned char> bytes(4); set32(bytes, 0, value); change(offset, bytes, reason);
    };
    change(0, {0}, "magic");
    u32(8, 2, "version");
    change(32, {static_cast<unsigned char>(original[32] ^ 1)}, "compatibility ID");
    change(64, {static_cast<unsigned char>(original[64] ^ 1)}, "source model");
    u32(192, 168, "tensor/record count");
    u32(256, 1, "tensor ID");
    u32(260, 2, "dtype");
    u32(264, 4, "rank");
    u32(272, 33, "tensor dimension");
    change(288, std::vector<unsigned char>(8, 255), "offset/size");
    change(304, std::vector<unsigned char>(8, 255), "offset/size");
    const auto payload_start = outloud::detail::u64(original, 176);
    change(payload_start, {static_cast<unsigned char>(original[static_cast<std::size_t>(payload_start)] ^ 1)}, "container SHA256");
    for (std::size_t i = 0; i < outloud::pocket_tensor_schema.size(); ++i) {
        const auto end = outloud::detail::u64(original, 256 + i * 64 + 32) + outloud::pocket_tensor_schema[i].elements * 2;
        if (end % 64 != 0) { change(end, {1}, "alignment padding"); break; }
    }
    std::filesystem::resize_file(scratch, original.size() - 1);
    rejected([&] { outloud::PocketModelAsset bad(scratch); }, "truncation");
    std::filesystem::resize_file(scratch, original.size() + 1);
    rejected([&] { outloud::PocketModelAsset bad(scratch); }, "oversized");
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 2) { throw std::invalid_argument("Usage: outloud-asset-tests asset-directory"); }
        const std::filesystem::path directory(argv[1]);
        const outloud::PocketModelAsset model(directory / "pocket-english.olp");
        outloud::test::check_model(model);
        outloud::test::check_bf16(directory / "bf16-f32-fixture.bin");
        for (const auto& expected : outloud::test::voice_expected) {
            const outloud::OutLoudVoice voice(directory / (std::string(expected.name) + ".olv"));
            outloud::test::check_voice(voice, model, expected);
            rejected([&] { (void)voice.cache(6); }, "layer");
        }
        rejected([&] { (void)model.tensor(static_cast<outloud::PocketTensorId>(169)); }, "tensor ID");
        std::array<std::uint16_t, 1> bits{};
        std::array<float, 2> wrong_size{};
        rejected([&] { outloud::widen_bf16(bits, wrong_size); }, "size mismatch");
        Scratch scratch;
        corrupt_voice(directory / "gnome_male.olv", scratch.voice, model);
        corrupt_model(directory / "pocket-english.olp", scratch.model);
        std::cout << "169 tensors / 99,719,330 elements: PASS\n"
                     "Full source BF16 bytes and FP32 PyTorch widening: PASS\n"
                     "All 65,536 BF16 patterns: PASS\n"
                  << outloud::test::voice_expected.size() << " original voice byte/schema checks: PASS\n"
                     "Corruption/bounds/dimensions/dtype rejection and informational voice identities: PASS\nRESULT: PASS\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
