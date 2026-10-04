#pragma once
#include <cstddef>
namespace outloud::cpu::detail {
float portable_dot(const float* a, const float* b, std::size_t size);
#ifdef OUTLOUD_HAS_AVX2
float avx2_fma_dot(const float* a, const float* b, std::size_t size);
#endif
} // namespace outloud::cpu::detail
