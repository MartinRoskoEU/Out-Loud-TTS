# Step 3D.3.2: Remove Voice

> Historical Step 3D.3.2 implementation/validation record. Current journaled recovery and GUI behavior are in [VOICE_MANAGER_GUIDE.md](VOICE_MANAGER_GUIDE.md); build/validation is in [A11/A12](BUILD_AND_VALIDATION_IMPROVEMENTS.md).

The original step record below is historical. Current removal now uses the
durable journal and token-aware recovery described in
[Stabilization Phase 2](STABILIZATION_PHASE_2.md), superseding its crash-recovery
and generic-trash-cleanup limitations. Read-only refresh does not recover;
authorized Remove retries and explicit administrator maintenance do. Nonempty
unjournaled trash is retained for review.

The normal x64, `asInvoker` Voice Manager now has a secondary **Remove** action
next to Import Voice. It uses the selected immutable library ID, asks for themed
confirmation and then uses the existing one-shot elevated helper. No Rename,
Import `.olv`, Export or other manager feature was added.

Entry was clean at `a45f9d8` (Add WPF voice manager frontend). This step did not
stage, commit or push. No automated test deletes production voices or invokes
real UAC, ProgramData/HKLM mutations, deployment/signing, shared COM registration,
or WoW launch/restart. Import/Replace remain supported and tested.

## Audit of existing APIs

`VoiceLibrary::remove_voice` already performs native path/ID/reparse/ACL checks,
holds the exclusive library lock, pins the entry directory, and preflights every
child for DELETE access. It accepts only `voice.olv` and `metadata.json`, rejects
unknown/read-only files and does not traverse recursively. Its commit moves the
entry to a random owned `.trash` directory, removing it from active enumeration.
Subsequent physical deletion can fail: `VoiceRemovalResult::cleanup_pending`
reports this, and the existing explicit cleanup API can finish the trash later.

The existing one-argument library API remains available, including removal of
unhealthy entries, and retains its standalone error/result semantics. A new overload
with `VoiceRemoveOptions` adds snapshot assertions and native coordination around
the same preflight/detach/delete machinery. C# implements no filesystem or registry
deletion rules.

`SapiVoiceManager::remove` reads exactly `OutLoud_Pocket_<id>`, verifies the managed
owner marker, matching LibraryId and CLSID, and rejects unexpected token shape.
`SapiVoiceRegistry::exchange` compares the snapshot again and removes only that
token and its permitted Attributes child using the existing registry transaction.
Failure rolls back that registry transaction. It does not need to load a library
entry or validate runtime/model/tokenizer/engine registration, so it can operate
while the library lock is held and can remove an orphan explicitly. No unrelated
token, engine CLSID/InprocServer32 or runtime asset is written by this API.

Audited `engine/src/asset_io.cpp`: the production read-only loader retains the
file and mapping handles with **FILE_SHARE_READ**, without FILE_SHARE_DELETE.
The SAPI `Resident` owns its voice mapping between requests. A finished/cancelled
Speak need not release the file. The native checker verifies the actual production
OutLoudVoice mapping refuses removal of a temporary voice before any token write.
Sharing/lifetime protections remain unchanged.

## Chosen order and stale protection

The helper's RemoveManagedVoice operation uses the canonical ProgramData library
and the existing machine HKLM Registry64 token scope. It performs:

1. Under the exclusive library lock, revalidate target ID, snapshot name, metadata
   `.olv` hash and library health. A disappeared target returns `voice_not_found`;
   changed assertions return `removal_conflict`. Pending replacement recovery blocks
   removal. Empty expected name/hash assert metadata was unavailable; they cannot
   authorize deletion of an entry that now has metadata. Unhealthy entries can be
   removed only under their matching snapshot health and existing safe-file rules.
2. Pin the directory and preflight all children, including DELETE access and ACLs.
   For healthy assets, rehash the held/pinned file before detachment to detect content
   drift. Sharing/lock violations return structured `voice_in_use` before mutation.
3. Release child handles for the existing Windows directory rename, retaining the
   pinned directory identity. Move it to owned random trash. No files are deleted yet.
4. Reacquire/validate all child DELETE handles in trash before calling SAPI. This
   closes the preflight/rename gap before token mutation. Keep these handles through
   token removal and physical deletion, preventing a new normal read-only mapping.
5. Invoke existing `SapiVoiceManager::remove` for the same immutable ID. Only owned,
   well-formed token state is removed. An already-missing token is already absent
   and allows safe completion of library removal.
6. Delete the bounded owned files/directory through the existing native routine.

Library detachment precedes token removal so an active-reader/rename failure does
not first remove the token. Detachment is reversible until SAPI succeeds. The
callback is trusted native composition, not an IPC-supplied command or callback;
it must not re-enter VoiceLibrary. Production requests cannot choose any library
root, file/destination path, hive/key, command or executable.

