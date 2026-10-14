# Shared installation and maintenance transaction

## File inventory changes in 0.1.0

Installed files are validated against their own protected schema-2 inventory;
incoming files are independently validated against the new package manifest,
compiled allowlist, hashes, versions and existing signing policy. Full installer
updates plan additions, replacements and retired paths from these two inventories.
Component-only development updates retain unselected inventory entries.

New paths must be absent unless already owned by the old inventory. Retired paths
must match its recorded hash, size and physical identity. Originals move through
the existing checked rename transaction into the protected archive; metadata then
publishes the new inventory. Rollback restores original files/identities and the
original inventory bytes. Unknown files, modified managed files, path aliases,
unsafe paths and foreign edits during recovery are refused, never adopted.

The journal wire format is unchanged. Historical full-package journals carry
their incoming paths in the existing input-hash rows, so recovery/archive
inspection does not require the current package to contain those same paths.
Installed inventories remain bounded and require canonical application paths.
Voice-library bindings, settings, COM registration, installation ID, certificate
and custom root use the existing preservation and authentication checks.

Verification: **99 focused isolated inventory checks** passed, including 23-to-24
release/repair, future 24-to-25 updates, obsolete-file retirement, rollback after
file/metadata publication, interrupted recovery and foreign-file refusals.
They exercise real private files and production planning/receipt/publication/
recovery code with simulated signing, registry and Inno host operations. Run only
these checks with `outloud-install-maintenance-tests.exe <short-workspace-fixture-parent> inventory`.
No live installation or uninstall was performed for this change.

## Historical A03.10 implementation record

Implemented in the workspace on 2026-10-10 for Out Loud 0.1.0. No installation
exists on this cleaned development machine. **A03.12 now enables the shared
lifecycle for controlled disposable-machine testing; standalone signing and
registration commands remain blocked.**
This document records A03.10 shared transaction infrastructure.
[A03.11 complete uninstall](UNINSTALL.md) extends it; [A03.12 integration](BUILD.md)
records the current build and recovery boundaries. It does not restore a standalone development installation.

## Authority and entry points

`OutLoudInstallSigning.exe` is the single native maintenance authority. Inno owns
UI, destination selection, consent, prerequisites, shortcuts and its own host
metadata. Development PowerShell discovers the same HKLM64 `OutLoud_is1` record,
builds the selected components and submits candidates. Neither caller publishes
product files, signs them independently or edits protected receipts.

The shared native lifecycle is available only with its production platform,
elevation, NTFS/security, identity, consent, ownership and lock checks. Inno
compilation requires a verified current stage. `--maintenance-status` is read-only;
`--maintain`, `--maintenance-recover`, `--maintenance-reconcile` and
`--maintenance-finalize` use the same native authority. Legacy independent
`--prepare`/registration mutation commands remain unconditionally blocked.

| Operation | Components | Intended native behavior |
| --- | --- | --- |
| Fresh | Package | Empty secure destination, absent COM registration, unique installation ID and machine key/certificate, staged signing, checked fresh COM creation |
| Release | Package | Existing Inno/ownership/inventory identity; numeric version comparison; existing certificate reused; unchanged COM |
| Repair | Package | Same version and authenticated metadata/certificate/COM; restore missing payload files only; changed/foreign files refused |
| Development | Engine / VoiceManager / Both | Same version/root/AppId/installation ID/key/COM; exact 1 / 6 / 7 payload files |

Repair cannot reconstruct missing or corrupt ownership, inventory, certificate,
key or registration receipts. It refuses those states. It does not adopt an old
CLSID or arbitrary files as a production installation.

## Candidate and installation checks

The existing `package.json` remains the 23-file payload authority. CMake generates
the native allowlist from it; staging generates the Inno extraction/materialization
includes from the same source. Candidate manifests bind product version, CLSID,
every requested path/hash and the read-only prerequisite detector/requirements.
Unexpected files/directories, unsafe paths, reparse points, hard links, wrong PE
architecture/product/version and changed pinned model/tokenizer assets are refused.
Ordinary development updates include no model, tokenizer, voice or settings file.

The controller checks native Windows 11 x64, elevated machine authority, local
NTFS placement and the existing ACL policy. Inherited Win32 tooling is not long-path
enabled: A03.12 computes the ordinary Win32 path budget from the actual package
and deepest vault paths before creating state. Roots over 100 code units are
accepted when every file is below MAX_PATH and every created parent directory
is below the Win32 directory limit. Extended/device/UNC paths remain unsupported.

Existing installation validation combines the typed HKLM64 Inno record, protected
signing and registration receipts, installation/certificate IDs, exact COM tree,
current-user override checks and a full 23-file hash/size/physical-ID inventory.
The engine's signed hash/certificate and required vendor dependency signatures are
checked. Missing payload files are allowed only for authenticated repair. Existing
legacy signing/registration journals are refused; they are not silently adopted.

The protected candidate vault is below `configuration/maintenance/<transaction-id>`.
It contains new files, verified copies of originals, originals moved by handle,
failed replacements and append-only numbered journal snapshots. No managed voice
root or user setting is part of the allowlist.

