# Step 3D.3: prepared Pocket voice import from WPF

> Historical Step 3D.3 implementation/validation record. Current operations and protocol v3 are in [VOICE_MANAGER_GUIDE.md](VOICE_MANAGER_GUIDE.md); build/validation is in [A11/A12](BUILD_AND_VALIDATION_IMPROVEMENTS.md). Earlier protocol/scope/test commands below are evidence, not current procedures.

[Step 3D.3.2](VOICE_MANAGER_REMOVE.md) adds Remove Voice through the same helper
and explicitly updates the current protocol to v3. Earlier protocol descriptions
below and in the Replace document are historical validation records.

The current workflow extends this step with [Step 3D.3.1 Import-or-Replace](VOICE_MANAGER_REPLACE.md).
That document supersedes the protocol v1 description below with protocol v2,
documents the additive read-only content getter, and describes replacement
and dismissible severity notifications. The user subsequently reported successful
live Step 3D.3 import and duplicate rejection; the original validation record below
describes what was verified during implementation.

The normal x64 Voice Manager now selects complete rows and imports prepared
Pocket `.safetensors` voices through a one-shot elevated native helper. The UI
remains `asInvoker`. Real UAC, machine import and Windows/WoW playback remain
manual and unverified. No production mutation, staging, commit or push occurred.

## Audit and runtime resolution

Audited the WPF view/model/service, read-only C ABI, `voice_library.hpp`,
`voice_import.hpp`, `sapi_voices.hpp`, their implementations, source/path pinning,
machine ACL checks, token ownership/transactions, engine file identity, CLI
composition, fixture seams and deployment scripts. Native business logic,
security checks, metadata/.olv schemas and read-only ABI v1 remain unchanged.

**Architecture gap found and reported before implementation:** the token CLI
requires explicit `--runtime-root`; there was no backend discovery API. Empty
runtime supports limited read diagnostics, not installation.

The new helper resolver follows the audited layout in
`sapi/scripts/prepare-deployment.ps1`, `register.ps1` and
`tools/sapi-voices/main.cpp`:

1. Read only the default machine Registry64 value at
   `HKLM\SOFTWARE\Classes\CLSID\{F38C3398-3552-43B9-8350-7DD301327D70}\InprocServer32`.
   Walk every ancestor with `REG_OPTION_OPEN_LINK`; reject links/unreadable state.
2. Require a well-formed REG_SZ local DLL path in the documented
   `<deployment>\bin\OutLoudPocketSAPI.dll` layout.
3. Reuse `validate_engine_registration` for pinned local engine-file identity.
4. Derive `<deployment>\runtime\pocket-english.olp` and
   `<deployment>\runtime\pocket-english-tokenizer.bin`.
5. Pass the configuration to existing `SapiVoiceManager::install`. It validates
   canonical machine root, healthy voice, engine identity again, model,
   tokenizer, compatibility and owned machine Registry64 token before writing.

No development deployment path, search/fallback, HKCU/HKCR overlay, token-selected
root or UI runtime configuration is used. Nothing registers the engine. Missing
registration, unsupported layout or invalid assets produce structured failure.
Resolution follows library import, so these failures keep the imported entry
and report partial success. Tests use private file-registration fixtures; they
do not call production discovery or inspect real machine registration.

## Selection, import UX and lifetime

The standard DataGrid already had `SelectionUnit=FullRow`, `SelectionMode=Single`.
Removed its individual cell-focus border; row selection/focus resources remain
readable in light/dark/high contrast. Refresh selection stays keyed by library ID.

Import Voice opens the normal Windows single-file picker for `.safetensors`.
The themed confirmation defaults `undead_female` to `Undead Female`, allows
editing, trims the name and checks basic presentation concerns only. It does
not read tensors or duplicate backend validation. The dialog explains UAC and
the supported Pocket English preparation assumption. The helper asserts the
existing supported compatibility ID; preparation provenance remains false,
since the source format cannot prove its originating model. `.olv` import,
rename/remove and duplicate overrides are outside this step.

The ViewModel guards both Import and Refresh before awaiting. Cancelling the
picker/name dialog launches nothing. After confirmation, a background task
starts the exact adjacent `OutLoudVoiceImportHelper.exe` with Windows `runas`.
Only generated endpoint/numeric parent PID are arguments. Source/name travel
over the pipe. The main UI remains normal and responsive. Win32 error 1223 maps
to cancellation without a backend call.

