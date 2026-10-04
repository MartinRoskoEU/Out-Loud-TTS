#pragma once

#include "outloud/cpu/kernels.hpp"

#include <functional>

namespace outloud {
using FlowLMObserver = std::function<void(std::string_view, std::span<const float>, std::span<const std::uint32_t>)>;

struct FlowLMLayerSuffix {
    std::span<const float> keys, values; // appended [positions,16,64], prefix is separate
    std::uint32_t offset;
};

class FlowLMPrefillState {
public:
    void reset();
    void invalidate() noexcept { valid_ = false; }
    [[nodiscard]] FlowLMLayerSuffix layer(std::uint32_t index) const;
    [[nodiscard]] std::uint32_t offset() const;
    [[nodiscard]] std::size_t suffix_storage_bytes() const;
    [[nodiscard]] std::uint32_t capacity() const { return capacity_; }
private:
    friend class FlowLMBackbone;
    FlowLMPrefillState(OutLoudVoice voice, std::uint32_t capacity);
    struct Layer { std::vector<float> keys, values; std::uint32_t offset; };
    OutLoudVoice voice_; // shares the original immutable mapping, no 5.5 MB prefix copy
    std::string voice_sha_, compatibility_;
    std::uint32_t capacity_;
    std::array<Layer, 6> layers_;
    bool valid_ = true;
};

struct FlowLMResult {
    std::vector<float> normalized; // [positions,1024]; last row is the hidden vector
    float eos_logit;
    std::size_t scratch_bytes; // activation/score storage + context's row buffer; excludes result/state
    [[nodiscard]] std::span<const float> last_hidden() const;
};

// Fixed deterministic six-layer text/backbone computation. No sampler or generation loop.
class FlowLMBackbone {
public:
    FlowLMBackbone(PocketModelAsset model, OutLoudVoice voice,
                   cpu::KernelPath path = cpu::KernelPath::portable);
    [[nodiscard]] FlowLMPrefillState make_state(std::uint32_t suffix_capacity) const;
    [[nodiscard]] std::vector<float> embed(std::span<const std::uint32_t> tokens) const;
    [[nodiscard]] FlowLMResult prefill(std::span<const std::uint32_t> tokens, FlowLMPrefillState& state,
                                       const FlowLMObserver& observer = {});
    // Also permits one supplied deterministic position for a standalone backbone timing probe.
    [[nodiscard]] FlowLMResult backbone(std::span<const float> embeddings, FlowLMPrefillState& state,
                                        const FlowLMObserver& observer = {});
    // Reuses output allocation and this single-threaded context's activation workspace.
    void backbone_into(std::span<const float> embeddings, FlowLMPrefillState& state, FlowLMResult& result,
                       const FlowLMObserver& observer = {});
    [[nodiscard]] std::size_t workspace_storage_bytes() const;
    [[nodiscard]] cpu::KernelPath selected_path() const { return kernels_.selected_path(); }
private:
    [[nodiscard]] ModelTensorView layer_weight(std::uint32_t layer, std::uint32_t field) const;
    PocketModelAsset model_;
    OutLoudVoice voice_;
    std::string voice_sha_;
    cpu::Kernels kernels_;
    struct Workspace { std::vector<float> hidden, norm, packed, queries, attention, update, ffn, scores; } work_;
};
} // namespace outloud
