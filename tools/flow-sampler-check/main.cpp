#include "flow_sampler_fixture_checks.hpp"

#include <iostream>
#include <optional>

int wmain(int argc, wchar_t** argv) {
    try {
        const bool measure = argc == 2 && std::wstring_view(argv[1]) == L"--measure";
        if (argc > 1 && !measure) { throw std::invalid_argument("Usage: outloud-flow-sampler-check [--measure]"); }
        const std::filesystem::path assets(OUTLOUD_ASSET_DIRECTORY), neural(OUTLOUD_NEURAL_DIRECTORY);
        const outloud::PocketModelAsset model(assets / "pocket-english.olp");
        const outloud::OutLoudVoice voice(assets / "gnome_male.olv");
        const outloud::Tokenizer tokenizer(OUTLOUD_TOKENIZER_ASSET);
        const outloud::test::SamplerFixture fixture(neural);
        std::optional<outloud::test::SamplerReport> portable;
        std::cout << "Out Loud Pocket flow sampler\n";
        for (const auto path : {outloud::cpu::KernelPath::portable, outloud::cpu::KernelPath::avx2_fma}) {
            if (path == outloud::cpu::KernelPath::avx2_fma && !outloud::cpu::optimized_available()) {
                std::cout << "AVX2/FMA: NOT AVAILABLE\n"; continue;
            }
            auto primitives = outloud::test::check_sampler_primitives(model, fixture, path);
            auto report = outloud::test::run_sampler_checks(model, voice, tokenizer, fixture, path);
            report.primitives = std::move(primitives);
            outloud::test::neural_require(report.primitives.size() == 130, "Missing isolated sampler primitive");
            outloud::test::write_sampler_report(neural / (path == outloud::cpu::KernelPath::portable
                ? "sampler-portable-parity.json" : "sampler-avx2-parity.json"), report, path);
            if (!measure) {
                outloud::test::enforce_sampler_errors(report.primitives);
                outloud::test::enforce_sampler_errors(report.errors);
            }
            double maximum = 0;
            for (const auto& [name, error] : report.errors) { maximum = std::max(maximum, error.raw.max_absolute); }
            for (const auto case_name : {"prefill", "bos"}) {
                const auto& error = report.errors.at(std::string(case_name) + ".latent");
                std::cout << "  " << case_name << (measure ? ": MEASURED" : ": hidden/time/blocks/velocity/latent PASS; noise EXACT")
                          << "; latent max abs " << error.raw.max_absolute << ", RMS " << error.raw.rms << '\n';
            }
            std::cout << outloud::cpu::path_name(path) << (measure ? ": MEASURED" : ": PASS") << "; max absolute " << maximum << "; sampler " << report.sampler_ms
                      << " ms; BOS candidate " << report.candidate_ms << " ms; offsets 132/133\n";
            if (portable) {
                std::map<std::string, outloud::test::SamplerError> agreement;
                for (const auto& [name, values] : report.outputs) {
                    agreement[name] = outloud::test::sampler_error(values, portable->outputs.at(name));
                }
                std::ofstream output(neural / "sampler-path-agreement.json");
                output << std::setprecision(17); outloud::test::write_sampler_errors(output, agreement); output << "\n";
                outloud::test::neural_require(bool(output), "Cannot write sampler path agreement");
                if (!measure) { outloud::test::enforce_sampler_errors(agreement); }
                std::cout << "Path agreement: " << (measure ? "MEASURED" : "PASS") << '\n';
            } else { portable = std::move(report); }
        }
        std::cout << (measure ? "RESULT: MEASURED (numerical gates bypassed)\n" : "RESULT: PASS\n");
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
