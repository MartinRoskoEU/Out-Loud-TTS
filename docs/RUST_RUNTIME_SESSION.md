# Official Rust runtime session

The official Pocket TTS Rust executable is the only production synthesis backend.
The C++ adapter resolves verified local assets, owns the runtime process and
streams its PCM through Windows SAPI. Native voice profiles have no model binding.

## Session and process lifetime

`outloud_rust_session` is the native session library. Production callers supply
absolute verified runtime/configuration/profile paths and retain their file leases.
The session starts the official interface without a command shell:

```text
<executable> serve --config <configuration> --default-voice <voice> --host 127.0.0.1 --port 0
```

An unnamed kill-on-close Job Object owns the process. The initial thread starts
suspended, with atomic job association and an explicit inherited-handle list,
then resumes after ownership is established. The child environment excludes
ambient Hugging Face credentials and cache overrides without changing its parent.
A joined monitor drains stdout/stderr into bounded in-memory diagnostic tails.
Readiness requires the discovered loopback endpoint and HTTP 200 from `/health`.
WinHTTP uses no proxy, redirects, cookies or authentication for this connection.

`stop()` is idempotent, terminates the owned job and verifies process exit.
Unconfirmed cleanup retains ownership for retry; the destructor retains the
kill-on-close fallback and joins the monitor. Session operations are serialized
by the caller. There is no automatic restart during an utterance.

## Streaming and cancellation

`read_pcm` sends form-encoded UTF-8 text to the prepared session's `/tts` endpoint,
reusing the default native voice. HTTP 200 is required before audio parsing.
The incremental WAV reader accepts PCM16 mono 24 kHz, validates chunk boundaries
and handles the supported official streaming-length placeholders. It rejects
unsupported formats, malformed/truncated data and incomplete sample bytes.

Fixed-size buffers feed the synchronous PCM sink without accumulating an entire
utterance or writing a temporary WAV. A slow sink provides backpressure. The
joined coordinator hands blocks to the `Speak` caller through the existing
bounded rendezvous; SAPI site calls remain on that caller thread. Partial writes,
volume, leading bookmarks and Sonic rate processing retain their existing paths.

Cancellation interrupts startup/WinHTTP work, stops the owned runtime and confirms
process exit before releasing verified assets. Resident destruction stops Rust
before its model/profile/runtime leases are destroyed. See
[cancellation and controls](SAPI_CANCELLATION.md) and [bookmarks](SAPI_BOOKMARKS.md).

## Production integration: independent native profiles and Settings selection

Native profiles and the official Rust runtime are the only production backend.
Native import uses operation 7 through the existing bounded, authenticated
one-shot elevated helper. Machine registration retains its Registry64 COM,
ownership, protected ProgramData ACL and transactional token checks. WinUI uses
native-only operations. Signing and certificate mechanisms are retained.

`VoiceLibrary::import_native_profile` copies a structurally valid Safetensors
container unchanged into `ProgramData/OutLoud/VoiceLibrary/voices/<id>/voice.safetensors`.
The existing `metadata.json` uses schema 2 with the library ID, display name,
profile/source SHA-256, original filename and import time. It contains **no model
ID, revision, compatibility assertion, or export provenance**. Native export
copies the stored bytes with the existing destination validation, optimistic
fingerprint and atomic publication rules. Unsupported metadata is not migrated
or converted.

New managed native tokens contain `Backend=Rust`, the application's own bundled
`RustRuntimePath` and protected `VoicePath`, plus the existing ownership,
identity and display fields. They do not contain a fixed model or per-voice
`RustConfigPath`: that would freeze the voice to a model, contrary to the current
selection requirement. The effective configuration path is prepared afresh by
`RustModelSelection` for the machine-wide explicitly selected Settings model.

At synthesis, `RustModelSelection` reads
`%ProgramData%/OutLoud/ApplicationData/model-selection.json` and schema-2
`model-inventory.json`. It never consults Studio Lite or the Hugging Face cache.
A missing, null, removed, invalid or unsupported selection fails synthesis; no
replacement is chosen and no downloads or updates are performed. Selection and
inventory identity are checked between utterances; changes stop the previous
resident before resolving a new one. A selection change does not interrupt an
already running utterance.

