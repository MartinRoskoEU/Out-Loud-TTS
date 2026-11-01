#pragma once
#include "outloud/cancellation.hpp"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace outloud {
// Mono 24 kHz PCM16. Called synchronously on the read_pcm caller thread, with
// 1..1920 samples. The view expires on return; copy it before asynchronous use.
// The sink may throw; no callback may reenter this session's methods.
using RustPCMSink = void (*)(void*, std::span<const std::int16_t>);
struct RustPCMResult {
    std::uint64_t samples = 0, blocks = 0;
    SynthesisStatus status = SynthesisStatus::completed;
};
struct RustRuntimeSessionOptions {
    // All three must be absolute local file paths. The caller resolves a trusted
    // executable and a config whose model/tokenizer references are verified local
    // files. This component neither discovers installations nor rewrites YAML.
    std::filesystem::path executable, configuration, default_voice;
    std::chrono::milliseconds startup_timeout{std::chrono::minutes(5)};
    // Production's verified official binary imports only Windows system DLLs.
    // Prefer System32 over a writable application directory when loading them.
    bool prefer_system_dlls = false;
};

// Owns one official `serve` process and its descendants. Successful start keeps
// the prepared model/voice alive until stop/destruction.
// Caller serializes all methods and destruction (including status queries).
// start blocks for readiness; use an appropriate coordinator thread, not a UI
// or SAPI site thread. Only the start/read_pcm stop source may be used concurrently;
// no other session method (including stop/destruction) may race a request.
class RustRuntimeSession {
public:
    RustRuntimeSession();
    ~RustRuntimeSession();
    RustRuntimeSession(const RustRuntimeSession&) = delete;
    RustRuntimeSession& operator=(const RustRuntimeSession&) = delete;
    // With a stop token, cancellation during startup returns with no live
    // session. The caller checks that token before submitting a request.
    void start(const RustRuntimeSessionOptions& options, std::stop_token cancellation = {});
    // Idempotent. Terminates only this session's job and confirms quiescence.
    // On failure throws and retains ownership for retry; destructor still closes
    // the kill-on-close job. Do not equate a thrown stop with confirmed cleanup.
    void stop();
    [[nodiscard]] bool is_ready() const;
    // Available only after readiness and while the process remains alive.
    [[nodiscard]] std::uint16_t port() const;
    [[nodiscard]] std::optional<std::uint32_t> exit_code() const;
    // POST UTF-8 text to the prepared default voice and deliver PCM as received.
    // No whole-utterance audio buffer. Returns only after a valid HTTP/WAV end;
    // errors can follow an already delivered prefix. A stop token interrupts
    // pending HTTP I/O and returns cancelled with completed-callback counts.
    // An in-flight sink must return normally; it cannot be forcibly interrupted.
    // Cancellation/failure closes HTTP and stops the owned job before return;
    // cleanup failure throws and retains ownership for explicit stop retry.
    // A pre-cancelled call sends nothing and preserves the current session.
    // After interruption, call start(options) to prepare a clean session.
    [[nodiscard]] RustPCMResult read_pcm(std::string_view utf8, RustPCMSink sink, void* context = nullptr,
        std::stop_token cancellation = {});
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace outloud
