#include "outloud/worker_pipeline.hpp"
#include "outloud/production_noise.hpp"
#include "outloud/text_pipeline.hpp"
#include "asset_io.hpp"
#include "worker_audit.hpp"
#include "../audio/wav.hpp"
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <new>

namespace outloud::development {
thread_local bool audit = false;
thread_local std::uint64_t allocations = 0;
}
void* operator new(std::size_t size) {
    if (void* p = std::malloc(size ? size : 1)) {
        if (outloud::development::audit) { ++outloud::development::allocations; }
        return p;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {
void require(bool condition, const char* message) { if (!condition) { throw std::runtime_error(message); } }
void validate(const outloud::WorkerResult& r, std::span<const float> pcm, std::uint64_t seed, std::uint32_t tokens) {
    require(r.draws == r.candidates+1 && r.samples == r.emitted*1920 && r.mimi_offset == r.emitted*16 &&
        r.flow_offset == 112+tokens+r.candidates && r.hot_allocations == 0 && r.samples <= pcm.size(),
        "Production request scheduling/state/allocation mismatch");
    if (seed == 73) {
        require(r.draws == 48 && r.candidates == 47 && r.first_eos == 43u && r.emitted == 46 && r.samples == 88320,
                "Canonical production request differs");
    }
    require(std::all_of(pcm.begin(), pcm.begin()+r.samples, [](float x) { return std::isfinite(x); }), "Nonfinite production PCM");
}
void serialize(std::ostream& out, const outloud::WorkerResult& r) {
    out << "{\"first_pcm_ms\":" << r.first_pcm_ms << ",\"total_ms\":" << r.total_ms
        << ",\"rtf\":" << r.samples/24.0/r.total_ms << ",\"draws\":" << r.draws
        << ",\"candidates\":" << r.candidates << ",\"emitted\":" << r.emitted << ",\"samples\":" << r.samples
        << ",\"eos\":" << r.first_eos.value_or(UINT32_MAX) << ",\"flow_offset\":" << r.flow_offset
        << ",\"mimi_offset\":" << r.mimi_offset << ",\"hot_allocations\":" << r.hot_allocations << '}';
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        auto path = outloud::cpu::KernelPath::portable;
        std::uint64_t seed = 73;
        std::size_t repetitions = 1;
        bool benchmark = false, write_audio = false;
        for (int i = 1; i < argc; ++i) {
            const std::wstring_view option(argv[i]);
            if (option == L"avx2") { path = outloud::cpu::KernelPath::avx2_fma; }
            else if (option == L"portable") { path = outloud::cpu::KernelPath::portable; }
            else if (option == L"--write-audio") { write_audio = true; }
            else if (option == L"--seed" && i+1 < argc) {
                const std::wstring value(argv[++i]);
                require(!value.empty() && value.find_first_not_of(L"0123456789") == std::wstring::npos, "Seed must be an unsigned decimal uint64");
                seed = std::stoull(value);
            } else if (option == L"--benchmark" && i+1 < argc) {
                const std::wstring_view count(argv[++i]);
                require(count == L"3" || count == L"5", "Use three timed runs, or five only for unexpected noise");
                repetitions = count == L"3" ? 3 : 5; benchmark = true;
            } else { throw std::invalid_argument("Usage: outloud-rng-synth [portable|avx2] [--seed uint64] [--benchmark 3|5] [--write-audio]"); }
        }
        const std::filesystem::path assets = OUTLOUD_ASSET_DIRECTORY;
        const outloud::PocketModelAsset model(assets / "pocket-english.olp");
        const outloud::OutLoudVoice voice(assets / "gnome_male.olv");
        const outloud::Tokenizer tokenizer(OUTLOUD_TOKENIZER_ASSET);
        const auto text = outloud::prepare_text("The watch captain needs your help before the next caravan arrives.", tokenizer);
        const auto tokens = tokenizer.encode(text.text);
        require(tokens.size() == 20, "Canonical tokenization differs");
        const auto maximum = outloud::FlowLMGenerator::estimate_max_gen_len(tokens.size());
        outloud::GenerationOptions options; options.frames_after_eos = text.frames_after_eos_guess+2;
        outloud::WorkerPipeline workers(model, voice, static_cast<std::uint32_t>(tokens.size()), maximum, path);
        std::vector<float> pcm(std::size_t(maximum)*1920);
        auto request = [&] {
            workers.prepare(tokens, seed, pcm, options); // owner reseed/reset before timing
            const auto r = workers.run();
            validate(r, pcm, seed, static_cast<std::uint32_t>(tokens.size())); // outside timing
            return r;
        };
        if (benchmark) { (void)request(); } // exactly one untimed warm-up
        std::array<outloud::WorkerResult, 5> runs;
        for (std::size_t i = 0; i < repetitions; ++i) {
            runs[i] = request();
            std::cout << "Production RNG " << i+1 << " first/total ms " << runs[i].first_pcm_ms << ' ' << runs[i].total_ms << '\n';
        }
        const auto samples = runs[repetitions-1].samples;
        const auto hash = outloud::detail::hex(outloud::detail::sha256({reinterpret_cast<const std::uint8_t*>(pcm.data()), std::size_t(samples)*4}));
        const auto directory = assets.parent_path() / "perf"; std::filesystem::create_directories(directory);
        const auto suffix = path == outloud::cpu::KernelPath::portable ? "portable" : "avx2";
        const auto filename = std::string("2k-")+(benchmark ? "production-benchmark-" : "standalone-synthesis-")+suffix+".json";
        std::ofstream report(directory / filename);
        report << std::setprecision(17) << "{\"seed\":" << seed << ",\"tokens\":20,\"captured_noise_files_used\":false,\"warmups\":"
            << (benchmark ? 1 : 0) << ",\"timed_repetitions\":" << repetitions << ",\"pcm_sha256\":\"" << hash
            << "\",\"rng_bytes\":" << sizeof(outloud::ProductionNoiseSource) << ",\"worker_inline_bytes\":" << workers.object_storage_bytes()
            << ",\"runs\":[";
        for (std::size_t i = 0; i < repetitions; ++i) { if (i) { report << ','; } serialize(report, runs[i]); }
        report << "]}\n"; require(bool(report), "Cannot write production report");
        if (write_audio) {
            const auto audio = assets.parent_path() / "audio"; std::filesystem::create_directories(audio);
            outloud::development::write_pcm16_wave(audio / (std::string("native-production-rng-segment1-")+suffix+".wav"), std::span(pcm).first(samples));
        }
        std::cout << "PASS: native tokenizer/model/voice/seed only; PCM SHA256 " << hash << '\n';
        return 0;
    } catch (const std::exception& e) { outloud::development::audit = false; std::cerr << e.what() << '\n'; return 1; }
}
