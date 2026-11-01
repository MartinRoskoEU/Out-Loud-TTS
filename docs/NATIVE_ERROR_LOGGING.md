# Bounded native error logging

The SAPI adapter records errors through
[`error_logger.hpp`](../sapi/src/error_logger.hpp) and
[`error_logger.cpp`](../sapi/src/error_logger.cpp).
Successful speech and expected cancellation do not produce routine log entries.

Logs are caller-local at `%LOCALAPPDATA%\OutLoud\Logs`:

- `engine.log`
- `engine.1.log`
- `engine.2.log`

Each file is limited to 2 MiB; rotation replaces the oldest known archive.
Records are bounded to 512 bytes and accept only closed event/component/error
categories and numeric codes. Speech text, profile/model paths, exception text,
credentials and raw Rust output are not accepted by the logging API.

A path-derived named mutex serializes writers across processes and sessions,
with a 10 ms wait bound. Logging is best effort: it preserves Windows/COM error
state, catches failures, does not retry writes or recursively log an error, and
does not add a background logger or successful-speech diagnostics.

Temporary speech configuration and operation markers are separate caller-local
resources, not model or Settings storage. Persistent models and Settings are
machine-wide; see [WinUI storage](../ui/OutLoud.WinUI/README.md).
The [production uninstaller](../installer/PRODUCTION.md) uses verified ownership
records for cleanup rather than deleting a directory just because of its name.
