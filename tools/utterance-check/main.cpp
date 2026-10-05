#include "outloud/orchestration.hpp"
#include "asset_io.hpp"
#include "../audio/wav.hpp"
#include <atomic>
#include <cstring>
#include <iomanip>
#include <iostream>
#ifdef OUTLOUD_UTTERANCE_CHECK
#include "orchestration_reference.hpp"
#include "worker_audit.hpp"
#include "cancellation_audit.hpp"
#include <condition_variable>
#include <mutex>
#include <thread>
#include <cstdlib>
#include <new>
namespace outloud::development {
thread_local bool audit = false;
thread_local std::uint64_t allocations = 0;
struct CancellationProbe {
    CancellationPoint target;
    std::mutex mutex;
    std::condition_variable changed;
    bool hit = false, released = false, done = false;
    explicit CancellationProbe(CancellationPoint point) : target(point) {}
    void point(CancellationPoint value,bool may_block) {
        std::unique_lock lock(mutex);
        if (value!=target || hit) { return; }
        hit = true; changed.notify_all();
        if (may_block) { changed.wait(lock,[&] { return released; }); }
    }
    void hold_sink() {
        std::unique_lock lock(mutex); changed.wait(lock,[&] { return released; });
    }
};
std::atomic<CancellationProbe*> cancellation_probe{nullptr};
void cancellation_point(CancellationPoint point,bool may_block) {
    if (auto probe = cancellation_probe.load()) { probe->point(point,may_block); }
}
}
void* operator new(std::size_t n) {
    if (void* p = std::malloc(n ? n : 1)) {
        if (outloud::development::audit) { ++outloud::development::allocations; }
        return p;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#endif

namespace {
using namespace outloud;
constexpr std::string_view canonical = "The watch captain needs your help before the next caravan arrives. Follow the river to the abandoned tower, speak with the scout waiting at its gate, and bring his report back to the village. Keep your lantern burning, for the path is narrow.";
void require(bool value, const char* message) { if (!value) { throw std::runtime_error(message); } }
void validate(const UtteranceResult& r) {
    require(r.draws == 176 && r.candidates == 173 && r.frames == 170 && r.samples == 326400 && r.segments.size() == 3,
            "Full utterance counts differ");
    constexpr std::array<std::uint32_t,3> tokens{20,39,16}, draws{48,86,42}, candidates{47,85,41}, eos{43,81,37}, frames{46,84,40};
    std::uint64_t offset = 0;
    for (std::size_t i = 0; i < 3; ++i) {
        const auto& s = r.segments[i]; const auto& w = s.worker;
        require(s.tokens == tokens[i] && s.first_draw == offset && w.draws == draws[i] &&
            w.candidates == candidates[i] && w.first_eos == eos[i] && w.emitted == frames[i] &&
            w.samples == frames[i]*1920 && w.flow_offset == 112+tokens[i]+candidates[i] && w.mimi_offset == frames[i]*16 &&
            w.termination == GenerationTermination::eos_tail && w.hot_allocations == 0, "Segment scheduling differs");
        offset += draws[i];
    }
}
void result_json(std::ostream& out, const UtteranceResult& r) {
    out << "{\"text_preparation_ms\":" << r.text_preparation_ms << ",\"first_pcm_ms\":" << r.first_pcm_ms
        << ",\"total_ms\":" << r.total_ms << ",\"rtf\":" << r.samples/24.0/r.total_ms
        << ",\"draws\":" << r.draws << ",\"candidates\":" << r.candidates << ",\"frames\":" << r.frames
        << ",\"samples\":" << r.samples << ",\"segments\":[";
    for (std::size_t i = 0; i < r.segments.size(); ++i) {
        const auto& s = r.segments[i]; const auto& w = s.worker; if (i) { out << ','; }
        out << "{\"tokens\":" << s.tokens << ",\"first_draw\":" << s.first_draw << ",\"draws\":" << w.draws
            << ",\"candidates\":" << w.candidates << ",\"eos\":" << w.first_eos.value_or(UINT32_MAX)
            << ",\"frames\":" << w.emitted << ",\"samples\":" << w.samples << ",\"audio_seconds\":" << w.samples/24000.0
            << ",\"worker_compute_ms\":" << w.total_ms << ",\"segment_wall_ms\":" << s.wall_ms
            << ",\"hot_allocations\":" << w.hot_allocations << '}';
    }
    out << "]}";
}
struct Sink {
    std::span<float> pcm;
    std::uint64_t frames = 0;
    std::size_t last_segment = 0;
    std::uint32_t next_local = 0;
    bool fail = false;
    std::atomic<std::uint32_t>* produced = nullptr;
    std::uint32_t first_candidates = 0;
    static void frame(void* context, std::uint64_t global, std::size_t segment, std::uint32_t local, std::span<const float> pcm) {
        auto& s = *static_cast<Sink*>(context);
        require(pcm.size() == 1920 && global == s.frames && segment <= s.last_segment+1, "Streaming order differs");
        if (segment != s.last_segment) { s.last_segment = segment; s.next_local = 0; }
        require(local == s.next_local++, "Segment-local streaming index differs");
        if (!s.frames && s.produced) { s.first_candidates = s.produced->load(); }
        if (s.fail) { throw std::runtime_error("Injected utterance sink failure"); }
        if (!s.pcm.empty()) { std::copy(pcm.begin(), pcm.end(), s.pcm.begin()+std::size_t(global)*1920); }
        ++s.frames;
    }
};
#ifdef OUTLOUD_UTTERANCE_CHECK
void cancellation_checks(UtterancePipeline& pipeline,std::span<const float> reference,const std::filesystem::path& directory) {
    using namespace outloud::development;
    std::vector<float> pcm(reference.size());
    const auto recover = [&] {
        pipeline.prepare(canonical,73); Sink sink; sink.pcm = pcm;
        const auto result = pipeline.run(Sink::frame,&sink); validate(result);
        require(result.status==SynthesisStatus::completed && std::memcmp(pcm.data(),reference.data(),pcm.size()*4)==0,
                "Native cancellation recovery changed PCM");
    };
    std::stop_source early; require(early.request_stop() && !early.request_stop(),"Stop is not idempotent");
    pipeline.prepare(canonical,73); Sink no_audio;
    const auto before = pipeline.run(Sink::frame,&no_audio,nullptr,early.get_token());
    require(before.status==SynthesisStatus::cancelled && before.draws==0 && before.frames==0 && before.segments.empty(),"Precancelled request computed audio");
    recover();
    const std::array<const char*,8> names{"before_prefill","after_prefill","candidate_complete","full_queue","empty_queue","before_decode","pcm_ready","segment_boundary"};
    std::ofstream report(directory/"2n-native-cancellation.json"); report << "{\"pre_cancelled\":true,\"cases\":[";
    for (unsigned i = 0; i < names.size(); ++i) {
        const auto point = static_cast<CancellationPoint>(i);
        CancellationProbe probe(point); std::stop_source source;
        struct Output {
            Sink sink; CancellationProbe* probe;
            static void frame(void* context,std::uint64_t global,std::size_t segment,std::uint32_t local,std::span<const float> pcm) {
                auto& o = *static_cast<Output*>(context); Sink::frame(&o.sink,global,segment,local,pcm);
                if (!global && o.probe->target==CancellationPoint::full_queue) { o.probe->hold_sink(); }
            }
        } output{{},&probe};
        pipeline.prepare(canonical,73); cancellation_probe = &probe;
        UtteranceResult result; std::exception_ptr failure;
        std::thread coordinator([&] {
            try { result = pipeline.run(Output::frame,&output,nullptr,source.get_token()); } catch (...) { failure = std::current_exception(); }
            { std::lock_guard lock(probe.mutex); probe.done = true; } probe.changed.notify_all();
        });
        bool reached = false;
        {
            std::unique_lock lock(probe.mutex);
            probe.changed.wait_for(lock,std::chrono::seconds(10),[&] { return probe.hit || probe.done; });
            reached = probe.hit;
        }
        const bool first_stop = source.request_stop(), second_stop = source.request_stop();
        { std::lock_guard lock(probe.mutex); probe.released = true; } probe.changed.notify_all();
        coordinator.join(); cancellation_probe = nullptr;
        if (failure) { std::rethrow_exception(failure); }
        require(reached && first_stop && !second_stop && result.status==SynthesisStatus::cancelled,"Native cancellation point failed");
        if (point==CancellationPoint::segment_boundary) { require(result.segments.size()==1 && result.draws==48 && result.frames==46,"Next segment started after cancellation"); }
        else { require(result.segments.size()==1 && result.segments[0].worker.status==SynthesisStatus::cancelled,"Worker cancellation was reported as completion"); }
        if (point==CancellationPoint::before_prefill || point==CancellationPoint::before_decode || point==CancellationPoint::pcm_ready) {
            require(output.sink.frames==0,"Cancelled frame was published");
        }
        recover();
        if (i) { report << ','; }
        report << "{\"point\":\"" << names[i] << "\",\"cancelled\":true,\"recovered_exact_pcm\":true}";
        std::cout << "PASS native cancellation: " << names[i] << "; exact same-pipeline recovery\n";
    }
    const auto storage = pipeline.storage();
    report << "],\"orchestration_object_bytes\":" << storage.object_bytes << ",\"worker_inline_bytes\":" << storage.worker_inline_bytes << "}\n";
    require(bool(report),"Cannot write native cancellation report");
}
void correctness(UtterancePipeline& pipeline, const std::filesystem::path& directory, const char* path) {
    using namespace outloud::test;
    require(canonical == full_text, "Canonical text changed");
    // Existing text API, source-backed empty/limit probes, no extra generation.
    for (auto empty : {"", " \t\n\xc2\xa0"}) {
        bool rejected = false;
        try { pipeline.prepare(empty, 73); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Empty text accepted");
    }
    pipeline.prepare(at_limit_text,73);
    require(pipeline.text().full_token_ids.size() == 50 && pipeline.text().segments.size() == 1, "Exact soft limit differs");
    pipeline.prepare(over_limit_text,73);
    require(pipeline.text().full_token_ids.size() == 51 && pipeline.text().segments.size() == 1, "Unsplittable soft limit differs");
    pipeline.prepare("Hello there.",73); require(pipeline.text().segments.size() == 1 && pipeline.text().segments[0].frames_after_eos == 5, "Short text preparation differs");
    std::string oversized;
    for (unsigned i = 0; i < 60; ++i) { oversized += "The "; }
    bool rejected = false;
    try { pipeline.prepare(oversized,73); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Resident token capacity not enforced");
    const auto rng_directory = directory.parent_path() / "rng";
    const detail::MappedFile rng(rng_directory / "rng-reference.bin",662000);
    require(detail::hex(detail::sha256(rng.bytes())) == "8dd4f21adbbc1f80260eee091eda129435c49fe45997bf31dcdd6994deb9872b", "RNG reference identity differs");
    std::vector<float> pcm(326400), first(326400);
    std::uint32_t seen = 0, segment = 0, local_candidate = 0;
    std::atomic<std::uint32_t> produced{0};
    const GenerationObserver observer = [&](const GenerationCall& step) {
        require(seen < 176 && std::memcmp(step.noise.data(),rng.bytes().data()+std::size_t(seen)*128,128) == 0, "Continuous noise bits differ");
        if (step.input_kind == GenerationInput::text) {
            segment = seen == 0 ? 0 : segment+1; local_candidate = 0; produced = 0;
            require(segment < 3 && seen == segment_oracles[segment].first_draw, "Prefill draw boundary differs");
            require(step.offset_before == 112 && step.offset_after == 112+segment_oracles[segment].tokens,
                    "Voice prefix was not restored");
        } else {
            require(step.index == local_candidate++, "Candidate sequence differs");
            require(step.emitted == (step.index < segment_oracles[segment].frames), "Emission timeline differs");
            require(step.latched_eos == (step.index < segment_oracles[segment].eos ? std::optional<std::uint32_t>{} :
                    std::optional<std::uint32_t>{segment_oracles[segment].eos}), "EOS latch timeline differs");
            produced.fetch_add(1);
        }
        ++seen;
    };
    std::array<UtteranceResult,2> results;
    std::uint32_t first_candidates = 0;
    for (unsigned repetition = 0; repetition < 2; ++repetition) {
        pipeline.prepare(canonical,73);
        const auto& text = pipeline.text();
        require(text.full_token_ids.size() == 75 && text.segments.size() == 3, "Full text token/split count differs");
        require(std::equal(full_ids.begin(),full_ids.end(),text.full_token_ids.begin()), "Full input token IDs differ");
        const std::array<std::span<const std::uint32_t>,3> ids{segment_ids_0,segment_ids_1,segment_ids_2};
        for (std::size_t i = 0; i < 3; ++i) {
            require(text.segments[i].text == segment_oracles[i].text && text.segments[i].token_ids.size() == ids[i].size() &&
                std::equal(ids[i].begin(),ids[i].end(),text.segments[i].token_ids.begin()), "Exact segment tokens/text differ");
        }
        seen = segment = local_candidate = 0; produced = 0;
        Sink sink; sink.pcm = pcm; sink.produced = &produced;
        results[repetition] = pipeline.run(Sink::frame,&sink,&observer);
        validate(results[repetition]); require(seen == 176 && sink.frames == 170 && sink.first_candidates < 47, "Output was deferred");
        first_candidates = sink.first_candidates;
        require(std::all_of(pcm.begin(),pcm.end(),[](float x) { return std::isfinite(x); }), "Nonfinite PCM");
        if (!repetition) {
            first = pcm;
            // Short sink failure only, then B proves clean reset/reseed recovery.
            pipeline.prepare(canonical,73); Sink failed; failed.fail = true;
            bool propagated = false;
            try { (void)pipeline.run(Sink::frame,&failed); } catch (const std::runtime_error&) { propagated = true; }
            require(propagated && failed.frames == 0, "Utterance sink failure lost");
            bool no_success = false;
            try { (void)pipeline.run(Sink::frame,&failed); } catch (const std::logic_error&) { no_success = true; }
            require(no_success, "Failed utterance remained runnable");
        } else { require(std::memcmp(pcm.data(),first.data(),pcm.size()*4) == 0, "Resident utterance reuse changed PCM bits"); }
    }
    const auto hash = detail::hex(detail::sha256({reinterpret_cast<const std::uint8_t*>(pcm.data()),pcm.size()*4}));
    std::ofstream report(directory/(std::string("2l-correctness-")+path+".json"));
    report << std::setprecision(17) << "{\"exact_tokens_and_segments\":true,\"exact_176_noise_rows\":true,\"eos_and_counts_match\":true,"
        "\"deterministic_reuse\":true,\"sink_error_recovery\":true,\"first_frame_candidate_count\":" << first_candidates
        << ",\"pcm_sha256\":\"" << hash << "\",\"requests\":[";
    result_json(report,results[0]); report << ','; result_json(report,results[1]); report << "]}\n";
    require(bool(report), "Cannot write correctness report");
    if (std::string_view(path) == "avx2") {
        development::write_pcm16_wave(directory.parent_path()/"audio/native-full-canonical-avx2.wav",pcm);
        std::ofstream raw(directory.parent_path()/"orchestration/native-full-canonical-avx2.f32",std::ios::binary);
        raw.write(reinterpret_cast<const char*>(pcm.data()),std::streamsize(pcm.size()*4)); require(bool(raw),"Cannot write native development PCM");
    }
    std::cout << "PASS: full tokens/splits, continuous RNG, EOS/counts, immediate streaming, A/B bit equality and sink error recovery; SHA " << hash << '\n';
    cancellation_checks(pipeline,first,directory);
}
#endif
}
int wmain(int argc, wchar_t** argv) {
    try {
        const bool portable = argc >= 2 && std::wstring_view(argv[1]) == L"portable";
        require(argc <= 3 && (argc < 2 || portable || std::wstring_view(argv[1]) == L"avx2"), "Usage: outloud-utterance-check|synth [avx2|portable] [bench]");
        const auto path = portable ? cpu::KernelPath::portable : cpu::KernelPath::avx2_fma;
        const char* suffix = portable ? "portable" : "avx2";
        const std::filesystem::path assets = OUTLOUD_ASSET_DIRECTORY;
        const PocketModelAsset model(assets/"pocket-english.olp");
        const OutLoudVoice voice(assets/"gnome_male.olv");
        const Tokenizer tokenizer(OUTLOUD_TOKENIZER_ASSET);
        // Fifty is a soft splitter limit; reserve 51 tokens to cover the edge
        // probe. Larger unsplittable segments require explicit larger capacity.
        UtterancePipeline pipeline(model,voice,tokenizer,51,FlowLMGenerator::estimate_max_gen_len(51),path);
        const auto directory = assets.parent_path()/"perf"; std::filesystem::create_directories(directory);
#ifdef OUTLOUD_UTTERANCE_CHECK
        require(argc <= 2, "Correctness checker has no benchmark mode");
        correctness(pipeline,directory,suffix);
#else
        const bool benchmark = argc == 3 && std::wstring_view(argv[2]) == L"bench";
        require(argc < 3 || benchmark, "Unknown synthesis mode");
        auto request = [&] {
            pipeline.prepare(canonical,73); Sink sink;
            const auto r = pipeline.run(Sink::frame,&sink); validate(r);
            require(sink.frames == 170, "Streaming frame count differs"); return r;
        };
        if (benchmark) { (void)request(); }
        std::array<UtteranceResult,3> runs;
        const std::size_t repetitions = benchmark ? 3 : 1;
        for (std::size_t i = 0; i < repetitions; ++i) {
            runs[i] = request();
            std::cout << "Full native first/total ms " << runs[i].first_pcm_ms << ' ' << runs[i].total_ms << '\n';
        }
        const auto storage = pipeline.storage();
        std::ofstream report(directory/(std::string("2l-")+(benchmark ? "benchmark-" : "synthesis-")+suffix+".json"));
        report << std::setprecision(17) << "{\"warmups\":" << (benchmark ? 1 : 0) << ",\"timed_repetitions\":" << repetitions
            << ",\"runtime_fixtures_used\":false,\"production_pcm_buffer_bytes\":0,\"orchestration_object_bytes\":" << storage.object_bytes
            << ",\"worker_inline_bytes\":" << storage.worker_inline_bytes << ",\"text_descriptor_capacity_bytes\":" << storage.text_descriptor_capacity_bytes
            << ",\"text_token_capacity_bytes\":" << storage.text_token_capacity_bytes << ",\"text_string_capacity_bytes\":" << storage.text_string_capacity_bytes
            << ",\"segment_result_capacity_bytes\":" << storage.segment_result_capacity_bytes << ",\"runs\":[";
        for (std::size_t i = 0; i < repetitions; ++i) { if (i) { report << ','; } result_json(report,runs[i]); }
        report << "]}\n"; require(bool(report),"Cannot write synthesis report");
        std::cout << "PASS: fixture-free full-text API, sink-only 170 frames / 326400 samples.\n";
#endif
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
