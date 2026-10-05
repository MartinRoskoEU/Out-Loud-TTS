#include "mimi_fixture_checks.hpp"
#include <iostream>
#include <optional>

namespace {
void little(std::ostream& out, std::uint32_t value, std::size_t bytes) {
    for (std::size_t i = 0; i < bytes; ++i) { out.put(static_cast<char>((value >> (i * 8)) & 255)); }
}
void wav(const std::filesystem::path& path, std::span<const float> pcm) {
    std::ofstream out(path, std::ios::binary);
    out.write("RIFF", 4); little(out, static_cast<std::uint32_t>(36 + pcm.size() * 2), 4);
    out.write("WAVEfmt ", 8); little(out, 16, 4); little(out, 1, 2); little(out, 1, 2);
    little(out, 24000, 4); little(out, 48000, 4); little(out, 2, 2); little(out, 16, 2);
    out.write("data", 4); little(out, static_cast<std::uint32_t>(pcm.size() * 2), 4);
    for (float value : pcm) {
        // Explicit round-to-nearest, ties-to-even, independent of process rounding mode.
        const double scaled = std::clamp(double(value), -1.0, 1.0) * 32767;
        const double low = std::floor(scaled), fraction = scaled - low;
        const auto lower = static_cast<int>(low);
        const int rounded = lower + (fraction > 0.5 || (fraction == 0.5 && lower % 2 != 0) ? 1 : 0);
        little(out, static_cast<std::uint16_t>(static_cast<std::int16_t>(rounded)), 2);
    }
    outloud::test::neural_require(bool(out), "Cannot write listening WAV");
}
void write_report(const std::filesystem::path& path, const outloud::test::MimiReport& r) {
    std::ofstream out(path);
    out << std::setprecision(17) << "{\n\"case_a\": "; outloud::test::write_sampler_errors(out, r.errors);
    out << ",\n\"case_b_informational\": "; outloud::test::write_sampler_errors(out, {{"pcm", r.native_error}});
    out << ",\n\"correlation\": " << r.correlation << ", \"peak_difference\": " << r.peak_difference
        << ", \"case_b_correlation\": " << r.native_correlation << ", \"case_b_peak_difference\": " << r.native_peak_difference
        << ",\n\"first_frame_ms\": " << r.first_ms << ", \"decoder_ms\": " << r.total_ms
        << ", \"generation_ms\": " << r.generation_ms << ", \"prefill_ms\": " << r.prefill_ms
        << ", \"first_candidate_ms\": " << r.first_candidate_ms
        << ",\n\"kv_bytes\": " << r.kv_bytes << ", \"conv_bytes\": " << r.conv_bytes
        << ", \"scratch_bytes\": " << r.scratch_bytes << "\n}\n";
    outloud::test::neural_require(bool(out), "Cannot write Mimi report");
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        bool measure = false, write_audio = false;
        for (int i = 1; i < argc; ++i) {
            const std::wstring_view option(argv[i]);
            if (option == L"--measure") { measure = true; }
            else if (option == L"--write-audio") { write_audio = true; }
            else { throw std::invalid_argument("Usage: outloud-mimi-check [--measure] [--write-audio]"); }
        }
        const std::filesystem::path directory = OUTLOUD_NEURAL_DIRECTORY, assets = OUTLOUD_ASSET_DIRECTORY;
        const outloud::PocketModelAsset model(assets / "pocket-english.olp");
        const outloud::OutLoudVoice voice(assets / "gnome_male.olv");
        const outloud::test::MimiFixture fixture(directory);
        const outloud::test::GenerationFixture generation(directory);
        const auto audio = directory.parent_path() / "audio";
        if (write_audio) {
            std::filesystem::create_directories(audio);
            wav(audio / "upstream-segment1.wav", fixture.values("pcm.complete"));
        }
        std::optional<outloud::test::MimiReport> portable;
        std::cout << "Out Loud Pocket Mimi decoder\nReference latent frames: 46\nSample rate: 24000\nSamples/frame: 1920\nExpected samples: 88320\n";
        for (const auto path : {outloud::cpu::KernelPath::portable, outloud::cpu::KernelPath::avx2_fma}) {
            if (path == outloud::cpu::KernelPath::avx2_fma && !outloud::cpu::optimized_available()) { std::cout << "AVX2/FMA: NOT AVAILABLE\n"; continue; }
            auto r = outloud::test::run_mimi_checks(model, voice, fixture, generation, path, measure);
            write_report(directory / (path == outloud::cpu::KernelPath::portable ? "mimi-portable-parity.json" : "mimi-avx2-parity.json"), r);
            const auto e = r.errors.at("pcm.complete");
            std::cout << std::setprecision(9) << outloud::cpu::path_name(path) << ": frame counts/offsets/reset PASS\n"
                      << "  Case A PCM max abs " << e.raw.max_absolute << "; RMS " << e.raw.rms << "; meaningful rel " << e.meaningful_relative
                      << "; correlation " << r.correlation << "; peak difference " << r.peak_difference << '\n'
                      << "  Case B (informational) max abs " << r.native_error.raw.max_absolute << "; RMS " << r.native_error.raw.rms
                      << "; correlation " << r.native_correlation << "; peak difference " << r.native_peak_difference << '\n'
                      << "  First frame " << r.first_ms << " ms; 46 frames " << r.total_ms << " ms; mean " << r.total_ms / 46 << " ms/frame\n"
                      << "  FlowLM/sampler " << r.generation_ms << " ms; combined compute RTF " << 3680 / (r.generation_ms + r.total_ms)
                      << "; first PCM estimate " << r.prefill_ms + r.first_candidate_ms + r.first_ms << " ms\n"
                      << "  KV " << r.kv_bytes << "; conv " << r.conv_bytes << "; scratch " << r.scratch_bytes << "; output 7680 bytes\n";
            std::cout << "  Latent preparation/projection/transformer/SEANet/PCM gates " << (measure ? "MEASURED" : "PASS") << '\n';
            if (portable) {
                std::map<std::string, outloud::test::SamplerError> agreement;
                for (const auto& [name, values] : r.outputs) { agreement[name] = outloud::test::sampler_error(values, portable->outputs.at(name)); }
                std::ofstream out(directory / "mimi-path-agreement.json"); out << std::setprecision(17);
                outloud::test::write_sampler_errors(out, agreement); out << '\n';
                outloud::test::neural_require(bool(out), "Cannot write Mimi agreement");
                if (!measure) { outloud::test::enforce_mimi_errors(agreement); }
                std::cout << "Path agreement: " << (measure ? "MEASURED" : "PASS") << '\n';
            } else {
                if (write_audio) {
                    wav(audio / "native-reference-latents.wav", r.reference_pcm);
                    wav(audio / "native-free-running.wav", r.native_pcm);
                }
                portable = std::move(r);
            }
        }
        std::cout << (measure ? "RESULT: MEASURED (numerical gates bypassed)\n" : "RESULT: PASS\n");
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
