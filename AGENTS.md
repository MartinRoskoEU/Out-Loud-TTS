# Out Loud contributor instructions

These instructions apply to the entire repository. Follow the user's current
scope and preserve unrelated working-tree changes.

## Current architecture

- `ui/OutLoud.WinUI` is the active WinUI 3 / .NET 10 desktop application.
- The official Pocket TTS Rust executable is the only synthesis backend. C++
  provides the COM/SAPI adapter, streaming integration, native profile validation,
  voice management, registration, elevated helper and deployment infrastructure.
- Voice profiles are native `.safetensors` files. Import/export preserves their
  original bytes. Profiles have no model ID or revision binding.
- Synthesis uses the explicitly selected machine-wide Settings model and the
  verified active runtime. Do not add automatic model selection or fallback.
  The bundled runtime has a fixed content-integrity pin; this is not a runtime
  version allowlist for verified updates.
- Preserve SHA-256 identity guards, file mapping, validation, concurrency guards,
  asset leases, streaming, cancellation, job/process cleanup and Sonic rate processing.
- Build and document only the current Rust/native-profile architecture. No Python
  preparation environment or converted voice/model containers are required.

## Authoritative references

- `README.md`: project overview and basic build instructions.
- `ui/OutLoud.WinUI/README.md`: current UI, model/runtime management and storage.
- `docs/ARCHITECTURE.md`, `docs/VOICE_LIBRARY.md`, `docs/MULTI_VOICE_SAPI.md`:
  active native contracts.
- `docs/README.md`: index of current user, build and integration documentation.
- `docs/BUILD_AND_VALIDATION.md`: current build and scoped validation instructions.
- `docs/RUST_RUNTIME_SESSION.md`: production session, streaming and asset selection.
- `docs/SAPI_ADAPTER.md`: supported development deployment and registration.
- `installer/PRODUCTION.md`: current installer, installed updates and uninstall.
- `THIRD_PARTY_NOTICES.md`: licenses and notices for the current distribution.

Keep active documentation aligned with the current implementation; do not retain
obsolete development instructions. Preserve published release records and past
changelog entries as version-specific records, not current instructions. Do not
rewrite past validation results as evidence for a new build.

## Shared storage and security

- Models, runtime updates, inventories and Settings:
  `%ProgramData%\OutLoud\ApplicationData`.
- Native voice profiles: `%ProgramData%\OutLoud\VoiceLibrary`.
- Managed SAPI tokens are machine-wide HKLM Registry64. Temporary diagnostics
  and speech operations can remain caller-local; they are not persistent stores.
- WinUI requests administrator approval at startup. Keep the existing helper,
  authenticated handoff and ownership checks. External SAPI clients do not need
  elevation for playback.
- Reuse the established signing, certificate, ACL, physical-directory binding,
  incoming payload/hash and registration checks. The cleanup-policy change below
  does not authorize executing unverified binaries or deleting outside owned roots.
- Default uninstall removes verified application-owned resources and preserves
  unknown user files. Their presence alone must not block removal of known owned
  resources; leave containing directories when needed.
  Completed owned maintenance archives are not unknown user content: retire their
  verified backups/checkpoints automatically after successful final verification.
  Keep a protected resumable deletion plan until cleanup is verified complete.
  Verify root identity; never follow links to delete external targets or infer
  ownership from a folder name alone. Invalid ownership, changed planned resources
  and active users of owned files remain reasons to stop safely.
- Upgrades preserve managed data. Automatic cleanup is limited to the completed,
  inactive operation's authenticated working directory; it does not authorize
  deleting an entire model/voice store during startup or an update. Keep the
  resources required to recover unfinished operations until safely resolved.
  After verified runtime activation, remove verified inactive downloaded runtime
  versions through existing staging cleanup/recovery under runtime-update.lock.
  Do not retain old update versions or delete the inventoried bundled baseline.
  Busy assets remain protected and cleanup retries at startup, never in synthesis.
  Startup staging cleanup may reclaim unrecorded GUID operation directories only
  after shared-store ownership/physical binding validation, under installation
  and metadata locks, and only for the exact supported temporary layouts with
  independently validated regular files. Publish the existing cleanup record
  before deleting payloads. Unknown layouts, links and invalid records remain
  protected; this is not permission to clear whole directories by name.
- See `installer/PRODUCTION.md#approved-file-cleanup-policy` for implementation
  scope and lifecycle boundaries. Release records do not override the current
  policy; do not report a source/build change as deployed or live-tested.
- Do not modify the installed application, registrations, certificates, trust or
  user data without explicit authorization for that operation. Existing explicit
  authorization remains valid; do not ask for it again unnecessarily.

## Build and targeted validation

Use Windows x64, PowerShell, Visual Studio 2022 C++ Build Tools, CMake 3.24+,
.NET 10 SDK and the Windows SDK required by the WinUI project.

```powershell
cmake -S . -B build-winui-native -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=OFF
cmake --build build-winui-native --config Release --target OutLoudPocketSAPI OutLoudVoiceManagerNative OutLoudVoiceImportHelper outloud-sapi-voices
dotnet build ui/OutLoud.WinUI/OutLoud.WinUI.csproj -c Release -p:Platform=x64
```

Build only affected targets. Native checks are opt-in through the existing CMake
development targets; `OUTLOUD_ENABLE_RUST_SAPI` controls private checker support,
not whether production uses Rust. Prefer existing targeted checks with isolated
files and simulated registrations. Do not introduce broad suites or new test
infrastructure. Documentation-only edits need link/content and diff checks, not
application builds. A successful build does not prove visual or live behavior.

The user handles Windows Sandbox installer acceptance testing. Do not start
Sandbox or perform live installation/uninstallation as part of build validation.
Report exactly what was checked and any unverified behavior.

## UI and packaging conventions

- Use the current Settings page as the canonical WinUI visual reference. Reuse
  native controls, theme resources, spacing, typography and existing dialogs.
- Keep visual/text changes separate from bindings, commands, backend behavior
  and model/runtime selection unless the task explicitly includes those changes.
- Product identity/version comes from `ProductVersion.props`. Do not change the
  version for an installer rebuild unless requested.
- Build installers only when requested, using `installer/inno/build-installer.ps1`.
  Its reviewed payload allowlist, upstream signature checks, pinned offline VC
  package and native maintenance lifecycle are authoritative. Output goes to
  `release/OutLoud-Setup.exe` with its SHA-256 file.
- Do not introduce another installer or deployment mechanism. Building an
  installer does not authorize running it or changing machine state.
- Do not modify Pocket TTS Studio Lite as part of an Out Loud task.
- Generated/build directories can contain required inputs and diagnostics;
  ignored files are not automatically disposable. Inspect and obtain cleanup
  authorization before deleting them.

Before finishing, run relevant checks and `git diff --check` for affected tracked
files. Do not commit, tag or publish unless requested. Summarize changes,
validation and material limitations concisely.
