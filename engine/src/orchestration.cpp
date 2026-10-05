#include "outloud/orchestration.hpp"
#include "outloud/production_noise.hpp"
#include "cancellation_audit.hpp"
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace outloud {
namespace {
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end-start).count();
}
}
struct UtterancePipeline::Impl {
    const Tokenizer& tokenizer;
    WorkerPipeline workers;
    ProductionNoiseSource noise{0};
    const std::uint32_t maximum_tokens, maximum_frames;
    TextPipelineResult text;
    std::vector<SegmentResult> results;
    std::uint64_t seed = 0, frame_base = 0;
    std::size_t segment = 0;
    bool prepared = false, running = false;
    double preparation_ms = 0, first_pcm_ms = 0;
    Clock::time_point start;
    UtterancePCMSink sink = nullptr;
    void* context = nullptr;
    std::stop_token cancellation;
    Impl(PocketModelAsset model, OutLoudVoice voice, const Tokenizer& tok,
         std::uint32_t tokens, std::uint32_t frames, cpu::KernelPath path)
        : tokenizer(tok), workers(model, voice, tokens, frames, path),
          maximum_tokens(tokens), maximum_frames(frames) {}
    static void frame(void* context, std::uint32_t local, std::span<const float> pcm) {
        auto& p = *static_cast<Impl*>(context);
        if (p.cancellation.stop_requested()) { return; }
        if (p.frame_base == 0 && local == 0) { p.first_pcm_ms = elapsed(p.start, Clock::now()); }
        p.sink(p.context, p.frame_base+local, p.segment, local, pcm);
    }
};
UtterancePipeline::UtterancePipeline(PocketModelAsset model, OutLoudVoice voice, const Tokenizer& tokenizer,
        std::uint32_t tokens, std::uint32_t frames, cpu::KernelPath path)
    : impl_(std::make_unique<Impl>(model, voice, tokenizer, tokens, frames, path)) {}
UtterancePipeline::~UtterancePipeline() = default;
void UtterancePipeline::prepare(std::string_view utf8, std::uint64_t seed) {
    auto& p = *impl_;
    if (p.running) { throw std::logic_error("Utterance is already running"); }
    p.prepared = false;
    const auto start = Clock::now();
    auto text = process_text(utf8, p.tokenizer);
    if (text.segments.empty()) { throw std::invalid_argument("Text has no generation segments"); }
    for (const auto& s : text.segments) {
        if (s.token_ids.empty() || s.token_ids.size() > p.maximum_tokens ||
            FlowLMGenerator::estimate_max_gen_len(s.token_ids.size()) > p.maximum_frames) {
            throw std::invalid_argument("Text segment exceeds configured resident capacity");
        }
    }
    p.text = std::move(text);
    p.results.resize(p.text.segments.size());
    p.seed = seed;
    p.preparation_ms = elapsed(start, Clock::now());
    p.prepared = true;
}
UtteranceResult UtterancePipeline::run(UtterancePCMSink sink, void* context, const GenerationObserver* observer,
        std::stop_token cancellation) {
    auto& p = *impl_;
    if (!p.prepared || p.running || !sink) { throw std::logic_error("Utterance needs prepare and a PCM sink"); }
    p.start = Clock::now();
    p.prepared = false; p.running = true;
    p.sink = sink; p.context = context; p.frame_base = 0; p.first_pcm_ms = 0;
    p.cancellation = cancellation;
    p.noise.reseed(p.seed); // coordinator only while producer idle; once per utterance
    UtteranceResult result;
    std::size_t recorded = 0;
    try {
        for (p.segment = 0; p.segment < p.text.segments.size(); ++p.segment) {
            if (p.segment) { OUTLOUD_CANCEL_POINT(segment_boundary,true); }
            if (cancellation.stop_requested()) { break; }
            const auto& s = p.text.segments[p.segment];
            const auto begin = Clock::now();
            const auto draw = p.noise.consumed();
            GenerationOptions options; options.frames_after_eos = s.frames_after_eos;
            p.workers.prepare(s.token_ids, p.noise, {}, options, Impl::frame, &p, observer);
            if (cancellation.stop_requested()) { break; }
            const auto worker = p.workers.run(cancellation);
            p.results[p.segment] = {static_cast<std::uint32_t>(s.token_ids.size()), draw, worker, elapsed(begin, Clock::now())};
            ++recorded;
            p.frame_base += worker.samples/1920;
            result.candidates += worker.candidates;
            result.samples += worker.samples;
            if (worker.status == SynthesisStatus::cancelled) { break; }
        }
        result.total_ms = elapsed(p.start, Clock::now());
        result.first_pcm_ms = p.first_pcm_ms;
        result.text_preparation_ms = p.preparation_ms;
        result.draws = p.noise.consumed(); result.frames = p.frame_base;
        result.status = cancellation.stop_requested() ? SynthesisStatus::cancelled : SynthesisStatus::completed;
        result.segments.assign(p.results.begin(),p.results.begin()+recorded); // outside compute timing
        p.running = false; p.sink = nullptr; p.context = nullptr;
        p.cancellation = {};
        return result;
    } catch (...) {
        // Worker run has joined both request completions before it rethrows.
        // No later segment is dispatched. A fresh prepare/reseed permits reuse.
        p.running = false; p.sink = nullptr; p.context = nullptr;
        p.cancellation = {};
        throw;
    }
}
const TextPipelineResult& UtterancePipeline::text() const {
    if (impl_->running) { throw std::logic_error("Utterance is running"); }
    return impl_->text;
}
OrchestrationStorage UtterancePipeline::storage() const {
    const auto& p = *impl_;
    if (p.running) { throw std::logic_error("Utterance is running"); }
    OrchestrationStorage s;
    s.object_bytes = sizeof(Impl)+sizeof(*this);
    s.worker_inline_bytes = p.workers.object_storage_bytes();
    s.text_descriptor_capacity_bytes = p.text.segments.capacity()*sizeof(TextSegment);
    s.text_token_capacity_bytes = p.text.full_token_ids.capacity()*sizeof(std::uint32_t);
    s.text_string_capacity_bytes = p.text.prepared_text.capacity()+1;
    for (const auto& segment : p.text.segments) {
        s.text_token_capacity_bytes += segment.token_ids.capacity()*sizeof(std::uint32_t);
        s.text_string_capacity_bytes += segment.text.capacity()+1;
    }
    s.segment_result_capacity_bytes = p.results.capacity()*sizeof(SegmentResult);
    return s;
}
} // namespace outloud
