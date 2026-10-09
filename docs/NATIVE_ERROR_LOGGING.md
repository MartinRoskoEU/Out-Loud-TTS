# Minimal native error logging

Date: 2026-10-09. This adds always-enabled, error-only file logging to the native
Pocket SAPI DLL. The earlier working-tree changes are preserved. It does not add
Voice Manager settings, registry configuration, a background thread or a neural
logging dependency. Workspace binaries were rebuilt; nothing was deployed.

## Architecture and error boundaries

[error_logger.hpp](../sapi/src/error_logger.hpp) accepts only closed enums and
numeric codes. [error_logger.cpp](../sapi/src/error_logger.cpp) formats and writes
the record. [adapter.cpp](../sapi/src/adapter.cpp) owns these boundaries:

| Boundary / stable event | Failures covered |
| --- | --- |
| `TOKEN_CONFIGURATION` | Token/configuration parsing and model/tokenizer/voice readability checks. |
| `TOKEN_ACCESS` | Operational token retrieval errors, including uninitialized engine. |
| `OUTPUT_FORMAT` | Format allocation, invalid output pointers or unsupported Speak format. |
| `SYNTHESIS` | Input collection/preparation, lazy resident asset loading, compatibility, native/worker exceptions, allocation/resource failures and unexpected internal cancellation. |
| `SITE_WRITE` | Site HRESULT failures and invalid consumed-byte counts; recorded after native work has quiesced. |
| `BOOKMARK_INTEREST`, `BOOKMARK_DELIVERY` | Failed event-interest query or AddEvents. |
| `SITE_RATE`, `SITE_VOLUME`, `SITE_SKIP_INFO`, `SITE_SKIP_COMPLETE` | Failed observational control queries; each kind is logged at most once per Speak. Their existing HRESULT policy remains unchanged. |
| `ENGINE_CREATE`, `FACTORY_CREATE` | Exceptions/resource failures constructing COM objects. |

Asset roles come only from recognized implementation prefixes; the record never
copies the exception message. Asset I/O/invalid-data/compatibility classifications,
Win32 values and existing mapped HRESULTs are preserved. Other kinds distinguish
allocation, invalid argument, filesystem and unexpected failures. `Kind` is a
stable numeric classification: operation 0, asset I/O 1, invalid asset 2,
compatibility 3, allocation 4, invalid argument 5, filesystem 6, unexpected 7,
unexpected cancellation 8.

Worker exceptions already propagate to the coordinator, which saves them without
logging. The Speak caller logs once when the existing exception boundary maps and
publishes the failure. A site-write failure takes its existing precedence over
the sink's propagated copy. No log calls are added to asset loaders or FlowLM,
FlowSampler, Mimi, generation, worker or orchestration loops.

Normal abort/cancellation, EOS, `SP_AUDIO_STOPPED`, empty requests, successful
initialization/requests and COM interface/class capability probes are not errors
to log. A genuine control-query failure can be recorded even if the original
observational policy permits Speak to complete successfully. Logging does not
change that policy or promote a warning into a different COM result.

## Location, format and privacy

The first actual failure resolves Windows `FOLDERID_LocalAppData` and lazily
creates its `OutLoud\Logs` child, normally:

```text
%LOCALAPPDATA%\OutLoud\Logs\engine.log
%LOCALAPPDATA%\OutLoud\Logs\engine.1.log
%LOCALAPPDATA%\OutLoud\Logs\engine.2.log
```

ASCII records are valid UTF-8, one line each, with UTC milliseconds, ERROR,
component, event, HRESULT hex, Win32 decimal, kind, process/thread IDs and a
fixed short message. For example:

```text
[2026-10-09T12:00:00.000Z] [ERROR] [Model] [TOKEN_CONFIGURATION] [HRESULT=0x80070002 Win32=2 Kind=1 PID=1234 TID=5678] Runtime asset I/O failed.
```

The API has no free-text field, so spoken/quest/input text, bookmarks, PCM,
tensors, credentials, usernames, paths and raw exception descriptions cannot
enter a record. Records use a 512-byte buffer and must fit in fewer than 512
bytes including newline. There are no INFO/DEBUG/TRACE levels or configuration.

## Rotation and concurrency

Each file is limited to **2 MiB (2,097,152 bytes)**. Before an append would cross
the limit, the oldest `.2` is discarded, `.1` becomes `.2`, and current becomes
`.1`; the new record starts a fresh current file. Three files total at most
**6 MiB** under cooperating writers. No timestamp archives or additional lock
files are created. Externally oversized regular logs are discarded/truncated
before appending; unsafe nonregular/reparse/hard-linked files are refused.