The actual production mapping test checks exact preservation of both library
files and the token tree/writes counter. Close WoW/other speech consumers and retry
when the app reports the in-use Warning. Do not change sharing or force deletion.

## Actual failure and partial semantics

Filesystem and registry changes are **not a combined atomic transaction**.

| Condition | Actual state/result |
| --- | --- |
| Stale/missing target, initial in-use/unsafe/read-only/unknown files, detach failure | No token change; entry not removed; structured failure |
| Post-detach preflight or SAPI removal fails, directory restoration succeeds | Original directory returns to its original path with unchanged files; original structured failure propagates |
| Restoration itself fails | `removal_partial` Warning; entry remains detached in reported trash, token needs review, files retained; no fabricated successful rollback |
| SAPI succeeds, file/trash cleanup fails | `cleanup_pending` Warning; row and token are removed, trash cleanup remains |
| Both finish | Success: voice removed from Out Loud and Windows speech |
| Helper communication becomes uncertain after dispatch | Existing `outcome_unknown` Warning and read-only refresh; no invented success/rollback |

The directory restoration uses the original pinned handle and does not recreate
the entry. Tests verify exact old file bytes after injected registry read/transaction
failures and library failure, plus retained originals when restoration fails.
Restoration failures preserve its Win32 code where available; partial diagnostics
also retain the original registration failure code/message.

There is a brief interval while the entry is detached in which a native SAPI open
using the old fixed path can fail. Existing consumers can also race the initial
preflight; post-detach checks prevent proceeding to token removal while files
cannot be safely acquired. Failure to restore in such a race is a reported partial
state. This does not promise uninterrupted synthesis during removal.

A process interruption after detachment can leave retained trash and an orphan
token, following the existing removal recovery model. No new cross-store crash
journal, automatic production repair or power-loss guarantee is claimed. Existing
read-only SAPI reconciliation can identify the orphan. Explicit native SAPI token
removal and library cleanup can complete it manually after review. Trash cleanup
does not restore a failed removal or remove an orphan token by itself. Do not run
cleanup solely to manufacture failure tests or delete retained files before review.

The shared engine registration remains installed. The Remove helper intentionally
does **not** call runtime resolution or engine verification: deletion must not
depend on healthy deployed model/tokenizer assets. Tests assert those calls are
absent and unrelated/other-ID token trees are unchanged.

## UI and protocol v3

Remove is disabled without selection, for non-managed/invalid directory IDs, and
while refresh/import/replace/remove is busy. A selected managed entry enables it,
including entries whose metadata is unavailable. Confirmation shows the name,
immutable ID, irreversible-action text, and the instruction to close active consumers.
Cancel is the default button; Remove has a distinct themed red treatment.
The dialog follows the existing light/dark/high-contrast/live caption service.

Cancel starts no helper or UAC and reports Info. Confirm requests `runas` through
the existing service; the main process remains non-elevated. The selected snapshot
is captured before confirmation, so subsequent selection does not change its target.
Post-dispatch outcomes, including failures/conflicts, refresh through the normal
read-only service. If the removed ID is absent, select the first remaining snapshot
row; removing the last voice yields the normal empty state and disables Remove.
A refresh failure retains the last snapshot and displays the existing refresh error.

Success uses the exact message **Voice removed from Out Loud and Windows speech.**
Cancellation is Info; in-use/conflict/missing target/partial/unknown outcomes are
Warning; ordinary failures are Error. The existing banner's severity text, dynamic
theme resources, meaningful diagnostics and ViewModel X-dismiss command are reused.
Dismissal hides both message and diagnostics; later operations show fresh results.

Both ends now require protocol **v3**. Magic, bounds, header and secure transport
are unchanged. Existing Import/Replace request schemas and final outcomes are
retained; new operation **3 = RemoveManagedVoice** contains exactly four bounded
UTF-8 strings: immutable target ID, expected stored name, expected metadata `.olv`
SHA256, expected native library-health label. It contains no source/destination path.
Malformed IDs/hashes, missing/trailing fields, unknown operations and old versions
are rejected. Final **8 = removed**, **9 = removal warning**, and ordinary **2 = failed**
retain operation=Remove and target ID, plus existing structured error fields.
Cross-operation replies are rejected. Generic pre-dispatch malformed-request replies
are presented using the original requested operation/target.

The parent/session/PID verification, first-instance remote-rejecting pipe and DACL,
held process handles, System32 dependency loading and short helper lifetime remain
unchanged. `IVoiceImportService`, `VoiceImportRequest` and helper executable keep
their existing names; they dispatch the operation discriminator rather than adding
a second privilege boundary. Ship the app/bridge/helper together; v2 peers cannot
perform v3 operations. Read-only C ABI, `.olv` and metadata schemas are unchanged.

