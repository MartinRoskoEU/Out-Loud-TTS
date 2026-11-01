#pragma once
#include "rust_pcm_reader.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <stdexcept>
#include <vector>

namespace rust_pcm_checks {
using Bytes = std::vector<std::uint8_t>;
inline void check(bool condition) { if (!condition) { throw std::runtime_error("Rust PCM reader check failed"); } }
inline void put(Bytes& bytes, std::size_t offset, std::uint32_t value, unsigned count = 4) {
    for (unsigned i = 0; i < count; ++i) { bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i)); }
}
inline void chunk(Bytes& bytes, const char* tag, const Bytes& contents) {
    bytes.insert(bytes.end(), tag, tag + 4);
    const auto offset = bytes.size(); bytes.resize(offset + 4);
    put(bytes, offset, static_cast<std::uint32_t>(contents.size()));
    bytes.insert(bytes.end(), contents.begin(), contents.end());
    if (contents.size() & 1) { bytes.push_back(0); }
}
inline Bytes wav(bool streaming, bool metadata = false, std::size_t samples = 4099) {
    Bytes bytes{'R','I','F','F',0,0,0,0,'W','A','V','E'};
    if (metadata) { chunk(bytes, "JUNK", {1,2,3}); }
    Bytes format(metadata ? 18 : 16, 0);
    put(format, 0, 1, 2); put(format, 2, 1, 2); put(format, 4, 24000);
    put(format, 8, 48000); put(format, 12, 2, 2); put(format, 14, 16, 2);
    chunk(bytes, "fmt ", format);
    Bytes pcm(samples * 2);
    for (std::size_t i = 0; i < samples; ++i) { put(pcm, i * 2, static_cast<std::uint16_t>(i * 73 + 32768), 2); }
    chunk(bytes, "data", pcm);
    if (metadata) { chunk(bytes, "LIST", {9,8,7}); }
    put(bytes, 4, streaming ? 2000000036U : static_cast<std::uint32_t>(bytes.size() - 8));
    if (streaming) { put(bytes, 40, 2000000000U); }
    return bytes;
}
struct Source {
    std::span<const std::uint8_t> bytes;
    std::size_t fragment = 1, position = 0, fail_at = (std::numeric_limits<std::size_t>::max)();
    bool eof = false;
    static std::size_t read(void* context, std::span<std::uint8_t> destination) {
        auto& self = *static_cast<Source*>(context);
        if (self.position >= self.fail_at) { throw std::runtime_error("fixture transport failure"); }
        const auto count = (std::min)({destination.size(), self.fragment, self.bytes.size() - self.position, self.fail_at - self.position});
        std::copy_n(self.bytes.data() + self.position, count, destination.data());
        self.position += count; if (!count) { self.eof = true; }
        return count;
    }
};
struct Sink {
    Source* source;
    std::uint64_t samples = 0, blocks = 0;
    static void pcm(void* context, std::span<const std::int16_t> values) {
        auto& self = *static_cast<Sink*>(context);
        check(!self.source->eof && !values.empty() && values.size() <= 1920);
        for (const auto value : values) {
            check(std::bit_cast<std::uint16_t>(value) == static_cast<std::uint16_t>(self.samples * 73 + 32768));
            ++self.samples;
        }
        ++self.blocks;
    }
};
inline void fails(const Bytes& bytes) {
    Source source{bytes, 3}; Sink sink{&source};
    try { (void)outloud::detail::read_rust_wav(Source::read, &source, Sink::pcm, &sink); }
    catch (const std::runtime_error&) { return; }
    throw std::runtime_error("Malformed or incomplete Rust WAV accepted");
}
inline void run() {
    // Every small read size includes header splits and odd sample boundaries.
    const auto stream = wav(true);
    for (std::size_t fragment = 1; fragment <= 65; ++fragment) {
        Source source{stream, fragment}; Sink sink{&source};
        const auto result = outloud::detail::read_rust_wav(Source::read, &source, Sink::pcm, &sink);
        check(result.samples == 4099 && result.samples == sink.samples && result.blocks == sink.blocks);
    }
    for (const auto fragment : {127U, 3840U, 8192U}) {
        const auto finite = wav(false, true);
        Source source{finite, fragment}; Sink sink{&source};
        const auto result = outloud::detail::read_rust_wav(Source::read, &source, Sink::pcm, &sink);
        check(result.samples == 4099 && result.blocks == sink.blocks);
    }
    const auto small = wav(false, false, 7);
    for (std::size_t size = 0; size < small.size(); ++size) { fails(Bytes(small.begin(), small.begin() + size)); }
    for (const auto offset : {0U, 8U, 20U, 22U, 24U, 28U, 32U, 34U}) {
        auto bad = small; bad[offset] ^= 2; fails(bad);
    }
    auto bad = small; put(bad, 16, 15); fails(bad); // Short format.
    bad = small; put(bad, 40, 13); fails(bad); // Odd finite PCM length.
    bad = small; put(bad, 4, 10); fails(bad); // Invalid RIFF size.
    bad = small; bad.push_back(0); fails(bad); // Extra bytes outside RIFF.
    bad = stream; bad.pop_back(); fails(bad); // Incomplete final streaming sample.
    fails(wav(false, false, 0)); fails(wav(true, false, 0));
    bad = small; std::copy_n("data", 4, bad.begin() + 12); fails(bad); // Data before format.
    bad = small; std::copy_n("fmt ", 4, bad.begin() + 36); fails(bad); // Duplicate format.
    bad = wav(false, true); put(bad, 16, 65536); put(bad, 4, 100000); fails(bad); // Bounded metadata.
    Source source{stream, 127, 0, 200}; Sink sink{&source};
    try { (void)outloud::detail::read_rust_wav(Source::read, &source, Sink::pcm, &sink); check(false); }
    catch (const std::runtime_error& error) { check(std::string_view(error.what()) == "fixture transport failure" && sink.samples > 0); }
    source = Source{stream, 127};
    const auto throwing_sink = [](void*, std::span<const std::int16_t>) { throw std::runtime_error("fixture sink failure"); };
    try { (void)outloud::detail::read_rust_wav(Source::read, &source, throwing_sink, nullptr); check(false); }
    catch (const std::runtime_error& error) { check(std::string_view(error.what()) == "fixture sink failure" && source.position < stream.size()); }
    check(outloud::detail::rust_tts_form("A+B & caf\xc3\xa9?") == "text=A%2BB+%26+caf%C3%A9%3F");
    for (const auto invalid : {std::string_view{}, std::string_view("\xc3", 1)}) {
        bool rejected = false;
        try { (void)outloud::detail::rust_tts_form(invalid); } catch (const std::exception&) { rejected = true; }
        check(rejected);
    }
    // A generated body substantially larger than the parser's buffers; neither
    // the source nor sink stores it. First PCM must arrive before body EOF.
    struct Generated {
        Bytes header = wav(true, false, 0);
        std::uint64_t position = 0, delivered = 0;
        static std::size_t read(void* context, std::span<std::uint8_t> destination) {
            auto& self = *static_cast<Generated*>(context);
            constexpr std::uint64_t total = 44 + 2 * 100001;
            const auto count = static_cast<std::size_t>((std::min)(static_cast<std::uint64_t>(destination.size()), total - self.position));
            for (std::size_t i = 0; i < count; ++i, ++self.position) {
                const auto p = self.position;
                destination[i] = p < 44 ? self.header[static_cast<std::size_t>(p)] :
                    static_cast<std::uint8_t>(((p - 44) / 2 * 73 + 32768) >> (((p - 44) % 2) * 8));
            }
            return count;
        }
        static void pcm(void* context, std::span<const std::int16_t> values) {
            auto& self = *static_cast<Generated*>(context);
            check(!values.empty() && values.size() <= 1920);
            if (!self.delivered) { check(self.position < 44 + 2 * 100001); }
            for (const auto value : values) { check(std::bit_cast<std::uint16_t>(value) == static_cast<std::uint16_t>(self.delivered++ * 73 + 32768)); }
        }
    } generated;
    const auto result = outloud::detail::read_rust_wav(Generated::read, &generated, Generated::pcm, &generated);
    check(result.samples == 100001 && generated.delivered == result.samples);
}
} // namespace rust_pcm_checks
