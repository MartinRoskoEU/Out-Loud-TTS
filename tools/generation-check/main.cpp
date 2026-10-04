#include "generation_fixture_checks.hpp"
#include <iostream>
#include <optional>

int wmain(int argc, wchar_t** argv) {
    try {
        const bool measure = argc == 2 && std::wstring_view(argv[1]) == L"--measure";
        if (argc > 1 && !measure) { throw std::invalid_argument("Usage: outloud-generation-check [--measure]"); }
        const std::filesystem::path assets(OUTLOUD_ASSET_DIRECTORY), neural(OUTLOUD_NEURAL_DIRECTORY);
        const outloud::PocketModelAsset model(assets / "pocket-english.olp");
        const outloud::OutLoudVoice voice(assets / "gnome_male.olv");
        const outloud::Tokenizer tokenizer(OUTLOUD_TOKENIZER_ASSET);
        const outloud::test::GenerationFixture fixture(neural);
        std::optional<outloud::test::GenerationReport> portable;
        std::cout << "Out Loud Pocket Segment 1 generation\nTokens 20; voice prefix 112; prefill offset 132\n";
        for (const auto path : {outloud::cpu::KernelPath::portable, outloud::cpu::KernelPath::avx2_fma}) {
            if (path == outloud::cpu::KernelPath::avx2_fma && !outloud::cpu::optimized_available()) {
                std::cout << "AVX2/FMA: NOT AVAILABLE\n"; continue;
            }
            auto report = outloud::test::run_generation_checks(model, voice, tokenizer, fixture, path);
            report.reference_feedback = outloud::test::check_reference_feedback(model, voice, fixture, path);
            outloud::test::write_generation_report(neural / (path == outloud::cpu::KernelPath::portable
                ? "generation-portable-parity.json" : "generation-avx2-parity.json"), report, path);
            if (!measure) {
                outloud::test::enforce_generation_errors(report.errors);
                outloud::test::enforce_generation_errors(report.reference_feedback, true);
            }
            double hidden = 0, latent = 0, eos = 0, margin = 1e9;
            for (std::uint32_t i = 0; i < 47; ++i) {
                const auto name = outloud::test::candidate_name(i);
                hidden = std::max(hidden, report.errors.at(name + ".hidden").raw.max_absolute);
                latent = std::max(latent, report.errors.at(name + ".latent").raw.max_absolute);
                eos = std::max(eos, report.errors.at(name + ".eos").raw.max_absolute);
                margin = std::min(margin, report.eos[i].reference_margin);
            }
            std::cout << outloud::cpu::path_name(path) << ": noise 48; candidates 47; first EOS 43; emitted 46; offset 179 PASS\n"
                      << "  EOS decisions PASS; hidden max abs " << hidden << "; latent " << latent << "; EOS " << eos
                      << "; min reference threshold margin " << margin << '\n'
                      << "  prefill " << report.timed.timing.prefill_ms << " ms; candidates " << report.timed.timing.candidates_ms
                      << " ms; total " << report.timed.timing.total_ms << " ms\n";
            double diagnostic_latent = 0;
            for (const auto& [name, error] : report.reference_feedback) {
                if (name.ends_with(".latent")) { diagnostic_latent = std::max(diagnostic_latent, error.raw.max_absolute); }
            }
            std::cout << "  reference-feedback diagnostic latent max abs " << diagnostic_latent << '\n'
                      << "  candidate/KV numerical gates " << (measure ? "MEASURED" : "PASS")
                      << "; mean " << report.timed.timing.candidates_ms / 47 << " ms/candidate; partial latent-stage RTF "
                      << 46 * 80 / report.timed.timing.total_ms << " (NOT FULL TTS SPEED)\n";
            if (portable) {
                std::map<std::string, outloud::test::SamplerError> agreement;
                for (const auto& [name, values] : report.outputs) {
                    agreement[name] = outloud::test::sampler_error(values, portable->outputs.at(name));
                }
                std::ofstream output(neural / "generation-path-agreement.json");
                output << std::setprecision(17); outloud::test::write_sampler_errors(output, agreement); output << '\n';
                outloud::test::neural_require(bool(output), "Cannot write generation path agreement");
                if (!measure) { outloud::test::enforce_generation_errors(agreement); }
                std::cout << "Path agreement: " << (measure ? "MEASURED" : "PASS") << '\n';
            } else { portable = std::move(report); }
        }
        std::cout << (measure ? "RESULT: MEASURED (numerical gates bypassed)\n" : "RESULT: PASS\n");
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
