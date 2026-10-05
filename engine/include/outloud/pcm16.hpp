#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace outloud {
// Existing Pocket listening conversion: clamp, scale by 32767, ties to even.
[[nodiscard]] inline std::int16_t float_to_pcm16(float value) {
    if (!std::isfinite(value)) { throw std::invalid_argument("Nonfinite PCM"); }
    const double scaled = std::clamp(double(value), -1.0, 1.0)*32767;
    const double low = std::floor(scaled), fraction = scaled-low;
    const auto lower = static_cast<int>(low);
    const int rounded = lower+(fraction>.5 || (fraction==.5 && lower%2!=0) ? 1 : 0);
    return static_cast<std::int16_t>(rounded);
}
inline void convert_pcm16(std::span<const float> input, std::span<std::int16_t> output) {
    if (input.size() != output.size()) { throw std::invalid_argument("PCM conversion shape differs"); }
    std::transform(input.begin(),input.end(),output.begin(),float_to_pcm16);
}
}
