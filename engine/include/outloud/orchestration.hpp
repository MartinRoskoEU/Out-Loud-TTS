#pragma once
#include "outloud/text_pipeline.hpp"
#include "outloud/worker_pipeline.hpp"

namespace outloud {
// Consumer-thread callback; PCM expires on return. Indices are zero based.
// One latent produces one 1920-sample frame. No reentry or concurrent API use.
using UtterancePCMSink = void (*)(void*, std::uint64_t utterance_frame,
                                std::size_t segment, std::uint32_t local_frame,
                                std::span<const float> pcm);
struct SegmentResult {
    std::uint32_t tokens = 0;
    std::uint64_t first_draw = 0;
    WorkerResult worker;
    double wall_ms = 0; // segment prepare/reset plus worker run
};
struct UtteranceResult {
    std::vector<SegmentResult> segments;
    std::uint64_t draws = 0, candidates = 0, frames = 0, samples = 0;
    double text_preparation_ms = 0, first_pcm_ms = 0, total_ms = 0;
};
struct OrchestrationStorage {
    std::size_t object_bytes = 0, worker_inline_bytes = 0;
    std::size_t text_descriptor_capacity_bytes = 0, text_token_capacity_bytes = 0, text_string_capacity_bytes = 0;
    std::size_t segment_result_capacity_bytes = 0;
};
// One coordinator, resident model/voice mappings and two persistent workers.
// The immutable tokenizer is borrowed and must outlive this object. Capacities
// bound a single prepared segment, not the number of segments in an utterance.
class UtterancePipeline {
public:
    UtterancePipeline(PocketModelAsset model, OutLoudVoice voice, const Tokenizer& tokenizer,
                      std::uint32_t maximum_segment_tokens, std::uint32_t maximum_segment_frames,
                      cpu::KernelPath path = cpu::KernelPath::portable);
    ~UtterancePipeline();
    UtterancePipeline(const UtterancePipeline&) = delete;
    UtterancePipeline& operator=(const UtterancePipeline&) = delete;
    // Native process_text with the verified default soft split limit of 50.
    // Validates every segment capacity before streaming any PCM; no neural job.
    void prepare(std::string_view utf8, std::uint64_t seed);
    // Reseed once, then retain one continuous RNG across segment reset jobs.
    // No PCM buffer is owned or required. Sink/context must survive this call.
    // Optional observer is development-only, called on the producer per draw.
    [[nodiscard]] UtteranceResult run(UtterancePCMSink sink, void* context = nullptr,
                                     const GenerationObserver* observer = nullptr);
    [[nodiscard]] const TextPipelineResult& text() const;
    [[nodiscard]] OrchestrationStorage storage() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace outloud
