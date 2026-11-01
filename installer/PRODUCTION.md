# WinUI production installer

`inno/build-installer.ps1` builds the Rust-only Release components, publishes WinUI as a self-contained .NET 10/Windows App SDK x64 application, verifies the reviewed payload, and compiles one `OutLoud-Setup.exe`. It never installs, registers, provisions certificates, or signs on the build PC. Windows 11 native x64 is required.

```powershell
powershell.exe -NoProfile -File installer/inno/build-installer.ps1 `
  -Compiler 'C:\path\to\Inno Setup 7\ISCC.exe' `
  -VcRedistributable 'C:\path\to\vc_redist.x64.exe'
```

The compiler must be Inno Setup 7.0.2 or later. Supply the official Microsoft VC v14 x64 14.51.36247.0 package pinned in `inno/runtime-packages.ini`. The build validates its size, SHA-256, Microsoft signature, and identity. No dependency download occurs during setup. `-SkipComponentBuild` reuses already-built native Release components; WinUI still publishes to a fresh stage.

Staging and build evidence go to a fresh `generated/production-installer/<id>` directory. The build script directs Inno's final output to the repository's `release/OutLoud-Setup.exe` and refreshes its `OutLoud-Setup.exe.sha256` checksum. Subsequent builds replace these two current-release files; older version-named releases are preserved. The exact allowed publish paths are in `inno/payload-layout.json`. The six compiled XAML/PRI files are copied from the same build output, following the development deployment convention. Project images require Out Loud version metadata. Microsoft images retain their original signatures; the SDK's exact, hash-pinned `Microsoft.Windows.Workloads.Resources_ec.dll` companion is retained unchanged alongside the x64 SDK payload. The official Rust executable is pinned to the validated v4.0.1 release bytes. Model weights, voice profiles, and PDBs are not packaged.

The payload includes licenses, the production SAPI DLL, native bridge, elevated helper, registration CLI, installation maintenance helper, prerequisite probe, official Rust CLI, and self-contained application dependencies. The VC redistributable is embedded separately and runs only if reliable detection establishes that it is missing or too old. Vendor restart requirements stop setup for a manual restart; shared Microsoft runtimes are never uninstalled.

Current legal material includes Sonic's Apache-2.0 license, native model/tokenizer
attribution and CC BY 4.0 terms, the Inno Setup license, original license/notice
files from the resolved NuGet packages, and the .NET runtime/Windows App SDK
terms. The original Pocket TTS MIT licenses remain beside the runtime and embedded
configuration metadata. `licenses/pocket-tts` contains the bundled runtime's
dependency notices, pinned Cargo.lock and component metadata. Complete published
source archives for its 18 MPL-2.0 dependencies are included; the build checks
their SHA-256 values against that exact lockfile. This source/notice collection
is legal distribution material and does not introduce runtime dependencies.

After a successful installer build and SHA-256 publication, the build script
records completion in that stage and removes previous completed GUID stages
under `generated/production-installer`. It retains the current stage and any
stage containing the supplied VC package. Incomplete/unmarked stages, linked
content and stages outside that fixed parent are preserved, with cleanup
failures reported as warnings. `-PrepareOnly` never performs this retirement.
This cleans project build output only; it does not run the installer or touch
installed application data. Existing historical stage output can be removed
manually after inspection; compact build logs remain available separately.

## Installation and signing

The standard elevated Inno wizard provides directory selection, native shortcut tasks, an informational certificate/registration page, Ready to Install, progress, and completion. Start Menu is checked initially; Desktop is unchecked. Inno preserves task choices on upgrades.

Preparation, native installation/signing, and final verification use Inno's built-in animated marquee progress page with stage descriptions. The animation indicates activity rather than a percentage of files installed; the native controller remains responsible for verified completion.

Setup and uninstall failures show a shared diagnostic dialog made from Inno's native form, read-only wrapping memo, and standard buttons. Full paths and filenames remain selectable without ellipsis; longer messages can be scrolled and the window resized. The user can open the generated report's folder directly. Uninstall uses the existing native progress bar in marquee mode while the maintenance helper checks, removes, or finalizes components, then restores Inno's normal progress handling. No timer, custom progress drawing, or invented percentage is used.

`native/production` implements the shared native installation lifecycle. Protected staging, pinned independent file identities, complete inventory receipts, ownership checks, durable checkpoints, and checked recovery are authoritative. The standalone development inspector and deployment inventory have separate ownership boundaries.

