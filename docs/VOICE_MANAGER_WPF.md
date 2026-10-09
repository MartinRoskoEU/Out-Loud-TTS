# Step 3D.1: read-only WPF Voice Manager

The current frontend also includes [Step 3D.4 Voice Tester](VOICE_MANAGER_TESTER.md),
with exact installed machine-token SAPI playback, cooperative Stop and WAV export.

The current frontend also supports [Step 3D.3.2 Remove Voice](VOICE_MANAGER_REMOVE.md),
with confirmation, helper-only elevation and the existing severity notifications.

The current frontend also implements [Step 3D.3 import and elevation](VOICE_MANAGER_IMPORT.md).
Mutations use the separate helper. [Step 3D.3.1](VOICE_MANAGER_REPLACE.md) adds
Import-or-Replace and an additive content-metadata getter, preserving existing
ABI v1 structures and read-only behavior.

The frontend's current visual design and Windows light/dark/high-contrast
support are described in [Step 3D.2](VOICE_MANAGER_THEME.md). The backend boundary
and Step 3D.1 functionality documented here remain unchanged.

Entry was clean at `e698d20` (Add machine-wide multi-voice support). Audited the
CMake targets/static dependencies, `voice_library.hpp`, `sapi_voices.hpp`, their
errors/results, known-folder resolution, Registry64 machine scope, SAPI COM
exports, x64 constraints and build ignores. No existing frontend or suitable
C ABI was present. Native business logic and security policies are unchanged.

## Target and build

The frontend, frontend tests and supplementary view-validation project target
**net10.0-windows, Windows x64**, using the intentionally selected .NET 10 LTS
SDK **10.0.401**. `dotnet --list-sdks` and `dotnet --info` confirmed that SDK at
`C:\Program Files\dotnet\sdk`, MSBuild 18.9.11, x64 host 10.0.12, and installed
.NETCore/WindowsDesktop runtimes 10.0.12. No SDK/workload was installed or
updated during this continuation. Projects have no third-party package
dependencies; PlatformTarget remains x64 and the app manifest remains asInvoker.

The initial implementation had used a provisional .NET 8 target while no SDK
was available. That build limitation is now resolved: normal SDK compilation
of the solution and real WPF/XAML/BAML generation pass with zero warnings or
errors. Existing native business logic, API/ABI and security behavior are
unchanged by the retarget.

From the repository root with the installed Windows .NET 10 SDK:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
$env:DOTNET_GENERATE_ASPNET_CERTIFICATE = 'false'
dotnet build ui/OutLoud.VoiceManager.sln -c Release -p:Platform=x64
dotnet run --project ui/OutLoud.VoiceManager.Tests/OutLoud.VoiceManager.Tests.csproj -c Release -p:Platform=x64
.\ui\build.ps1
.\ui\validation\validate-with-runtime.ps1
git diff --check
```

`ui/build.ps1` configures/builds the bridge, builds the x64 WPF solution and runs
its already-built fake-service tests. It never starts the app. Both build/check
scripts temporarily disable the SDK's first-run ASP.NET certificate generation
and restore the previous process setting afterward. `NativeBuildDirectory` defaults to
repository `build/`; override it for another CMake tree. The project requires
`<NativeBuildDirectory>/<Configuration>/OutLoudVoiceManagerNative.dll` and
copies that exact file beside the executable. No deployed runtime or personal
path is used by the build. Normal Microsoft VC runtime dependencies must be
available. This step does not package/sign/deploy those dependencies.

Both main projects explicitly use `bin/<Configuration>/` before the target
framework suffix, so `-p:Platform=x64` does not add an extra x64 directory to
their executable paths. Release output is
`ui/OutLoud.VoiceManager/bin/Release/net10.0-windows/`, containing:

```text
OutLoud.VoiceManager.exe
OutLoud.VoiceManager.dll
OutLoud.VoiceManager.deps.json
OutLoud.VoiceManager.runtimeconfig.json
OutLoud.VoiceManager.pdb
OutLoudVoiceManagerNative.dll
```

The executable, managed assembly and bridge PE machine fields are AMD64. The
copied bridge is byte-identical (SHA256) to `build/Release/OutLoudVoiceManagerNative.dll`.
Runtime configuration targets net10.0 and the Core/WindowsDesktop frameworks;
the installed 10.0.12 runtimes satisfy it. The native resolver still constructs
its path from AppContext.BaseDirectory, with no personal/deployed asset path in
the frontend configuration. Real-machine loader/enumeration behavior remains
for the manual launch.

## Structure and UI

```text
frontend/native/                  dedicated C ABI bridge and isolated tests
ui/OutLoud.VoiceManager.sln
ui/OutLoud.VoiceManager/
  App.xaml(.cs), app.manifest      composition; requestedExecutionLevel=asInvoker
  Views/MainWindow.xaml(.cs)      standard WPF controls; no native calls
  ViewModels/                     notifications, refresh command/state
  Models/                         immutable snapshots, native error data
  Services/                       injectable service; Task.Run native adapter
  NativeInterop/                  ABI marshalling, SafeHandle, DLL resolver
  Themes/                         shared styles, palettes and Windows-theme helper