State text distinguishes choosing, waiting for approval, importing, success,
cancellation, failure and partial success. The helper sends a started frame
before native dispatch; no percentage is invented. Results are inline, with
expandable native diagnostics/duplicate IDs. Import cannot overlap another
import or Refresh in the same UI. Existing locks/transactions govern separate
processes. Main/dialog content shares dynamic resources, and the theme service
tracks both native captions, detaching closed dialogs safely.

The helper requires an enabled Administrator token, accepts one request,
performs one import/install sequence and exits. It statically links project
backend code and never loads the read-only bridge or invokes a CLI. It always
constructs default canonical ProgramData VoiceLibrary and machine scope. Private
test injection is statically linked C++ code, never a command-line option.

## IPC and privilege boundary

Versioned little-endian binary, maximum **65536 bytes per frame**:

| Field | Encoding |
| --- | --- |
| Header | uint32 magic `0x314d4c4f`, version `1`, kind, payload byte length |
| Request kind | `1`: import prepared Pocket voice and install machine token |
| Request payload | source path, display name; no additional fields |
| Strings | uint32 byte length + strict UTF-8, max 32768 bytes, no NUL |
| Result kinds | `1` success, `2` failure, `3` partial; `4` started has empty payload |
| Result payload | ID, domain, code, message, path, library cause, import cause, tensor, uint32 Win32 error, uint32 duplicate count, duplicate IDs |

Reject unknown versions/operations, malformed UTF-8, oversized/truncated fields,
NUL, trailing data and inconsistent result IDs/outcomes. Source must be an
absolute local `.safetensors` path. Existing native validation owns regular-file,
reparse/hardlink/ADS, name, tensor/schema and compatibility checks. No request can
choose a destination, library root, hive/key, runtime root, program or command.
Only the newly imported ID goes to the existing owned-token installation API.

The UI creates a random local pipe before launch: first instance, one connection,
remote-client rejection, no inherited handle, protected DACL for exact caller
SID, Administrators and SYSTEM. The administrator ACE supports alternate-user
UAC. No temporary request/result files exist. The UI accepts only the client PID
of the process returned by Windows and retains its process handle. The helper
checks server PID against its held parent process handle, matching session and
exact adjacent `OutLoud.VoiceManager.exe` image path. Identification-only client
security QoS prevents an unelevated server impersonating an elevated token.
These PID queries are documented [Windows APIs](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getnamedpipeclientprocessid).

