# Production installer build

Out Loud 0.1.0 is prepared as a normal public release. Keep the product version
in `ProductVersion.props`; AppId, CLSID and installation lifecycle are unchanged.
Run from the repository root with the supported build tools and pinned offline
assets described in the [README](../../README.md#development):

```powershell
.\installer\inno\build-stage.ps1 -StageDirectory "$PWD\generated\installer\stages\release-0.1.0"
.\installer\inno\verify-stage.ps1 -StageDirectory "$PWD\generated\installer\stages\release-0.1.0"
.\installer\inno\build-installer.ps1 -StageDirectory "$PWD\generated\installer\stages\release-0.1.0"
```

Choose a fresh stage path for each build. The workflow builds production Release
x64 components, validates all allowlisted payload hashes/versions and legal
material, and publishes the compiler output to `release/OutLoud-Setup-0.1.0.exe`
with SHA-256 verification. Packaging records identify a `public-release`;
publisher signing and installer execution are not performed. GitHub publication
requires final manual approval. This preparation performs build/packaging checks,
not a new installation or broad lifecycle test campaign.

## Historical implementation and live validation records

The following records describe earlier development builds and their observed
limits. They are retained as evidence, not release-channel labels for 0.1.0.

A03.13 Phase 1 found that the first live fresh-install attempt stopped during
staging: the native controller treated the prerequisite detector's documented
BOM-prefixed UTF-16LE report as ASCII (`maintenance_input_encoding_invalid`).
The controller now decodes that format only for the detector report; request and
ownership metadata retain their strict ASCII contract. The failed transaction is
preserved for the existing checked setup recovery path. Corrected candidates
were rebuilt and verified before retrying; this change does not enable voice,
audio, development-update or uninstall testing. The subsequent attempt exposed
a second adapter mismatch: dependency verification reopened the engine while
signing already held its writable pin. PE inspection now reads that held handle,
including resetting its file cursor, without loosening the sharing/lock contract.
After a fully verified fresh-install rollback, setup now audits all terminal
archives and confirms absence of the Inno host, COM class, recorded certificates,
machine key and any installation-created library. It then records a protected
`.outloud-fresh-cleanup.dat` with every eligible file and directory identity before
removing those artifacts and the empty selected application directory. Cleanup
also handles earlier authenticated fresh rollbacks when setup is rerun. Unknown,
changed or locked resources stop removal; the immutable receipt survives partial
cleanup for checked forward recovery. Updates and awaiting-host installations are
excluded. This is failed-install cleanup, not execution of the uninstaller.
The following attempt reached `awaiting_host` with verified payload/signing/COM,
then exposed Inno 7's `Out Loud 0.1.0` default uninstall display name. Native host
validation now accepts that exact version-bound form as well as the earlier forms;
future setup explicitly records `Out Loud`. This keeps existing development
discovery compatible without changing the deployment workflow. Interrupted host
completion is resumed through the existing controller, preserving its recorded
payload and installation-owned certificate.

Out Loud **0.1.0**, Windows 11 native x64. AppId `OutLoud`, the authoritative SAPI
CLSID, MIT LICENSE and Nendis copyright are unchanged. This phase builds an
installable test candidate; it does not execute installation, uninstall, runtime
installers, signing/certificate creation, SAPI mutation or application shutdown on
the development machine. Full functional testing belongs to A03.13.

## Integrated lifecycle

The existing 23-file production stage, native maintenance authority and Inno UI
remain the architecture. Setup requires interactive license/Kyutai policy consent,
Windows eligibility, elevation, verified prerequisites and a secure local NTFS
directory. Updates stay at the HKLM64 Inno-recorded location, preserve managed
voices/settings and reuse the exact installation-owned certificate/key. Native
SAPI registration and signature verification remain inside the shared transaction.
Development Engine/VoiceManager/Both deployment scripts and selections are unchanged;
uninstall uses their updated complete inventory.

`PrepareToInstall` only prepares candidates after consent/prerequisites.
`ssInstall` invokes native publication and disables cancellation during publication
and Inno host completion. Inno alone writes shortcuts, uninstall registration,
uninstaller and its internal log. `ssPostInstall` verifies that host's root/version
and protected uninstall executable/log before native commit. Native failure before
host work stops setup; incomplete finalization replaces the finish-page success
text and produces setup exit code 2. Runtimes already installed remain shared.

`--maintenance-reconcile` authenticates the saved native transaction. Earlier
uncommitted work rolls back; a verified `awaiting_host` payload is retained and
the matching setup reruns Inno host work before finalization. A committed marker
is verified and retained. Setup never writes a fabricated uninstall log or rolls
old payload beneath a potentially newer Inno host. Fresh interrupted setup without
a host record must be rerun with the same selected directory (`/DIR` may select it).
Malformed/foreign host metadata refuses and needs review.

A protected `.outloud-bootstrap.dat` is written before transaction infrastructure.
It permits reconstruction of a missing initial frame/pending pointer, or promotion
of a fully framed interrupted initial write, after baseline authentication. Each
completed staged file and bootstrap output receives a journal checkpoint. A fully
owned terminal fresh rollback can retry with the same installation ID while keeping
its authenticated archives; machine-key absence is required before fresh generation.
Unknown, partial or modified output is retained/refused. Interruption within an
unjournaled filesystem/signing side effect can require inspection; recovery is not
unconditionally automatic or globally atomic.

The former 100-code-unit root restriction is replaced with the actual package/vault
path budget. All generated files must remain below ordinary Win32 MAX_PATH and
their parent directories below its directory-creation limit. Longer secure roots
can fit; extended-length/device/UNC paths and insecure/reparse ancestry stay refused.
This does not claim application-wide long-path support.
Missing selected parent containers are created with the same protected directory
policy. Containers outside the selected application root are not assigned product
ownership and are preserved by uninstall.

Complete uninstall retains A03.11's explicit permanent-data confirmation,
ownership checks, managed-token/COM ordering, bound library/log cleanup and exact
certificate/key cleanup. It refuses active clients without terminating them. Native
cleanup precedes Inno host removal; finalization requires absence of the host record
and recorded uninstaller/.dat/.msg files before consuming the helper and recovery
plan. A post-host failure reports retained state, then exits only the uninstaller
with code 2; Inno otherwise catches that event's exceptions and can display success.
The private helper copy remains available for reviewed recovery. Shared Microsoft
runtimes, unrelated resources, independent exports and unbound data are preserved.

Shared maintenance/uninstall availability is enabled. Legacy standalone signing and
registration mutation commands remain blocked; all platform, ACL, identity, lock,
certificate, transaction and explicit-consent checks remain active. No runtime
override, Force option or independent uninstall framework is added.

The event ordering was reviewed against the official [Inno event contract](https://jrsoftware.org/ishelp/topic_scriptevents.htm)
and [Inno Setup source](https://github.com/jrsoftware/issrc), including host-log save
before `ssPostInstall` and host deletion before `usPostUninstall`. Live durability,
cancellation and timing still require A03.13.

## Build

Use the existing offline production inputs and installed toolchain:

```powershell
.\installer\inno\build-stage.ps1 -StageDirectory "$PWD\generated\installer\stages\a0312-02"
.\installer\inno\build-installer.ps1 -StageDirectory "$PWD\generated\installer\stages\a0312-02" -OutputDirectory "$PWD\generated\installer\compiler-output\a0312-02"
```

Both destinations must be new. The first compilation in `a0312-01` is retained
as diagnostic evidence: compilation succeeded, but the initial wrapper rejected
Inno's space-padded resource strings. The wrapper now checks exact numeric versions,
product/copyright and trimmed display strings. The corrected candidate uses
`a0312-02`. Production staging uses incremental Release x64
targets with tests/development tools OFF, matching framework-dependent WPF publish,
pinned assets and complete offline license documents. The compiler wrapper validates
the whole current stage, source provenance, installed Inno 7.0.2+ and output identity,
then records compiler version, installer SHA-256/size and source/stage provenance in
`installer-build.json`. It never runs setup or replaces an earlier generated candidate.
After all checks and the build record succeed, it calls `publish-installer.ps1` to
publish `release/OutLoud-Setup-<version>.exe` at the repository root. The copied file
must match the recorded SHA-256 before promotion and at its final path. A previous
same-version EXE is safely replaced with a recovery backup in the generated build
directory; failed publication preserves/restores it. After success, older versioned
release EXEs are archived there, preserving locked files with a warning. `release/`
is Git-ignored; stage/compiler output and provenance remain under `generated/`.
An existing validated build can be published directly with
`publish-installer.ps1 -BuildDirectory <recorded-compiler-output-directory>`;
its recorded source/stage identity is checked without requiring today's source
fingerprint to match a historical build. Publication does not change signing.

**Build result: PASS**, Inno Setup **7.1.0**, no warnings on the corrected build.
Output: `generated/installer/compiler-output/a0312-02/OutLoud-Setup-0.1.0.exe`.
Size: **145,677,687 bytes**. SHA-256:
`43a204c6894de797c072878d98dc6753d93874ab7679838d04df9b93a71f9994`.
The complete 23-file stage passed validation before and after compilation;
numeric/display versions, product/copyright, unsigned status and source stability
passed. Build-time provenance remains in `installer-build.json`; this result
paragraph was added afterward without changing installer code or payload.

The executable is **publisher-unsigned**. Installation-time LocalMachine engine
signing does not sign the distributable installer or establish publisher trust.
No publisher certificate/key is used, no SignTool workflow is introduced and public
distribution/signing qualifications remain open. THIRD_PARTY_NOTICES.md was reviewed:
no new third-party payload/dependency is introduced; it and LICENSE remain unchanged.

## Focused verification and A03.13 readiness

Affected native helper/checker builds, existing private-file maintenance/uninstall
checks, targeted Inno integration/parameter checks, current-stage validation and
real Inno 7 compilation are the essential checks. No broad regression suite,
benchmark or live functional test is part of A03.12. Final evidence/output is recorded
in `generated/installer/compiler-output/a0312-02/installer-build.json`.

Focused results: affected Release helper/checker builds pass with `/W4 /WX`;
**102** native maintenance checks, **70** native uninstall checks, **31** shared
integration checks and **18** uninstall integration checks pass. Both integration
scripts compile actual Pascal units in inert, always-refusing harnesses and probe
incomplete arguments without supplying any live installation root. Installed Inno
Setup compiler version is **7.1.0**. Production compilation/output is recorded by
the compiler wrapper; no setup executable is run by any of these checks.

A03.13 must cover fresh/custom-path installation, prerequisites and consent,
release update/repair, voice/settings preservation, certificate reuse, SAPI verification,
development deployment, active-client refusal, interruption/cancellation/host resume,
complete uninstall and finalization failure. Refusals for corrupt/unrecorded recovery
content, foreign host metadata, inaccessible offline profiles and unbound historical
data must be exercised and reviewed. The candidate is not a qualified public release.

## Files changed for A03.12

- Native: `maintenance.hpp`, `maintenance_files.inc`, `maintenance_windows.inc`,
  `maintenance_tests.cpp`, `signing.cpp`, `uninstall.hpp`, `uninstall_windows.inc`.
- Inno: `OutLoud.iss`, `maintenance.iss`, `installation.iss`, `runtime-install.iss`,
  `prerequisites.iss`, `uninstall.iss`, `build-stage.ps1`, `production-common.ps1`, `version-common.ps1`,
  new `build-installer.ps1`.
- Existing contract checks: `test-maintenance.ps1`, `test-uninstall.ps1`,
  `test-stage.ps1`, `test-consent.ps1`, `test-runtime-acquisition.ps1`,
  `test-signing.ps1`, `test-registration.ps1`, `test-prerequisites.ps1`,
  `test-paths-identity.ps1`; changes replace obsolete foundation-gate expectations.
- Development check: `sapi/scripts/test-installed-update.ps1` updates only the
  read-only shared-availability expectation; deployment scripts are unchanged.
- Documentation: root/installer/Inno READMEs, `MAINTENANCE.md`, `UNINSTALL.md`,
  `SIGNING.md`, `REGISTRATION.md`, `PREREQUISITES.md`, `CONSENT.md`, `VERSIONING.md`,
  `INSTALLATION_PATHS.md`, `docs/DEVELOPMENT_DEPLOYMENT.md`, new `BUILD.md`.

Pre-existing workspace changes and all earlier ignored stages are preserved.
No development deployment architecture, AppId, CLSID, product version, licensing
payload, commit or push is changed. No A03.13 testing is performed.

## A03.13 Phase 1 live installation result

With explicit authorization, interactive setup completed on this Windows 11 x64
development machine at `C:\Program Files\OutLoud` on 2026-10-11. The corrected
candidate is `generated/installer/compiler-output/a0313-03/OutLoud-Setup-0.1.0.exe`
(Inno 7.1.0, 145,686,526 bytes), SHA-256
`0431e21884aafd1421932f5e6ea38f547daac2675fad4bf76e41ce523a7350dd`.
It remains publisher-unsigned. The existing awaiting-host transaction from
`a0313-02` completed without republishing its recorded payload or recreating its
installation-owned certificate; all 23 installed payload files match that
transaction's protected inventory and applicable package hashes.

Read-only verification passed for payload hashes/physical identities, installed
Voice Manager/runtime configuration, HKLM64 COM registration and receipt, valid
engine signature, exact LocalMachine My/Root certificates, nonexportable
3072-bit signing-only machine key with protected Administrators/SYSTEM access,
Inno host metadata/uninstaller, Start menu/selected desktop shortcuts and bound
ProgramData library identities/permissions. VC 14.51.36247.0 and .NET 10.0.12 resolve
the installed prerequisites. Native maintenance is committed, no pending
transaction remains and no restart was required. Earlier verified failed-fresh
folder cleanup also completed through setup's recovery path.

Affected Release builds, **143** focused private native maintenance checks and
**32** Inno integration checks passed. Evidence is retained under the ignored
`generated/installer/a0313-phase1-01` directory, including setup logs,
`installed-verification.json` and `prerequisites-installed.ini`. The diagnostic
checks only read live resources. They accept Windows' full-access key mask using
the production bitmask contract and preserve the library's subsequently present
voice data; neither condition required a production change.

At the end of Phase 1, Out Loud remained installed for manual Voice Manager, import and TTS testing.
The agent did not import voices, synthesize speech, play audio, deploy development
updates, uninstall, terminate applications or perform later A03.13 phases.
Remaining functional/release qualifications above still apply.

A03.13 Phase 3's later authorized live uninstall attempt refused during preflight;
the installation remains intact. Read-only diagnostics and source-only fixes for
the Windows LocalSystem registry alias and service-profile known folders are
documented in [UNINSTALL.md](UNINSTALL.md#a0313-phase-3-live-refusal-and-diagnostic-fixes).
Physical certificate-store absence/inventory handling is also corrected in source,
and the helper's read-only preflight now reports ready. The user has authorized
the necessary checked repair/reinstallation. Publication of the corrected helper
and the subsequent normal interactive uninstall still require live verification;
the earlier candidate is not uninstall-qualified.

Phase 3 subsequently completed the authorized interactive checked repair using
`a0313-04`, preserving the installed voice data, installation ID and certificate.
The normal installed uninstaller removed native resources and the Inno host, then
reported a finalization failure on `maintenance.lock`. The uninstall-only epoch
handle now includes the Windows `DELETE` right. **99** focused uninstall checks
pass, including actual private-file deletion through the exclusive handle.
Checked native forward recovery completed using the saved protected plan; final
read-only verification confirms complete removal. Unrelated certificate/token
inventories, VC runtime hashes, .NET frameworks and development resources are
preserved apart from the deliberate source fixes. See
[the live completion record](UNINSTALL.md#live-repair-finalization-correction-and-verified-removal).

The corrected production candidate is rebuilt at
`generated/installer/compiler-output/a0313-05/OutLoud-Setup-0.1.0.exe`.
Check its `installer-build.json` for the recorded source/stage/compiler identity
and SHA-256. It remains publisher-unsigned and is not installed again during this
phase. Out Loud is no longer installed on the development machine; broader A03.13
functional coverage and publisher signing remain separate release requirements.
