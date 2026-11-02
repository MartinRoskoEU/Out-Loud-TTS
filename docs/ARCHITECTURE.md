# Current native architecture

The production synthesis backend is the official Pocket TTS Rust runtime.
The C++ Windows x64 COM/SAPI adapter remains the Windows integration layer.
All neural inference runs in the official executable; native code owns Windows
integration and verified asset access.

## Native profiles and model selection

Profiles remain independent `.safetensors` files in the protected machine Voice
Library. Schema-2 metadata and byte-preserving native import/export are unchanged.
Shared mapped-file access, SHA-256 identity, bounds validation, file/ancestor pins,
ACL/ownership checks and optimistic concurrency guards remain. Unsupported legacy
data is not converted or changed during normal reads/import. Confirmed complete
uninstall has a separate [owned-resource cleanup policy](../installer/PRODUCTION.md#approved-file-cleanup-policy):
remove verified managed resources and leave unknown user content in place.

The existing managed SAPI token contains `Backend=Rust`, the pinned runtime path,
native profile path and ownership/identity/display fields. It has no model binding.
`RustModelSelection` reads the machine-wide explicit WinUI Settings selection,
verifies it against the compiled configuration/baseline identities or an independently
validated protected model-update receipt, and retains the existing model/profile/runtime
leases. Confirmed model updates preserve the selected alias and retire superseded,
unreferenced files through durable cleanup; rollback is not exposed. Unavailable
selection is an error, with no fallback.
See [production Rust integration](RUST_RUNTIME_SESSION.md#production-integration-independent-native-profiles-and-settings-selection).

## SAPI and process lifetime

The existing official-runtime session owns its loopback server process/job,
incremental WAV/PCM reader, cancellation and cleanup. The joined C++ coordinator
retains the existing rendezvous and caller-thread SAPI site calls, format checks,
partial writes, volume, bookmarks and Sonic speech-rate processing. Resident
destruction still stops Rust before releasing verified asset leases. Settings
selection changes take effect between utterances through the existing mechanism.

OutLoud.WinUI requests administrator approval at startup and runs elevated for
the session. Model, runtime-update and Settings changes use the existing
administrator service paths without further UAC prompts. The authenticated
elevated helper still provides native import, registration and coordinated
removal, and retains its allowlisted shared-data worker route. External SAPI
clients do not need elevation for playback. Persistent state is under
`%ProgramData%\OutLoud\ApplicationData`, protected against ordinary-user writes;
native voice profiles are shared under `%ProgramData%\OutLoud\VoiceLibrary`.
Private temporary speech/diagnostic operations remain caller-local.
Signing, certificates, strict registration
schemas, transaction/rollback, protected ownership/ACLs and deployment validation
are retained. [SAPI_ADAPTER.md](SAPI_ADAPTER.md) documents the fixed WinUI deployment
inventory and clean development workflow. Builds and tests do not install anything.

See the [current documentation index](README.md) and
[build and validation guide](BUILD_AND_VALIDATION.md).
