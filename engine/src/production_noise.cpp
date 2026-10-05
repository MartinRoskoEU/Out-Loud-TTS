#include "outloud/production_noise.hpp"
#include <bit>
#include <cmath>

// Altered scalar subset of the pinned ATen MT19937 and avx_mathfun algorithms.
// This is not the original upstream implementation. See RNG_NOTICES.md for
// the retained original notices. No PyTorch headers/library are used at runtime.
namespace outloud {
namespace {
// Positive-domain log256_ps lane semantics. Inputs are exactly in [2^-24,1].
float normal_log(float value) noexcept {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    float x = std::bit_cast<float>((bits & ~0x7f800000u) | 0x3f000000u);
    float e = static_cast<float>(static_cast<int>(bits >> 23) - 127);
    e = e + 1.0f;
    const bool small = x < 0.707106781186547524f;
    const float temporary = small ? x : 0.0f;
    x = x - 1.0f;
    e = e - (small ? 1.0f : 0.0f);
    x = x + temporary;
    const float z = x * x;
    float y = 7.0376836292E-2f;
    y = y * x; y = y + -1.1514610310E-1f;
    y = y * x; y = y + 1.1676998740E-1f;
    y = y * x; y = y + -1.2420140846E-1f;
    y = y * x; y = y + 1.4249322787E-1f;
    y = y * x; y = y + -1.6668057665E-1f;
    y = y * x; y = y + 2.0000714765E-1f;
    y = y * x; y = y + -2.4999993993E-1f;
    y = y * x; y = y + 3.3333331174E-1f;
    y = y * x;
    y = y * z;
    const float q1 = e * -2.12194440e-4f;
    y = y + q1;
    const float half = z * 0.5f;
    y = y - half;
    const float q2 = e * 0.693359375f;
    x = x + y;
    return x + q2;
}
// sincos256_ps lane semantics, restricted to nonnegative angles below 2*pi.
void normal_sincos(float angle, float& sine, float& cosine) noexcept {
    float x = angle;
    const float scaled = x * 1.27323954473516f;
    const auto quadrant = (static_cast<std::uint32_t>(scaled) + 1u) & ~1u;
    const float y = static_cast<float>(quadrant);
    const float dp1 = y * -0.78515625f;
    const float dp2 = y * -2.4187564849853515625e-4f;
    const float dp3 = y * -3.77489497744594108e-8f;
    x = x + dp1; x = x + dp2; x = x + dp3;
    const float z = x * x;
    float c = 2.443315711809948E-005f;
    c = c * z; c = c + -1.388731625493765E-003f;
    c = c * z; c = c + 4.166664568298827E-002f;
    c = c * z; c = c * z;
    const float half = z * 0.5f;
    c = c - half; c = c + 1.0f;
    float s = -1.9515295891E-4f;
    s = s * z; s = s + 8.3321608736E-3f;
    s = s * z; s = s + -1.6666654611E-1f;
    s = s * z; s = s * x; s = s + x;
    const bool select_sine = (quadrant & 2u) == 0;
    const float sin2 = select_sine ? s : 0.0f;
    const float sin1 = select_sine ? 0.0f : c;
    s = s - sin2; c = c - sin1;
    sine = sin1 + sin2;
    cosine = c + s;
    sine = std::bit_cast<float>(std::bit_cast<std::uint32_t>(sine) ^ ((quadrant & 4u) << 29));
    cosine = std::bit_cast<float>(std::bit_cast<std::uint32_t>(cosine) ^ ((~(quadrant - 2u) & 4u) << 29));
}
}
void ProductionNoiseSource::reseed(std::uint64_t seed) noexcept {
    seed_ = seed; draws_ = 0; left_ = 1; position_ = 0;
    state_[0] = static_cast<std::uint32_t>(seed);
    for (std::uint32_t i = 1; i < 624; ++i) {
        state_[i] = 1812433253u * (state_[i-1] ^ (state_[i-1] >> 30)) + i;
    }
    row_.fill(0.0f);
}
std::uint32_t ProductionNoiseSource::random() noexcept {
    if (--left_ == 0) {
        for (std::size_t i = 0; i < 624; ++i) {
            const auto next = state_[(i + 1) % 624];
            const auto mixed = (state_[i] & 0x80000000u) | (next & 0x7fffffffu);
            state_[i] = state_[(i + 397) % 624] ^ (mixed >> 1) ^ ((next & 1u) ? 0x9908b0dfu : 0u);
        }
        left_ = 624; position_ = 0;
    }
    auto value = state_[position_++];
    value ^= value >> 11;
    value ^= (value << 7) & 0x9d2c5680u;
    value ^= (value << 15) & 0xefc60000u;
    value ^= value >> 18;
    return value;
}
std::span<const float> ProductionNoiseSource::next() {
    for (auto& value : row_) { value = static_cast<float>(random() & 0xffffffu) * 0x1p-24f; }
    const float deviation = std::bit_cast<float>(0x3f0c378cu); // FP32(sqrt(double(0.3)))
    const float two_pi = std::bit_cast<float>(0x40c90fdbu); // FP32(2 * pi<double>)
    for (std::size_t block = 0; block < 32; block += 16) {
        for (std::size_t j = 0; j < 8; ++j) {
            const float u1 = 1.0f - row_[block+j];
            const float radius = std::sqrt(-2.0f * normal_log(u1));
            const float angle = two_pi * row_[block+j+8];
            float sine, cosine; normal_sincos(angle, sine, cosine);
            const float n1 = radius * cosine, n2 = radius * sine;
            // With finite n and mean +0, fmadd(n,std,+0) is multiply then
            // add +0, including the signed-zero result. Strict FP prevents
            // polynomial contraction or elimination of this zero addition.
            row_[block+j] = n1 * deviation + 0.0f;
            row_[block+j+8] = n2 * deviation + 0.0f;
        }
    }
    ++draws_;
    return row_;
}
} // namespace outloud
