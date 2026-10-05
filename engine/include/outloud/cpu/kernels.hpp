#pragma once

#include "outloud/assets.hpp"

#include <array>
#include <span>
#include <string_view>

namespace outloud::cpu {
enum class KernelPath { portable, avx2_fma, automatic };
struct CpuFeatures { bool avx2, fma, os_avx_state; };
[[nodiscard]] CpuFeatures cpu_features();
[[nodiscard]] bool optimized_available();
[[nodiscard]] std::string_view path_name(KernelPath path);

// Row-major FP32 activations only; no tensor ownership or generic graph abstraction.
struct MatrixView { std::span<const float> data; std::size_t rows, columns; };
struct MutableMatrixView { std::span<float> data; std::size_t rows, columns; };

class Kernels {
public:
    explicit Kernels(KernelPath requested = KernelPath::portable);
    [[nodiscard]] KernelPath selected_path() const { return path_; }
    // Weights [output,input], activations [tokens,input], result [tokens,output].
    // Portable widens one row into fixed 16 KiB scratch; AVX2 widens in registers.
    void linear(MatrixView input, ModelTensorView weight, MutableMatrixView output,
                std::span<const std::uint16_t> bias = {});
    [[nodiscard]] float dot(std::span<const float> a, std::span<const float> b) const;
    // Mimi windows: reuse one FP32 weight row across independent time positions.
    void dot_rows(std::span<const float> weight, MatrixView input, std::span<float> output,
                  std::size_t output_stride, float bias = 0.0f) const;
    [[nodiscard]] static constexpr std::size_t widening_scratch_bytes() { return 4096 * sizeof(float); }
private:
    using Dot = float (*)(const float*, const float*, std::size_t);
    Dot dot_;
    KernelPath path_;
    std::array<float, 4096> widened_row_{};
};

void layer_norm(MatrixView input, std::span<const std::uint16_t> scale,
                std::span<const std::uint16_t> bias, MutableMatrixView output);
void gelu_tanh(std::span<float> values);
void silu(std::span<float> values);
// Pocket flow-MLP norms, distinct from transformer LayerNorm. Fixed width 512.
void sampler_layer_norm(std::span<const float> input, std::span<const std::uint16_t> scale,
                        std::span<const std::uint16_t> bias, std::span<float> output);
// Upstream "RMSNorm": no centering, sample variance (correction=1), epsilon 1e-5.
void sample_variance_norm(std::span<const float> input, std::span<const std::uint16_t> alpha,
                          std::span<float> output);
void time_features(float time, std::span<const std::uint16_t> stored_frequencies, std::span<float> output);
void residual_add(std::span<float> destination, std::span<const float> update);
// Interleaved adjacent complex coordinates, new [tokens,16,64] Q/K only.
void rope(std::span<float> queries, std::span<float> keys, std::size_t tokens, std::uint32_t offset);

struct AttentionCacheView {
    std::span<const float> prefix_keys, prefix_values; // immutable [prefix,16,64]
    std::span<const float> suffix_keys, suffix_values; // mutable-state view [suffix,16,64]
    std::uint32_t prefix_length, previous_suffix_length;
};
// Output [tokens,1024]. Prefix is already post-RoPE; key j visible iff j <= offset+q.
void causal_attention(const Kernels& kernels, std::span<const float> queries, std::size_t tokens,
                      AttentionCacheView cache, std::span<float> output, std::span<float> score_scratch);
[[nodiscard]] bool causal_visible(std::uint32_t query_absolute, std::uint32_t key_absolute) noexcept;
} // namespace outloud::cpu
