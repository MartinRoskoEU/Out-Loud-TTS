#include "outloud/perf_diagnostics.hpp"
#include "outloud/cpu/kernels.hpp"
#include "dot.hpp"

#include <intrin.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace outloud::cpu {
namespace {
void require(bool condition, const char* message) {
    if (!condition) { throw std::invalid_argument(message); }
}
void matrix(std::size_t size, std::size_t rows, std::size_t columns) {
    require(rows > 0 && columns > 0 && columns <= 4096 && rows <= 4096 &&
            size == rows * columns, "Invalid CPU matrix dimensions");
}
float sum8(const float* lanes) {
    return ((lanes[0] + lanes[1]) + (lanes[2] + lanes[3]))
         + ((lanes[4] + lanes[5]) + (lanes[6] + lanes[7]));
}
} // namespace
namespace detail {
float portable_dot(const float* a, const float* b, std::size_t size) {
    float sums[8]{};
    std::size_t i = 0;
    for (; i + 8 <= size; i += 8) {
        for (std::size_t lane = 0; lane < 8; ++lane) { sums[lane] += a[i + lane] * b[i + lane]; }
    }
    float result = sum8(sums);
    for (; i < size; ++i) { result += a[i] * b[i]; }
    return result;
}
} // namespace detail

CpuFeatures cpu_features() {
    int registers[4]{};
    __cpuid(registers, 0);
    const auto maximum_leaf = registers[0];
    if (maximum_leaf < 1) { return {}; }
    __cpuidex(registers, 1, 0);
    const bool fma = (registers[2] & (1 << 12)) != 0;
    const bool avx = (registers[2] & (1 << 28)) != 0;
    const bool osxsave = (registers[2] & (1 << 27)) != 0;
    const bool os_avx_state = avx && osxsave && (_xgetbv(0) & 6) == 6;
    bool avx2 = false;
    if (maximum_leaf >= 7) { __cpuidex(registers, 7, 0); avx2 = (registers[1] & (1 << 5)) != 0; }
    return {avx2, fma, os_avx_state};
}
bool optimized_available() {
#ifdef OUTLOUD_HAS_AVX2
    const auto features = cpu_features();
    return features.avx2 && features.fma && features.os_avx_state;
#else
    return false;
#endif
}
std::string_view path_name(KernelPath path) {
    switch (path) {
    case KernelPath::portable: return "portable";
    case KernelPath::avx2_fma: return "AVX2/FMA";
    case KernelPath::automatic: return "automatic";
    }
    throw std::invalid_argument("Invalid CPU kernel path");
}
Kernels::Kernels(KernelPath requested) : dot_(detail::portable_dot), path_(KernelPath::portable) {
    (void)path_name(requested);
    if (requested == KernelPath::portable) { return; }
    if (!optimized_available()) {
        if (requested == KernelPath::avx2_fma) { throw std::invalid_argument("AVX2/FMA path unavailable"); }
        return;
    }
#ifdef OUTLOUD_HAS_AVX2
    dot_ = detail::avx2_fma_dot;
    path_ = KernelPath::avx2_fma;
#endif
}
float Kernels::dot(std::span<const float> a, std::span<const float> b) const {
    require(a.size() == b.size(), "Dot product dimension mismatch");
    return dot_(a.data(), b.data(), a.size());
}
void Kernels::dot_rows(std::span<const float> weight, MatrixView input, std::span<float> output,
                        std::size_t output_stride, float bias) const {
    matrix(input.data.size(), input.rows, input.columns);
    require(weight.size() == input.columns && !output.empty() && output_stride > 0 &&
            input.rows - 1 <= (output.size() - 1) / output_stride, "Mimi row-dot dimensions mismatch");
#ifdef OUTLOUD_HAS_AVX2
    if (path_ == KernelPath::avx2_fma) {
        detail::avx2_fma_dot_rows(weight.data(), input.data.data(), output.data(), input.rows, input.columns, output_stride, bias);
        return;
    }
#endif
    for (std::size_t row = 0; row < input.rows; ++row) {
        output[row * output_stride] = dot_(weight.data(), input.data.data() + row * input.columns, input.columns) + bias;
    }
}
void Kernels::linear(MatrixView input, ModelTensorView weight, MutableMatrixView output,
                     std::span<const std::uint16_t> bias) {
    OUTLOUD_PERF_SCOPE(::outloud::perf::linear_stage(static_cast<std::uint32_t>(weight.id)));
    matrix(input.data.size(), input.rows, input.columns);
    matrix(output.data.size(), output.rows, output.columns);
    require(weight.dtype == AssetDtype::bf16 && weight.dimensions.size() == 2 &&
            weight.dimensions[0] == output.columns && weight.dimensions[1] == input.columns &&
            weight.bf16_values.size() == output.columns * input.columns && output.rows == input.rows &&
            (bias.empty() || bias.size() == output.columns), "Linear weight/activation dimensions mismatch");
    require(input.data.data() != output.data.data(), "Linear input/output cannot alias");
#ifdef OUTLOUD_HAS_AVX2
    if (path_ == KernelPath::avx2_fma) {
        detail::avx2_bf16_linear(input.data.data(), weight.bf16_values.data(), output.data.data(),
                                input.rows, input.columns, output.columns, bias.empty() ? nullptr : bias.data());
        return;
    }
#endif
    const auto row = std::span(widened_row_).first(input.columns);
    for (std::size_t out = 0; out < output.columns; ++out) {
        widen_bf16(weight.bf16_values.subspan(out * input.columns, input.columns), row);
        const float add = bias.empty() ? 0.0f : bf16_to_float32(bias[out]);
        for (std::size_t token = 0; token < input.rows; ++token) {
            output.data[token * output.columns + out] =
                dot_(input.data.data() + token * input.columns, row.data(), input.columns) + add;
        }
    }
}

