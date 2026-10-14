#pragma once
#include "support.hpp"
#include <sonic.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

namespace outloud::sapi {
inline float rate_tempo(long rate) noexcept {
    return static_cast<float>(std::pow(3.0, std::clamp(rate, -10L, 10L)/10.0));
}

// Owned by one Speak's caller thread. No Sonic allocation or processing at zero.
// Keep a stream across frames/segments and nonzero changes. On return to zero,
// flush the old tail exactly once before delivering new input directly.
class SpeechRate {
public:
    SpeechRate() = default;
    ~SpeechRate() { if (stream_) { sonicDestroyStream(stream_); } }
    SpeechRate(const SpeechRate&) = delete;
    SpeechRate& operator=(const SpeechRate&) = delete;

    template<class Sink>
    HRESULT write(std::span<const std::int16_t> pcm, long rate, Sink&& sink) {
        if (!rate) {
            const auto hr = finish(sink);
            return hr==S_OK ? sink(pcm) : hr;
        }
        if (!stream_) {
            stream_ = sonicCreateStream(24000, 1);
            if (!stream_) { return E_OUTOFMEMORY; }
        }
        // Speed changes duration while pitch and playback rate stay at 1.0.
        sonicSetSpeed(stream_, rate_tempo(rate));
        if (!sonicWriteShortToStream(stream_, pcm.data(), static_cast<int>(pcm.size()))) {
            return E_OUTOFMEMORY;
        }
        return drain(sink);
    }

    template<class Sink>
    HRESULT finish(Sink&& sink) {
        if (!stream_) { return S_OK; }
        if (!sonicFlushStream(stream_)) { return E_OUTOFMEMORY; }
        const auto hr = drain(sink);
        sonicDestroyStream(stream_); stream_ = nullptr;
        return hr;
    }
private:
    template<class Sink>
    HRESULT drain(Sink&& sink) {
        std::array<std::int16_t,1920> output;
        for (;;) {
            const auto count = sonicReadShortFromStream(stream_, output.data(), static_cast<int>(output.size()));
            if (!count) { return S_OK; }
            const auto hr = sink(std::span<const std::int16_t>(output.data(), static_cast<std::size_t>(count)));
            if (hr!=S_OK) { return hr; }
        }
    }
    sonicStream stream_ = nullptr;
};
}
