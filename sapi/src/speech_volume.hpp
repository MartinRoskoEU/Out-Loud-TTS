#pragma once
#include <algorithm>
#include <cstdint>

namespace outloud::sapi {
// SAPI baseline and XML fragment levels are independent percentages.
inline unsigned volume_gain(unsigned baseline, unsigned fragment) noexcept {
    return (std::min)(baseline,100U)*(std::min)(fragment,100U);
}
inline std::int16_t scale_pcm16(std::int16_t sample, unsigned gain) noexcept {
    // Signed, wide intermediates; nearest with ties to even, as in PCM conversion.
    const std::int64_t product=std::int64_t{sample}*(std::min)(gain,10000U);
    const auto magnitude=product<0 ? -product : product;
    auto rounded=magnitude/10000;
    const auto remainder=magnitude%10000;
    if (remainder>5000 || (remainder==5000 && rounded%2)) { ++rounded; }
    const auto result=product<0 ? -rounded : rounded;
    return static_cast<std::int16_t>(std::clamp(result,std::int64_t{-32768},std::int64_t{32767}));
}
}
