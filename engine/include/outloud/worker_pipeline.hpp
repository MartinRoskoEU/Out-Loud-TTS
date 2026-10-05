#pragma once
#include "outloud/generation.hpp"
#include "outloud/mimi.hpp"
#include <memory>
#include <ostream>

namespace outloud {
// Invoked on the consumer immediately after copying one frame to the destination.
// The destination view remains valid; callback/context must outlive run().
using PCMFrameSink = void (*)(void*, std::uint32_t, std::span<const float>);
struct WorkerResult {
    std::uint32_t candidates = 0, emitted = 0, draws = 0, samples = 0;
    std::uint32_t flow_offset = 0, mimi_offset = 0;
    std::optional<std::uint32_t> first_eos;
    GenerationTermination termination = GenerationTermination::maximum_length;
    double first_pcm_ms = 0, total_ms = 0;
    std::uint64_t hot_allocations = 0; // populated by development audit build only
};
// Single coordinator, one prepared segment at a time. Two persistent workers.
// Model/voice mappings are shared read-only. No API is reentrant or concurrent
// with prepare/run/destruction; sinks/observers must not call back into this API.
class WorkerPipeline {
public:
    WorkerPipeline(PocketModelAsset model, OutLoudVoice voice, std::uint32_t maximum_text_tokens,
                   std::uint32_t maximum_frames, cpu::KernelPath path = cpu::KernelPath::portable);
    ~WorkerPipeline();
    WorkerPipeline(const WorkerPipeline&) = delete;
    WorkerPipeline& operator=(const WorkerPipeline&) = delete;
    // Reset while workers are idle, before the measured dispatch/prefill boundary.
    // Borrowed inputs and destination must remain valid until run returns.
    void prepare(std::span<const std::uint32_t> tokens, NoiseSource& noise, std::span<float> destination,
                 const GenerationOptions& options = {}, PCMFrameSink sink = nullptr, void* context = nullptr,
                 const GenerationObserver* observer = nullptr);
    // Production mode: the producer reseeds its owned RNG during the untimed
    // reset job. The generation/queue/decoder path is shared with supplied noise.
    void prepare(std::span<const std::uint32_t> tokens, std::uint64_t seed, std::span<float> destination,
                 const GenerationOptions& options = {}, PCMFrameSink sink = nullptr, void* context = nullptr,
                 const GenerationObserver* observer = nullptr);
    [[nodiscard]] WorkerResult run(); // joins request completion, not OS threads
    // Completed state views are valid until the next prepare; diagnostic use only.
    [[nodiscard]] const FlowLMPrefillState& flow_state() const;
    [[nodiscard]] const MimiDecoderState& mimi_state() const;
    [[nodiscard]] std::size_t backbone_workspace_bytes() const;
    [[nodiscard]] std::size_t mimi_scratch_bytes() const;
    [[nodiscard]] std::size_t object_storage_bytes() const; // inline contexts/control, not heap backing or OS stacks
#ifdef OUTLOUD_WORKER_TRACE
    void write_trace(std::ostream& output) const;
#endif
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace outloud
