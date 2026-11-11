# Out Loud WinUI application

OutLoud.WinUI is the Out Loud 0.4.4 desktop frontend: an unpackaged WinUI 3 /
.NET 10 application with Voice Library, Voice Tester and Settings navigation.
It uses the application's Mica backdrop, theme resources and native controls.
The custom XAML title bar is 40 DIP high, displays the application title without
an icon and preserves native caption controls. The executable and taskbar use
`Assets/OutLoud.ico`.

The official Pocket TTS Rust executable is the only synthesis backend. C++
provides the COM/SAPI adapter, native profile validation/management, bridge,
authenticated helper and deployment infrastructure. External SAPI applications
use the registered engine rather than a separate WinUI synthesis pipeline.

## Shared storage and administrator approval

The manifest requests `requireAdministrator`. Windows requests UAC approval
when needed at startup; the application then uses that authority throughout the
session. Native voice operations retain their authenticated helper and checks.
External SAPI clients, including games, do not need elevation for playback.

| Shared resource | Location |
| --- | --- |
| Models, runtime updates, inventories and Settings | `%ProgramData%\OutLoud\ApplicationData` |
| Native voice profiles | `%ProgramData%\OutLoud\VoiceLibrary` |
| Managed SAPI voice tokens | HKLM Registry64 |

`MachineDataStorage` resolves CommonApplicationData. Protected store bindings
verify the owning installation and physical directory identity. SYSTEM and
administrators can write; ordinary users can read. Updates preserve managed
data. No alternate application's store or Hugging Face cache is used.

Temporary diagnostic job markers are caller-local under
`%LOCALAPPDATA%\Temp\OutLoud-Operations`, with named jobs
`Global\OutLoud.WinUI-<operation-guid>`. Temporary speech configuration uses
`%LOCALAPPDATA%\Temp\OutLoud-Synthesis`. These are not persistent model or
Settings stores. Native error logs are separately bounded; see
[error logging](../../docs/NATIVE_ERROR_LOGGING.md).

## Runtime management

Settings checks for published stable Out Loud releases automatically at startup.
The Out Loud card shows the installed version, check result and **View release**
link when a newer version is available. Checks use the public GitHub release API,
have a bounded timeout, and never install application updates automatically.

Settings has one **Check for updates** button in its header for application,
runtime and model checks. It forces a fresh application/runtime check and the existing manual model check,
which bypasses the model comparison cache. Each check keeps its own status;
unavailable runtime metadata does not skip model checks. Concurrent checks or
conflicting operations disable the shared action. **Update** appears in the runtime
card only when available, with **Cancel** during an operation. Checking never
installs an update, and runtime/model updates retain separate confirmations.
The application includes its own verified baseline runtime;
there is no runtime Install/Remove workflow. Runtime updates share the existing
operation guards with model installation, verification, removal and selection.

**Repair runtime** appears when the selected executable is missing. Confirming
repair downloads that exact release from Kyutai, verifies the archive digest
and the saved executable hash (the fixed pin for the baseline), and atomically
publishes only the missing EXE. Existing files are never overwritten. The saved
runtime identity, model selection, models and voices are preserved. Update
receipts and the existing LICENSE must remain valid; missing directories or
invalid metadata require installer repair. Repair uses the existing elevated
helper, runtime lock and journaled staging cleanup. It never changes antivirus
settings or retries automatically if security software blocks the file.
Atomic repair publication requires staging and the runtime directory on the same
volume; cross-volume installation layouts fail safely and require installer repair.

The baseline is `runtime/pocket-tts/pocket-tts.exe` beside the application, from
official release `v4.0.1` (CLI diagnostic version `pocket-tts 3.3.0`). Its
SHA-256 is `813CABFC2F8FC7A4E5310DEDA6025426B3164610DD9F3C3098A6078E5CC1895A`.
`PocketTtsRuntimeSourceDir` defaults to this repository's retained
`development/runtime/pocket-tts/v4.0.1/windows-x64/extracted/pocket-tts-3.3.0-x86_64-pc-windows-msvc/`.
The build verifies the executable and copies it with its original LICENSE.
See [upstream identity](../../reference/UPSTREAM.md).

Update checks verify the current identity and query the official
`https://api.github.com/repos/kyutai-labs/pocket-tts/releases/latest` endpoint.
Updates require confirmation and retain anonymous HTTPS host restrictions,
archive digest/size checks, safe extraction, executable/CLI checks, receipts and
atomic activation. The active verified runtime is used by SAPI without a version
allowlist. Invalid selected updates fail instead of silently selecting a fallback.