Maintenance directories and their working subdirectories grant Users inherited
read/traverse access, with modification restricted to Administrators and SYSTEM.
This lets Explorer browse them without granting a personal account write access.
Verified existing maintenance directories receive the same read grant when opened
by maintenance, through a validated directory handle, preserving the owner and
existing ACL entries. Unsafe owners, user-write grants and reparse points still
stop the operation; they are not automatically repaired. The machine signing key
keeps its separate administrator/SYSTEM-only permissions.

Fresh setup creates an installation-owned, nonexportable RSA-3072/SHA-256 machine signing key using Microsoft Software KSP. Only Administrators and SYSTEM can access it. The code-signing certificate is stored in LocalMachine My, with its public certificate in LocalMachine Root. Setup signs the eight reviewed Out Loud images and verifies their signer and unchanged SIP content before publication/registration. It never re-signs Microsoft or Kyutai files. The setup executable itself is publisher-unsigned; no build-machine certificate or trust store is modified.

The established COM/SAPI CLSID remains `{F38C3398-3552-43B9-8350-7DD301327D70}`. Fresh setup creates the protected ProgramData voice library and binds its physical directory identity. No voices or models are automatically installed. Conflicting COM registrations, unbound pre-existing voice libraries, and unsupported ownership state are refused.

Before Microsoft prerequisites or product installation, the existing read-only `--registration-inspect` operation checks Windows speech tokens for Out Loud engine references, managed markers, and managed token-name candidates. Fresh installation and maintenance permit existing native machine-wide tokens with the exact supported schema, owner marker, engine identity, matching library ID, and canonical profile path under the fixed shared voice library. Stored application/runtime locations are metadata: old deployment paths, mixed Debug/runtime locations, and missing old folders do not block installation and are never probed. Unverified ownership, unsupported registry scope, or an unreadable inspection still stops setup with a plain-language explanation. Candidate detection never authorizes deletion, adoption, path rewriting, or access to files named by a token. Installation leaves existing voice registrations unchanged; it does not repair stale playback metadata. Unrelated voices are ignored.

Inspection reuses the native token reader and ownership/path validator. It covers classic machine-wide Registry64 SAPI plus the other machine/user views, Speech OneCore, and accessible loaded user hives already inspected by uninstall; offline hives are not mounted. Exact token/name/scope/path diagnostics are encoded in the report, with only the explanation and report location shown by setup. Native maintenance repeats the check before staging/signing and before publication, including direct development maintenance. Installation checks only detect conflicts and never clean up or adopt registrations. Confirmed uninstall has the separate registry-only cleanup policy below.

Self-DELETE is excluded only for a handle-verified physical volume root during ancestor replacement checks. Ordinary directories, drive aliases, reparses, insecure owners, and other writable ACLs receive no exemption.

## Upgrades and persistent storage

The stable Inno AppId is `OutLoud`. Only the current WinUI production receipt format (`OutLoud.Inventory.3`) is supported. Upgrades reuse the verified Inno directory, installation identity, and exact owned certificate/key. Older installer formats and development deployments are not adopted. Equal-version checked repair and newer releases use the existing shared maintenance lifecycle. Models, runtime updates, selected model, Settings, native profiles, and SAPI voice tokens are untouched by upgrades.

### Updating an installed application during development

After installing once, use elevated x64 Windows PowerShell 5.1 to update that same installation without running or rebuilding Setup:

```powershell
.\sapi\scripts\update-installed.ps1 -Action Plan -DeploymentRoot 'C:\Program Files\OutLoud'
.\sapi\scripts\update-installed.ps1 -Action Prepare -DeploymentRoot 'C:\Program Files\OutLoud'
.\sapi\scripts\update-installed.ps1 -Action Deploy -DeploymentRoot 'C:\Program Files\OutLoud'
```

Use the actual recorded installation directory. Close Out Loud, World of Warcraft and other applications using its components manually first. Plan performs read-only discovery and reports the intended operation; it does not claim complete native verification. Prepare builds the current native Release components and self-contained WinUI payload through `build-installer.ps1 -PrepareOnly`, then runs native read-only preflight. It leaves installed files unchanged and candidates unsigned. Deploy independently prepares the current source, preflights, signs with the exact installation-owned certificate/key, and publishes through the existing native maintenance lifecycle. No compiler or VC redistributable is needed for this update path: existing prerequisites must already be compatible; they are verified rather than installed. Development-bundle publish/signing overrides are refused for installer-managed applications.

