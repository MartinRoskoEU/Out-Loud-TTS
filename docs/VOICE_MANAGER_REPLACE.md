# Step 3D.3.1: Import-or-Replace and dismissible notifications

> Historical Step 3D.3.1 implementation/validation record. Current Import-or-Replace, protocol v3 and result handling are in [VOICE_MANAGER_GUIDE.md](VOICE_MANAGER_GUIDE.md). Use [A11/A12](BUILD_AND_VALIDATION_IMPROVEMENTS.md) for current build/validation.

The current helper uses [Step 3D.3.2 protocol v3 and Remove Voice](VOICE_MANAGER_REMOVE.md).
The protocol v2 description below records this step's original implementation;
Import/Replace payloads and behavior are retained in v3.

The existing **Import Voice** action now asks for replacement confirmation when
the final display name matches a managed voice in the current snapshot. Replacement
uses a first-class native operation and retains the existing entry and SAPI identity.
The main application remains x64, `asInvoker`; only the existing one-shot helper
requests elevation, after explicit confirmation. No separate Replace toolbar action,
external UI framework, permanent elevated service or alternate mutation path is added.

The user reported successful live Step 3D.3 import, helper-only UAC, machine
library/HKLM installation and duplicate rejection. Those are user-reported results.
This step's replacement, actual UAC and Windows/WoW playback have **not** been
live-tested. Automated validation uses temporary libraries and fake registration.

## Audit findings and name/content semantics

Audited the existing dialogs/ViewModel/service, read-only bridge, helper framing
and security, `VoiceLibrary`, importer, native file/path/ACL utilities,
`SapiVoiceManager`, `.olv` ownership and the resident SAPI engine.

The native backend validates and trims display names but does not impose name
uniqueness. The UI compares `Trim()` results with `StringComparison.OrdinalIgnoreCase`
for a Windows display-name presentation choice; it does not change backend naming
semantics. A replacement retains the target's exact stored spelling and name.

- No match: ordinary import with the existing native duplicate-content policy.
- One match: themed Cancel/Replace confirmation, displaying the immutable ID.
- Multiple matches: list all matching immutable IDs; no initial target selection.
  Replace remains disabled until the user chooses a target with snapshot content
  identity. The ViewModel also verifies the returned target belongs to that snapshot.
- Cancel: inline Info outcome, no helper launch and no UAC.

A name collision is a UI routing decision. Content duplication is decided by the
native backend. Replacement compares validated conditioning bytes (prefix length,
compatibility and all six cache layers), so differently encoded source headers do
not force a needless replacement. Equal target conditioning returns **unchanged**,
keeping the original metadata too. Content matching another library ID is rejected
with duplicate IDs. Ordinary import duplicate behavior is unchanged.

## Actual mapping behavior

`engine/src/asset_io.cpp`, `MappedFile`, opens assets with `GENERIC_READ` and
**FILE_SHARE_READ only**, creates a read-only mapping and keeps both file and
mapping handles until destruction. `OutLoudVoice` owns/shared-owns that mapping.
`sapi/src/adapter.cpp`, `Resident`, retains the voice and pipeline across Speak
calls; request completion or cancellation does not close the mapping. Engine
destruction or a runtime/token change releases that resident owner.

Windows requires compatible sharing for DELETE access, which includes rename;
the existing reader does not grant it. See Microsoft's
[CreateFile sharing rules](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilea).
A test using the **actual production OutLoudVoice loader** against a temporary
library verified that acquiring the replacement DELETE handle fails while mapped.
The operation returns structured `voice_in_use`, preserves the exact old pair and
tells the user to close WoW/other active speech consumers and retry. No sharing
flags, mapping lifetime, native neural code or path protections were weakened.

Another actual Windows check found that renaming onto a pinned existing destination
could fail with access denied. Publication therefore moves pinned old files aside
within the owned staging directory, then renames the new files into the now-empty
fixed paths. It never renames, deletes or recreates the managed entry directory.

## Native operation and identity

