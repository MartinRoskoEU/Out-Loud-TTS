# Managed machine SAPI voices

Out Loud uses the fixed machine Registry64 category
`HKLM\SOFTWARE\Microsoft\Speech\Voices\Tokens`. A managed token key is
`OutLoud_Pocket_<library_id>`, with owner `OutLoud.ManagedVoice.1`, the existing
engine CLSID, native profile path and names/English language/vendor attributes.

Native tokens use `Backend=Rust`, `RustRuntimePath` and `VoicePath`. They contain
no model ID, revision or per-voice model configuration. The registered SAPI engine resolves the machine-wide explicitly
selected WinUI Settings model using the existing trusted asset resolver.

Registration validates a healthy native profile, canonical machine storage,
the exact trusted runtime and installed engine identity. Registry operations
retain typed snapshots, strict token schemas, ownership/CLSID checks,
transactional compare/exchange and rollback. Foreign, malformed or unsupported
legacy token shapes are refused rather than adopted or automatically removed.
Removal retains the token fingerprint and coordinated library recovery behavior.
Confirmed native profile replacement keeps the existing library ID, profile path
and token key/name; it does not remove and recreate the voice registration.
No source build changes Windows registrations or installed profiles.

The existing `outloud-sapi-voices` commands remain `list`, `inspect`, `reconcile`,
`install`, `update`, `remove`, `verify-engine`, and explicit `recover-removals`.
Install/update require the intended `--runtime-root`; the CLI supplies
`runtime/pocket-tts/pocket-tts.exe` and validates it through the existing runtime
trust boundary. It never assigns a model to a voice.

Deployment/signing/registration instructions are in [SAPI_ADAPTER.md](SAPI_ADAPTER.md).