This path requires the installed and source product versions to match. Use the production installer for a release version change or changes to the Inno wizard/uninstaller script. Same-version updates cover the complete reviewed package, including XBF/PRI resources, native components, runtime dependencies, licenses and the inventory-aware maintenance helper. They update protected signing/inventory receipts and retire only obsolete inventoried payload files. The Inno uninstall executable/log, installer record, installation identity, certificate/key, COM/SAPI registration, voice tokens and managed application data are preserved. Complete native verification must succeed before the update is reported as verified. Locked files, foreign/legacy ownership, changed receipts or incompatible dependencies stop the update. Existing native recovery records remain authoritative on failure; do not manually overwrite files or remove them.

Staging and reports remain under `generated/installed-updates/<id>`. A development deployment without installer ownership continues to use the existing independent bundle path; the installed update route does not adopt or remove it. Building or preparing these tools does not retire an existing development deployment or authorize installation on the build PC.

The installer-managed WinUI application uses the shared `%ProgramData%\OutLoud\ApplicationData` store. Its binding records the installation ID, application root and physical directory identity, without a user SID. Existing stores require a matching verified binding before shared changes; they are never automatically adopted. Earlier per-user stores are left untouched and are not used for current models or Settings. The shared storage and uninstall boundaries are detailed below.

## Uninstallation and limitations

### Approved file cleanup policy

Default confirmed uninstall removes verified application-owned
resources and preserves unknown user content. An unlisted file's presence alone
does not block deletion of other known resources; containing directories remain
when needed by retained files. Root bindings, ownership, signing, registry checks,
exact identities of planned resources and active-file checks remain mandatory.
Changed planned resources still stop deletion safely.

Completed maintenance archives are owned working data. After a committed operation
has passed payload/host verification, finalization retires its verified backups
and checkpoints automatically, including completed older archives. A protected
immutable `configuration/archive-cleanup.dat` plan records exact file stamps,
directory identities, installation/certificate identity and physical root identity
before deletion starts. Interrupted deletion resumes at the next completed
maintenance operation. Uninstall can also inventory surviving resources from this
plan without first repairing the app. The plan is deleted last, after all recorded
files are confirmed absent.

Installation commit and archive cleanup are reported separately. If archive
cleanup cannot finish after a verified host/payload commit, the native report
contains `archive_cleanup=pending`; the existing Inno diagnostic dialog or
development-update warning reports it explicitly. The completed installation is
not rolled back because a later archive file is busy. Recovery records remain
for the next maintenance operation; cleanup is not reported as complete.

Archive audit reads one checkpoint at a time rather than retaining every full
snapshot. File/directory inventory limits are consistently 65,536, with the
existing 16 MiB encoded-plan bound. Automatic retirement prevents successful
operations from accumulating unneeded backups/checkpoints. Limits fail closed.

Atomic model-inventory, model-selection, model-update-cache and runtime-identity
writes reuse the existing schema-1 staging records with a single `value.json`
payload. Their existing destination and atomic replace/backup behavior are
unchanged. A shared zero-length `metadata-write.lock` guards metadata publication
and recovery. Recovery removes only the recorded temporary payload, never the
committed destination. Uninstall recognizes these metadata records too. The lock
is used by metadata writes/recovery only; SAPI synthesis and streaming do not
acquire it. Startup recovery may reclaim unrecorded GUID staging operations only
after the existing shared-store ownership/physical binding validation, under both
installation and metadata locks. The complete directory must match a supported
temporary layout (model GUID `.partial` files, runtime ZIP/extraction files, or
one `value.json`), with independent regular-file identity checks. The same
schema-1 cleanup record is persisted before deletion; no new cleanup format or
installer behavior is introduced. Torn temporary journal publications without a
payload directory are disposable too. Unknown layouts, links and invalid or
conflicting operation records remain protected. This runs at startup, never in
the synthesis path, and does not touch installed assets or committed Settings.

Failed fresh setup retains its separate signing/library cleanup path. Unfinished
operations keep their recovery data. Archive retirement never deletes current
payload, models, profiles, Settings, registry entries or signing resources.
Unknown files encountered inside an archive remain outside the deletion plan.

