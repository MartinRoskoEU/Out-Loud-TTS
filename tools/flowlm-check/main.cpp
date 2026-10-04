#include "flowlm_fixture_checks.hpp"

#include <iostream>
#include <optional>

int wmain(int argc, wchar_t** argv) {
    try {
        const bool measure = argc == 2 && std::wstring_view(argv[1]) == L"--measure";
        if (argc > 1 && !measure) { throw std::invalid_argument("Usage: outloud-flowlm-check [--measure]"); }
        const std::filesystem::path neural(OUTLOUD_NEURAL_DIRECTORY), assets(OUTLOUD_ASSET_DIRECTORY);
        const outloud::PocketModelAsset model(assets / "pocket-english.olp");
        const outloud::OutLoudVoice voice(assets / "gnome_male.olv");
        const outloud::Tokenizer tokenizer(OUTLOUD_TOKENIZER_ASSET);
        const outloud::test::NeuralFixture fixture(neural);
        std::cout << "Out Loud Pocket FlowLM prefill\n\nSegment tokens: 20\nVoice prefix: 112\nFinal offset: 132\n";
        std::optional<outloud::test::PrefillReport> portable;
        for (auto path : {outloud::cpu::KernelPath::portable, outloud::cpu::KernelPath::avx2_fma}) {
            if (path == outloud::cpu::KernelPath::avx2_fma && !outloud::cpu::optimized_available()) {
                std::cout << "Optimized path: NOT AVAILABLE\n"; continue;
            }
            auto primitive_errors = outloud::test::check_real_primitives(model, voice, fixture, path);
            if (!measure) { outloud::test::enforce_errors(primitive_errors, true); }
            auto report = outloud::test::run_prefill_checks(model, voice, tokenizer, fixture, path);
            report.primitives = std::move(primitive_errors);
            outloud::test::write_report(neural / (path == outloud::cpu::KernelPath::portable ? "portable-parity.json" : "avx2-parity.json"), report, path);
            if (!measure) { outloud::test::enforce_report(report); }
            double maximum = 0;
            for (const auto& [name, error] : report.checkpoints) { maximum = std::max(maximum, error.max_absolute); }
            std::cout << outloud::cpu::path_name(path) << (measure ? ": MEASURED" : ": PASS") << "; max absolute " << maximum
                      << "; EOS " << report.eos << "; embedding " << report.embedding_ms
                      << " ms; prefill " << report.prefill_ms << " ms; single backbone " << report.single_ms << " ms\n";
            if (path == outloud::cpu::KernelPath::portable) { portable = std::move(report); }
            else {
                const auto agreement = outloud::test::compare_paths(*portable, report);
                if (!measure) { outloud::test::enforce_path_agreement(agreement); }
                std::ofstream output(neural / "path-agreement.json");
                output << std::setprecision(17); outloud::test::write_error_map(output, agreement); output << "\n";
                outloud::test::neural_require(bool(output), "Cannot write CPU path comparison");
                double max_abs = 0, max_rms = 0;
                for (const auto& [name, error] : agreement) {
                    max_abs = std::max(max_abs, error.max_absolute); max_rms = std::max(max_rms, error.rms);
                }
                std::cout << "Path agreement: max absolute " << max_abs << "; RMS " << max_rms << "\n";
            }
        }
        if (measure) { std::cout << "\nRESULT: MEASURED (regression limits bypassed explicitly)\n"; }
        else {
            std::cout << "\nEmbedding: PASS\n";
            for (int layer = 0; layer < 6; ++layer) { std::cout << "Layer " << layer << ": PASS\n"; }
            std::cout << "Final norm: PASS\nLast hidden: PASS\nEOS logit: PASS\n\nRESULT: PASS\n";
        }
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
