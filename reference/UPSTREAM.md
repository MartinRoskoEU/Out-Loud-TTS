# Current upstream inputs

Out Loud 0.4.0 uses the official [Kyutai Pocket TTS](https://github.com/kyutai-labs/pocket-tts)
Rust executable. C++ supplies the Windows COM/SAPI integration and native voice
management; it does not implement neural inference.

## Bundled runtime

- Official release: `v4.0.1`.
- Source commit: `a922b912dfa366975294afb568d815b305acde5d`.
- Windows x64 CLI diagnostic version: `pocket-tts 3.3.0`.
- Executable SHA-256: `813CABFC2F8FC7A4E5310DEDA6025426B3164610DD9F3C3098A6078E5CC1895A`.
- Original LICENSE SHA-256: `36516AEFDC84C5D5A1E7485425913A22DBDA69EB1930C5E84D6AE4972B5194B9`.

The retained executable/license inputs are under
`development/runtime/pocket-tts/v4.0.1/windows-x64/extracted/pocket-tts-3.3.0-x86_64-pc-windows-msvc/`.
The build copies them to `runtime/pocket-tts/` and verifies executable identity.
The adjacent provenance/checksum files record the upstream release source.

This baseline content pin is not a runtime-version allowlist. Verified runtime
updates activated through Settings use the existing protected update receipt,
content verification and file leases.

## Model and profile inputs

Settings uses the public `kyutai/pocket-tts-without-voice-cloning` model repository.
Embedded configurations and immutable repository metadata under the WinUI
project's `RuntimeMetadata/v4.0.1` establish trusted asset mappings. Each installed
model's inventory records its actual asset revisions, sizes and content hashes.
No Python oracle, converted model container or reference recording is required.

Users supply native `.safetensors` voice profiles. Import/export preserves their
bytes; voices are not bound to model identifiers or revisions. Synthesis uses
the model explicitly selected in shared Settings.

See [third-party licenses and source availability](../THIRD_PARTY_NOTICES.md),
[model management](../docs/MODEL_UPDATES.md),
[Sonic sources](../third_party/sonic/) and
[production integration](../docs/RUST_RUNTIME_SESSION.md).