ui/OutLoud.VoiceManager.Tests/     console assertion harness; no package dependency
ui/validation/                    SDK-built supplementary BAML/binding check
```

`MainWindow -> MainViewModel -> IVoiceManagerService -> NativeVoiceManager ->
C ABI -> VoiceLibrary/SapiVoiceManager`. No C# CLI process, registry access,
metadata parser or ProgramData walk. The main title is **Out Loud**, page
**Voices**, header **Out Loud Voice Manager**. Refresh reads on a pool thread;
view-model updates resume on the WPF context. The guard is set before awaiting,
so duplicate refreshes are rejected and the command is disabled while loading.
Snapshots are replaced as a whole; selection is retained by immutable ID,
including DataGrid selection clearing when its ItemsSource changes. A failed
refresh keeps the last successful snapshot and displays an error/diagnostics.

Columns show name, exact native library health and exact native SAPI status.
Selection shows ID, health/detail, SAPI status/detail or structured row error,
OLV version, compatibility ID, import kind and provenance flag. Missing metadata
displays unavailable values. Health already carries native compatibility
classification; no additional model/provenance inference is made. Loading,
empty and error states use inline controls. There are no mutation buttons.

## Exact C ABI, v1

Declared in `frontend/native/voice_manager.h`, exported from the separate
`OutLoudVoiceManagerNative.dll` (SAPI COM exports are untouched):

```c
uint32_t __cdecl outloud_vm_abi_version(void);
int32_t __cdecl outloud_vm_read(outloud_vm_result** result);
const outloud_vm_snapshot_v1* __cdecl outloud_vm_result_snapshot(const outloud_vm_result* result);
const outloud_vm_error_v1* __cdecl outloud_vm_result_error(const outloud_vm_result* result);
void __cdecl outloud_vm_result_free(outloud_vm_result* result);
```

Windows x64 only, extern C, pack 8; no STL types or exceptions cross the ABI.
All strings are **NUL-terminated UTF-8**. The opaque result owns the POD record
array, nested errors and all strings. Returned pointers are borrowed and stable
until that result is freed. Copy everything before freeing; free only the
result using the exported function, never a field or a different CRT allocator.
Calls own independent state; consuming one result does not invalidate another.
Getters/free accept NULL. Caller must not free a result during access or twice.

| POD | x64 bytes | Fields in declaration order |
| --- | ---: | --- |
| snapshot | 24 | UTF-8 library_root, uint64 voice_count, voice record pointer |
| voice | 88 | library_id, nullable display_name, health, library_detail, nullable sapi_status, nullable sapi_detail, nullable compatibility_id, nullable import_kind, nullable sapi_error pointer; uint32 metadata_present/olv_version/provenance_verified/reserved |
| error | 64 | domain, code, message, path, library_code, import_code, tensor; uint32 system_error/reserved |

Error strings are non-NULL; absent causes/path/tensor are empty. Voice metadata
fields/version/provenance are meaningful only with metadata_present=1. Flags
are 0/1, reserved fields zero. Empty arrays have count=0 and voices=NULL.

Read first clears the output pointer. Status values: OK=0, ERROR=1,
INVALID_ARGUMENT=2, OUT_OF_MEMORY=3, INTERNAL_ERROR=4. OK returns an owned result
with snapshot and no top error. ERROR returns an owned result with structured
error and no snapshot. Other statuses return no result. A per-voice SAPI
inspection failure is part of an otherwise successful snapshot: sapi_status
is NULL and sapi_error preserves domain/code/message/Win32 and nested causes.
There is no fabricated boolean or token status. Native library/SAPI errors
retain their original names, library/import causes and relevant paths/tensor.
Unexpected C++ exceptions become a generic bridge internal_failure; allocation
or boundary failures remain explicit status codes, never escaping to C#.

C# checks ABI version, copies UTF-8/PODs into managed immutable objects and frees
the native allocation through SafeHandle, including failure paths. Missing DLL,
wrong architecture, missing export and unsupported ABI have distinct bridge
codes. Concise summaries and expandable diagnostics retain native error data.
The resolver loads an absolute path under AppContext.BaseDirectory; dependencies
search the DLL directory and System32, with no bridge lookup through PATH/CWD.

## Native read semantics and scope

The public read has **no root, scope or mutation option**. It constructs the
existing default VoiceLibrary (SHGetKnownFolderPath ProgramData), calls
list_voices, then existing SapiVoiceManager.inspect for each ID with **machine
scope** (HKLM Registry64), the real registry adapter and production policy.
Malformed library entries remain visible; an invalid ID can carry a row-level
SAPI error. Registry-only orphan tokens are not additional library rows.

No intended shared runtime configuration is supplied. Existing inspect semantics
compare library identity/path/name and preserve registered AssetRoot/ModelPath/
TokenizerPath. The native detail **"Library identity/path/name checked; shared
runtime configuration was not supplied."** is shown. A Correct status here is
not a fresh engine DLL/model/tokenizer runtime validation. The compatibility ID
is metadata; library health is the existing backend validation result.

Library and registry reads occur sequentially under their existing safeguards;
this is a completed UI snapshot, not a transaction spanning both stores. A
concurrent external mutation can change observations between reads; Refresh
obtains another snapshot. No reconcile, install/update/remove, ACL hardening,
COM activation/registration, elevation or mutation broker is called.

## Validation and remaining manual check

| Executed check | Result |
| --- | --- |
| `cmake -S . -B build -A x64` | Passed, MSVC/Windows SDK 10.0.26100.0 |
| `cmake --build build --config Release --parallel` | Passed, including dedicated x64 DLL/test |
| `ctest --test-dir build -C Release --output-on-failure` | **17/17 passed**, 48.91 s (16 existing + one bridge regression) |
| `dotnet build ui/OutLoud.VoiceManager.sln -c Release -p:Platform=x64` | Passed: SDK 10.0.401, zero warnings/errors, real WPF compilation |
| `dotnet run --project ui/OutLoud.VoiceManager.Tests/OutLoud.VoiceManager.Tests.csproj -c Release -p:Platform=x64` | Passed: **27** fake-service checks, real SDK-built net10.0 assembly |
| `.\ui\build.ps1` | Passed: native bridge + x64 solution + all 27 frontend checks |
| `.\ui\validation\validate-with-runtime.ps1` | Passed: real MainWindow BAML/InitializeComponent/layout/bindings with fake data; no window shown |
| Generated WPF files and output packaging | MainWindow.g.cs/MainWindow.baml/App.g.cs generated; x64 executable/assembly/bridge, matching bridge hash |
| `git diff --check` | Passed |

Native tests compile the identical bridge implementation with a private C++
injection seam, not a production export. They use unique temporary libraries
and a fake machine registry. Fixtures are prepared using existing import/token
APIs; mutation/engine verification is rejected during tested reads. Tests cover
empty/multiple/Unicode records, metadata presence, stable independent result
ownership, native missing/correct/stale/wrong/malformed/conflict/unhealthy
statuses, production policy rejection of temporary roots, per-row registry
denials, top library failures, hidden unexpected exceptions and unchanged file/
registry contents. DLL export loading/version checks never call production read.

C# tests use only a fake service: empty/multiple listing, selected details,
atomic replacement, ID-preserved/removed selection, structured errors retaining
the previous snapshot, loading, duplicates, recovery, absent metadata and ABI
layout. No production reads or real registry mutation in automated validation.

The primary frontend validation is now the normal SDK solution build and test
run. App.xaml passes the WPF toolchain and generates App.g.cs; its current
resources merge the initial Light palette and Common styles added in Step 3D.2.
MainWindow.xaml generates Views/MainWindow.g.cs, with the real
PresentationBuildTasks InitializeComponent calling Application.LoadComponent,
and Views/MainWindow.baml embedded into the assembly's generated resources.
The production compile has no validation-only InitializeComponent stub.

`ui/validation/validate-with-runtime.ps1` now runs a net10.0 SDK project that
references the actual WPF project. Its STA harness constructs MainWindow, uses
the real generated InitializeComponent/BAML and lays out fake empty, selected,
replaced and error states while monitoring binding errors. It initializes App's
resource dictionaries without calling App.OnStartup, using NativeVoiceManagerService,
invoking production enumeration or showing a window. The old raw compiler/stub fallback is no longer
used; prior ignored generated files are outside every production compile item.
This supplementary harness passed with no binding errors. Automated validation
does **not establish real-machine production P/Invoke, installed voice contents,
visible UI rendering or operation without Administrator privileges**.

The user can now perform the real-machine read-only
manual check from the repository root in a **normal, non-elevated** shell:

```powershell
.\ui\OutLoud.VoiceManager\bin\Release\net10.0-windows\OutLoud.VoiceManager.exe
```

Confirm the Out Loud / Voices title/header, Gnome Male and Undead Male entries,
native Library/SAPI statuses and details; select a voice and Refresh to check
retained selection. Confirm the app runs without Administrator privileges. Repeated clicks
while loading should start one refresh. An empty library or read denial should
show the corresponding inline state. Close the app. Do not change the production
library/registry to manufacture test cases. This manual app launch reads the
real library/HKLM and was not performed during this step.

SDK first-run side effect: the first normal SDK build reported "Installed an
ASP.NET Core HTTPS development certificate." The certificate-generation default
had not been disabled before that command. Subsequent SDK commands/scripts set
DOTNET_GENERATE_ASPNET_CERTIFICATE=false, as described in the
[Microsoft CLI environment-variable documentation](https://learn.microsoft.com/en-us/dotnet/core/tools/dotnet-environment-variables#dotnet_generate_aspnet_certificate).
No certificate cleanup/trust command was run and the certificate store was not
otherwise inspected or changed. There was no project signing, deployment,
elevation, COM/SAPI registration or production library/token mutation.

Step 3D.2 refines the visual design and adds Windows theme awareness. Future
mutation work may add .safetensors/.olv import, rename/remove, SAPI orchestration
and a separate elevated broker/UAC boundary; none is implemented here.
No deployment/signing/COM registration/WoW activity, and no files staged,
committed or pushed.