void layer_norm(MatrixView input, std::span<const std::uint16_t> scale,
                std::span<const std::uint16_t> bias, MutableMatrixView output) {
    OUTLOUD_PERF_SCOPE(::outloud::perf::Stage::normalization);
    matrix(input.data.size(), input.rows, input.columns);
    matrix(output.data.size(), output.rows, output.columns);
    require(input.rows == output.rows && input.columns == output.columns &&
            scale.size() == input.columns && bias.size() == input.columns && input.columns % 8 == 0,
            "LayerNorm dimensions mismatch");
    const float inverse_width = 1.0f / static_cast<float>(input.columns);
    for (std::size_t token = 0; token < input.rows; ++token) {
        const auto x = input.data.subspan(token * input.columns, input.columns);
        float sums[8]{};
        for (std::size_t i = 0; i < x.size(); i += 8) {
            for (std::size_t lane = 0; lane < 8; ++lane) { sums[lane] += x[i + lane]; }
        }
        const float mean = sum8(sums) * inverse_width;
        float squared[8]{};
        for (std::size_t i = 0; i < x.size(); i += 8) {
            for (std::size_t lane = 0; lane < 8; ++lane) {
                const float delta = x[i + lane] - mean; squared[lane] += delta * delta;
            }
        }
        const float inverse_std = 1.0f / std::sqrt(sum8(squared) * inverse_width + 1e-5f);
        for (std::size_t i = 0; i < x.size(); ++i) {
            output.data[token * input.columns + i] =
                ((x[i] - mean) * inverse_std) * bf16_to_float32(scale[i]) + bf16_to_float32(bias[i]);
        }
    }
}
void gelu_tanh(std::span<float> values) {
    OUTLOUD_PERF_SCOPE(::outloud::perf::Stage::gelu);
    constexpr float sqrt_two_over_pi = 0.7978845608028654f;
    for (auto& x : values) {
        const float cube = (x * x) * x;
        x = (0.5f * x) * (1.0f + std::tanh(sqrt_two_over_pi * (x + 0.044715f * cube)));
    }
}
void residual_add(std::span<float> destination, std::span<const float> update) {
    OUTLOUD_PERF_SCOPE(::outloud::perf::Stage::residual);
    require(destination.size() == update.size(), "Residual dimension mismatch");
    for (std::size_t i = 0; i < destination.size(); ++i) { destination[i] += update[i]; }
}
void rope(std::span<float> queries, std::span<float> keys, std::size_t tokens, std::uint32_t offset) {
    OUTLOUD_PERF_SCOPE(::outloud::perf::Stage::rope);
    require(tokens > 0 && tokens <= 4096 && queries.size() == tokens * 1024 && keys.size() == queries.size() &&
            offset <= std::numeric_limits<std::uint32_t>::max() - tokens, "RoPE dimensions/positions mismatch");
    // PyTorch multiplies arange(float32) by this Python-double scalar cast to FP32.
    const float exponent_scale = static_cast<float>(-std::log(10000.0) * 2.0 / 64.0);
    std::array<float, 32> frequencies{};
    for (std::size_t pair = 0; pair < 32; ++pair) {
        frequencies[pair] = std::exp(static_cast<float>(pair) * exponent_scale);
    }
    for (std::size_t token = 0; token < tokens; ++token) {
        const float position = static_cast<float>(offset) + static_cast<float>(token);
        for (std::size_t pair = 0; pair < 32; ++pair) {
            const float angle = frequencies[pair] * position;
            const float cosine = std::cos(angle), sine = std::sin(angle);
            for (std::size_t head = 0; head < 16; ++head) {
                const auto i = token * 1024 + head * 64 + pair * 2;
                for (auto values : {queries, keys}) {
                    const float real = values[i], imaginary = values[i + 1];
                    values[i] = real * cosine - imaginary * sine;
                    values[i + 1] = real * sine + imaginary * cosine;
                }
            }
        }
    }
}
bool causal_visible(std::uint32_t query_absolute, std::uint32_t key_absolute) noexcept {
    return key_absolute <= query_absolute;
}
void causal_attention(const Kernels& kernels, std::span<const float> queries, std::size_t tokens,
                      AttentionCacheView cache, std::span<float> output, std::span<float> score_scratch) {
    OUTLOUD_PERF_SCOPE(::outloud::perf::Stage::attention);
    const std::size_t prefix = cache.prefix_length, previous = cache.previous_suffix_length;
    require(tokens > 0 && tokens <= 4096 && prefix > 0 && prefix <= 21845 && previous <= 4096 &&
            cache.prefix_keys.size() == prefix * 1024 && cache.prefix_values.size() == prefix * 1024 &&
            cache.suffix_keys.size() == (previous + tokens) * 1024 && cache.suffix_values.size() == cache.suffix_keys.size() &&
            queries.size() == tokens * 1024 && output.size() == queries.size() &&
            score_scratch.size() >= prefix + previous + tokens, "Attention cache/activation dimensions mismatch");
    auto cached = [&](std::span<const float> immutable, std::span<const float> suffix,
                      std::size_t position, std::size_t head) {
        return position < prefix ? immutable.subspan(position * 1024 + head * 64, 64)
                                 : suffix.subspan((position - prefix) * 1024 + head * 64, 64);
    };
    for (std::size_t token = 0; token < tokens; ++token) {
        const auto visible = prefix + previous + token + 1;
        for (std::size_t head = 0; head < 16; ++head) {
            const auto query = queries.subspan(token * 1024 + head * 64, 64);
            float maximum = -std::numeric_limits<float>::infinity();
            OUTLOUD_PERF_CALL(::outloud::perf::Stage::scores, {
            for (std::size_t position = 0; position < visible; ++position) {
                score_scratch[position] = kernels.dot(query, cached(cache.prefix_keys, cache.suffix_keys, position, head)) * 0.125f;
                maximum = std::max(maximum, score_scratch[position]);
            }
            });
            OUTLOUD_PERF_CALL(::outloud::perf::Stage::softmax, {
            float sum = 0.0f;
            for (std::size_t position = 0; position < visible; ++position) {
                score_scratch[position] = std::exp(score_scratch[position] - maximum); sum += score_scratch[position];
            }
            require(std::isfinite(sum) && sum > 0, "Nonfinite attention softmax");
            for (std::size_t position = 0; position < visible; ++position) { score_scratch[position] /= sum; }
            });
            const auto result = output.subspan(token * 1024 + head * 64, 64);
            std::fill(result.begin(), result.end(), 0.0f);
            OUTLOUD_PERF_CALL(::outloud::perf::Stage::values, {
            for (std::size_t position = 0; position < visible; ++position) {
                const auto value = cached(cache.prefix_values, cache.suffix_values, position, head);
                const float probability = score_scratch[position];
                for (std::size_t component = 0; component < 64; ++component) { result[component] += probability * value[component]; }
            }
            });
        }
    }
}
} // namespace outloud::cpu