Earlier per-user stores are not adopted. External exports, unrelated registrations,
certificates and shared Microsoft runtimes remain outside cleanup scope. Older
downloaded runtime versions are removed after verified activation, reusing runtime
staging cleanup records and checked deletion. Busy versions are retried at startup
under the runtime update lock; active and unverified runtimes are preserved. The
inventoried bundled baseline is an application component, not an update archive.
Crash leftovers outside recorded download/maintenance operations are separate
lifecycle work. Source changes require a new installer or
the existing development update workflow before they affect installed binaries.
Release records do not establish live validation of a later build.

### Existing implementation and limitations

The standard Inno uninstall confirmation explains that removal applies to all users and permanently removes the application, engine, managed voices/tokens, models, runtime updates, Settings and verified data, plus the installation-owned signing certificate/private key. Declining cancels uninstall. Full native preflight runs after this confirmation and before either native or Inno host removal. Voices and models do not need to be removed manually. The native uninstaller verifies the installation, certificate/key ownership, COM registration, owned voice-token schema, complete file receipts, active users of files, and data bindings before removal. Owned tokens and COM are removed in one guarded registry transaction; deletion is forward-only and retryable through protected receipts, never represented as atomic file/certificate rollback.

Complete uninstall includes every verified native Out Loud-managed token in classic machine-wide Registry64 SAPI that depends on the owned engine, including historical deployment paths and missing profile files. Proof requires the exact managed namespace, schema, owner marker, matching library ID, vendor, Rust backend, engine CLSID, and canonical voice path inside the independently bound shared voice library. Stored AssetRoot and RustRuntimePath values must retain their supported registry types but do not determine registry cleanup eligibility. The current installation must still prove ownership of the COM engine and library through its existing signed inventory and protected receipts; another engine owner is never adopted. Neither the existence nor absence of an old deployment directory authorizes cleanup, and no files are accessed or deleted through a token's stored application/runtime paths. The file plan is built from verified payload/data inventories and authenticated interrupted-cleanup plans. Unknown user content is not added merely because it is inside a managed root.

Unverified or modified Out Loud candidates stop removal before deletion instead of being silently ignored, including entries whose CLSID was removed or changed. Other registry locations remain inspection-only and are never adopted for cleanup. Unrelated Windows voices remain untouched. The exact planned tokens are revalidated before and inside the existing registry transaction; changed/new candidates stop it, and absent planned entries are accepted only when retrying the authenticated protected removal plan. Registration cleanup does not expand the file/directory plan. Current COM engine, installation receipt, signing, and shared-data ownership checks remain unchanged.

Failures use a concise explanation of the actual category (voice ownership, files in use, unverified data, unfinished maintenance, or signing resources). Inventory/file lists and raw reason codes remain in the native report and automatically enabled Inno uninstall log rather than filling the user dialog. A failed preflight is distinguished from interrupted cleanup. Unverified voice metadata identifies the voice and retains the exact token/field/paths in diagnostics; token names are encoded so malformed registry names cannot inject report fields. Historical deployment paths never grant file ownership, and profile paths outside the canonical shared library remain protected.

Install finalization and uninstall validate the same Inno host command: a quoted `uninsNNN.exe` directly in the recorded installation directory, with either no arguments or Inno's exact ` /LOG` suffix for automatic logging. Other arguments, executables and locations are refused. The corresponding uninstall data log and existing ownership/security checks remain required.

Models, runtime updates, inventories, model selection, and model-update check state now use `%ProgramData%\OutLoud\ApplicationData`. OutLoud.WinUI declares `requireAdministrator` in its application manifest, requesting administrator approval at startup. The application then uses the existing administrator paths for shared-data operations without further UAC prompts during that session. Voice operations retain the existing elevated helper and its authorization checks. External SAPI clients do not require elevation for playback.

The existing elevated voice helper also retains its allowlisted shared-data worker route to the existing managed services in a windowless instance of the same installed WinUI executable. The handoff reuses the production Authenticode and owner/ACL policy, verifies the helper, WinUI executable and managed assembly have the same trusted signing certificate, and pins the protected deployment/dependencies until the worker exits. Unsigned or user-writable build output is not an elevated worker source; use the established signed deployment workflow. Shared stores retain the protected administrator/SYSTEM-write, Users-read/execute ACL. No alternative executable, storage root, arbitrary command, or download destination is accepted from the caller. Cancellation remains cooperative through existing service cleanup.

The helper requests `READ_CONTROL` when opening its ancestor pins and reads the ACLs through those same retained handles. Other users of the shared pin utility retain the default attributes-only access. No directory permissions or validation rules are changed, and the helper does not depend on reopening directory handles later.