## Signing, publication and commit

Fresh installation uses the existing unique LocalMachine My/Root certificate
contract and nonexportable machine key with Administrators/SYSTEM access. Updates
reuse the exact owned certificate/key; no CurrentUser fallback, per-update key,
private-key export or vendor re-signing is introduced. The existing signing policy
signs the protected staged engine before publication and checks code identity,
signature, certificate and Windows/VC dependency signatures.

The logical phases are preparation, staged signing, ready, payload publication,
registration, receipt publication, verification, host completion and commit.
Hash/size/file-ID records include old/new files, copied backups and expected typed
registry snapshots. Signing, registration and complete inventory records are
prepared together; the final inventory uses the signed engine container hash.
Normal updates preserve the registration receipt and COM tree, and never invoke
unregister/register or modify voice tokens.

Restart Manager is used only to list holders/reboot requirements. The controller
never calls shutdown/restart. A private exclusive maintenance epoch serializes
controllers. Current/staged/original/backup files are pinned without read/write
sharing before publication; all current package files are held, including files
outside a selective update. Revalidation precedes publication. New clients cannot
normally open those pinned files during the update. Inaccessible/active resources
cause refusal; no forced unload, process termination, reboot replacement or retry
to weaken sharing is provided.

Verified original bytes are copied before publication, and the actual original
file is moved into the vault by its held handle. The staged file is renamed into
the now-empty destination without replacement. Existing destination security is
copied to the staged replacement; fresh/missing files receive the protected
application read/execute ACL. Original file identities and security are retained
for rollback. Unrelated file permissions are not rewritten.

**There is no global or multi-file atomicity.** Even an individual original/new
rename pair has an observable namespace gap. Protection is a logical transaction
with pins, flushed records and checked recovery. Snapshots use flushed new files
and write-through namespace publication. Windows file/registry/cache behavior
under abrupt power loss still needs VM validation; this is not a tested storage
durability guarantee. The Windows API used is
[SetFileInformationByHandle](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle).

## Failure and interruption recovery

Recovery acquires the same exclusive epoch, validates contiguous bounded snapshots,
root/installation/component identities and protected ownership, and inspects the
actual file namespace. Original and replacement hashes alone are insufficient:
physical IDs and copied backup identities must also match. All rows and registry/
resource ownership are checked before the first restore. Foreign edits, unknown
keys/certificates, malformed/truncated/gapped records or inaccessible resources
cause refusal with evidence retained.

Uncommitted recognized state rolls back: restore the exact owned fresh COM tree
only where necessary, move failed replacements aside, return original handles to
their paths, restore receipts, verify originals and record rollback. Fresh-only
resource cleanup is limited to the journaled machine key and exact owned My/Root
entries, and refuses another certificate referencing that key. Voice dependencies
prevent deleting the fresh class; no token or voice data is removed. Normal updates
never remove signing resources. Installed-update snapshots/backups remain after
terminal pointer retirement.

A03.13 Phase 1 adds checked failed-fresh-install cleanup after verified rollback.
The existing archive audit must authenticate every remaining artifact, and the
host/COM class, recorded certificates/key and installation-created library must
be absent. A protected `.outloud-fresh-cleanup.dat` snapshots all file/directory
identities before deletion. Setup removes only those artifacts and empty managed
directories, including the selected application root. Unknown, changed or locked
resources are preserved; a partial cleanup keeps its receipt for a checked retry.
The native `--maintenance-fresh-cleanup` operation deletes no registry, certificate,
voice or installed-host resources and cannot clean an installed update. It is
called after failed fresh setup and while reconciling an earlier fresh failure.
New publication refuses an outstanding cleanup receipt. Shared runtimes and
containers outside the application root are preserved.

A durable committed marker is verified and kept, not undone because a report was
lost. Inno's `awaiting_host` state can be finalized only after its actual recorded
root/version agrees; otherwise checked rollback or refusal preserves the journal.
After Inno records a new release version, native rollback refuses to restore older
payload beneath that newer host record: checked finalization or explicit Inno
host recovery is required. It does not silently rewrite Inno metadata.
A03.12 writes `.outloud-bootstrap.dat` before transaction infrastructure. The
protected initial journal can reconstruct a missing pending pointer and promote
a complete interrupted initial frame. Truncated records, changed baselines and
unknown bootstrap content are preserved/refused. Staged file and detector output
identities are checkpointed after each completed operation. Interruption within
a write or signing side effect can still require review; unknown bytes are never
removed by filename. A fully authenticated rolled-back fresh archive permits
native retry with its installation ID and refuses unexpected content. The Inno
caller now performs the checked fresh cleanup above before a new attempt, which
then creates a new installation ID. A new signing certificate can be generated only after the old machine
key is proved absent; normal installed updates always reuse their owned key.

## Inno integration and A03.11 contract

