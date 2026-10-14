# A03.11: complete, ownership-checked uninstallation

Implemented for Out Loud **0.1.0**, Windows 11 x64, on 2026-10-11. A03.12 subsequently integrates and enables shared maintenance/uninstall for a
controlled-test candidate; standalone signing/registration stays blocked.
See [BUILD.md](BUILD.md). No installation or uninstall is executed during builds;
A03.13 functional testing belongs on disposable Windows.
AppId `OutLoud`, SAPI CLSID, version, MIT LICENSE and Nendis copyright are unchanged.

## Behavior and ownership

There is one complete uninstall, with an interactive warning that **all managed
voices, settings and application data will be permanently deleted**. Confirmation
defaults to No. Silent uninstall is refused. Declining or failing preflight leaves
the installation intact; initial preflight acquires existing locks but creates no removal
plan and deletes nothing. Checked retries can recreate an interrupted finalization's
missing private maintenance lock after authenticating the saved plan. Shared Microsoft VC++/.NET runtimes and independently
exported backups are outside removal authority.

The actual application directory comes from the existing HKLM64 Inno
`OutLoud_is1` record. Inno checks it against its own recorded `{app}`; native
maintenance authenticates publisher, product, protected metadata, installation
ID, current complete inventory, registration receipt and signing ownership.
There is no default-path fallback, relocation, data-preservation mode or separate
uninstaller framework. Source repositories, builds and tools are never searched
for deletion. Unknown resources in the selected application directory cause
refusal; the entire application directory is never recursively deleted.

The current inventory supplies all native/frontend binaries, dependencies,
models, tokenizer and legal payload files. Size, SHA-256 and physical file identity
must match. Development Engine/VoiceManager/Both updates already replace that
inventory through A03.10; uninstall uses its current stamps rather than original
package hashes. `update-installed.ps1` is unchanged. Maintenance refuses a pending
`.outloud-uninstall.dat` so a new update cannot invalidate a removal plan.

## Managed voices and application data

Fresh native installation reserves an absent known-folder-resolved
`ProgramData/OutLoud/VoiceLibrary` and writes its protected
`.outloud-installation.dat` binding. The inventory binds its physical directory
identity to the installation ID and actual application root. An installation-created
OutLoud parent is also tracked and removed only when empty; an existing or shared
parent is preserved. Existing unbound libraries are never adopted or deleted.
The parent/library ACLs must match the existing machine-library security contract;
existing permissions are not silently repaired.

The A03.10 journal and inventory now use schema **2** to persist these identities
through publication, repair, development updates and fresh-install recovery. Old
schema-1 staging fixtures are refused, not migrated. Those schemas were behind
closed gates and have never been a released installation format. Rebuild future
candidates with the current sources; historical ignored stages remain untouched.
Bootstrap detector, requirements and report stamps are also journaled so terminal
archives can be authenticated before deletion. Unjournaled output from an early
interruption remains a review/refusal state rather than being deleted by name.

Bound managed data includes every allowlisted voice/metadata entry and recognized
`.staging` replacement and `.trash` removal transaction content. Full uninstall
deletes this data after detaching its registrations, rather than restoring voices
that the user has explicitly elected to delete. Active library operations cannot
be removed: the existing library lock file and all data identities must be pinned.
Unknown transaction directories, extra files, reparse points or hard links cause
refusal. This preserves external exports even if someone placed one among managed
data under an unexpected name. File names alone do not authorize an unbound library.

All dependent HKLM64 Speech tokens must have the existing exact managed schema,
marker, ID, CLSID, installation asset/model/tokenizer paths and bound-library voice
path. Orphaned managed tokens with valid ownership are included. Unowned dependent
tokens stop removal. Other machine/user views, Speech OneCore and loaded user
registrations are inspected for foreign dependencies/COM overrides and preserved.
Owned tokens and the exact owned COM class are deleted in one checked Windows
registry transaction, with no nontransactional fallback. COM is not removed while
a known foreign voice dependency remains.

The SAPI logger now marks a newly created installed log directory with the
installation ID, actual application root and directory identity. It never marks
existing unbound logs retroactively. Native cleanup checks current and accessible
registered profiles' Local AppData paths and removes only the bound marker plus
`engine.log`, `engine.1.log` and `engine.2.log`. User data is pinned without changing
its ACLs, and ancestors are held against replacement. A logged-off profile with an
unavailable hive requires review: uninstall cannot safely exclude redirected logs
or per-user registrations there and refuses before deletion. It does not mount or
rewrite user hives. The current app has no separate persisted settings/cache store;
Windows theme settings are read-only and remain untouched. Future persistent
stores must join this ownership contract before being eligible for deletion.
Unbound parent directories are preserved, even when empty.