The first approved shared-data operation creates an installation/physical-directory binding (`OutLoud.WinUI.Data.2`), without binding it to an individual user's SID. Existing stores without a matching verified binding are never adopted by a production installation. Development bundles without a production signing receipt remain unbound and are not eligible for production cleanup. Upgrades validate and preserve an existing bound shared store; fresh installation refuses an existing unbound/conflicting store. No per-user data is moved, copied, deleted, or automatically selected. Previously installed per-user models and Settings remain untouched and are not the shared model source.

Uninstall verifies the current shared binding, fixed metadata schemas/names, independent file identities, immutable model blob content hashes (SHA-256 or Git blob SHA-1), and updated-runtime receipts/hashes. There is no legacy per-user storage discovery, ProfileList enumeration, user-hive loading, or cleanup of user-profile directories. Signed-out accounts do not block removal of current global data. Existing user-profile data, including diagnostic logs outside the shared store, is left untouched rather than adopted or migrated. Inspection of loaded Windows speech registrations remains a protection for the current engine's dependencies; it does not discover profile files. Long-lived operation locks and loaded payload/runtime files still prevent removal while active. Speech configuration and diagnostic job markers remain private temporary operation files, not persistent shared models or Settings.

Model downloads and runtime updates persist an immutable schema-1 operation record beside their staging directory before creating download payloads. It records the operation ID and kind, shared store physical identity, and exact temporary filenames; it accepts no caller-supplied absolute cleanup paths. The existing model-install/runtime-update lock remains held through cleanup. Startup recovery reuses those locks and the shared-store ownership binding, verifies the complete operation before deleting files through the existing checked-handle mechanism, and removes directories nonrecursively. Complete temporary journal publications are recoverable too. Uninstall validates the same records against its independently verified shared-store binding and captures only the recorded staging resources in its existing pinned deletion plan. Staged runtime files do not need a completed runtime-installation receipt; the download operation record establishes their temporary ownership. Installed runtimes retain their existing receipt/hash validation.

Unlisted files and unrecorded/invalid download metadata grant no deletion authority. They stay outside the deletion plan without blocking known cleanup solely by their presence. Corrupt inventoried content, root-binding failures, inaccessible roots, active owned files and changed planned resources still stop safely. The bounded plan size/count checks remain; they are not a substitute for retiring completed history.

## Targeted validation and Sandbox boundary

The existing native checks are compiled with the production inventory. They exercise certificate/signing policy with doubles, in-memory registry transactions, file inventories, ownership, path/ACL rules, and isolated cleanup fixtures. The WinUI binding integration is build-checked. No application startup, model download, synthesis, installation, uninstallation, signing, certificate provisioning, or COM mutation is part of build validation.

The uninstall progress bar animates during preflight and native removal while Inno's progress form exists. Inno destroys that form before `usPostUninstall`; post-host native finalization never accesses its controls. Both expected native refusals and unexpected post-host script exceptions use the existing diagnostic/nonzero-exit path, so incomplete finalization cannot fall through to Inno's success message. The protected removal plan remains until verified finalization; subsequent setup refuses to adopt unfinished ownership state.

Uninstall captures the maintenance helper's launch result, exit code, stdout and stderr in Inno's persistent log, using the same capture mechanism as installation inspection. When no native report was created, the diagnostic window links to that log instead of advertising a missing report. Native failure reporting retains the original reason and finalization phase even when inventory diagnostics or either report destination are unavailable; reporting failures do not replace the cleanup error or relax cleanup validation.

During bottom-up directory cleanup, extra shared-data ancestor handles are closed only for the directory identity currently being removed, while its independently verified DELETE-capable pin remains held. Other ancestor handles stay protected until their own cleanup or normal release. This avoids keeping removed model directories pending deletion through the uninstaller's own handles. The bound shared parent uses the same checked handle deletion. A nonempty directory is left in place for retained user content. It does not cause failure once all planned owned resources have been verified removed.

Before distribution, the user should test fresh install, same/newer-version maintenance, cancellation/recovery, locked files, signed-out accounts, confirmed uninstall, preservation of unlisted files without blocking known removal, refusal of unbound roots and external link targets, diagnostic folder actions, progress presentation, and certificate cleanup in Windows Sandbox. Successful build and source checks do not replace acceptance testing of the actual release installer.