Inno extracts generated `dontcopy` entries into its unique temporary input tree.
`PrepareToInstall` materializes inputs after consent and prerequisites.
`ssInstall` publishes through native fresh/release/repair, then Inno writes its
host. `ssPostInstall` verifies host registration/uninstaller/log and finalizes.
Awaiting-host interruption resumes verified native payload and reruns host work;
shutdown never rolls back payload beneath a potentially changed Inno host. Earlier independent
post-copy signing/registration functions are retained but are no longer called
by this flow.

Inno's [dontcopy contract](https://jrsoftware.org/ishelp/topic_filessection.htm)
means these payload files are **not recorded as copied product files in Inno's
uninstall log**. This is deliberate native ownership, not a forged uninstall log.
Inno continues to own its uninstaller, shortcuts and host metadata. A03.11
authenticates the native inventory, preserves the helper through host cleanup,
and removes verified payload/receipts/archives, managed data/tokens and signing/COM
resources after explicit complete-uninstall confirmation. Unknown ownership, locks
and pending maintenance stop removal. See [UNINSTALL.md](UNINSTALL.md) for the
forward-recovery and host-failure boundary. External exports remain outside
installer ownership.

A03.11 adds a protected library directory binding (including a newly created parent),
bootstrap file stamps and schema-2 journal/inventory persistence. Component updates
retain the data binding. Existing schema-1 staging fixtures are refused rather than
adopted; no such format has been released. Fresh rollback retains ambiguous data,
and never deletes foreign content to force recovery.

The [Inno event boundaries](https://jrsoftware.org/ishelp/topic_scriptevents.htm),
actual host-record timing, failure/cancellation after host metadata changes,
repair of that boundary and uninstall coherence require disposable installation
tests. The native transaction does not back up or fabricate Inno internal logs.
Live behavior is not certified by fixture tests. A03.12 implements the bounded
host-resume policy; functional qualification remains A03.13 work. Malformed or
foreign host metadata is still preserved/refused.

## Historical A03.10 files and focused verification

Native additions: `maintenance.hpp`, `maintenance_codec.hpp`,
`maintenance_files.inc`, `maintenance_windows.inc`, `maintenance_tests.cpp`.
Changed native wiring: `signing.cpp`, installer native `CMakeLists.txt`.
Inno additions: `maintenance-common.ps1`, `maintenance.iss`, `test-maintenance.ps1`;
changed `OutLoud.iss`, `build-stage.ps1`, `production-common.ps1` and affected
signing/registration/stage contract assertions. Existing Pascal units received
valid code comments; the unused earlier signing function's unsupported
`WizardIsUpgrade` was replaced with existing installation discovery.
Development changes: `update-installed.ps1`, `installed-update-common.ps1` and
its focused tests. Current documentation: root/installer READMEs, this report and
`docs/DEVELOPMENT_DEPLOYMENT.md`.

| Evidence | Result and limit |
| --- | --- |
| Incremental Release x64 helper and maintenance checker | Built with /W4 /WX; final helper rebuild passed without warnings/errors; checker checks below were retained without another run |
| Native transaction checks | 86 passed; real private-file pins/copy/rename/journals; simulated certificate/registry/signature hooks |
| Shared integration checks | 30 passed; manifests/selection, PowerShell parsing, closed native gates, real Inno 7 compilation of actual Pascal units in an inert always-refusing harness; production compiler gate still rejects |
| Existing development-update checks | 64 passed; fake installation observations, read-only native refusal, no build or live publication |
| Existing engine dependency inventory | Read-only verification passed for 367 resolved Windows/VC images; optional unavailable system aliases were reported separately; no synthesis or signing |

Native cases cover exact selections, full fresh/release/repair planning, receipt
binding, six normal failure points, partial publication/receipt rollback, a newly
constructed recovery controller, competing epoch refusal, unconfirmed host refusal,
foreign last-row protection, locked-file refusal, committed-marker retention and
corrupt-journal preservation. Test ACL checks are fixture containment substitutes;
production ACL creation/copy is not claimed tested. No full project suite,
benchmark, inference/WPF build, full production-stage rebuild or real installer
execution was performed. Generated integration evidence is ignored under
`generated/maintenance-validation`; short native fixture evidence is under the
user temporary directory to stay within the inherited Win32 path limit.

After the user requested no further repeated tests, only the final affected native
helper was rebuilt and documentation links/whitespace were checked. The 86/30/64
results above are the existing successful focused runs; final extra fresh-identity
and changed-host rollback refusal guards were compiled, not separately rerun.

## Current qualification boundaries

A03.12 completes bounded bootstrap and host coordination and enables the shared
lifecycle for a controlled test candidate. A03.13 must validate real elevated
NTFS ACL/pins/Restart Manager, staged signing/key reuse, fresh COM creation,
publication, cancellation/interruption, Inno completion and full uninstall on
disposable Windows 11 x64. Missing/expired keys, changed ownership, corrupt
records and unknown output remain refusal states. See [BUILD.md](BUILD.md) for
current build evidence and the full A03.12 file list. No live operation is run on
the development machine, no public release is declared and no commit/push occurs.
