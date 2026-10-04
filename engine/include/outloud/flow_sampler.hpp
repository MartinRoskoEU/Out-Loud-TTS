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
    [[nodiscard]] cpu::KernelPath selected_path() const { return kernels_.selected_path(); }
    [[nodiscard]] static constexpr std::size_t activation_scratch_bytes() { return (8 * 512 + 1536 + 256) * sizeof(float); }
private:
    PocketModelAsset model_;
    cpu::Kernels kernels_; // reuses its one-row widening buffer for all sampler linears
};
} // namespace outloud
