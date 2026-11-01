# SAPI cancellation and speech controls

The production C++ COM/SAPI adapter streams audio from the official Rust runtime.
It polls `ISpTTSEngineSite::GetActions` on the `Speak` caller thread. A joined
coordinator performs runtime work and hands PCM blocks back through the existing
bounded rendezvous; site calls are not made from a background worker.

`SPVES_ABORT` requests cancellation of asset preparation, session startup or
streaming. The active WinHTTP request is interrupted, the owned runtime job is
stopped and process exit is checked before verified asset leases are released.
Cancellation does not deliver later audio writes. Session cleanup is required
before another session can start; there is no detached worker or automatic
retry during speech.

The caller retains rate, volume and skip handling. Rate changes use Sonic;
volume is applied through the existing PCM path. Unsupported skip requests are
acknowledged with `CompleteSkip(0)`. Output partial writes and site failures
retain their existing handling. Expected SAPI abort is not an error-log event.

In Voice Tester, Stop and a change of voice cancel the active SAPI session.
Navigation and closing wait for its cleanup. WAV export uses the same SAPI path;
cancellation does not publish an incomplete destination.

See [Rust session lifetime](RUST_RUNTIME_SESSION.md),
[bookmarks](SAPI_BOOKMARKS.md) and [targeted checks](BUILD_AND_VALIDATION.md).