## Files changed

- Native API/coordination: `engine/include/outloud/voice_library.hpp`,
  `engine/src/voice_library.cpp`.
- Helper protocol/composition: `frontend/mutation/mutation.hpp`, `mutation.cpp`,
  `helper.cpp` (generic mutation rejection wording).
- UI request/results/commands: `ui/OutLoud.VoiceManager/Models/VoiceImport.cs`,
  `ViewModels/MainViewModel.cs`.
- Service/IPC/dialog wiring: `Services/IVoiceImportService.cs`, `ImportDialogService.cs`,
  `ImportProtocol.cs`, `ElevatedVoiceImportService.cs`.
- Views/styles: `Views/MainWindow.xaml`, new `RemoveVoiceDialog.xaml` and `.xaml.cs`,
  `Themes/Common.xaml`.
- Focused checks: new `frontend/mutation/remove_tests.hpp`, `frontend/native/tests.cpp`,
  test-only `engine/src/replacement_audit.hpp`,
  `ui/OutLoud.VoiceManager.Tests/ImportTests.cs`, `ui/validation/RuntimeValidation.cs`.
- Documentation: this file; current-workflow links in `VOICE_MANAGER_IMPORT.md`,
  `VOICE_MANAGER_REPLACE.md`, `VOICE_MANAGER_WPF.md`.

No SapiVoiceManager/registry implementation, model/voice importer, native SAPI
adapter, sharing flags, deployment scripts, CMake target count or native/frontend
ABI structure was changed. Test probes remain compiled only in the existing
checker; no new CTest, performance gate or production hook was introduced.

## Validation

Required CMake configure and full native Release x64 build passed. The WPF
solution built Release x64 with **0 warnings and 0 errors**. **97 frontend checks
passed**. Existing CTests: **17/17 passed in 60.58 seconds**, including all prior
library, Import/Replace and native synthesis checks. SDK-built WPF view validation
passed, including Remove enablement, Cancel-default confirmation, destructive
styling in Light/Dark/High Contrast, remaining-row/empty-state refresh and dismissal.
All SDK commands set `DOTNET_GENERATE_ASPNET_CERTIFICATE=false`.
`git diff --check` and separate checks of all four untracked source/document files
passed. Final `git status --short` shows 20 unstaged modified files and four
untracked additions. Nothing was staged, committed or pushed.
All new native mutations occur in temporary Scratch libraries, with FakeRegistry
and existing runtime fixtures; the production helper is not launched. An actual
OutLoudVoice mapping is used only on a temporary file. Frontend tests use fake
dialogs/services/elevation and the existing in-process secure-pipe fake peer.
SDK-built WPF validation initializes resources/BAML without App startup or showing
a window and uses fake data, including destructive styling in all three themes.

Focused coverage includes no selection/invalid ID, immutable-ID routing with
duplicate names, Cancel/no helper, count/selection/empty refresh, active mapping
refusal, name/hash/health/content drift, missing targets, unowned/malformed tokens,
registry read/transaction failures with restoration, library detach failure,
restoration failure, post-token cleanup failure, token already absent, unavailable
metadata, malformed/narrow protocol, severities/dismissal and uncertain responses.
All prior Import/Replace checks are retained.

## Exact live test (manual only)

Use a disposable/test voice. Close WoW and other consumers first so they release
resident mappings. This operation is irreversible after successful deletion.

1. Launch the normal, non-elevated Voice Manager:
   `ui/OutLoud.VoiceManager/bin/Release/net10.0-windows/OutLoud.VoiceManager.exe`.
2. Select a disposable/test voice and note its immutable ID and current count.
3. Click **Remove** and verify the name/ID in the themed confirmation.
4. Click **Cancel** once; verify no UAC, no row/count change and Info notification.
5. Repeat and explicitly confirm **Remove**.
6. Verify UAC appears only after confirmation.
7. Approve the helper.
8. Verify the selected row disappears, count decreases, a remaining row is selected
   or the normal empty state appears, and the Success notification can be dismissed.
9. Click Refresh; the removed voice must stay absent.
10. Restart WoW manually and verify the removed voice is no longer enumerated.
11. Verify remaining voices still work.

Also check light/dark/high-contrast Remove styling and banner readability/X dismissal.
An already-active consumer may produce an in-use Warning; close it and retry.
Do not mutate production solely to force partial/error cases; automated fixtures
cover those. No app/game launch or real deletion was automated. Stop after this
refinement; actual desktop/UAC/production removal and Windows/WoW enumeration
remain unverified until this manual test.
