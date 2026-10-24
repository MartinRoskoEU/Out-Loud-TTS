# Out Loud production installer: A03.1-A03.12

Current product version: **0.3.0**, from `ProductVersion.props`. Current changes,
the October English model identity and compatibility findings are in the
[0.3.0 release notes](../../docs/releases/0.3.0.md).
The A03 records below retain their historical 0.1.0 validation context.

A03.12 completes the shared native/Inno integration for a controlled-test
**OutLoud-Setup-0.1.0.exe**, Windows 11 x64. The build scripts stage and compile
only; they never execute setup, register SAPI or create certificates. Functional
installation, update, recovery, development deployment and complete uninstall
qualification belongs to A03.13 on disposable Windows.

Inno owns UI, consent, prerequisites, shortcuts and host metadata. The A03.10
native controller owns signed payload publication and recovery; A03.11 supplies
ownership-checked complete uninstall. Shared maintenance/uninstall is enabled.
Standalone legacy signing/registration commands remain blocked. Unknown ownership,
changed identities, unsafe paths and active resources remain refusal states.
See [BUILD.md](BUILD.md) for lifecycle changes, build results and limitations.

## Build and stage

Run from the repository in Windows PowerShell 5.1 or later on Windows 11 x64:

```powershell
.\installer\inno\build-stage.ps1 -StageDirectory "$PWD\generated\installer\stages\candidate-01"
.\installer\inno\verify-stage.ps1 -StageDirectory "$PWD\generated\installer\stages\candidate-01"
.\installer\inno\build-installer.ps1 -StageDirectory "$PWD\generated\installer\stages\candidate-01"
```

The validated installer is automatically copied to the repository-root
`release/OutLoud-Setup-<version>.exe`; the filename follows `ProductVersion.props`.
SHA-256 is checked before and after publication. Same-version replacement uses a
same-volume staged copy and preserves the previous file on publication failure.
After success, older versioned installers move out of `release/` into the current
`generated/installer/compiler-output/` build directory. Locked obsolete files are
preserved with a warning. Other release-directory content is left alone.
Pass `-PreserveHistoricalInstallers` to `build-installer.ps1` or
`publish-installer.ps1` to leave older versioned installers in `release/` instead.
The 0.3.0 release preparation uses this switch to preserve earlier release artifacts.
Build output, provenance, replacement backups and diagnostics remain under
`generated/`; both output locations are ignored by Git. This does not provide
publisher signing or authorize installer execution.

To publish an already validated build without rebuilding it:

```powershell
.\installer\inno\publish-installer.ps1 -BuildDirectory "$PWD\generated\installer\compiler-output\a0313-05"
```

Prerequisites: existing Git, CMake 3.24+, Visual Studio 2022 MSVC/Windows SDK,
.NET 10 SDK and its installed Windows Desktop/reference/apphost packs. Current
reviewed x64 apphost pin is **10.0.12**, matching the existing apphost notice.
A differing apphost pack fails before publication; update its pin/notices only
after reviewing the actual replacement. No tool or prerequisite is installed.

Default assets are `generated/assets/pocket-english.olp` and
`generated/tokenizer/pocket-english-tokenizer.bin`. Optional `-ModelPath` and
`-TokenizerPath` accept other local copies only when size/SHA-256 match
[package.json](package.json). No conversion or asset download occurs.

Build behavior:

1. Refuse an existing destination or any output outside `generated/installer/stages`.
2. Validate complete legal inputs and pinned assets before building. Reject reparse points in input/output ancestry.
3. Configure `build-installer-production` with tests/development tools OFF,
   AVX2 ON and Visual Studio 2022 x64. Refuse a cache belonging to another source root.
4. Build engine, bridge, helper and the checked token-maintenance CLI in Release.
5. Restore WPF only from an empty local feed using installed SDK packs. Publish
   framework-dependent Release x64 into a fresh ignored build directory.
   No solution tests, application windows or helper operations are launched.
6. Require published bridge/helper hashes to match that native build. Reject
   unexpected publish files rather than harvesting them.
7. Build the static-CRT prerequisite probe using the installed apphost pack's
   public hostfxr header. Record the selected toolchain's matched VC runtime floor.
8. Copy exactly the allowlisted payload into a unique `.partial-*` stage;
   compare source and destination hashes, then validate the complete stage.
