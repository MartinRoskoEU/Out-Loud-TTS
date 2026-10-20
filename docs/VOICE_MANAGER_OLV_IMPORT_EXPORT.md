# Voice Manager OLV import and export

Implemented 2026-10-10 against `d42c2bf`, preserving the pre-existing
`FINAL_ENGINE_VALIDATION.md`. This is a frontend/library integration. The installed
engine, model, OLV format, compatibility ID, inference, SAPI adapter and deployment
are unchanged. Automated mutations below used private scratch libraries and fake
registries. No real voice was imported, replaced, removed or exported.

## Integration and workflows

Review established that `VoiceLibrary::import_olv` and `export_olv` already validate
and copy OLV files. The existing bridge could only read snapshots; the helper and
replacement entry point accepted prepared Pocket input. The implementation extends
those boundaries instead of adding a second import system.

**Import Voice** opens the existing Windows picker with the default combined
`Supported Voice Files (*.safetensors;*.olv)` filter, followed by separate
`Prepared Pocket Voice (*.safetensors)` and `Out Loud Voice (*.olv)` filters.
The existing themed confirmation shows the selected path and source format,
suggests an editable filename-derived name and explains validation/installation.
Picker/confirmation cancellation does not invoke the helper or UAC.

After confirmation, WPF -> ViewModel -> service -> authenticated one-shot runas
helper -> native library -> existing SAPI manager remains the privileged path.
Prepared `.safetensors` uses its original conversion operation. `.olv` is validated
and copied unchanged with `import_olv`; it is never converted back to prepared
state and requires no Python/PyTorch. A successful import creates a new immutable
library ID and its owned HKLM Registry64 token, then refreshes/selects that ID.
Registration failure after publication remains partial success with a retained
entry. Transport loss after submission remains unknown completion and refreshes;
dispatch alone never produces a success notification.

Trimmed ordinal case-insensitive same-name matches use the existing Replace
confirmation. Multiple candidates require explicit selection. Replacement carries
the selected ID, stored name and expected full-file SHA-256, rechecks them under
the lock and stages `replace_olv` through the existing replacement journal,
validation callback, rollback and recovery code. It retains the ID/name/token;
it does not delete/reimport. Equal validated conditioning reports unchanged.
Duplicate content under another name remains a structured rejection with matching
IDs. For multiple backend duplicate IDs, OLV import refreshes and offers the same
exact-target dialog only when every ID can be resolved. It never chooses by order
or name. Confirmation does not waive native duplicate/ownership checks: replacement
still refuses content belonging to another retained entry.

**Export Voice** is beside Import/Remove/Refresh on the Library page. It requires
selected `valid` health, a stored name and valid ID/hash metadata; SAPI status need
not be `correct` because export does not synthesize or use a token. The standard
Save File dialog suggests a sanitized Unicode filename and requires `.olv`.
Existing files receive a standard Windows Yes/No overwrite prompt, default No.
Native read-only inspection captures destination identity before that prompt;
the same fingerprint accompanies the export request. Save/refusal cancellation
does no copy or elevation.

Export uses WPF -> ViewModel -> non-elevated service -> native bridge ->
`VoiceLibrary::export_olv`. It writes a real portable OLV file, with no library
metadata, machine paths or registry configuration added. Confirmed completion
shows Success; structured failures preserve the last snapshot/selection and
diagnostics. There is no library refresh after export. Shared busy/testing guards
block conflicting operations; closing waits for an active export and cancels a
still-unsubmitted export after the dialog returns. Tester **Export WAV** remains
the separate synthesis-to-audio operation.

## Validation, privilege and file safety

OLV validation uses strict header/version/layout/payload/integrity and finite
FP32 K/V prefix checks. Voice support depends on structure and integrity under the current
[playback policy](MODEL_UPDATES.md); preparing model revisions are not tracked. A readable
or syntactically valid file alone is insufficient. Missing/locked/corrupt/version/
compatibility failures retain native structured error causes. OLV version 1,
metadata schema 1 and the supported model compatibility identifier are unchanged.

