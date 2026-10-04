#include "asset_fixture_checks.hpp"

#include <iomanip>
#include <iostream>

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc > 2) { throw std::invalid_argument("Usage: outloud-asset-check [asset-directory]"); }
        const auto directory = argc == 2 ? std::filesystem::path(argv[1]) : std::filesystem::path(L"generated/assets");
        const outloud::PocketModelAsset model(directory / "pocket-english.olp");
        const outloud::OutLoudVoice voice(directory / "gnome_male.olv");
        outloud::test::check_model(model);
        outloud::test::check_voice(voice, model, outloud::test::voice_expected.front());
        outloud::test::check_bf16(directory / "bf16-f32-fixture.bin");
        std::cout << "Out Loud Pocket assets\n\nModel:\n  version: " << model.version()
                  << "\n  tensors: " << model.tensor_count() << "\n  source SHA256: " << model.source_sha256()
                  << "\n  payload: " << model.payload_bytes() << " bytes (" << std::fixed << std::setprecision(3)
                  << double(model.payload_bytes()) / (1024 * 1024) << " MiB)\n\nVoice:\n  version: " << voice.version()
                  << "\n  prefix: " << voice.prefix_length() << "\n  layers: " << voice.layers()
                  << "\n  heads: " << voice.heads() << "\n  head dim: " << voice.head_dimension()
                  << "\n  compatible: PASS\n\nBF16 -> FP32 checks (all 65,536 patterns and full model): PASS\n"
                     "Original model/voice byte checks: PASS\nIntegrity: PASS\n\nRESULT: PASS\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