9. Require the source revision/content fingerprint to remain unchanged during
   the build. Move the completed stage into the requested new destination.

SDK first-run ASP.NET certificate generation is disabled for .NET invocations.
Stages and build directories are ignored by Git. A verified completed stage no
longer needs its private intermediate publish tree; that tree is removed after
staging succeeds. Failed or interrupted build/stage output remains for diagnosis.
A missing local SDK pack fails; it is not downloaded.

Successful publication writes `publication-record.json` with complete stage and
compiler-output inventories. Retention keeps the current publication and one
previous successful pair, ordered by recorded publication time. It also preserves
any pair referenced by external generated evidence or diagnostic code, and every
stage referenced by a retained installer/build-input/publication record. A failed
compiler's `build-input.json` pins its stage. New workflow operations share a
workspace mutex; interruption releases the mutex but does not authorize deletion
of incomplete artifacts. Unmarked legacy artifacts are never automatically adopted.

Re-publication keeps one distinct workflow-owned rollback EXE per output, plus
externally referenced backups; duplicate copies of the current EXE are removed.
Unknown files, changed hashes, locked files, malformed records and reparse points
refuse cleanup. Deletion checks absolute containment, inventories every file, and
removes files then empty directories without recursive traversal. Cleanup receipts
remain under `generated/installer/cleanup-receipts/`. Cleanup failures warn and
preserve remaining artifacts without undoing an already verified publication.
Retention never operates on `release/`, canonical assets, development fixtures,
installed state, signing keys, or voice data. Evidence and incomplete builds may
exceed the normal two-pair bound and require explicit review.

Focused synthetic checks (no build, installer execution, signing or live state):

```powershell
.\installer\inno\test-artifact-retention.ps1
```

## Structure and inventory

```text
generated/installer/stages/<candidate>/
    package-manifest.json
    build-record.json
    prerequisites.ini              # Toolchain-derived VC and published .NET floors
    signing-policy.ini            # Machine signing/code identity; no key/certificate
    maintenance-input.ini         # Complete hash-bound native transaction input
    maintenance-files.iss         # Generated dontcopy payload entries
    maintenance-copy.iss          # Generated temporary candidate materialization
    runtime-packages.ini           # Pinned Microsoft acquisition/verification metadata
    bootstrap/OutLoudPrerequisites.exe  # Temporary detection only; not app payload
    payload/
        bin/
            OutLoudPocketSAPI.dll
            OutLoudVoiceManagerNative.dll
            OutLoudVoiceImportHelper.exe
            outloud-sapi-voices.exe
            OutLoudInstallSigning.exe
            OutLoud.VoiceManager.exe
            OutLoud.VoiceManager.dll
            OutLoud.VoiceManager.deps.json
            OutLoud.VoiceManager.runtimeconfig.json
        runtime/
            pocket-english.olp
            pocket-english-tokenizer.bin
        licenses/                 # Required licenses, attributions, policy and generated terms
        LICENSE
        THIRD_PARTY_NOTICES.md
        RNG_NOTICES.md
```

The 24-file payload is selected by [package.json](package.json), not by recursive
copying of build or user directories. Stage metadata is outside the installable
payload. It records version, source identity, SDK/apphost provenance, native build
pairing and every payload size/hash. Hash metadata provides reproducibility and
accidental-change detection; it is **not signed release authentication**.

Repeatability means the same declared inputs and validated package layout. It
does not promise bit-for-bit native clean-build reproducibility across toolchains,
timestamps or build directories. Compare payload hashes between stages; provenance
can differ when documentation, source inputs or build paths differ.

Validation rejects all extra files/directories, including DLL/EXE, OLV/safetensors,
recordings, temporary files, debug/development artifacts and certificate/key files.
It checks all seven payload PE files for AMD64/PE32+, reviewed imports, exact model/tokenizer
size/hash, framework-dependent .NET 10 dependency closure, native/frontend pairing,
license copies and every original executable icon frame. It walks directories
without following reparse points. It does not load the engine or mutate a registry.

OS dependencies include SAPI/COM, UCRT, kernel/security/shell APIs and USER32 for
the .NET apphost. External prerequisites remain system-installed VC v14 x64
(`msvcp140`, `msvcp140_atomic_wait`, `vcruntime140`, `vcruntime140_1`) and .NET 10
Desktop Runtime x64. No vendor DLLs, runtime installers, SignTool or SDK files
are copied. The helper's System32 loader policy is preserved.