Read-only inspection of the actual ProgramData library root, lock and voice files
confirmed protected SYSTEM/Administrators full control and Users read/execute.
Export therefore needs no UAC. The app remains x64 `asInvoker`; the bridge has no
installed-library mutation, arbitrary root/scope, registry or elevated destination
write interface. Existing library ACL/owner checks, shared/exclusive locking,
local path/ancestor pins and reparse/hard-link policies remain enforced. Export
holds a shared library epoch and revalidates selected name/hash; a changed or
removed source fails rather than silently exporting a different voice.

Destination inspection requires a local `.olv` path and safe existing parent.
An existing target fingerprint hashes file identity, size, last-write time and
all bytes, including an empty file. Inspection has the existing maximum voice
size bound and streams through 64 KiB scratch. Export compares that fingerprint
before creating output and holds the existing destination against ordinary
writes/deletion throughout copying. Read-only/inaccessible/locked/unsafe targets
are refused. Destinations inside the source library are rejected.

The existing unique sibling temporary-file flow copies the source, validates the
whole output and its expected SHA-256, verifies the temporary file identity and
publishes through the existing atomic handle rename. A read pin now remains live
from validation through publication so the validated temporary cannot be rewritten
in the validation/rename interval. Failed copy/validation/commit attempts delete
only the identified temporary file; normal failures preserve an existing target.
An absent destination is always committed without replacement, so a file created
concurrently is preserved and export fails. No partial file is published as success.

**Overwrite concurrency limit:** Windows denies replacement while the destination
is pinned without delete sharing; the destination pin must close immediately before
the existing atomic rename. Identity changes since confirmation and ordinary
changes during copying are rejected/prevented, but this is not an atomic
compare-and-swap of the destination identity. Another same-user writer deliberately
racing that final close/rename interval can replace the target. Prefer a fresh
filename when another application actively manages that path. No transactional
filesystem, destructive retry or share-access bypass was introduced. Microsoft's
[CreateFile sharing rules](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew)
describe the handle lifetime constraint. Forced process termination/power loss or
an OS refusal to delete a temporary can leave a hidden `.outloud-export-*.tmp`;
there is no new automatic destination-directory recovery service. Normal shutdown
and injected normal failure cleanup are covered below.

## ABI and protocol compatibility

The opaque native result remains the owner of borrowed UTF-8 strings/errors and
is freed using `outloud_vm_result_free`. ABI v1, original six exports, record
layouts and error status values remain intact. Four additive exports are:

| Export | Contract |
| --- | --- |
| `outloud_vm_capabilities` | Bit `OUTLOUD_VM_CAP_OLV_EXPORT_V1` advertises export. |
| `outloud_vm_export_destination` | Read-only local destination inspection into an owned result. |
| `outloud_vm_result_destination` | Borrowed `absent` or 64-hex fingerprint; no fabricated snapshot. |
| `outloud_vm_export` | Fixed production library; selected ID/name/hash, destination and consent fingerprint; owned structured result. |

The new frontend checks ABI and capability before export. Missing older exports
produce an actionable unsupported-export error; listing through the old exports
remains available. Inputs are bounded/validated UTF-8 and exceptions remain inside
the native C boundary. The old bool `export_olv` overload remains for CLI callers;
the new options overload supplies frontend concurrency assertions.

Helper framing/version **3**, authentication, 64 KiB request limit, UTF-8 limits
and fixed production root/hive remain unchanged. Explicit operation codes are
1 prepared import, 2 prepared replace, 3 remove, **4 OLV import, 5 OLV replace**.
OLV operations use the corresponding existing import/replacement fields, with
strict source extension/operation checks. Old operations are not reinterpreted.
Existing response outcome codes retain their meanings and now validate against
the explicit OLV operation. An older helper safely rejects an unknown operation;
the frontend preserves that failure, including when the rejection uses its old
default operation field. UI-only Export never crosses the helper protocol.

SAPI install still uses `OutLoud_Pocket_<id>`, the fixed CLSID, exact stored name,
Vendor `Out Loud`, English `409`, current ownership/default generation fields and
strict validation. Existing installed tokens are not rewritten by this feature.

## Changed files