```text
%ProgramData%\OutLoud\ApplicationData\
  runtime-identity.json
  runtime-identity.previous.json
  runtime-update.lock
  runtimes\<release-tag>\<uppercase-executable-SHA256>\
    pocket-tts.exe
    LICENSE
    runtime-installation.json
  runtimes\.staging\<operation-guid>\...
  runtimes\.staging\<operation-guid>.json
```

After verified activation, inactive verified downloaded runtime versions are
removed using the existing staging cleanup record. Busy/interrupted cleanup is
retried at startup under the runtime update lock, never during synthesis.
The inventoried bundled baseline remains part of the application payload.

## Public model management

The verified runtime's supported language identifiers and embedded immutable
configurations/snapshots establish the install plans. Only the public
`https://huggingface.co/kyutai/pocket-tts-without-voice-cloning` repository is
permitted. Plans use the configuration's synthesis-only weights and required
tokenizer. No cloning encoder, gated weights, authentication or conversion is
required. Unknown identifiers without trusted mappings cannot be installed.

Anonymous HTTPS requests retain host/redirect restrictions, streamed progress,
timeouts, cancellation and free-space checks. Weights are checked for valid
safetensors structure and exact content identity; all active references must
resolve to inventoried local assets. A registered, verified private installation
is required for Installed status. SHA-256 blobs deduplicate shared weights;
small non-LFS assets retain Git-blob SHA-1 verification.

```text
%ProgramData%\OutLoud\ApplicationData\
  model-inventory.json
  model-inventory.previous.json
  model-selection.json
  model-install.lock
  metadata-write.lock
  models\blobs\sha256\<lowercase SHA-256>
  models\blobs\git-blob-sha1\<lowercase Git blob SHA-1>
  models\.staging\<operation-guid>\...
  models\.staging\<operation-guid>.json
  models\.operations.lock
  update-checks\model-update-checks.json
  update-checks\model-update-checks.previous.json
  update-checks\model-update-checks.lock
```

**Install** confirms, downloads, verifies and registers a model without selecting
it. **Remove** unregisters it and uses its durable pending-cleanup record to
delete only verified blobs no retained inventory record references. Unsafe or
busy assets remain protected, including leases held by synthesis.

**Check for updates** compares installed files' hashes/sizes with a pinned
current public repository snapshot. It downloads metadata only. Startup and
successful model installation queue the same check; successful comparisons are
cached by installation identity for 24 hours. Manual checks bypass that interval.
Unavailable metadata does not change the installation or selection. **Update**
appears before **Remove** when a verified compatible model has an available update.
It requires confirmation of the pinned official repository snapshot, reuses the
existing download/verification and operation guards, and checks that tensor names,
shapes and dtypes still match the installed architecture. Configuration and asset
paths remain derived from the embedded template, never remote YAML.

An immutable SHA-256-addressed update receipt occupies the existing model blob
store; schema-2 inventory references it through optional `UpdateReceiptSha256`.
The native resolver independently validates the receipt, fixed template, inventory
and all asset bytes. One atomic inventory commit activates the update and records
the superseded blobs for the existing reference-checked cleanup. Selection stays
unchanged. Previous files are removed once no installed model references them;
busy cleanup retries at startup. There is no rollback, automatic replacement or
automatic repair. Failure/cancellation before activation leaves the previous
model and selection intact. No voice profile or SAPI token is rewritten.

**Selected model** lists only verified, runtime-compatible installations.
An explicit choice is verified and atomically persisted. Startup restores only
the saved usable choice. Removal or invalidation clears the choice; a missing
selection never chooses a first model or fallback. SAPI uses this shared
selection; voice profiles remain independent of model IDs and revisions.