## Inno Setup lifecycle

[OutLoud.iss](OutLoud.iss) declares a machine-wide, user-selected secure directory,
defaulting to `{autopf}\OutLoud`. Updates keep the existing AppId's recorded
directory; see [installation paths and identity](INSTALLATION_PATHS.md).
**Windows 11 native x64 client only**, explicit payload entries and Start
Menu/optional desktop shortcuts. `MinVersion=10.0.22000`, `x64os` and the
read-only [preflight](prerequisites.iss) check the actual NT build, workstation
product type and native architecture. Windows 10, ARM64, x86 and Server are
rejected with an explicit support message. This is a technical eligibility floor,
not a claim that every Windows 11 servicing release remains supported by Microsoft.

Stable product identity is `AppId=OutLoud`, name `Out Loud`, publisher `Nendis`.
Version comes only from root `ProductVersion.props`; the planned first release
is `0.1.0`. CMake/Windows resources, WPF SDK properties, stage/build records and
generated `installer-version.iss` derive it automatically. Inno uses that staged
input for `AppVersion`, Windows file/product information and the
`OutLoud-Setup-0.1.0.exe` filename; compiler version overrides are refused.
AppId, SAPI CLSID and token identity remain unchanged. See [VERSIONING.md](VERSIONING.md)
for version increments, numeric comparison and validation. No release is declared.

The source now requires **Inno Setup 7.0.2+** authoring, checked through `VER` and
`EncodeVer`, for the reviewed temporary-directory/download security and `x64os`.
Staging does not install or require a compiler; compilation uses installed Inno 7.
`SetupIconFile` references the original WPF
`Assets/OutLoud.ico`. Shortcuts and uninstall display use the executable's icon
at index 0. No duplicate source icon or loose runtime icon is added.

Compilation requires a verified current production stage; the compiler wrapper
checks stage hashes, source provenance, compiler version and output identity.
Each compiler output directory must be new, so prior candidates are preserved.
There are no Registry, Run, UninstallRun or UninstallDelete sections. Native
publication starts at `ssInstall`; finalization checks Inno's completed host at
`ssPostInstall`. Awaiting-host interruptions resume verified payload and rerun
host work. Earlier incomplete native work receives checked rollback. Fully
rolled-back fresh installs now receive ownership-checked folder cleanup after
host, COM and signing-resource absence checks; interrupted cleanup retains a
protected receipt for retry. Installed updates keep their recovery archives. Applications
are never closed or restarted automatically.

## Deferred lifecycle and next step

A03.13 must exercise consent and prerequisite download/restart behavior, real
ACLs/CNG/signing, SAPI registration, updates and repairs, custom directories,
interruption/cancellation, development-updated payloads and full uninstall.
Corrupt or unrecorded recovery output and unavailable user hives are preserved
for review; they are never deleted or adopted to force success. See
[MAINTENANCE.md](MAINTENANCE.md), [UNINSTALL.md](UNINSTALL.md) and [BUILD.md](BUILD.md).

The candidate is **not publisher-signed**. Installation-owned LocalMachine
engine signing does not authenticate the downloadable setup executable.
Publisher signing and public distribution qualifications remain separate work.
Development deployment retains the existing Plan/Prepare/Deploy architecture;
only its shared native availability changes. No test in this phase executes
setup, uninstall, runtime installers, certificate creation or registry mutation.

## Licensing and validation limits

MIT copyright **2026 Nendis** and all original notices remain unchanged. A04.3
cleared the pinned model/tokenizer for unchanged bundling with CC/Unicode
attribution and the upstream policy reference. Full legal texts are local in the
payload; repository audit/exporter links remain background source references,
not additional developer-tool payload. No voice rights are implied.

VC redistribution entitlement, any redistributed .NET package's complete notices,
live per-machine signing validation and installer publisher signing remain release
qualifications. Packaging the existing notices does not settle these automatically.

`test-stage.ps1` mutates a unique test-owned workspace copy, checks rejection of
invalid stages and validates agreement between the Inno file list and the package
policy. It never uses production storage, UAC or COM. Its recursive cleanup verifies
the absolute test-owned target before removal. Inno compilation is checked in A03.12; live Windows lifecycle testing remains A03.13 work.

