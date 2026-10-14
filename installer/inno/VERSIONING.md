# A03.8: unified product versioning

**Current status:** [A03.12](BUILD.md) supersedes earlier foundation/compiler-gate
statements below. Shared native maintenance/uninstall and Inno lifecycle are
integrated for a controlled-test build. Standalone legacy signing/registration
commands remain blocked. Historical phase evidence below does not establish
live qualification; A03.13 functional testing is still required. Path budgeting
now uses the complete vault layout, rather than the former 100-character root cap.


The authoritative source is root [ProductVersion.props](../../ProductVersion.props).
Its `OutLoudProductVersion` is **0.1.0**, the planned first public release. This is
pre-1.0 Semantic Versioning, not a declaration of release or production readiness.
Production upgrade/downgrade enforcement, whole-install rollback/recovery and
uninstall remain incomplete. All compiler, installer and native mutation gates
remain closed; no distributable installer is generated.

## Single source and propagation

Use canonical decimal `MAJOR.MINOR.PATCH`: exactly three components, no leading
zeros, suffix, build metadata or whitespace. Each component is 0..65534, the
shared bound supported by Windows resources and .NET assembly metadata. This
bounded product representation is deliberately narrower than unrestricted SemVer.
`MAJOR.MINOR.PATCH.0` is always derived, never edited separately.

| Output | Derived metadata |
| --- | --- |
| Native CMake projects | `project(... VERSION ...)` reads the source through `cmake/OutLoudVersion.cmake`; changes automatically trigger CMake regeneration. |
| SAPI DLL and production native binaries | Generated Windows RC resources: product `Out Loud`, ProductVersion `0.1.0`, FileVersion `0.1.0.0`, numeric fixed versions `0,1,0,0`, Copyright `Copyright (c) 2026 Nendis`. |
| WPF DLL/apphost | SDK-generated assembly/file version `0.1.0.0`, informational/product version exactly `0.1.0`, product and copyright from the source. Automatic commit-SHA suffix is disabled; build records still retain source HEAD/tree hashes. |
| Installer bootstrap and maintenance helper | The same native version resources, independent of their separate build directory and static CRT. |
| Build/package records | Product version/name/copyright, four-part file version, central source SHA-256, installer AppId/version/file-version/output filename. |
| Inno | Generated stage-only `installer-version.iss`, consumed by the existing project. `AppVersion=0.1.0`, `VersionInfoVersion=0.1.0.0`, product text `0.1.0`; future output `OutLoud-Setup-0.1.0.exe`. |

The native resources cover SAPI, bridge, Voice Manager mutation helper and the
three production CLI targets, plus both installer executables. Static libraries
and development-only checker projects do not get independent product-version
copies. The existing WPF SDK configuration generates assembly attributes and
copies the correct resource to the Windows apphost. No extra AssemblyInfo file,
version-management dependency, About screen or other UI feature is introduced.

AppId remains `OutLoud`; assembly name remains `OutLoud.VoiceManager`. SAPI CLSID,
token/library IDs, ABI, helper protocol, installation/certificate ownership,
manifests' existing SxS identities and asInvoker behavior are unchanged. SxS
manifest identity versions are activation identities, not this product's PE or
.NET version fields. Model/tokenizer formats, compatibility IDs, Microsoft runtime
versions, SDK packs and third-party licensing/provenance remain independent.

