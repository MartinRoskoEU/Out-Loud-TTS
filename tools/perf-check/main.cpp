#include "outloud/perf_diagnostics.hpp"
#include "mimi_fixture_checks.hpp"
#include <Windows.h>
#include <cstdlib>
#include <iostream>
#include <new>

namespace { bool audit = false; std::uint64_t allocations = 0; }
void* operator new(std::size_t size) {
    if (void* p = std::malloc(size == 0 ? 1 : size)) { if (audit) { ++allocations; } return p; }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {
using Clock = std::chrono::steady_clock;
double ms(Clock::time_point start, Clock::time_point end) { return std::chrono::duration<double, std::milli>(end-start).count(); }
double process_cpu_ms() {
    FILETIME creation, exit, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) { throw std::runtime_error("Cannot read process CPU time"); }
    auto ticks = [](FILETIME f) { return (std::uint64_t(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
    return static_cast<double>(ticks(kernel) + ticks(user)) / 10000;
}
struct Run {
    double first = 0, total = 0, prefill = 0, ar = 0, mimi = 0, cpu = 0;
    std::array<double, 47> candidates{};
    std::array<double, 46> frames{};
    std::uint64_t hot_allocations = 0;
    std::uint32_t calls = 0, emitted = 0, first_eos = 0, offset = 0, draws = 0, samples = 0;
};
class Resident {
public:
    Resident(outloud::PocketModelAsset model, outloud::OutLoudVoice voice, outloud::cpu::KernelPath path,
             std::span<const float> noise)
        : backbone_(model, voice, path), sampler_(model, path), mimi_(model, path), flow_state_(backbone_.make_state(129)),
          mimi_state_(109), noise_(noise), pcm_(88320) {
        // Same reusable candidate reservation as the existing Step 2F generator.
        candidate_.backbone.normalized.resize(1024);
    }
    Run run() {
        flow_state_.reset(); mimi_state_.reset();
        outloud::FixtureNoiseSource noise(noise_);
        outloud::GenerationStopPolicy stop({});
        Run r;
        const auto cpu = process_cpu_ms();
        const auto start = Clock::now();
        const auto prefill = backbone_.prefill(outloud::test::neural_tokens, flow_state_);
        OUTLOUD_PERF_CALL(::outloud::perf::Stage::noise, first_noise_ = noise.next());
        (void)sampler_.sample(prefill.last_hidden(), first_noise_); // Required discarded sampler call.
        const auto prefilled = Clock::now(); r.prefill = ms(start, prefilled);
        allocations = 0; audit = true;
        try {
            for (std::uint32_t i = 0; i < 109; ++i) {
                if (i >= r.candidates.size()) { throw std::runtime_error("Unexpected candidate count in canonical benchmark"); }
                const auto candidate_start = Clock::now();
                std::span<const float> row;
                OUTLOUD_PERF_CALL(::outloud::perf::Stage::noise, row = noise.next());
                if (i == 0) { sampler_.bos_candidate_into(backbone_, flow_state_, row, candidate_); }
                else { sampler_.frame_candidate_into(backbone_, flow_state_, previous_, row, candidate_); }
                const auto candidate_end = Clock::now();
                r.candidates[i] = ms(candidate_start, candidate_end); r.ar += r.candidates[i]; ++r.calls;
                if (!stop.emit(i, candidate_.backbone.eos_logit)) { break; }
                if (r.emitted >= r.frames.size()) { throw std::runtime_error("Unexpected emitted count in canonical benchmark"); }
                previous_ = candidate_.sample.latent;
                const auto decode_start = Clock::now();
                const auto pcm = mimi_.decode_frame(previous_, mimi_state_);
                const auto decoded = Clock::now();
                if (r.emitted == 0) { r.first = ms(start, decoded); }
                r.frames[r.emitted] = ms(decode_start, decoded); r.mimi += r.frames[r.emitted];
                std::copy(pcm.begin(), pcm.end(), pcm_.begin() + r.samples);
                r.samples += static_cast<std::uint32_t>(pcm.size()); ++r.emitted;
            }
        } catch (...) { audit = false; throw; }
        const auto end = Clock::now(); audit = false;
        r.total = ms(start, end); r.cpu = process_cpu_ms() - cpu; r.hot_allocations = allocations;
        noise.finish_segment(); // Validation outside timed region.
        r.offset = flow_state_.offset(); r.draws = static_cast<std::uint32_t>(noise.consumed()); r.first_eos = stop.first_eos().value_or(UINT32_MAX);
        outloud::test::neural_require(r.calls == 47 && r.emitted == 46 && r.draws == 48 && r.first_eos == 43 &&
            r.offset == 179 && r.samples == 88320 && mimi_state_.offset() == 736 && r.hot_allocations == 0,
            "Performance harness changed canonical workload or hot-loop allocation behavior");
        outloud::test::neural_require(std::all_of(pcm_.begin(), pcm_.end(), [](float x) { return std::isfinite(x); }), "Nonfinite benchmark PCM");
        return r;
    }
    [[nodiscard]] std::span<const float> pcm() const { return pcm_; }
private:
    outloud::FlowLMBackbone backbone_;
    outloud::FlowSampler sampler_;
    outloud::MimiDecoder mimi_;
    outloud::FlowLMPrefillState flow_state_;
    outloud::MimiDecoderState mimi_state_;
    outloud::DeterministicCandidate candidate_;
    std::array<float, 32> previous_{};
    std::span<const float> noise_, first_noise_;
    std::vector<float> pcm_;
};
void array(std::ostream& out, std::span<const double> values) {
    out << '['; bool first = true;
    for (auto v : values) { if (!first) { out << ','; } first = false; out << v; } out << ']';
}
void serialize(std::ostream& out, const Run& r) {
    out << "{\"first_pcm_ms\":" << r.first << ",\"total_ms\":" << r.total << ",\"prefill_ms\":" << r.prefill
        << ",\"ar_including_sampler_ms\":" << r.ar << ",\"mimi_ms\":" << r.mimi << ",\"rtf\":" << 3680/r.total
        << ",\"process_cpu_ms\":" << r.cpu << ",\"cpu_wall_ratio\":" << r.cpu/r.total
        << ",\"hot_allocations\":" << r.hot_allocations << ",\"samples\":" << r.samples << ",\"candidates\":" << r.calls
        << ",\"emitted\":" << r.emitted << ",\"noise_draws\":" << r.draws << ",\"first_eos\":" << r.first_eos << ",\"final_offset\":" << r.offset
        << ",\"candidates_ms\":"; array(out, r.candidates); out << ",\"mimi_frames_ms\":"; array(out, r.frames); out << '}';
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc > 2) { throw std::invalid_argument("Usage: outloud-perf[-profile] [portable|avx2]"); }
        const bool avx = argc == 2 && std::wstring_view(argv[1]) == L"avx2";
        if (argc == 2 && !avx && std::wstring_view(argv[1]) != L"portable") { throw std::invalid_argument("Unknown kernel path"); }
        const auto path = avx ? outloud::cpu::KernelPath::avx2_fma : outloud::cpu::KernelPath::portable;
        const std::filesystem::path assets = OUTLOUD_ASSET_DIRECTORY, neural = OUTLOUD_NEURAL_DIRECTORY;
        const outloud::PocketModelAsset model(assets / "pocket-english.olp"); const outloud::OutLoudVoice voice(assets / "gnome_male.olv");
        const outloud::test::GenerationFixture fixture(neural);
        Resident context(model, voice, path, fixture.values("noise.stream"));
        (void)context.run(); // Exactly one untimed warm-up, also verifies workload.
        const auto report_directory = neural.parent_path() / "perf"; std::filesystem::create_directories(report_directory);
#ifdef OUTLOUD_PERF_DIAGNOSTICS
        outloud::perf::counters = {}; outloud::perf::enabled = true;
        const auto run = context.run(); outloud::perf::enabled = false;
        std::ofstream out(report_directory / (avx ? "native-avx2-profile.json" : "native-portable-profile.json"));
        out << std::setprecision(17) << "{\"profiled_run\":"; serialize(out, run); out << ",\"counters\":[";
        bool first = true;
        for (std::size_t d = 0; d < 4; ++d) {
            for (std::size_t s = 0; s < outloud::perf::names.size(); ++s) {
                const auto& c = outloud::perf::counters[d * outloud::perf::names.size() + s]; if (!c.calls) { continue; }
                if (!first) { out << ','; } first = false;
                out << "{\"domain\":\"" << outloud::perf::domains[d] << "\",\"stage\":\"" << outloud::perf::names[s]
                    << "\",\"calls\":" << c.calls << ",\"inclusive_ms\":" << static_cast<double>(c.inclusive_ns)/1e6
                    << ",\"exclusive_ms\":" << static_cast<double>(c.exclusive_ns)/1e6 << '}';
            }
        }
        out << "]}\n";
        std::cout << "One diagnostic profile complete; timings are instrumented, not benchmark results.\n";
#else
        std::array<Run, 5> runs;
        for (std::size_t i = 0; i < runs.size(); ++i) {
            runs[i] = context.run();
            std::cout << outloud::cpu::path_name(path) << ' ' << i+1 << " first/total ms " << runs[i].first << ' ' << runs[i].total << '\n';
        }
        std::ofstream out(report_directory / (avx ? "native-avx2-benchmark.json" : "native-portable-benchmark.json"));
        out << std::setprecision(17) << "{\"warmups\":1,\"timed_repetitions\":5,\"no_timed_observers_or_profiler\":true,\"runs\":[";
        for (std::size_t i = 0; i < runs.size(); ++i) { if (i) { out << ','; } serialize(out, runs[i]); } out << "]}\n";
#endif
        outloud::test::neural_require(bool(out), "Cannot write performance report");
        return 0;
    } catch (const std::exception& e) { audit = false; std::cerr << e.what() << '\n'; return 1; }
}
