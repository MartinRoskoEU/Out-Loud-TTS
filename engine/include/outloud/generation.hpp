#pragma once

#include "outloud/flow_sampler.hpp"

#include <optional>

namespace outloud {
// Supplied, already-scaled FP32 noise. A span remains valid until the next draw.
class NoiseSource {
public:
    virtual ~NoiseSource() = default;
    [[nodiscard]] virtual std::span<const float> next() = 0;
    virtual void finish_segment() {} // a future unbounded source need not enforce fixture EOF
};
// Non-owning deterministic rows [draws,32]. No random number generation.
class FixtureNoiseSource final : public NoiseSource {
public:
    explicit FixtureNoiseSource(std::span<const float> rows);
    [[nodiscard]] std::span<const float> next() override;
    void finish_segment() override;
    [[nodiscard]] std::size_t consumed() const { return consumed_; }
private:
    std::span<const float> rows_;
    std::size_t consumed_ = 0;
};
struct GenerationOptions {
    std::uint32_t max_gen_len = 0; // zero selects upstream ceil((tokens/3 + 2) * 12.5)
    std::uint32_t frames_after_eos = 3;
    float eos_threshold = -4.0f;
};
class GenerationStopPolicy {
public:
    explicit GenerationStopPolicy(const GenerationOptions& options);
    // Called AFTER the candidate is computed/sampled and its cache position is consumed.
    [[nodiscard]] bool emit(std::uint32_t index, float eos_logit);
    [[nodiscard]] std::optional<std::uint32_t> first_eos() const { return first_eos_; }
private:
    float threshold_;
    std::uint32_t tail_;
    std::optional<std::uint32_t> first_eos_;
};
enum class GenerationInput { text, bos, previous_normalized_latent };
enum class GenerationTermination { eos_tail, maximum_length };
struct GenerationCall {
    // Spans and sample reference are borrowed only for the observer invocation.
    // Copy any data needed later; resident buffers can be overwritten on the next call.
    GenerationInput input_kind;
    std::uint32_t index, offset_before, offset_after;
    std::span<const float> input_latent, hidden, noise;
    const FlowSample& sample;
    float eos_logit;
    bool eos, emitted;
    std::optional<std::uint32_t> latched_eos;
};
using GenerationObserver = std::function<void(const GenerationCall&)>;
struct GenerationTiming {
    double prefill_ms = 0, candidates_ms = 0, total_ms = 0, min_candidate_ms = 0, max_candidate_ms = 0;
};
struct GenerationResult {
    std::vector<std::array<float, 32>> emitted;
    std::uint32_t candidate_count = 0, noise_draw_count = 0, prefill_offset = 0, final_offset = 0;
    std::optional<std::uint32_t> first_eos;
    GenerationTermination termination = GenerationTermination::maximum_length;
    GenerationTiming timing;
};
// One segment at a time, single-threaded; backbone/sampler/voice mappings remain resident.
class FlowLMGenerator {
public:
    FlowLMGenerator(FlowLMBackbone& backbone, FlowSampler& sampler) : backbone_(backbone), sampler_(sampler) {}
    [[nodiscard]] GenerationResult generate_segment(std::span<const std::uint32_t> tokens, NoiseSource& noise,
        const GenerationOptions& options = {}, const GenerationObserver& observer = {});
    void reset(); // required after ANY started generation failure
    [[nodiscard]] const FlowLMPrefillState& state() const;
    [[nodiscard]] static std::uint32_t estimate_max_gen_len(std::size_t token_count);
private:
    FlowLMBackbone& backbone_;
    FlowSampler& sampler_;
    std::optional<FlowLMPrefillState> state_;
    DeterministicCandidate candidate_;
    bool failed_ = false, running_ = false;
};
} // namespace outloud