The implementation uses supported [SDK assembly properties](https://learn.microsoft.com/dotnet/standard/assembly/set-attributes-project-file),
the [.NET assembly component limit](https://learn.microsoft.com/dotnet/api/system.reflection.assemblyversionattribute),
and Inno [version resources](https://jrsoftware.org/ishelp/topic_setup_versioninfoversion.htm)
and [AppVersion](https://jrsoftware.org/ishelp/topic_setup_appversion.htm), which
supplies the future Windows Apps & Features display version. No installed entry
was created or updated by this task.

## Version changes and builds

1. Edit only `<OutLoudProductVersion>` in root `ProductVersion.props`. Select the
   next MAJOR/MINOR/PATCH value deliberately; version numbering does not itself
   certify compatibility or authorize a release.
2. Build using existing commands. CMake reads it automatically. The WPF project
   imports the same file for ordinary builds and publication. Do not pass
   `-p:Version`, other version overrides, or Inno `/DProductVersion` arguments.
3. Produce a **new** workspace stage and run consistency/tamper checks:

```powershell
.\installer\inno\build-stage.ps1 -StageDirectory "$PWD\generated\installer\stages\candidate-01"
.\installer\inno\test-version.ps1 -StageDirectory "$PWD\generated\installer\stages\candidate-01"
.\installer\inno\test-stage.ps1 -StageDirectory "$PWD\generated\installer\stages\candidate-01"
```

`build-stage.ps1` is the current single build/publish/validate command; it creates
unsigned workspace outputs and an explicit payload, not an installer. Regular
incremental CMake or WPF builds stamp versions but do not run installer validation
or broader correctness/lifecycle checks. A release additionally requires those
checks, completed production transaction/uninstall, authorized signing and live VM
validation. A future reviewed release command can extend this stage into Inno
compilation/signing after gates are legitimately removed; no such operation is
implemented or enabled now.

Native resource changes alter unsigned DLL/SIP hashes. Existing staging regenerates
signing/registration policies from these new outputs. Existing signed/deployed
engines are left intact; they must not be mistaken for rebuilt versioned outputs.
Old stages stay historical and are rejected as current candidates if their version
metadata/source fingerprint or binary resources do not match the current source.

## Consistency and future comparisons

MSBuild validates source values before assembly generation and rejects divergent
version/identity properties, including overrides. CMake and PowerShell enforce
the same representation/bounds. Packaging reads actual PE numeric and string
versions for all eight production/bootstrap images and the WPF assembly identity
without loading application code. Missing resources, stale native/frontend files,
wrong product/copyright, stale central-source hashes, changed installer macros,
different AppId/filename, or mutually consistent but wrong build/package release
values fail stage validation. Existing payload, signature-input, asset, icon,
dependency and legal integrity checks remain in place.

`Compare-OutLoudProductVersion` parses bounded versions into `System.Version`
and compares numerically: `0.10.0 > 0.9.0`. The stage has explicit semantic and
four-part values ready for later installed-version comparison. This helper does
not detect an installed release, prevent downgrades, replace a registration or
implement upgrade/rollback. Installation receipts and policy for those operations
must be completed in their later tasks.

## Task files

New: `ProductVersion.props`, `cmake/OutLoudVersion.cmake`,
`cmake/ProductVersion.rc.in`, `installer/inno/version-common.ps1`,
`installer/inno/test-version.ps1`, this guide.

Modified: root `CMakeLists.txt`, WPF `OutLoud.VoiceManager.csproj`,
`installer/native/CMakeLists.txt`, `installer/inno/OutLoud.iss`,
`build-stage.ps1`, `production-common.ps1`, `test-stage.ps1`, `verify-stage.ps1`,
root `README.md` and installer `README.md`. No runtime implementation, voice,
registry, certificate, licensing or asset changes.

## Executed A03.8 validation

Validated on 2026-10-10. At entry, root CMake independently specified `0.1.0`,
staging separately supplied WPF's version, and ordinary WPF builds used SDK
defaults. The previous staged native engine had no VERSIONINFO resource. These
independent definitions are now replaced by the shared source above.

The final unsigned workspace stage is `generated/installer/stages/a038-02`:
23 allowlisted payload files, with versioned installer input outside the installed
payload. Its eight production/bootstrap PE images have matching product/file
metadata; WPF assembly identity is `OutLoud.VoiceManager, Version=0.1.0.0`.
Production native Release x64, installer helper/bootstrap Release x64 and WPF
Release x64 publication succeeded. The two additional native CLI targets also
built successfully and their version resources passed inspection. An ordinary
incremental WPF Release x64 build, without a version argument, succeeded with
**0 warnings / 0 errors**. Final native logs contain no compiler/linker warnings
or errors.

| Focused check executed on final sources | Result |
| --- | --- |
| `test-version.ps1 -StageDirectory .../a038-02` | **78 checks PASS**: parsing/bounds, numeric comparison, CMake/MSBuild propagation in private fixtures, override rejection, actual PE/.NET metadata and installer identity/input. |
| `test-stage.ps1 -StageDirectory .../a038-02` | **53 checks PASS**, including seven new version/source/installer metadata tampering cases and the existing payload integrity cases. |
| `test-signing.ps1 -StageDirectory .../a038-02` | **27 integration checks PASS**; read-only signature/dependency inspection and gated-operation checks; no certificate/key creation or signing. |
| `test-registration.ps1 -StageDirectory .../a038-02 -ExpectedInspection migration_required` | **27 integration checks PASS**; read-only installed-registration conflict detection and closed lifecycle gates; no registry mutation. |
| `verify-stage.ps1 -StageDirectory .../a038-02` | PASS: product/file version, AppId, planned installer filename, metadata/source hashes and existing payload policy. |
| Additional resource/export inspection | PASS: both extra native CLIs and ordinary WPF DLL/apphost metadata; SAPI's two exports and bridge's ten exports match the previous A03.7 stage. |
| Script parsing and Git whitespace | All 15 installer PowerShell scripts parse; `git diff --check` passes. |

Parser/propagation tests use temporary private metadata fixtures; staging tamper
tests operate on private copies. No full correctness suite, synthesis campaign,
benchmark or profiler was run. Native policy unit-test executables were not rerun;
their implementation is unchanged. Initial focused checks passed before the final
shared component bound/override checks; only the affected version/staging checks
were repeated afterward.

Read-only before/after snapshots compare equal: **91 registry records**, **136
certificate-store entries**, and **30 installed/library files** including their
names, sizes and SHA-256 values. The installed engine remains hash
`e1c7115d42200f3f804bbcb5cd6e583cc15a51f895ce4d70e027d04e2956fd86`.
No installed binary, registry/voice registration, certificate/key/trust store,
voice library, runtime installation, speech setting or game process was changed.
Nothing was staged in Git, committed or pushed. Logs and snapshots are ignored
under `generated/installer/a038/`.

The ordinary build executable is:

```text
C:\Users\rosko\Desktop\Nendis\application\Out Loud TTS\generated\installer\a038\wpf\Release\OutLoud.VoiceManager.exe
```

Inno configuration/generated input was checked, but **Inno was not compiled**:
the compiler is unavailable locally and the unconditional compiler gate remains
intentional. Windows Apps & Features output, installed-version comparisons and
real lifecycle operations remain unverified until installer completion and live
validation. No installer, installation or release was produced. The future
filename and display version are prepared, not observed on an installed product.
