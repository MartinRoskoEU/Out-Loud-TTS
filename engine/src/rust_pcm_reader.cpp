#include "rust_pcm_reader.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <stdexcept>

namespace outloud::detail {
namespace {
void valid(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
std::uint16_t u16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (static_cast<unsigned>(p[1]) << 8)); }
std::uint32_t u32(const std::uint8_t* p) {
    return p[0] | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) |
        (static_cast<std::uint32_t>(p[3]) << 24);
}
class Reader {
public:
    Reader(RustByteSource source, void* context) : source_(source), context_(context) {}
    std::size_t read(std::span<std::uint8_t> destination) {
        const auto count = source_(context_, destination);
        valid(count <= destination.size(), "Rust WAV source returned an oversized read");
        return count;
    }
    void exact(std::span<std::uint8_t> destination) {
        while (!destination.empty()) {
            const auto count = read(destination);
            valid(count != 0, "Incomplete Rust WAV header or chunk");
            destination = destination.subspan(count);
        }
    }
    void metadata(std::uint64_t bytes) {
        valid(bytes <= 65536 - metadata_bytes_, "Rust WAV metadata exceeds 64 KiB");
        metadata_bytes_ += bytes;
    }
    void skip(std::uint64_t bytes) {
        std::array<std::uint8_t, 512> scratch{};
        while (bytes) {
            const auto count = static_cast<std::size_t>((std::min)(bytes, static_cast<std::uint64_t>(scratch.size())));
            exact(std::span(scratch).first(count)); bytes -= count;
        }
    }
    RustPCMResult audio(std::uint32_t bytes, bool streaming, RustPCMSink sink, void* context) {
        valid(streaming || (bytes && bytes % 2 == 0), "Rust WAV data must contain complete PCM16 samples");
        std::array<std::uint8_t, 3840> input{};
        std::array<std::int16_t, 1920> output{};
        std::uint8_t low = 0;
        bool pending = false;
        RustPCMResult result;
        while (streaming || bytes) {
            const auto wanted = streaming ? input.size() : (std::min)(input.size(), static_cast<std::size_t>(bytes));
            const auto count = read(std::span(input).first(wanted));
            if (!count) { valid(streaming, "Incomplete Rust WAV PCM data"); break; }
            std::size_t samples = 0;
            for (std::size_t i = 0; i < count; ++i) {
                if (!pending) { low = input[i]; pending = true; }
                else {
                    const auto bits = static_cast<std::uint16_t>(low | (static_cast<unsigned>(input[i]) << 8));
                    output[samples++] = std::bit_cast<std::int16_t>(bits); pending = false;
                }
            }
            if (samples) {
                sink(context, std::span<const std::int16_t>(output).first(samples));
                result.samples += samples; ++result.blocks;
            }
            if (!streaming) { bytes -= static_cast<std::uint32_t>(count); }
        }
        valid(!pending, "Rust WAV ended with an incomplete PCM16 sample");
        valid(result.samples != 0, "Rust runtime returned no PCM samples");
        return result;
    }
private:
    RustByteSource source_;
    void* context_;
    std::uint64_t metadata_bytes_ = 12;
};
} // namespace

RustPCMResult read_rust_wav(RustByteSource source, void* source_context, RustPCMSink sink, void* sink_context) {
    if (!source || !sink) { throw std::invalid_argument("Rust WAV source and PCM sink are required"); }
    Reader reader(source, source_context);
    std::array<std::uint8_t, 12> riff{};
    reader.exact(riff);
    valid(std::memcmp(riff.data(), "RIFF", 4) == 0 && std::memcmp(riff.data() + 8, "WAVE", 4) == 0,
        "Rust runtime response is not RIFF/WAVE");
    const auto riff_size = u32(riff.data() + 4);
    valid(riff_size >= 36, "Invalid Rust WAV RIFF size");
    std::uint64_t remaining = static_cast<std::uint64_t>(riff_size) - 4;
    bool format = false, data = false;
    RustPCMResult result;
    while (remaining) {
        valid(remaining >= 8, "Incomplete Rust WAV chunk header");
        std::array<std::uint8_t, 8> chunk{};
        reader.exact(chunk); remaining -= chunk.size(); reader.metadata(chunk.size());
        const auto size = u32(chunk.data() + 4);
        const auto padded = static_cast<std::uint64_t>(size) + (size & 1U);
        valid(padded <= remaining, "Rust WAV chunk exceeds RIFF boundary");
        if (std::memcmp(chunk.data(), "fmt ", 4) == 0) {
            valid(!format && !data && size >= 16, "Invalid or duplicate Rust WAV format chunk");
            reader.metadata(padded);
            std::array<std::uint8_t, 16> fmt{};
            reader.exact(fmt);
            valid(u16(fmt.data()) == 1 && u16(fmt.data() + 2) == 1 && u32(fmt.data() + 4) == 24000 &&
                u32(fmt.data() + 8) == 48000 && u16(fmt.data() + 12) == 2 && u16(fmt.data() + 14) == 16,
                "Rust WAV must be mono 24 kHz PCM16");
            reader.skip(padded - fmt.size()); format = true;
        } else if (std::memcmp(chunk.data(), "data", 4) == 0) {
            valid(format && !data, "Rust WAV data requires one preceding format chunk");
            data = true;
            // Official v4.0.1 StreamingWavWriter's unseekable header announces
            // 1,000,000,000 frames. HTTP body EOF, not that size, ends its PCM.
            const bool streaming = riff_size == 2000000036U && size == 2000000000U && remaining == size;
            result = reader.audio(size, streaming, sink, sink_context);
            if (streaming) { return result; }
        } else {
            reader.metadata(padded); reader.skip(padded);
        }
        remaining -= padded;
    }
    valid(data, "Rust WAV has no data chunk");
    std::array<std::uint8_t, 1> extra{};
    valid(reader.read(extra) == 0, "Rust WAV has bytes beyond its RIFF boundary");
    return result;
}
} // namespace outloud::detail