`VoiceLibrary::replace_pocket_voice(id, source, VoiceReplaceOptions)` requires the
expected current display name and `.olv` SHA256. Under the existing exclusive
library lock it revalidates a healthy managed target and both expectations. A missing
target returns `voice_not_found`; changed name/content returns `replacement_conflict`.
The UI refreshes after replacement outcomes, including failures.

The operation preserves:

- Immutable library ID and display name.
- Managed directory identity, including its Windows file ID and volume.
- Fixed `voices/<id>/voice.olv` path, hence the token's `VoicePath`.
- `OutLoud_Pocket_<id>` token ID and all existing token values.
- Logical Windows/WoW voice identity; no extra library row is created.

Replacement changes content, not entry identity. It reuses the prepared Pocket
importer, compatibility checks, source pinning, secure file/ACL policy and `.olv` v1.
Metadata remains schema 1. It refreshes content/source hashes, source filename,
format and compatibility fields, `import_kind=pocket_prepared`, and the existing
false model-provenance claim. `imported_utc` records import of the **current content**,
including replacement; it is not an entry-creation timestamp or replacement history.
Second-resolution timestamps can coincide when imports occur in the same second.

The helper's prepublication callback invokes `SapiVoiceManager::validate_replacement`.
It validates the staged voice against the resolved deployed model/tokenizer/engine
and requires the existing owned machine token tree to equal its expected configuration.
This check performs **no registry writes** and does not reacquire the library lock.
Missing, malformed or stale token state requires reconciliation and fails before
content publication. It does not silently install/update a token. External admin
changes after this read-only preflight are outside a combined file/registry transaction.

The additive read-only `outloud_vm_result_content` export exposes content/source
hashes, source filename and import timestamp while the existing result owner is
alive. Existing ABI v1 structures, version and release semantics remain unchanged.
The frontend copies these fields into immutable records and displays them in Details.
The main app, bridge and helper should be shipped together; old helpers reject v2.

## Publication, rollback and recovery guarantees

Staging completes conversion, validation, compatibility checks, hashing, metadata
preparation and SAPI/runtime preflight before touching the original published pair.
Invalid/incompatible input, stale target, duplicate data, unsafe paths/ACLs,
preflight failure or unavailable DELETE access leave the original pair unchanged.

Publication uses the existing private staging area and exclusive library lock:

1. Copy and validate both originals into `old.olv` and `old.json` backups.
2. Pin the entry directory; acquire read/DELETE handles for both original and new
   files. Rehash held handles to detect changed input/live state before publication.
3. Write and flush a strict `replace.json` journal: target ID, directory file ID,
   volume and old/new hashes for both files. Recovery owns the staging directory now.
4. Move originals to staged retired slots; publish the new pair at the original
   paths through pinned handles. New DELETE handles temporarily prevent normal
   production read-only consumers opening a partially published pair.
5. Write/flush a commit marker bound to the journal, close publication handles,
   and remove bounded staging files. A cleanup failure after commit reports
   successful replacement **with cleanup pending**, not a failed replacement.

There is **no two-file atomic filesystem transaction**. Windows rename acts on one
file at a time. Ordinary caught publication failures attempt immediate rollback;
tests verify exact restoration of both original files after first/second publication
failures. Interruption or a rollback failure can leave an absent/mixed live pair:
the journal and validated original backups remain, library health becomes
`replacement_pending`, and further entry mutations are blocked.

Existing `VoiceLibrary::cleanup_pending()` now recovers journal-bearing staging
directories in place. Uncommitted recovery validates directory identity, strict
journal fields, backup hashes/schema/pairing, ACLs and recognized old/new live hashes
before restoring. It keeps backups until both restores succeed, allowing retry
after an interrupted rollback. Unknown live contents are retained for review.
Committed cleanup first requires a healthy live entry; an unhealthy target retains
originals for review. Cleanup accepts a bounded list of owned filenames and never
recursively deletes arbitrary paths.

This is process-failure recovery, not a proven guarantee against every power-loss,
storage or filesystem failure. Native SAPI opens `.olv` directly and does not consult
the library journal/lock: transient opens during publication can fail; after a
process interruption a valid newly published voice could become mapped before
recovery. Close active consumers before attempting recovery. No automatic rollback
inside the read-only UI or production SAPI loader was added.