See [model/update procedures](../../docs/MODEL_UPDATES.md) and
[native asset selection](../../docs/RUST_RUNTIME_SESSION.md#production-integration-independent-native-profiles-and-settings-selection).

## Operation cleanup

Model/runtime downloads record temporary ownership before creating payloads.
Atomic Settings/inventory writes reuse cleanup records with a temporary
`value.json`. Startup recovery verifies shared-store binding and takes the
existing installation/metadata locks before removing inactive temporary work.
Committed Settings and installed assets are not removed by staging recovery.

Unrecorded GUID operation directories are reclaimable only when they exactly
match supported temporary layouts with independently verified regular files.
Recovery publishes the existing cleanup record before deletion. Links, unknown
layouts and invalid/conflicting records remain protected. No periodic cleaner
or synthesis-path scan is used. Complete uninstall follows the
[owned-resource cleanup policy](../../installer/PRODUCTION.md#approved-file-cleanup-policy).

## Voice Library

The page shows managed machine-wide voices, library health and SAPI status.
**Detail**, **Refresh**, **Import**, **Bulk import**, **Export** and **Remove** use the existing
native bridge/helper infrastructure. Details show status, metadata and selectable
technical information. Missing/empty libraries use a plain-language empty state;
other failures retain operation diagnostics.

Refresh preserves selection by library ID and keeps the previous snapshot on
failure. Import selects the resulting entry. Removal refreshes and can select
the first remaining row if the target disappeared. Export retains selection and
snapshot. Import/export support only `.safetensors`, preserving original bytes.
Export retains destination inspection, identity checks and overwrite confirmation.
Identical profile content is rejected regardless of its name, with a separate
message from name collisions. Names are unique without regard to case or surrounding
whitespace. Importing a different profile with an existing name offers Replace
voice, Change name or Cancel. Confirmed replacement preserves the existing name,
library ID and Windows speech identity; active voices must be released first.
Closing waits for an active helper/export operation and reconciliation.

Bulk import selects multiple `.safetensors` files and confirms the batch once.
Each default name is the unchanged filename without its extension. Files are
validated and imported sequentially through the same helper; identical profiles
are skipped, while name conflicts offer replacement, renaming or skipping.
The existing notification panel reports imported/replaced, skipped, failed and
unprocessed counts, with per-file diagnostic details. Unconfirmed/partial results
or a failed refresh stop the remaining batch. Closing waits for the current
operation and does not submit the remaining files.

## Voice Tester

The tester provides a native-profile voice selector, editable default English
test text, character count, Play, Stop, Export WAV and diagnostic messages.
The selector displays profile names only and has a selection placeholder. Play
eligibility explains missing voice/model prerequisites. It prefers the library's
selected voice when entering the section without implicitly choosing a first voice.

Play sends the full text once as plain asynchronous SAPI speech, with neutral
rate, full volume and default Windows output. The registered Rust engine resolves
the explicit shared Settings model. There is no per-voice model selector or
alternate synthesis pipeline. Known failures use plain-language guidance with
technical details available separately.

The owned STA, purge/wait and COM teardown remain authoritative. Changing voice
stops playback; navigation and close wait for cleanup. Text/tester state survive
navigation. WAV export synthesizes PCM16 mono 24 kHz through the same SAPI path,
confirms overwrite and publishes only after stream closure and validation.
Cancellation or invalid output preserves an existing destination.

## Build and run

Use the [native and WinUI build instructions](../../docs/BUILD_AND_VALIDATION.md)
from the repository root. The project uses `NativeBuildDirectory`, defaulting to
`build-winui-native`, for the matching bridge/helper output. Example:

```powershell
dotnet build ui/OutLoud.WinUI/OutLoud.WinUI.csproj -c Release -p:Platform=x64
```

Launching the built executable requests UAC according to its manifest. Use an
elevated development shell for `dotnet run`, or Visual Studio's elevated launch
workflow. Playback requires a separately deployed, registered production SAPI DLL;
building alone does not register it. See [development deployment](../../docs/SAPI_ADAPTER.md).

## Prerequisites and configuration

- Windows 11 native x64, build 22000 or later, and an AVX2/FMA-capable processor.
- .NET 10 SDK; target `net10.0-windows10.0.26100.0`.
- Windows SDK 10.0.26100.0 for development.
- Declared NuGet packages: Microsoft.WindowsAppSDK 2.5.1 and
  Microsoft.Windows.SDK.BuildTools 10.0.28000.2705.
- Default framework-dependent output requires .NET 10 x64, Windows App Runtime
  2.5.1 (or a compatible serviced runtime) and the Visual C++ v14 x64 runtime.

The application uses `WindowsPackageType=None` and the SDK bootstrapper for
framework-dependent development output. The production installer publishes
self-contained .NET/Windows App SDK components and embeds the checked VC
redistributable for conditional offline installation. See
[production packaging](../../installer/PRODUCTION.md). Product version and identity
come from `ProductVersion.props`. A successful build does not prove live UI,
playback or installer behavior; report acceptance testing separately.