| Area | Files |
| --- | --- |
| Native library | `engine/include/outloud/voice_library.hpp`, `engine/src/voice_library.cpp`, `voice_library_replace.inc`, `replacement_audit.hpp` (test-only export fault probes) |
| Helper | `frontend/mutation/mutation.hpp`, `mutation.cpp` |
| Bridge | `frontend/native/voice_manager.h`, `voice_manager.cpp`, `voice_manager_internal.hpp` (private test injection only) |
| Models/ViewModel | `ui/OutLoud.VoiceManager/Models/VoiceImport.cs`, new `VoiceExport.cs`, `ViewModels/MainViewModel.cs` |
| Services/interop | `Services/ImportDialogService.cs`, `ImportProtocol.cs`, new `IVoiceExportService.cs`, `NativeVoiceExportService.cs`, `VoiceExportDialog.cs`, `NativeInterop/NativeVoiceManager.cs`, `App.xaml.cs` |
| Views | `Views/ImportVoiceDialog.xaml`, `MainWindow.xaml` (same resources; toolbar wraps when needed) |
| Native tests | new `frontend/mutation/olv_tests.hpp`, `frontend/native/tests.cpp`, `tests/voice_library_test.cpp`, `sapi/tests/multi_voice.cpp` |
| Managed tests | `ui/OutLoud.VoiceManager.Tests/ImportTests.cs`, new `OlvTests.cs`, `Program.cs`, test `.csproj`, `ui/validation/RuntimeValidation.cs` |
| Current docs | `README.md`, `docs/VOICE_MANAGER_GUIDE.md`, `VOICE_LIBRARY.md`, this document |

Historical reports are preserved. No neural/SAPI adapter source, CMake test
registration, production root policy, installed binary or certificate changed.

## Executed validation

Release x64 incremental production bridge/helper/library CLI and affected
development checkers built successfully, with no compiler/linker warnings or
errors. The new test harness initially had a C++ `auto` declaration error; it was
corrected. Both final .NET 10 WPF solution and SDK view-validation builds report
**0 warnings / 0 errors**. Updated bridge/helper copies in the normal WPF output
have identical SHA-256 values to the production builds. VC runtime dependencies
use the existing installed prerequisite; no deployment or vendor copying occurred.

The relevant existing A11/A12 CTest selection contains **seven tests**. All seven
passed on their final affected sources; none remains failed/skipped/unavailable:

| Existing test | Final result |
| --- | --- |
| `voice-import-regressions` | PASS |
| `voice-library-regressions` | PASS; 236 assertions, including restricted-token read-only export and denied destination checks |
| `voice-manager-bridge-regressions` | PASS; 689 checks including helper/protocol, OLV replacement/rollback and export |
| `sapi-token-regressions` | PASS; existing fake token identity/ownership behavior |
| `sapi-multi-voice-regressions` | PASS; isolated exact OLV bytes/reimport and complete native PCM16 equality |
| `frontend-OutLoud.VoiceManager.Tests` | PASS; 208 checks, fake services/dialogs/process/UAC/pipe peers |
| `frontend-validation` | PASS; compiled WPF/BAML, fake services, no window shown |

The first selection exposed overwrite sharing violations in two native tests and
a view-test brush expectation error. Both were fixed, and the three affected
tests passed. Later focused checks were repeated only after adding explicit
restricted-token export, shutdown/ambiguous-duplicate UI coverage and direct byte
comparison to the existing round-trip test. Final cleanup review added a fourth
export fault point while the temporary read pin is active, corrected that cleanup
handle's sharing and reran the two affected native tests successfully. This is a final per-test result across
those focused runs, not a claim of one new full 31-test campaign. Existing
prepared import/replace/remove, journal recovery and Voice Tester tests remain.

New coverage includes OLV operation framing/cross-format rejection, corrupt
input, duplicate IDs, identical/changed/stale replacement, three publication
rollback points, exact token retention, checked bridge export, changed source
assertions, changed/empty/read-only destinations, partial/disk-full copy and
pre-publication faults with no temporary residue, concurrent new destination
creation, Unicode/long filename suggestions, cancel/refusal, partial/unknown
outcomes, selection/no-refresh, busy guards, dismissal and shutdown waiting.
Existing library tests retain malformed version/compatibility, in-use, access,
hard-link/reparse and metadata security checks. Real disk exhaustion was simulated
through the existing private fault-injection build; no production disk was filled.

