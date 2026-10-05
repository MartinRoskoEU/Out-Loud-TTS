#include "dot.hpp"
#include <immintrin.h>
#include <bit>

namespace outloud::cpu::detail {
namespace {
__m256 load_bf16(const std::uint16_t* row) {
    return _mm256_castsi256_ps(_mm256_slli_epi32(
        _mm256_cvtepu16_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(row))), 16));
}
float scalar_bf16(std::uint16_t value) { return std::bit_cast<float>(std::uint32_t(value) << 16); }
float reduce8(__m256 sum) {
    alignas(32) float lanes[8];
    _mm256_store_ps(lanes, sum);
    return ((lanes[0] + lanes[1]) + (lanes[2] + lanes[3]))
         + ((lanes[4] + lanes[5]) + (lanes[6] + lanes[7]));
}
float bf16_dot(const float* input, const std::uint16_t* row, std::size_t width) {
    auto sum = _mm256_setzero_ps();
    std::size_t i = 0;
    for (; i + 8 <= width; i += 8) { sum = _mm256_fmadd_ps(_mm256_loadu_ps(input + i), load_bf16(row + i), sum); }
    float value = reduce8(sum);
    for (; i < width; ++i) { value += input[i] * scalar_bf16(row[i]); }
    return value;
}
}
// This translation unit alone targets AVX2/FMA. Callers must dispatch before entry.
float avx2_fma_dot(const float* a, const float* b, std::size_t size) {
    auto sum = _mm256_setzero_ps();
    std::size_t i = 0;
    for (; i + 8 <= size; i += 8) {
        sum = _mm256_fmadd_ps(_mm256_loadu_ps(a + i), _mm256_loadu_ps(b + i), sum);
    }
    alignas(32) float lanes[8];
    _mm256_store_ps(lanes, sum);
    float result = ((lanes[0] + lanes[1]) + (lanes[2] + lanes[3]))
                 + ((lanes[4] + lanes[5]) + (lanes[6] + lanes[7]));
    for (; i < size; ++i) { result += a[i] * b[i]; }
    return result;
}
void avx2_bf16_linear(const float* input, const std::uint16_t* weight, float* output,
                      std::size_t tokens, std::size_t width, std::size_t outputs,
                      const std::uint16_t* bias) {
    // Four independent output rows reuse one activation load for single-token AR.
    if (tokens == 1) {
        std::size_t out = 0;
        for (; out + 4 <= outputs; out += 4) {
            const auto* w = weight + out * width;
            auto s0 = _mm256_setzero_ps(), s1 = s0, s2 = s0, s3 = s0;
            std::size_t i = 0;
            for (; i + 8 <= width; i += 8) {
                const auto x = _mm256_loadu_ps(input + i);
                s0 = _mm256_fmadd_ps(x, load_bf16(w + i), s0);
                s1 = _mm256_fmadd_ps(x, load_bf16(w + width + i), s1);
                s2 = _mm256_fmadd_ps(x, load_bf16(w + 2 * width + i), s2);
                s3 = _mm256_fmadd_ps(x, load_bf16(w + 3 * width + i), s3);
            }
            float values[4] = {reduce8(s0), reduce8(s1), reduce8(s2), reduce8(s3)};
            for (; i < width; ++i) {
                for (std::size_t r = 0; r < 4; ++r) { values[r] += input[i] * scalar_bf16(w[r * width + i]); }
            }
            for (std::size_t r = 0; r < 4; ++r) { output[out + r] = values[r] + (bias ? scalar_bf16(bias[out + r]) : 0.0f); }
        }
        for (; out < outputs; ++out) { output[out] = bf16_dot(input, weight + out * width, width) + (bias ? scalar_bf16(bias[out]) : 0.0f); }
        return;
    }
    // Four positions share each widened vector; covers Mimi's 16 and text's 20.
    for (std::size_t out = 0; out < outputs; ++out) {
        const auto* w = weight + out * width;
        const float add = bias ? scalar_bf16(bias[out]) : 0.0f;
        std::size_t token = 0;
        for (; token + 4 <= tokens; token += 4) {
            const auto* x = input + token * width;
            auto s0 = _mm256_setzero_ps(), s1 = s0, s2 = s0, s3 = s0;
            std::size_t i = 0;
            for (; i + 8 <= width; i += 8) {
                const auto wv = load_bf16(w + i);
                s0 = _mm256_fmadd_ps(_mm256_loadu_ps(x + i), wv, s0);
                s1 = _mm256_fmadd_ps(_mm256_loadu_ps(x + width + i), wv, s1);
                s2 = _mm256_fmadd_ps(_mm256_loadu_ps(x + 2 * width + i), wv, s2);
                s3 = _mm256_fmadd_ps(_mm256_loadu_ps(x + 3 * width + i), wv, s3);
            }
            float values[4] = {reduce8(s0), reduce8(s1), reduce8(s2), reduce8(s3)};
            for (; i < width; ++i) {
                const float value = scalar_bf16(w[i]);
                for (std::size_t r = 0; r < 4; ++r) { values[r] += x[r * width + i] * value; }
            }
            for (std::size_t r = 0; r < 4; ++r) { output[(token + r) * outputs + out] = values[r] + add; }
        }
        for (; token < tokens; ++token) {
            output[token * outputs + out] = bf16_dot(input + token * width, w, width) + add;
        }
    }
}
void avx2_fma_dot_rows(const float* weight, const float* input, float* output,
                        std::size_t rows, std::size_t width, std::size_t output_stride, float bias) {
    std::size_t row = 0;
    for (; row + 4 <= rows; row += 4) {
        const auto* x = input + row * width;
        auto s0 = _mm256_setzero_ps(), s1 = s0, s2 = s0, s3 = s0;
        std::size_t i = 0;
        for (; i + 8 <= width; i += 8) {
            const auto w = _mm256_loadu_ps(weight + i);
            s0 = _mm256_fmadd_ps(w, _mm256_loadu_ps(x + i), s0);
            s1 = _mm256_fmadd_ps(w, _mm256_loadu_ps(x + width + i), s1);
            s2 = _mm256_fmadd_ps(w, _mm256_loadu_ps(x + 2 * width + i), s2);
            s3 = _mm256_fmadd_ps(w, _mm256_loadu_ps(x + 3 * width + i), s3);
        }
        float values[4] = {reduce8(s0), reduce8(s1), reduce8(s2), reduce8(s3)};
        for (; i < width; ++i) {
            for (std::size_t r = 0; r < 4; ++r) { values[r] += weight[i] * x[r * width + i]; }
        }
        for (std::size_t r = 0; r < 4; ++r) { output[(row + r) * output_stride] = values[r] + bias; }
    }
    for (; row < rows; ++row) { output[row * output_stride] = avx2_fma_dot(weight, input + row * width, width) + bias; }
}
} // namespace outloud::cpu::detail
