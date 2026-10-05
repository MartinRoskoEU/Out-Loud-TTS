#include "outloud/worker_pipeline.hpp"
#include "worker_audit.hpp"
#include "mimi_fixture_checks.hpp"
#include <Windows.h>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>

namespace outloud::development {
thread_local bool audit = false;
thread_local std::uint64_t allocations = 0;
}
namespace { using outloud::development::audit; using outloud::development::allocations; }
void* operator new(std::size_t size) {
    if (void* p = std::malloc(size ? size : 1)) { if (audit) { ++allocations; } return p; }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#include "../perf-check/resident.hpp"

namespace {
using namespace outloud;
using test::neural_require;
void validate(const WorkerResult& r, const FixtureNoiseSource& noise, std::span<const float> pcm) {
    neural_require(r.candidates == 47 && r.emitted == 46 && r.draws == 48 && noise.consumed() == 48 &&
        r.samples == 88320 && r.first_eos == 43u && r.flow_offset == 179 && r.mimi_offset == 736 &&
        r.termination == GenerationTermination::eos_tail && r.hot_allocations == 0,
        "Worker scheduling/state/allocation result differs");
    neural_require(std::all_of(pcm.begin(), pcm.begin() + r.samples, [](float x) { return std::isfinite(x); }),
                   "Nonfinite worker PCM");
}
WorkerResult request(WorkerPipeline& workers, std::span<const float> rows, std::span<float> pcm) {
    FixtureNoiseSource noise(rows); // full fixture validation before timing
    workers.prepare(test::neural_tokens, noise, pcm);
    auto r = workers.run(); validate(r, noise, pcm); return r;
}
void worker_json(std::ostream& out, const WorkerResult& r, double cpu) {
    out << "{\"first_pcm_ms\":" << r.first_pcm_ms << ",\"total_ms\":" << r.total_ms
        << ",\"rtf\":" << 3680 / r.total_ms << ",\"process_cpu_ms\":" << cpu
        << ",\"cpu_wall_ratio\":" << cpu / r.total_ms << ",\"hot_allocations\":" << r.hot_allocations
        << ",\"candidates\":" << r.candidates << ",\"emitted\":" << r.emitted << ",\"noise_draws\":" << r.draws
        << ",\"samples\":" << r.samples << ",\"first_eos\":" << r.first_eos.value_or(UINT32_MAX)
        << ",\"flow_offset\":" << r.flow_offset << ",\"mimi_offset\":" << r.mimi_offset << '}';
}
#ifndef OUTLOUD_WORKER_TRACE
struct SavedCall {
    GenerationInput kind;
    std::uint32_t index, before, after;
    float eos;
    bool classified, emitted;
    std::optional<std::uint32_t> latched;
    std::vector<float> input, hidden, noise;
    FlowSample sample;
};
struct SinkCheck {
    std::span<const float> expected;
    std::atomic<unsigned>* candidates;
    unsigned frames = 0, first_candidates = 0;
};
void check_sink(void* context, std::uint32_t frame, std::span<const float> pcm) {
    auto& s = *static_cast<SinkCheck*>(context);
    neural_require(frame == s.frames && pcm.size() == 1920 &&
        test::same_float_bits(pcm, s.expected.subspan(std::size_t(frame) * 1920, 1920)), "PCM streaming boundary/order differs");
    if (!frame) { s.first_candidates = s.candidates->load(); neural_require(s.first_candidates < 47, "PCM was delayed until complete generation"); }
    ++s.frames;
}
void throwing_sink(void*, std::uint32_t, std::span<const float>) { throw std::runtime_error("Injected consumer failure"); }
class ThrowingNoise final : public NoiseSource {
    std::span<const float> next() override { throw std::runtime_error("Injected producer failure"); }
};
void correctness(const PocketModelAsset& model, const OutLoudVoice& voice, cpu::KernelPath path,
        std::span<const float> rows, const std::filesystem::path& directory) {
    // A clean sequential equivalent supplies the exact native arithmetic reference.
    FlowLMBackbone backbone(model, voice, path); FlowSampler sampler(model, path);
    FlowLMGenerator generator(backbone, sampler);
    FixtureNoiseSource sequential_noise(rows);
    std::vector<SavedCall> calls;
    GenerationObserver save = [&](const GenerationCall& c) {
        calls.push_back({c.input_kind, c.index, c.offset_before, c.offset_after, c.eos_logit, c.eos, c.emitted,
            c.latched_eos, {c.input_latent.begin(), c.input_latent.end()}, {c.hidden.begin(), c.hidden.end()},
            {c.noise.begin(), c.noise.end()}, c.sample});
    };
    const auto generated = generator.generate_segment(test::neural_tokens, sequential_noise, {}, save);
    MimiDecoder mimi(model, path); MimiDecoderState state(109);
    std::vector<float> expected(88320), pcm(109 * 1920);
    std::size_t samples = 0;
    for (const auto& latent : generated.emitted) {
        const auto frame = mimi.decode_frame(latent, state);
        std::copy(frame.begin(), frame.end(), expected.begin() + samples); samples += frame.size();
    }
    neural_require(calls.size() == 48 && samples == 88320, "Invalid clean equivalent");
    { WorkerPipeline idle(model, voice, 20, 109, path); } // idle destruction joins both workers
    WorkerPipeline workers(model, voice, 20, 109, path);
    std::size_t seen = 0;
    std::atomic<unsigned> candidates{0};
    GenerationObserver compare = [&](const GenerationCall& c) {
        const auto& e = calls.at(seen++);
        neural_require(c.input_kind == e.kind && c.index == e.index && c.offset_before == e.before && c.offset_after == e.after &&
            c.eos_logit == e.eos && c.eos == e.classified && c.emitted == e.emitted && c.latched_eos == e.latched &&
            test::same_float_bits(c.input_latent, e.input) && test::same_float_bits(c.hidden, e.hidden) &&
            test::same_float_bits(c.noise, e.noise) && test::same_float_bits(c.sample.velocity, e.sample.velocity) &&
            test::same_float_bits(c.sample.latent, e.sample.latent), "Worker prefill/candidate timeline or arithmetic differs");
        if (c.input_kind != GenerationInput::text) { ++candidates; }
    };
    std::array<WorkerResult, 2> results;
    unsigned first_candidates = 0;
    for (unsigned repetition = 0; repetition < 2; ++repetition) {
        seen = 0; candidates = 0;
        FixtureNoiseSource noise(rows);
        SinkCheck sink{expected, &candidates};
        workers.prepare(test::neural_tokens, noise, pcm, {}, check_sink, &sink, &compare);
        results[repetition] = workers.run(); validate(results[repetition], noise, pcm);
        first_candidates = sink.first_candidates;
        neural_require(seen == 48 && sink.frames == 46 && test::same_float_bits(std::span(pcm).first(88320), expected),
                       "Worker complete PCM/reuse differs");
        for (unsigned layer = 0; layer < 6; ++layer) {
            const auto a = generator.state().layer(layer), b = workers.flow_state().layer(layer);
            neural_require(a.offset == b.offset && test::same_float_bits(a.keys, b.keys) && test::same_float_bits(a.values, b.values),
                           "Worker FlowLM final cache differs");
        }
        for (unsigned layer = 0; layer < 2; ++layer) {
            neural_require(test::same_float_bits(state.keys(layer), workers.mimi_state().keys(layer)) &&
                test::same_float_bits(state.values(layer), workers.mimi_state().values(layer)), "Worker Mimi cache differs");
        }
        for (unsigned configuration = 0; configuration < 12; ++configuration) {
            neural_require(test::same_float_bits(state.convolution_state(configuration),
                workers.mimi_state().convolution_state(configuration)), "Worker Mimi carry differs");
        }
        if (!repetition) {
            // Exercise both error directions between A and B: waits must wake and both
            // workers must quiesce before reset. These are short failures, not stress runs.
            FixtureNoiseSource failed_noise(rows);
            workers.prepare(test::neural_tokens, failed_noise, pcm, {}, throwing_sink);
            bool failed = false;
            try { (void)workers.run(); } catch (const std::runtime_error&) { failed = true; }
            neural_require(failed, "Consumer exception lost");
            ThrowingNoise throwing_noise;
            workers.prepare(test::neural_tokens, throwing_noise, pcm);
            failed = false;
            try { (void)workers.run(); } catch (const std::runtime_error&) { failed = true; }
            neural_require(failed, "Producer exception lost");
        }
    }
    std::ofstream out(directory / "2j-correctness.json");
    out << "{\"path\":\"" << cpu::path_name(path) << "\",\"native_float_bits_equal\":true,\"eos_timeline_equal\":true,"
        "\"all_flow_mimi_cache_and_carry_bits_equal\":true,\"reuse_equal\":true,\"idle_shutdown_passed\":true,"
        "\"producer_consumer_error_recovery_passed\":true,\"first_pcm_candidate_count\":" << first_candidates << ",\"requests\":[";
    worker_json(out, results[0], 0); out << ','; worker_json(out, results[1], 0); out << "]}\n";
    neural_require(bool(out), "Cannot write worker correctness report");
    std::cout << "PASS: exact prefill, 47 candidates/EOS/noise, 46 latents/frames, 88320 PCM and all final states; A/B reuse, both error directions, idle/completed shutdown; zero hot allocations.\n";
}
void benchmark(const PocketModelAsset& model, const OutLoudVoice& voice, std::span<const float> rows,
        const std::filesystem::path& directory, std::size_t repetitions) {
    const auto path = cpu::KernelPath::avx2_fma;
    Resident sequential(model, voice, path, rows);
    WorkerPipeline workers(model, voice, 20, 109, path); // startup barrier outside measurements
    std::vector<float> pcm(109 * 1920);
    (void)sequential.run();
    std::array<Run, 5> s;
    for (std::size_t i = 0; i < repetitions; ++i) {
        s[i] = sequential.run(); std::cout << "S1 " << i+1 << " first/total ms " << s[i].first << ' ' << s[i].total << '\n';
    }
    (void)request(workers, rows, pcm);
    std::array<WorkerResult, 5> w;
    std::array<double, 5> cpus{};
    for (std::size_t i = 0; i < repetitions; ++i) {
        FixtureNoiseSource noise(rows);
        workers.prepare(test::neural_tokens, noise, pcm);
        const auto cpu = process_cpu_ms();
        w[i] = workers.run(); cpus[i] = process_cpu_ms() - cpu;
        validate(w[i], noise, pcm);
        neural_require(test::same_float_bits(std::span(pcm).first(88320), sequential.pcm()), "Benchmark worker/sequential PCM differs");
        std::cout << "W1 " << i+1 << " first/total ms " << w[i].first_pcm_ms << ' ' << w[i].total_ms << '\n';
    }
    const auto filename = repetitions == 5 ? "2j-final-benchmark.json" : "2j-intermediate-benchmark.json";
    std::ofstream out(directory / filename);
    out << std::setprecision(17) << "{\"warmups_per_mode\":1,\"timed_repetitions\":" << repetitions
        << ",\"same_process\":true,\"queue_capacity\":2,\"trace_and_profiler_compiled_out\":true,\"sequential\":[";
    for (std::size_t i = 0; i < repetitions; ++i) { if (i) { out << ','; } serialize(out, s[i]); }
    out << "],\"worker\":[";
    for (std::size_t i = 0; i < repetitions; ++i) { if (i) { out << ','; } worker_json(out, w[i], cpus[i]); }
    out << "],\"memory\":{\"flow_suffix\":" << workers.flow_state().suffix_storage_bytes()
        << ",\"flow_workspace\":" << workers.backbone_workspace_bytes() << ",\"mimi_kv\":" << workers.mimi_state().kv_bytes()
        << ",\"mimi_carry\":" << workers.mimi_state().convolution_bytes() << ",\"mimi_scratch\":" << workers.mimi_scratch_bytes()
        << ",\"queue_payload\":256,\"inline_object_bytes\":" << workers.object_storage_bytes()
        << ",\"destination_bytes\":" << pcm.size() * sizeof(float) << "}}\n";
    neural_require(bool(out), "Cannot write benchmark report");
}
#endif
}
int wmain(int argc, wchar_t** argv) {
    try {
        const std::filesystem::path assets = OUTLOUD_ASSET_DIRECTORY, neural = OUTLOUD_NEURAL_DIRECTORY;
        const PocketModelAsset model(assets / "pocket-english.olp"); const OutLoudVoice voice(assets / "gnome_male.olv");
        const test::GenerationFixture fixture(neural);
        const auto rows = fixture.values("noise.stream");
        const auto directory = neural.parent_path() / "perf";
        std::filesystem::create_directories(directory);
#ifdef OUTLOUD_WORKER_TRACE
        (void)argc; (void)argv;
        Resident sequential(model, voice, cpu::KernelPath::avx2_fma, rows);
        WorkerPipeline workers(model, voice, 20, 109, cpu::KernelPath::avx2_fma);
        std::vector<float> pcm(109 * 1920);
        (void)sequential.run(); // match the benchmark's resident contexts and preceding sequential work
        (void)request(workers, rows, pcm); // one untimed warm-up
        // One diagnostic run; no separate repeated profiling campaign.
        FixtureNoiseSource noise(rows);
        workers.prepare(test::neural_tokens, noise, pcm);
        const auto cpu = process_cpu_ms();
        const auto result = workers.run();
        const auto used_cpu = process_cpu_ms() - cpu;
        validate(result, noise, pcm);
        neural_require(test::same_float_bits(std::span(pcm).first(88320), sequential.pcm()), "Diagnostic PCM differs");
        std::ofstream out(directory / "2j-pipeline-trace.json");
        out << "{\"instrumented\":true,\"resident_setup_matches_benchmark\":true,\"result\":"; worker_json(out, result, used_cpu);
        out << ",\"trace\":"; workers.write_trace(out); out << "}\n";
        neural_require(bool(out), "Cannot write trace report");
        std::cout << "One diagnostic pipeline trace complete.\n";
#else
        const std::wstring_view mode = argc >= 2 ? argv[1] : L"check";
        if (mode == L"check" && argc <= 3) {
            const bool portable = argc == 3 && std::wstring_view(argv[2]) == L"portable";
            if (argc == 3 && !portable && std::wstring_view(argv[2]) != L"avx2") { throw std::invalid_argument("Unknown path"); }
            correctness(model, voice, portable ? cpu::KernelPath::portable : cpu::KernelPath::avx2_fma, rows, directory);
        } else if (mode == L"bench" && argc <= 3) {
            const std::wstring_view count = argc == 3 ? argv[2] : L"5";
            if (count != L"2" && count != L"5") { throw std::invalid_argument("Use two intermediate or five final runs"); }
            benchmark(model, voice, rows, directory, count == L"2" ? 2 : 5);
        } else { throw std::invalid_argument("Usage: outloud-worker-check check [avx2|portable] | bench [2|5]"); }
#endif
        return 0;
    } catch (const std::exception& e) { audit = false; std::cerr << e.what() << '\n'; return 1; }
}