The round trip imports the pinned Gnome OLV into one test library, exports it,
compares every byte with the source, reimports into another test library and
compares every byte again. Production loader validation passes, and the complete
`Hello traveler.` native PCM16 at seed 73/AUTO/segment limit 128 equals the
existing native reference. Existing multi-voice COM/cancellation/reassignment
checks still pass. UI tests do not synthesize for every case.

WPF validation covers OLV confirmation, export command enablement and keyboard
focusability, dynamic brushes, success/dismissal, selection and resize in Light,
Dark and High Contrast. It does not exercise real Windows file dialogs or UAC.

Build commands:

```powershell
cmake --build build-production --config Release --parallel --target OutLoudVoiceManagerNative OutLoudVoiceImportHelper outloud-voice-library
cmake --build build --config Release --parallel --target OutLoudVoiceManagerNative OutLoudVoiceImportHelper outloud-voice-import-tests outloud-voice-library-tests outloud-voice-manager-tests outloud-sapi-voice-tests outloud-sapi-multi-check
dotnet build ui/OutLoud.VoiceManager.sln -c Release -p:Platform=x64 -p:NativeBuildDirectory="$PWD\build-production" --no-restore
dotnet build ui/validation/OutLoud.VoiceManager.Validation.csproj -c Release -p:Platform=x64 -p:NativeBuildDirectory="$PWD\build-production" --no-restore
ctest --test-dir build -C Release --parallel 1 --output-on-failure -R '^(voice-import-regressions|voice-library-regressions|voice-manager-bridge-regressions|sapi-token-regressions|sapi-multi-voice-regressions|frontend-OutLoud.VoiceManager.Tests|frontend-validation)$'
```

Logs and read-only typed registry/file/ACL snapshots are ignored under
`generated/olv-import-export/`. Before/after production snapshots compare equal
across installed deployment/library files and speech/COM hives/views. Installed
engine hash remains `e1c7115d42200f3f804bbcb5cd6e583cc15a51f895ce4d70e027d04e2956fd86`.
The pre-snapshot was captured before the final focused validation, not before
all initial workspace edits/tests. No automated operation targeted production
storage at any point. Git whitespace checks pass; nothing was staged/committed/pushed.

## Manual desktop validation still required

Launch this newly built executable:

```text
C:\Users\rosko\Desktop\Nendis\application\wow-pocket-tts-sapi\ui\OutLoud.VoiceManager\bin\Release\net10.0-windows\OutLoud.VoiceManager.exe
```

1. Select a healthy voice. Verify Export disabled without selection/unhealthy
   metadata, then enabled for the valid selection. Check Light/Dark/High Contrast,
   Alt+E/Tab focus, resizing and focus return after dialogs.
2. Export to a fresh user-owned Unicode `.olv` filename. Confirm no UAC, correct
   success/dismissal, unchanged selection/list, and a valid OLV file. Repeat with
   an existing target: No leaves it unchanged; Yes explicitly replaces it.
3. Open Import Voice and verify both default filter formats, suggested/editable
   name and source-format caption. Cancel both picker and confirmation; no UAC
   should appear. Selecting the exported OLV must use the same workflow.
4. Only when deliberately authorizing a real installation, confirm Import/UAC
   using a compatible new OLV or prepared file; verify new-ID selection and exact
   SAPI name. Reimporting an identical installed OLV should report duplicate
   content. A same-name different voice requires the existing Replace dialog and
   retains the chosen ID/token after successful replacement.
5. Check cancel/refusal, stale selection, inaccessible/read-only target and
   notification diagnostics. Export must preserve the list on failure. Verify
   existing Refresh, Remove confirmation, Voice Tester Play/Stop/Export WAV.

No live UI, real UAC/helper, production mutation/export, audio-device playback or
WoW was exercised here. These desktop checks remain unverified; they are not
implicit authorization to change the current installation.
