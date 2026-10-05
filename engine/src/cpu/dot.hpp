#pragma once
#include <cstddef>
#include <cstdint>
namespace outloud::cpu::detail {
float portable_dot(const float* a, const float* b, std::size_t size);
#ifdef OUTLOUD_HAS_AVX2
float avx2_fma_dot(const float* a, const float* b, std::size_t size);
void avx2_bf16_linear(const float* input, const std::uint16_t* weight, float* output,
                      std::size_t tokens, std::size_t width, std::size_t outputs,
                      const std::uint16_t* bias);
void avx2_fma_dot_rows(const float* weight, const float* input, float* output,
                        std::size_t rows, std::size_t width, std::size_t output_stride, float bias);
#endif
} // namespace outloud::cpu::detail
