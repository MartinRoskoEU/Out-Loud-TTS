#pragma once
#include "outloud/rust_runtime_session.hpp"
#include <string>

namespace outloud::detail {
// Internal transport seam. Return a short read or zero for confirmed body EOF;
// transport errors throw. No HTTP framing bytes reach the WAV parser.
using RustByteSource = std::size_t (*)(void*, std::span<std::uint8_t>);
[[nodiscard]] RustPCMResult read_rust_wav(RustByteSource source, void* source_context,
    RustPCMSink sink, void* sink_context);
[[nodiscard]] std::string rust_tts_form(std::string_view utf8);
} // namespace outloud::detail
