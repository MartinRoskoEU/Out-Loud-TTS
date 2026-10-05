#include "outloud/perf_diagnostics.hpp"
#include "outloud/cpu/kernels.hpp"

#include <cmath>
#include <stdexcept>

namespace outloud::cpu {
namespace {
void require(bool condition, const char* message) {
    if (!condition) { throw std::invalid_argument(message); }
}
float sum(const float* lanes) {
    return ((lanes[0] + lanes[1]) + (lanes[2] + lanes[3]))
         + ((lanes[4] + lanes[5]) + (lanes[6] + lanes[7]));
}
struct Moments { float mean, variance; };
Moments moments(std::span<const float> input, bool sample) {
    float lanes[8]{};
    for (std::size_t i = 0; i < 512; i += 8) {
        for (std::size_t lane = 0; lane < 8; ++lane) { lanes[lane] += input[i + lane]; }
    }
    const float mean = sum(lanes) / 512.0f;
    float squared[8]{};
    for (std::size_t i = 0; i < 512; i += 8) {
        for (std::size_t lane = 0; lane < 8; ++lane) {
            const float delta = input[i + lane] - mean; squared[lane] += delta * delta;
        }
    }
    return {mean, sum(squared) / (sample ? 511.0f : 512.0f)};
}
} // namespace
void silu(std::span<float> values) {
    OUTLOUD_PERF_SCOPE(::outloud::perf::Stage::silu);
    for (auto& x : values) { x = x / (1.0f + std::exp(-x)); }
}
void sampler_layer_norm(std::span<const float> input, std::span<const std::uint16_t> scale,
                        std::span<const std::uint16_t> bias, std::span<float> output) {
    OUTLOUD_PERF_SCOPE(::outloud::perf::Stage::normalization);
    require(input.size() == 512 && output.size() == 512 &&
            ((scale.empty() && bias.empty()) || (scale.size() == 512 && bias.size() == 512)),
            "Sampler LayerNorm dimensions mismatch");
    const auto stats = moments(input, false);
    const float denominator = std::sqrt(stats.variance + 1e-6f);
    for (std::size_t i = 0; i < 512; ++i) {
        float value = (input[i] - stats.mean) / denominator;
        if (!scale.empty()) { value = value * bf16_to_float32(scale[i]) + bf16_to_float32(bias[i]); }
        output[i] = value;
    }
}
void sample_variance_norm(std::span<const float> input, std::span<const std::uint16_t> alpha,
                          std::span<float> output) {
    OUTLOUD_PERF_SCOPE(::outloud::perf::Stage::normalization);
    require(input.size() == 512 && output.size() == 512 && alpha.size() == 512,
            "Sample-variance norm dimensions mismatch");
    const auto stats = moments(input, true);
    const float inverse_std = 1.0f / std::sqrt(stats.variance + 1e-5f);
    for (std::size_t i = 0; i < 512; ++i) {
        output[i] = input[i] * (bf16_to_float32(alpha[i]) * inverse_std);
    }
}
void time_features(float time, std::span<const std::uint16_t> stored_frequencies, std::span<float> output) {
    OUTLOUD_PERF_SCOPE(::outloud::perf::Stage::time_features);
    require(std::isfinite(time) && stored_frequencies.size() == 128 && output.size() == 256,
            "Time feature dimensions/value mismatch");
    for (std::size_t i = 0; i < 128; ++i) {
        const float argument = time * bf16_to_float32(stored_frequencies[i]);
        output[i] = std::cos(argument); output[128 + i] = std::sin(argument);
    }
}
} // namespace outloud::cpu
