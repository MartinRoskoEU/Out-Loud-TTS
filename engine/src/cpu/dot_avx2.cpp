#include "dot.hpp"
#include <immintrin.h>

namespace outloud::cpu::detail {
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
} // namespace outloud::cpu::detail