## Certificates and private key

Cleanup reuses A03.10's machine-signing resource cleanup with an uninstall-specific
ownership check. Only the recorded LocalMachine My/Root certificate, exact SHA-256/
thumbprint and installation-specific Microsoft KSP machine key are eligible.
Expiration does not revoke ownership for deletion. The key must still have the
expected owner, SYSTEM/Administrators ACL, nonexportability and signing-only policy.
All enumerable machine and accessible user certificate stores, including archived
certificates, are checked for other references to the container or the same RSA
public key. User stores are opened through the physical registry provider to avoid
counting inherited machine trust copies as independently owned user certificates;
see the [Windows store-provider contract](https://learn.microsoft.com/en-us/windows/win32/api/wincrypt/nf-wincrypt-certopenstore). Another certificate/store using that key
blocks removal. Unrelated certificates and keys are preserved. Missing resources
after partial cleanup are accepted on a checked retry of the protected plan.
Absence of both owned certificate copies and the private key is verified before
the native controller permits Inno host cleanup.

## Inno lifecycle, failure and recovery

`uninstall.iss` uses the existing `OutLoudInstallSigning.exe`. The installed and
temporary helper hashes are checked against the native inventory before execution;
the helper also authenticates the source identity and temporary copy. The copy
remains in Inno's protected temporary directory until native work finishes.

`InitializeUninstall` performs elevated ownership preflight; a new uninstall only reads/pins existing state. After normal Inno
confirmation, `usUninstall` requests the explicit complete-removal confirmation
and performs native cleanup. A native failure displays the verified remaining
file/component state and calls `Abort`, before Inno removes its host metadata.
This uses Inno's documented [Abort event contract](https://jrsoftware.org/ishelp/topic_isxfunc_abort.htm).
Inno subsequently removes its own shortcuts, uninstall registration, logs and
uninstaller. `usPostUninstall` invokes native finalization from the temporary copy.
A03.12 exits only the uninstaller with code 2 after reporting a post-host
finalization failure, preventing Inno from displaying an ordinary success message.
Its private helper and protected recovery information remain. Native maintenance owns payload/data cleanup; Inno never fabricates a payload
uninstall log and has no wildcard UninstallDelete section.
The recorded host executable's matching `.dat` and optional signed-uninstaller
`.msg` files are left to Inno. The [.msg contract](https://jrsoftware.org/ishelp/topic_setup_signeduninstaller.htm)
does not add an A03.12 signing workflow here.

The immutable, protected `.outloud-uninstall.dat` at the actual application root
records installation and
certificate ownership, original token snapshots and every removal file/directory
identity before irreversible cleanup. Normal uninstall removes tokens/COM, managed
data, payload and signing resources. The installed helper, receipts and terminal
maintenance archives remain through Inno host processing. Finalization removes
those only after native absence checks and absence of the Inno host record and
recorded uninstaller/.dat/.msg files, then
deletes the removal plan last, after native directory cleanup. Keeping this receipt
at the application root permits retry even when configuration directory cleanup fails. Known application directories are removed only
when empty. A newly arrived or changed resource is preserved.

Removal is **not atomic**. File and certificate/key deletion cannot be rolled back
by a registry transaction. On failure, partial progress is reported, completion
is not claimed, and surviving receipts/archives/removal plan remain for checked
forward recovery. Reports distinguish present, absent and unknown COM, voice,
certificate/key states and list surviving recorded file paths. When a plan exists,
the report is also retained as `configuration/uninstall.failure.ini`; encoded paths
are UTF-16LE hex and Inno displays decoded paths. A reporting failure does not
authorize additional deletion. Unexpected recovery content remains protected.

An outstanding A03.10 `maintenance.pending` or older signing/registration journal
stops uninstall. Use that controller's checked recovery/finalization first; a
host version change may require explicit host recovery rather than rollback.
Do not discard its journals or backups to bypass a refusal. After a pre-host
uninstall failure, close clients/review the refusal and rerun the same uninstaller;
only exact surviving identities or absence recorded by the saved plan are accepted.
After a post-host failure or process interruption, preserve the plan/report and
use a reviewed matching helper copy for checked native finalization. The Inno host
may already be absent. There is no automatic rollback, process termination,
reboot, deferred deletion or success-only fallback.

Restart Manager and exclusive file/directory pins detect Voice Manager, the import
helper, SAPI clients such as World of Warcraft and data consumers. They are never
terminated automatically. Relevant applications must be closed by the user.

## Focused verification and release blockers

Build only `OutLoudInstallSigning`, `outloud-install-uninstall-tests`,
`outloud-install-maintenance-tests` and `outloud-uninstall-logger-check` in the
existing `build-installer-prerequisites` Release build. The last target compiles
only the changed production logger translation unit. Run native tests with a
private workspace parent such as `generated/u` (short enough for the inherited
Win32 path limit), then:

```powershell
.\installer\inno\test-uninstall.ps1
```

The integration check probes argument validation without a live root and compiles the actual
Pascal units in an inert, always-refusing harness. It does not compile a production
installer or run any uninstaller. Native tests use private files and simulated
registry/certificate operations; no live registry, certificate or voice data is
modified. Evidence stays in ignored workspace directories.

Historical A03.11 results (current A03.12 evidence is in [BUILD.md](BUILD.md)): Release helper and affected logger translation unit compile
with MSVC `/W4 /WX`; **70** native uninstall checks, **86** focused shared-maintenance
checks and **18** uninstall integration checks pass. Integration includes real Inno
7 compilation of the inert harness and confirmation that production compilation
at that time still failed at the unconditional gate. One long fixture path hit Win32 error 206;
the unchanged maintenance tests pass under the shorter private workspace parent.
No application, engine-wide build, broad regression suite or benchmark was run.

## Files changed for A03.11

- Native additions: `installer/native/data_binding.inc`, `uninstall.hpp`,
  `uninstall_windows.inc`, `uninstall_logs.inc`, `uninstall_tests.cpp`.
- Native changes: `installer/native/CMakeLists.txt`, `installation_path.hpp`,
  `maintenance.hpp`, `maintenance_codec.hpp`, `maintenance_files.inc`,
  `maintenance_windows.inc`, `maintenance_tests.cpp`, `signing.cpp`.
- Inno additions: `installer/inno/uninstall.iss`, `test-uninstall.ps1`, `UNINSTALL.md`.
- Inno changes: `installer/inno/OutLoud.iss`, `maintenance.iss`, `README.md`,
  `MAINTENANCE.md`, `SIGNING.md`.
- Logger: `sapi/src/error_logger.cpp`, new `sapi/src/log_ownership.hpp`, and the
  explicit Windows security-library dependency in root `CMakeLists.txt`.
- Other documentation: root `README.md`, `installer/README.md`,
  `docs/DEVELOPMENT_DEPLOYMENT.md`.

Pre-existing workspace changes are preserved. No development deployment script,
AppId, CLSID, product version, LICENSE or THIRD_PARTY_NOTICES.md was changed.

## Remaining release blockers

Before release, separately authorized disposable Windows 11 x64 testing must
exercise real Inno host ordering/abort behavior, ACLs, certificate stores/CNG,
managed tokens/library locks, active SAPI/Voice Manager clients, development-updated
payloads, custom paths, profile redirection/offline-profile refusals, cancellation,
interruption and finalization failures. Existing A03.10 prerequisite/signing/
publication/host-recovery validation and release qualifications also remain open.
General automatic cleanup of offline or unbound historical user data is unsupported:
ownership review is required. A03.12 subsequently integrates the controlled candidate. No A03.13 functional
testing, public release, commit or push is performed here. No third-party dependency was introduced;
THIRD_PARTY_NOTICES.md and LICENSE require no change.

## A03.13 Phase 3: live refusal and diagnostic fixes

On 2026-10-11, explicitly authorized interactive uninstallation of the Phase 1
installation stopped in `InitializeUninstall`, before confirmation/deletion, with
`registry_link_or_unexpected_link_value`. The installation, its one managed voice
token, certificate/key and recovery archives remain. No uninstall plan was
created. The saved Inno log confirms preflight refusal, not completed removal.
Evidence is in the ignored `generated/installer/a0313-phase3-20261011-030717`
directory. No application was terminated, no resource was forcibly deleted and
no reinstall or installed-helper replacement had been performed at that point.

Read-only native diagnostics identify Windows' standard
`HKEY_USERS\S-1-5-18` link to `\Registry\User\.Default`. Source now validates only
that exact alias and opens the physical `.DEFAULT` hive directly for loaded-user
inspection; arbitrary links remain forbidden. See Microsoft's
[LocalSystem alias explanation](https://devblogs.microsoft.com/oldnewthing/20070302-00/?p=27783).
The subsequent profile lookup exposed service profiles with absent/unpopulated
cached `Shell Folders` values. Source now also reads `User Shell Folders`, expands
only `%USERPROFILE%` using that profile's recorded directory, and checks both
configured and cached locations. Unknown expansions, inaccessible/offline hives,
unsafe paths and unbound logs still refuse cleanup. Affected helper/registration/
uninstall builds pass; **317** in-memory registration assertions and **95** focused
private-file uninstall checks passed before the checked repair described below.

The next corrected helper's read-only `--uninstall-check` reached a further refusal:
`maintenance_user_certificate_store_unavailable`. Windows enumerates default
logical store names (including `UserDS`) without an existing default registry
store. The physical registry adapter returns `ERROR_FILE_NOT_FOUND`; the current
cleanup controller treated this as unavailable. Source now enumerates physical
store definitions, inspects their read-only registry/system providers including
archived certificates, and distinguishes the exact owned default machine store
and verified machine inheritance from independent user/policy/custom copies.
An `ERROR_FILE_NOT_FOUND` is accepted only after independently checking the backing
registry namespace and, for logical providers, all enumerated physical sources.
Access/provider errors, unreviewed providers, relocation, remote sources and
unverifiable inventory still refuse deletion. Traversal is bounded. No empty
store is created and certificate/key conflicts remain fatal. Source-specific
Windows error diagnostics are now included in native/Inno failure reports.
The [store-provider contract](https://learn.microsoft.com/en-us/windows/win32/api/wincrypt/nf-wincrypt-certopenstore)
distinguishes a logical collection from one physical registry store.

The final corrected helper's read-only `--uninstall-check` reports **ready**
against the actual installation. The installed Inno uninstaller authenticates its
helper against the current protected inventory; copying a new binary or editing
that inventory would bypass its ownership contract. Applying a corrected helper
requires the existing checked package-maintenance repair path. The user subsequently
authorized the necessary repair/reinstallation to finish verification. The
corrected candidate must publish through that controller before retrying the
normal interactive installed uninstaller; no ad hoc helper/inventory replacement
is permitted.

### Live repair, finalization correction and verified removal

Candidate `a0313-04` compiled with Inno Setup 7.1.0; installer SHA-256 is
`78ff27306f12bc4a3f40ba45bc2bdc60eb634d3944970783c53925146ff88233`.
The authorized normal interactive same-version repair committed through shared
native maintenance. The installed helper matched the candidate inventory, the
installation ID and signing certificate were reused, all four bound library
files retained their hashes, and no pending maintenance transaction remained.

The installed Inno uninstaller then passed preflight and both user confirmations.
Native removal detached the managed voice and COM registrations and deleted
payload, managed data and the owned certificates/private key. Inno removed its
host record, files and shortcuts. Post-host native finalization correctly refused
to claim completion with `uninstall_checked_delete_failed`: its exclusive
maintenance-epoch handle lacked `DELETE` access. Only the protected removal plan,
maintenance lock and failure report remained. Inno's earlier generic success log
was insufficient; its subsequent native failure report recorded this partial state.

`MaintenanceFiles::acquire_epoch` now requests deletion access only for uninstall;
ordinary maintenance retains its previous access. The same authenticated exclusive
handle removes the lock, without closing/reopening it or bypassing ownership.
Four added private Windows-file checks verify ordinary-maintenance refusal,
resource preservation, exclusive locking and successful checked lock deletion.
The affected Release helper/tests build with `/W4 /WX`; **99** uninstall checks
pass, alongside the preceding **317** registration and **18** Inno integration
checks. A reviewed corrected helper ran the existing `--uninstall-finalize`
recovery entry point after host absence was verified. The saved immutable plan
authenticated surviving identities; finalization returned `uninstall=complete`.
The recovery driver then performed Inno's same nonrecursive, empty-directory
completion step. No surviving file was forcibly deleted. Original diagnostics and
a copy of the plan were retained in the ignored workspace evidence directory.

Final elevated read-only verification confirms absence of the application root,
bound library, COM registration, owned voice tokens, owned certificate copies,
machine key/container file, shortcuts, Inno registration and installation-owned
recovery artifacts. CNG reports `NTE_BAD_KEYSET` for the recorded key. All **264**
unrelated logical certificate entries and **4** unrelated SAPI token entries match
the baseline; VC runtime hashes and .NET shared-framework directories are unchanged.
Of **369** recorded development files, only the **11** deliberate source/test/doc
fixes changed. No application was terminated. External backups were outside
removal authority; the user declined separate backup hashing. Source/build assets,
candidates and diagnostic evidence remain in the workspace. Out Loud is no longer
installed.

Candidate `a0313-04` therefore requires the final lock correction before reuse.
The corrected production candidate is rebuilt under `a0313-05`; its authoritative
identity and compilation result are in that output directory's `installer-build.json`.
No further live installation is performed. This single-machine repair/removal
verification does not qualify all interruption, lock, custom-path or release cases;
the remaining release requirements above still apply.