If the UI reports recovery pending, first close consumers. The existing CLI can
be invoked **manually**, from an Administrator shell at the repository root:

```powershell
.\build\Release\outloud-voice-library.exe cleanup
```

It uses the canonical default machine root. Review any reported pending paths;
do not delete journal/backups or manufacture a production failure. This command
was not run against the real machine library during implementation.

## One-shot helper protocol v2 and scope

Both protocol ends now explicitly require version **2** with the existing magic,
16-byte frame header, bounded UTF-8 fields and 65536-byte frame limit.

| Operation | Request payload after header |
| --- | --- |
| 1: ImportPreparedPocketVoice | Source path, display name |
| 2: ReplacePreparedPocketVoice | Source path, exact expected name, target 32-hex ID, expected 64-hex `.olv` SHA256 |

Additional fields, malformed IDs/hashes, wrong versions and inconsistent outcomes
are rejected. Production requests cannot select root, destination, hive/registry
path, command or executable. Production library root and HKLM64 scope remain fixed.
The existing runtime resolver remains read-only and follows the registered engine's
documented deployment layout; replacement has no fallback deployment path.

Started is outcome 4 with no payload. Final replies retain structured diagnostics
and add the operation discriminator after the library ID. Existing final outcomes
1/2/3 mean import success/failure/partial; new outcomes 5/6/7 mean replaced/unchanged/
replaced-with-cleanup-warning. Failed replacement replies retain the target ID.
The frontend rejects cross-operation responses. Unknown transport outcomes warn
and refresh instead of inventing success or rollback.

Pipe DACL, random first-instance local endpoint, remote rejection, PID/process-handle
and session checks, adjacent parent/helper identity, System32 dependency loading,
identification QoS and one-shot lifetime remain as audited in
[Step 3D.3](VOICE_MANAGER_IMPORT.md). The app remains non-elevated and invokes `runas`
only after confirmation. The unsigned developer-workspace trust limitation remains;
production protected packaging/signing is outside this refinement.

## Notifications

`VoiceImportResult` has explicit Success/Warning/Error/Info presentation severity,
separate from backend result codes. Clean import/replacement is Success; unchanged
content and cancellation are Info; partial results, in-use, conflict, missing target,
pending recovery and unknown outcomes are Warning; other failures are Error.
Native domain/code/path/Win32/tensor/duplicate IDs remain available in Diagnostic
details when useful. Clean success and cancellation omit the expander.

Common WPF styles use dynamic palette resources for green success, amber warning,
red error and neutral informational treatment. Visible severity text and messages
provide cues beyond color. High Contrast uses the existing system colors.
The themed replacement dialog follows the existing live theme/caption service.
The right-side X invokes `DismissNotificationCommand` in the ViewModel; both banner
and its diagnostic expander disappear together. A later operation clears the
previous result and can show a new one. Errors have no timeout. Full-row selection
and refresh behavior are retained; replacement refresh selects the original ID.

## Files changed in this refinement

- Native library: `engine/include/outloud/voice_library.hpp`,
  `engine/src/voice_library.cpp`, new private `voice_library_replace.inc` and
  test-only `replacement_audit.hpp`.
- Read-only SAPI preflight: `engine/include/outloud/sapi_voices.hpp`,
  `sapi/src/voice_tokens.cpp`.
- Bridge metadata: `frontend/native/voice_manager.h`, `voice_manager.cpp`,
  `tests.cpp`; UI `NativeInterop/NativeVoiceManager.cs`, `Models/VoiceSnapshot.cs`.
- Helper protocol/composition: `frontend/mutation/mutation.hpp`, `mutation.cpp`;
  the existing helper consumes the shared decoder without a new security path.
- Frontend: `Models/VoiceImport.cs`, `ViewModels/MainViewModel.cs`,
  `Services/IVoiceImportService.cs`, `ImportProtocol.cs`, `ImportDialogService.cs`,
  `ElevatedVoiceImportService.cs`; `Views/MainWindow.xaml`, new
  `ReplaceVoiceDialog.xaml`/`.xaml.cs`; `Themes/Common.xaml`, `StatusToneConverter.cs`.