The native configuration authority and baseline asset identities are generated at
CMake configuration from WinUI's pinned official runtime YAML and public repository
snapshots. They are compiled into the native binary, independently of editable
inventory. A confirmed model update can replace only the fixed template's weight
and tokenizer references with identities from the pinned official public snapshot.
Its separate immutable receipt is stored as a SHA-256-addressed model blob and
referenced by optional `UpdateReceiptSha256` in schema-2 inventory. The resolver
pins the receipt, validates its protected owner/ACL and complete SHA-256, and checks
its exact schema, alias, repository, revision, trusted asset paths and canonical
blob identities. Inventory fields must match this receipt and the fixed template;
editable inventory YAML or hashes alone never authorize updated assets. Baseline
installations without a receipt still require the compiled exact identities.
Update installation checks that tensor names, shapes and dtypes match the previous
model, commits the new inventory with durable superseded-blob cleanup, and preserves
the selected alias. Old unreferenced weights/receipts are removed, with busy cleanup
retried at startup; no rollback is exposed. The resolver does not download metadata
or perform cleanup. Runtime YAML is generated from the trusted template, references
only verified local assets, and cannot contain unresolved
active remote references or caller-injected configuration. It lives in an owned
caller-private temporary operation directory under
`%LOCALAPPDATA%/Temp/OutLoud-Synthesis` and is removed
when the resident releases it.

The existing model-store reader/writer lease and deny-write/delete file and
ancestor handles protect the model, tokenizer, generated YAML, native profile,
voice metadata and runtime for the Rust resident's lifetime. Rust is destroyed
before these leases are released. An active SAPI resident can therefore block
model removal until the client releases it or its selection becomes unavailable.
The bundled runtime identity is the official v4.0.1 Windows x64 executable SHA-256
`813cabfc2f8fc7a4e5310deda6025426b3164610dd9f3c3098a6078e5cc1895a`
(the release's CLI reports `pocket-tts 3.3.0`), with explicit x86-64-v3 CPU/OS-state
checks before execution. The production child uses Windows image-load mitigation
to prefer System32 for its Windows-library imports. Asset/profile hashing observes
the SAPI cancellation token before the runtime is launched. Production SAPI uses
the active runtime recorded by Settings, without a runtime-version allowlist.
Installed updates resolve through the existing protected
`runtimes/<release>/<SHA256>/` storage and `runtime-installation.json` receipt;
the executable and license hashes and official release-asset identity must match.
These files remain leased for the resident's lifetime. A missing or invalid
selected update fails instead of silently selecting the bundled runtime. The
release tag identifies the installed directory; it is not a compatibility gate.

The existing `RustRuntimeSession`, joined SAPI coordinator, PCM streaming, site
controls, cancellation and job cleanup are reused. The official runtime loads
the selected model and native profile together and performs structural
compatibility checks. Successful loading **does not prove profile origin**;
quality degradation across otherwise structurally compatible model revisions is
accepted. Neither filenames nor tensor dimensions select a model.

Development build (no installer or registry writes):

```powershell
cmake -S . -B build-winui-native
cmake --build build-winui-native --config Debug --target OutLoudPocketSAPI OutLoudVoiceImportHelper OutLoudVoiceManagerNative
dotnet build ui/OutLoud.WinUI/OutLoud.WinUI.csproj -c Debug -p:Platform=x64
```

WinUI copies its own native helper/bridge alongside the application using the
existing `NativeBuildDirectory` convention (default `build-winui-native`). The
registered machine COM DLL must separately be deployed through the existing
trusted signing/installation procedure before real managed SAPI voices can use
this implementation. Builds do not deploy the DLL or change machine registration.

The opt-in `outloud-rust-selection-tests` checker copies explicitly supplied Out
Loud model assets to a unique temporary directory, creates synthetic native
profile fixtures, uses an in-memory registry, and calls the real official runtime.
With the existing private SAPI checker/DLL arguments, it also exercises real
SpVoice/SpStream through the managed selection resolver. The path overrides and
raw-path development tokens exist only in the private test build; the shipping
DLL exposes only its existing COM factory/unload exports. No Studio models,
real voice registrations, audio devices or user-data changes are needed.
