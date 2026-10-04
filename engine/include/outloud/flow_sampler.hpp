#pragma once

#include "outloud/flowlm.hpp"

namespace outloud {
using SamplerObserver = std::function<void(std::string_view, std::span<const float>)>;
struct FlowSample {
    std::array<float, 32> velocity, latent; // normalized latent = supplied noise + velocity
};
struct DeterministicCandidate { FlowLMResult backbone; FlowSample sample; };

// Fixed batch-1, s=0, t=1, one-step LSD. Noise is already scaled and supplied by caller.
class FlowSampler {
public:
    explicit FlowSampler(PocketModelAsset model, cpu::KernelPath path = cpu::KernelPath::portable);
    [[nodiscard]] FlowSample sample(std::span<const float> hidden, std::span<const float> supplied_noise,
                                    const SamplerObserver& observer = {});
    [[nodiscard]] DeterministicCandidate bos_candidate(FlowLMBackbone& backbone, FlowLMPrefillState& state,
        std::span<const float> supplied_noise, const SamplerObserver& observer = {});
    [[nodiscard]] DeterministicCandidate frame_candidate(FlowLMBackbone& backbone, FlowLMPrefillState& state,
        std::span<const float> previous_normalized_latent, std::span<const float> supplied_noise,
        const SamplerObserver& observer = {});
    void bos_candidate_into(FlowLMBackbone& backbone, FlowLMPrefillState& state, std::span<const float> noise,
                            DeterministicCandidate& result);
    void frame_candidate_into(FlowLMBackbone& backbone, FlowLMPrefillState& state, std::span<const float> previous,
                              std::span<const float> noise, DeterministicCandidate& result);
    [[nodiscard]] cpu::KernelPath selected_path() const { return kernels_.selected_path(); }
    [[nodiscard]] static constexpr std::size_t activation_scratch_bytes() { return (8 * 512 + 1536 + 256) * sizeof(float); }
private:
    void candidate_into(FlowLMBackbone& backbone, FlowLMPrefillState& state, std::span<const float> input,
                        std::span<const float> noise, DeterministicCandidate& result, const SamplerObserver& observer);
    PocketModelAsset model_;
    cpu::Kernels kernels_; // reuses its one-row widening buffer for all sampler linears
};
} // namespace outloud