Helper resolution is an absolute application-directory path; working directory
is System32. No selected path/name enters shell arguments. The helper uses
`/DEPENDENTLOADFLAG:0x800` and `SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32)`
for initial/subsequent DLL imports. New managed Windows imports also specify
System32. The helper has no project DLL dependency; the existing absolute
read-only bridge loader is unchanged. A System32 x64 VC runtime is required;
adjacent redistributable DLLs are not a fallback. See
[Microsoft's linker documentation](https://learn.microsoft.com/en-us/cpp/build/reference/dependentloadflag?view=msvc-170).
Read-only dumpbin inspection verified the built helper's `0800` load flag and
Windows/VC/UCRT dependency closure.

**Trust assumptions:** UI/helper executable and managed app files must themselves
be trusted. Workspace output is an unsigned, user-writable developer build the
tester explicitly chooses to approve in UAC. PID/path checks do not authenticate
a replaced binary or protect a compromised same-user UI. Production packaging
must establish publisher trust and install executable/app files in an
Administrator-protected directory. No signing/deployment was done here. Machine
registration/runtime are existing administrator-managed configuration. Source
contents are pinned/validated when native import opens them; the picker does
not establish a pre-UAC content identity.

Handshake I/O waits are bounded (UI 20 seconds after approval; helper 15 seconds
or parent exit). Native mutation has no forced timeout/kill: once dispatched,
it finishes the existing operations and exits even if the UI disappears. Parent
exit observed before dispatch suppresses dispatch. No privileged persistent
service/listener remains. Exceptions are contained by operation/process entry
boundaries; no C++ exception crosses the frontend ABI.

## Outcomes

| Outcome | State/UI behavior |
| --- | --- |
| Picker/name/UAC cancelled | No native mutation; return to normal usable UI |
| Import failure | Preserve structured native cause; no successful ID |
| Duplicate | Existing IDs shown; no new entry or override |
| Import/install succeed | Read-only refresh; select new immutable ID |
| Import succeeds, install fails | Keep entry; “Voice imported but SAPI registration failed”; preserve error/ID; refresh/select |
| Communication fails after submission | Unknown outcome; refresh; inspect before retrying |
| Post-import refresh fails | Preserve previous snapshot and import outcome; separate refresh error |

There is no atomic library/registry transaction and no silent rollback/delete.
The read-only bridge retains its documented identity/path/name-only diagnostics;
full runtime validation occurs during installation. Existing Gnome/Undead entries
are not changed by this operation.

## Files and automated validation

New native files: `frontend/mutation/{mutation.hpp,mutation.cpp,helper.cpp,tests.hpp}`.
`CMakeLists.txt` adds the helper and extends existing bridge regression fixtures;
`frontend/native/tests.cpp` composes those fixtures. No new CTest; count stays 17.

New UI files: `Models/VoiceImport.cs`, services `IVoiceImportService.cs`,
`ImportProtocol.cs`, `ElevatedVoiceImportService.cs`, `ImportDialogService.cs`,
view `ImportVoiceDialog.xaml`/code-behind and tests `ImportTests.cs`.
Updated main ViewModel/view, App composition, Common styles, WindowsThemeService,
app/test project files, `ui/build.ps1` and `ui/validation/RuntimeValidation.cs`.
This document is linked from earlier WPF/theme documentation.

```powershell
$env:DOTNET_GENERATE_ASPNET_CERTIFICATE = 'false'
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
dotnet build ui/OutLoud.VoiceManager.sln -c Release -p:Platform=x64
dotnet run --project ui/OutLoud.VoiceManager.Tests/OutLoud.VoiceManager.Tests.csproj -c Release -p:Platform=x64 --no-build
ctest --test-dir build -C Release --output-on-failure
.\ui\validation\validate-with-runtime.ps1
git diff --check
git status --short
```

Release x64 native/helper and WPF build checks; **56 frontend checks**, including
actual local pipe round trips with an in-process fake peer, result/peer rejection,
UAC-denial fake launcher, dialog/busy/cancel/duplicate/partial/unknown handling,
refresh and new-ID selection. Native checker uses Scratch and FakeRegistry for
valid/invalid prepared imports, duplicate IDs, successful install, preserved
partial state, runtime failure, Unicode, request bounds/unauthorized operations
and registration-file layout. The full existing suite passes **17/17 CTests**.
SDK-built WPF validation creates compiled views without showing windows and
checks full-row/focus styling, dialog editing, theme resources, states/selection
and resize layout. No automated production import, real HKLM mutation, UAC or
production discovery. SDK commands suppress ASP.NET certificate generation.

## Manual real-machine test — not executed

The machine engine must already be manually registered from the documented
deployment with valid model/tokenizer assets. Do not redeploy it for this test.
Choose a known prepared Pocket English voice whose content is not already in
the library; reusing existing Gnome/Undead content should reject a duplicate.

1. Launch from a normal repository shell:
   `& .\ui\OutLoud.VoiceManager\bin\Release\net10.0-windows\OutLoud.VoiceManager.exe`.
2. Verify no startup UAC. Click cells across all columns/use keyboard navigation;
   confirm complete-row selection in each theme.
3. Click **Import Voice**; select the prepared `.safetensors`.
4. Enter/confirm its name; check permission/model explanation. Cancel once:
   no UAC or new voice should appear.
5. Repeat and click **Import**. UAC should appear only now for the adjacent
   `OutLoudVoiceImportHelper.exe`; verify its expected developer-build location.
6. Reject UAC once and verify cancellation/re-enabled commands. Repeat and approve.
7. Verify the refreshed list selects the new entry with **Library Status = valid**
   and **SAPI Status = correct**.
8. Inspect ID, compatibility, OLV v1, `pocket_prepared`, provenance false.
9. Refresh/resize; verify selection remains. Test light/dark/high contrast and
   live theme changes with the confirmation dialog open.
10. Repeat the same file: duplicate explanation/ID, no additional entry.
11. Restart WoW manually, enumerate TTS voices, find the exact imported `<name>` and speak
    a short test with it. Fixtures do not prove live enumeration/playback.

Stop after Step 3D.3 automated validation. No real import, elevation, production
registry/library mutation, signing, deployment, registration/unregistration or
WoW launch occurred. No staged/committed/pushed files.
