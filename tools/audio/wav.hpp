#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>

namespace outloud::development {
// Development listening copy only; conversion follows the existing Mimi tool.
inline void write_pcm16_wave(const std::filesystem::path& path, std::span<const float> pcm) {
    std::ofstream out(path, std::ios::binary);
    auto little = [&](std::uint32_t value, std::size_t bytes) {
        for (std::size_t i = 0; i < bytes; ++i) { out.put(static_cast<char>((value >> (i*8)) & 255)); }
    };
    out.write("RIFF", 4); little(static_cast<std::uint32_t>(36+pcm.size()*2), 4);
    out.write("WAVEfmt ", 8); little(16, 4); little(1, 2); little(1, 2);
    little(24000, 4); little(48000, 4); little(2, 2); little(16, 2);
    out.write("data", 4); little(static_cast<std::uint32_t>(pcm.size()*2), 4);
    for (float value : pcm) {
        const double scaled = std::clamp(double(value), -1.0, 1.0)*32767;
        const double low = std::floor(scaled), fraction = scaled-low;
        const auto lower = static_cast<int>(low);
        const int rounded = lower+(fraction>.5 || (fraction==.5 && lower%2!=0) ? 1 : 0);
        little(static_cast<std::uint16_t>(static_cast<std::int16_t>(rounded)), 2);
    }
    if (!out) { throw std::runtime_error("Cannot write listening WAV"); }
}
}