`test-prerequisites.ps1` checks the Inno source contract and the built probe on
the developer machine, including actual hostfxr roll-forward, invalid config,
controlled wrong-architecture/missing roots and report preservation. Pure native
fact tests cover unsupported operating systems and missing/outdated/wrong-architecture
runtimes without changing installed software. See the configuration guide for commands.

`test-runtime-acquisition.ps1` checks wizard wiring and read-only native verification
with controlled inputs. Optional `-PackageDirectory` uses existing local Microsoft
packages to verify exact hashes, signatures and bundle metadata; it never downloads
or executes an installer. The native `outloud-runtime-install-tests` target runs
the production orchestration policy with fake acquisition/detection/execution.
Out Loud never removes shared Microsoft runtimes, including after failed setup.

`test-consent.ps1` checks the standard page, explicit decline/defaults, production
Boolean acceptance predicate, ordering, silent refusal and complete offline
presentation. Missing/changed legal inputs are tested only in private copies.
Staging also refuses absent legal payload files and a changed generated agreement,
even when the test manifest hash is refreshed. No actual installer is compiled
or run; Inno wizard rendering and real button behavior remain manual validation.

A03.6 additionally stages the static-CRT signing helper and hash-pinned signing
policy metadata. `test-signing.ps1` performs controlled/read-only verification;
`outloud-install-signing-tests` uses operation doubles for mutations. Neither
creates keys/certificates nor signs files. The helper has its own hard mutation
gate, even when invoked directly. See the signing guide for inventory, limitations
and final results.

A03.7 uses that same helper for checked production registration. Stages additionally
contain canonical `registration-policy.ini`, binding all 23 payload files and the
read-only runtime detector. `test-registration.ps1` checks integration, hard refusal
and read-only conflict classification; `outloud-install-registration-tests` uses
an in-memory registry. No test registers an engine or changes a voice token.

## Installation identity and current ownership policy

The production installer has no special knowledge of prior deployment paths or
deployment types. The stable Inno AppId discovers one installed release, while
protected signing ownership and exact file identity authorize SAPI maintenance.
A matching CLSID or marker alone is insufficient. The selectable-directory and
generic conflict changes are described in [INSTALLATION_PATHS.md](INSTALLATION_PATHS.md).

### Historical A03.9 cleanup record

The following records the earlier cleanup, not the current source inventory or
current verification. Its retained conflict diagnostic was subsequently removed
in favor of the generic ownership check above.

The abandoned A03.9 controller, commands, journals, restoration, certificate
transition, cleanup planning, tests and Inno integration have been removed.
The six exclusive migration files are deleted; the helper no longer depends on
Restart Manager or the Voice Manager bridge for migration inspection. The typed
registry serializer remains shared because production registration inspection
and its recovery records use it. A03.1-A03.8 functionality and all gates remain.

No partial A03.10 implementation files were found; only inspection had occurred.
Ignored historical migration stages, binaries and evidence remain untouched.
They are not current production candidates: rebuild a new stage in a later,
authorized validation task before using the maintenance helper.

Modified files: root `README.md`; native `signing.cpp`, `CMakeLists.txt` and
`registration_policy.hpp` (diagnostic clarification only); Inno `OutLoud.iss`,
`registration.iss`, `runtime-install.iss`, `signing.iss`, `production-common.ps1`,
`package.json`, `test-registration.ps1`, this guide, `REGISTRATION.md`, `SIGNING.md`,
`PREREQUISITES.md`, `CONSENT.md` and `VERSIONING.md`. No application, engine,
licensing material or existing development cleanup script was changed.

Cleanup validation passed: all 15 installer PowerShell scripts parsed, package
JSON parsed, 19 focused source/configuration checks passed, 56 local Markdown
link targets resolved, and `git diff --check` plus modified-file whitespace
checks passed. Checks covered manual-refusal routing, the explicit 23-file
payload, production target/wizard wiring and all safety gates. Remaining
migration references are the retained A03.7 conflict diagnostic, its refusal
tests, historical results and documentation of the unsupported feature.
Native/WPF builds, staging, automated regression/integration suites, benchmarks
and live operations are deliberately deferred. Production upgrades, repair,
rollback/recovery and full uninstall are not implemented by this cleanup.