Every error call creates/opens an opaque path-derived named Windows mutex in the
`Global\` namespace and waits at most **10 ms** for ownership. The same canonical
per-user directory yields the same mutex for threads/processes across sessions.
The existing Windows object security descriptor governs access. If creation,
access or waiting fails, the event is dropped. An abandoned mutex is acquired
and existing bounded file state is checked before continuing. Rotation and append
remain inside the same ownership interval. Handles close at the end of the call.
Microsoft documents [global kernel namespaces](https://learn.microsoft.com/en-us/windows/win32/termserv/kernel-object-namespaces)
and [mutex ownership/abandonment](https://learn.microsoft.com/en-us/windows/win32/sync/mutex-objects).

No directory enumeration occurs; only the three known file names are checked.
The limit assumes these cooperating writers, not another program arbitrarily
altering files during an append. Rotation is not a filesystem transaction and
does not promise crash/power-loss atomicity or complete log retention.

## Failure behavior, lifetime and overhead

Logging is best effort and noexcept. Directory, lock, allocation, rotation, open
and write failures are ignored without recursive reporting. A failed/short write
attempts to restore the previous EOF; if that truncation also fails or a process
dies mid-write, a partial record can remain. No write retry or synchronous disk
flush is performed. Original thread Win32 error and COM IErrorInfo are preserved;
asset IErrorInfo is still published by the existing caller-side code.

Successful requests never enter the logger, so it performs no routine log-file
open/write, directory access or synchronization. Failed requests pay path lookup,
formatting, mutex and filesystem costs. **10 ms bounds mutex waiting only**, not
the latency of OS path/file operations. There is no measured zero-overhead or
real-time guarantee. Control handling adds failed-HRESULT branches and a per-request
deduplication bit mask; no success-path diagnostic strings are constructed.

Production has no persistent logger handle, dynamically initialized logger owner,
logging thread, unload callback or DllMain work. Existing workers still join before
engine destruction. Added `SHELL32.dll` is a Windows dependency for known-folder
resolution; the four VC runtime imports remain unchanged. No external framework,
new vendor redistributable, native frontend ABI or WPF change is introduced.

## Focused validation

The development-only `outloud-sapi-logger-test.dll` compiles the same adapter/logger
with private test path/fault injection and two extra test exports. Without explicit
configuration it discards events instead of resolving LocalAppData. Production
has no override/fault mechanism and retains exactly `DllGetClassObject` and
`DllCanUnloadNow`. Existing expected-error SAPI CTests now use that private DLL,
keeping their assertions while protecting user logs. No new CTest or performance
gate was registered.

The standalone [focused harness](../sapi/tests/error_logger_test.cpp) reuses the
existing registration-free in-memory token/site and test-owned scratch utility.
It tests fixed record fields/privacy, exact rotation order, all file size bounds,
four threads and four gated child processes, contention/event discard, a real
unwritable path/open conflict, injected access denial and injected partial-write
disk failure with rollback. It verifies the original Win32 error and IErrorInfo.
Injected faults simulate failure outcomes; the actual disk was not filled and
no real user directory ACL was changed.

COM checks exercise missing/corrupt model, tokenizer and voice; site writes and
bookmark failures; worker-thread unexpected/allocation failures; one record per
failure; and logger failure preserving the original asset HRESULT/IErrorInfo.
Entry/streaming abort, audio stop and same-resident recovery are silent. Worker
injection uses a test-only observer; production contains no such hook.

Executed results:

| Check | Result |
| --- | --- |
| Affected native Release x64 build | PASS: production engine, private test DLL, logger harness, existing SAPI/Mimi checkers. Final builds have no compiler/linker warnings. |
| Focused logger harness | PASS, 5.89 s including isolated child writers and short synthesis. |
| Successful/cancelled/stopped/recovered requests | **Zero logger calls**, and private quiet directory never created; hence no logger file operations were reached. This is not an OS-wide I/O trace. |
| Short `Hello traveler.` seed-73 Auto PCM | **72,960 bytes**, exact byte equality with the workspace DLL preserved before logger edits and exact same-resident recovery. |
| Existing `canonical-mimi-decoder`, `sapi-input-regressions`, `sapi-asset-diagnostics` | **3/3 PASS**, 8.39 s. Existing independent deterministic Mimi PCM fixture retained. |
| Production PE/exports/imports | x64; exactly two original COM exports; Windows Shell32 added, no test exports or private worker-fault string. |
| Shutdown | Joined requests, COM references released and DllCanUnloadNow=S_OK; modules unloaded normally. |

One affected-target incremental build was attempted, then corrected for two
test-variable shadow warnings and the private DLL's reused LIBRARY-name directive.
The successful build was followed by a checker-only rebuild to fix a missing-voice
test fixture's `.olp` extension: existing validation correctly rejected it before
the intended missing-file check. These were harness/build issues, not changed
production HRESULT behavior. No clean rebuild or whole-project campaign ran.

Reproduce from an already prepared development build:

```powershell
cmake --build build --config Release --parallel --target OutLoudPocketSAPI outloud-error-logger-tests outloud-sapi-check outloud-mimi-check
.\build\Release\outloud-error-logger-tests.exe .\build\Release\outloud-sapi-logger-test.dll .\generated\assets\pocket-english.olp .\generated\tokenizer\pocket-english-tokenizer.bin .\generated\assets\gnome_male.olv
ctest --test-dir build -C Release --output-on-failure -R '^(canonical-mimi-decoder|sapi-asset-diagnostics|sapi-input-regressions)$'
```

An optional final harness argument supplies the preserved pre-logger DLL for
old/new comparison; that is local evidence, not a mandatory fixture in a fresh
checkout. The executed copy/logs/PE outputs are in ignored
`generated/error-logging/`. Temporary test logs were confined to test-owned roots.

## Limits and scope

The full correctness suite, all control-query failure permutations, every possible
exception/resource exhaustion, cross-session/different-integrity hosts, power-loss
recovery, actual disk exhaustion, Debug/non-AVX2 builds and installed SAPI/WoW were
not exercised. No benchmark/profile or end-to-end performance campaign ran; added
wall-clock overhead is unmeasured. Repeated independent failing requests can each
log an event; bounded rotation limits storage, not total error volume.

Files changed: root CMake and development/validation wiring, SAPI adapter,
new private logger source/header, test DLL definition and focused harness, plus
current guide pointers and this document. No neural/model/RNG/PCM implementation,
voice format, WPF service/UI or frontend ABI was changed.

No existing user log, real registry entry, ProgramData Voice Library, installed
DLL/asset, certificate or speech setting was modified. No WPF rebuild, real
elevated helper, deployment, registration, signing, dependency installation or
game/user-process action ran. Nothing was staged, committed or pushed.