- Validation: `CMakeLists.txt`, new `frontend/mutation/replace_tests.hpp`,
  `ui/OutLoud.VoiceManager.Tests/ImportTests.cs`, `Program.cs`, and
  `ui/validation/RuntimeValidation.cs`.
- Documentation: this file and links from `VOICE_MANAGER_IMPORT.md`,
  `VOICE_MANAGER_WPF.md`, `VOICE_MANAGER_THEME.md`.

The earlier frontend/docs remain untracked in this workspace. Their presence,
and the prior `.gitignore` changes, are not new staging or commits for this step.

## Automated validation

Ran CMake configure and full Release x64 build successfully. The WPF solution
Release x64 build passed with **0 warnings and 0 errors**. Frontend checks:
**73 passed**. Existing CTests: **17/17 passed, 57.99 seconds**; no new CTest or
performance gate was added. The SDK-built in-memory WPF validation passed without
App startup, a visible window, production native read or real registry/UAC access.
SDK commands set `DOTNET_GENERATE_ASPNET_CERTIFICATE=false`.
`git diff --check` passed; separate whitespace checks also passed for all 51
untracked source/document files. Final `git status --short` confirms unstaged
native edits and the untracked frontend/docs, with nothing staged.

Focused native tests use temporary directories, a fake registry/runtime fixture,
and actual `.olv` mappings. They verify identity/directory/path/count/token preservation,
updated hashes/source metadata, invalid/incompatible/stale/missing targets,
unchanged content, other-ID duplicates, active mapping refusal, structured preflight
failure, exact rollback, recoverable interruption, interrupted rollback, committed
cleanup warning and malformed/extra protocol fields. Test-only fault probes compile
into the existing checker; production library, DLL and helper contain no probes.

Frontend/fake-pipe tests cover name routing, no elevation after Cancel, explicit
multi-target selection, expected target hash/name, same-ID refresh, protocol outcomes,
cross-operation rejection, four severities, dismissal/reappearance and diagnostic
lifecycle. Actual SDK-built WPF resources/templates were checked for Light/Dark/
High Contrast severity styling, replacement selection, X dismissal and diagnostic
visibility alongside existing full-row selection, state and resize checks.

Real desktop interaction, UAC, replacement in ProgramData, existing HKLM token
preservation and Windows/WoW playback remain manual. No production replacement,
registry mutation, signing/deployment/registration, WoW launch, staging, commit or
push was performed. No new certificate was created in this step; the historical
Step 3D.1 SDK certificate side effect remains disclosed in its original document.

## Exact manual live test

Close WoW and other active consumers before the approved replacement so they release
their resident voice mappings. Use a genuinely different prepared conditioning file;
an identical file produces an Info unchanged result. Note the current ID/count/hash.

1. Launch the normal, non-elevated Voice Manager:
   `ui/OutLoud.VoiceManager/bin/Release/net10.0-windows/OutLoud.VoiceManager.exe`.
2. Click **Import Voice**.
3. Select a **new** prepared `.safetensors` file.
4. Enter the name of an existing voice, for example `Undead Female`.
5. Verify the existing name is detected and the themed Replace confirmation appears.
   If names are ambiguous, explicitly choose the matching immutable ID.
6. Click **Cancel** once; verify no content change and no UAC. Verify Info and X dismissal.
7. Repeat the same flow and choose **Replace**.
8. Verify UAC appears only after that Replace confirmation.
9. Approve the helper's request.
10. Verify count does not increase, the same Library ID and display name remain,
    Library Status is `valid`, SAPI Status is `correct`, and the Details hashes,
    source filename and current-content import timestamp reflect the replacement.
    Verify the Success banner and X dismissal; Refresh must retain selection.
11. Restart WoW manually.
12. Verify the same Out Loud voice entry now speaks with the replacement conditioning.

Also check light/dark/high-contrast readability and X dismissal for cancellation/Info,
duplicate/Error, and partial/Warning where practical. Do not mutate production
state solely to force errors or partial outcomes; these have automated fake coverage.
If an already-active consumer causes `voice_in_use`, verify the Warning, close that
consumer, refresh and retry rather than changing mapping protection.
