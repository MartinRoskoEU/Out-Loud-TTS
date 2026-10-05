#pragma once
#include "outloud/cpu/mimi_kernels.hpp"
#include <functional>

namespace outloud {
using MimiObserver = std::function<void(std::string_view, std::span<const float>)>;
class MimiDecoderState {
public:
    explicit MimiDecoderState(std::uint32_t maximum_frames);
    void reset();
    [[nodiscard]] bool valid() const { return valid_; }
    [[nodiscard]] std::uint32_t offset() const { return offset_; }
    [[nodiscard]] std::uint32_t capacity() const { return capacity_; }
    [[nodiscard]] std::span<const float> keys(std::size_t layer) const;
    [[nodiscard]] std::span<const float> values(std::size_t layer) const;
    [[nodiscard]] std::span<const float> convolution_state(std::size_t configuration) const;
    [[nodiscard]] std::size_t kv_bytes() const;
    [[nodiscard]] std::size_t convolution_bytes() const;
private:
    friend class MimiDecoder;
    std::array<std::vector<float>, 2> keys_, values_;
    std::array<std::vector<float>, 12> carry_;
    std::uint32_t capacity_, offset_ = 0;
    bool valid_ = true, busy_ = false;
};

// Single caller; output view remains valid until the next decode on this context.
// Normalized latent in; unclamped FP32 mono 24 kHz PCM out. No frame coalescing.
class MimiDecoder {
public:
    explicit MimiDecoder(PocketModelAsset model, cpu::KernelPath path = cpu::KernelPath::portable);
    [[nodiscard]] std::span<const float> decode_frame(std::span<const float> normalized_latent,
                                                    MimiDecoderState& state, const MimiObserver& observer = {});
    [[nodiscard]] std::size_t scratch_bytes() const;
    [[nodiscard]] cpu::KernelPath selected_path() const { return kernels_.selected_path(); }
private:
    void transformer(MimiDecoderState& state, const MimiObserver& observer);
    PocketModelAsset model_;
    cpu::Kernels kernels_;
    cpu::MimiConvolutionKernels convolutions_;
    std::array<float, 32> denormalized_;
    std::array<float, 512> projection_;
    std::array<float, 1920> pcm_;
    std::vector<float> a_, b_, residual_, hidden_, norm_, packed_, queries_, keys_, values_, attention_, update_, ffn_;
    bool busy_ = false;
};
} // namespace outloud
