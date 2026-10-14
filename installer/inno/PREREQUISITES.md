# A03.3–A03.4: Windows and Microsoft runtime prerequisites

**Current status:** [A03.12](BUILD.md) supersedes earlier foundation/compiler-gate
statements below. Shared native maintenance/uninstall and Inno lifecycle are
integrated for a controlled-test build. Standalone legacy signing/registration
commands remain blocked. Historical phase evidence below does not establish
live qualification; A03.13 functional testing is still required. Path budgeting
now uses the complete vault layout, rather than the former 100-character root cap.


Implemented 2026-10-10. Only **Windows 11 native x64 client** is supported.
Detection is read-only apart from an explicitly requested new diagnostic report.
A03.4 implements consent, acquisition, verification and prerequisite installation
behind the foundation gates. No actual installer was executed during development
or verification. The compiler, InitializeSetup and prerequisite execution gates
remain unconditional; this project is not distributable.

## Windows compatibility and Inno integration

[OutLoud.iss](OutLoud.iss) sets `MinVersion=10.0.22000` and
`ArchitecturesAllowed=x64os`, with 64-bit installation mode. The included
[prerequisites.iss](prerequisites.iss) uses supported Inno APIs:
[GetWindowsVersionEx](https://jrsoftware.org/ishelp/topic_isxfunc_getwindowsversionex.htm)
and [ProcessorArchitecture](https://jrsoftware.org/ishelp/topic_isxfunc_processorarchitecture.htm).
It requires NT 10.0, build >= 22000, `VER_NT_WORKSTATION` and `paX64`.
Windows 10, Server/domain controllers, x86 and native ARM64 (even with x64
emulation) receive a clear refusal. Unknown or failed detection never passes.

This numeric rule identifies Windows 11 under its NT 10.0 identity; marketing
strings are not used. Microsoft servicing eligibility is a separate release
support policy, not inferred from the build floor. A future different NT major
requires an explicit reviewed policy update.

The Inno helper extracts only the probe, requirements INI and published
runtimeconfig into its temporary directory, executes the probe hidden and checks
exit status, schema and all three compatibility results. Reports are never reused.
The four `dontcopy` entries (including package metadata) do not install files into the application directory.
Failure is visible and prevents progress. Even when all prerequisites pass,
`InitializeSetup` still returns false; the unconditional compiler `#error` also
remains. Inno Setup is not installed here and this gated project was not compiled.
Source-contract tests do not establish actual Inno UI behavior on other systems.

## Bootstrap implementation and dependencies

[installer/native](../native/CMakeLists.txt) builds an AMD64, C++20, `asInvoker`
probe with MSVC's static Release CRT (`/MT`). Its verified imports are Windows
system DLLs only, so it can run before VC++ or .NET prerequisites exist. A03.4 adds
Windows BCrypt, WinTrust and Crypt32 imports for hashing/signature verification. It does
not reuse the privileged mutation helper, run Voice Manager or require an SDK on
the user's system. The installed SDK's reviewed public `hostfxr.h` is a build-time
header only; no SDK/header/vendor runtime is copied into the stage.

The application payload remains the existing explicit 20 files. New bootstrap
and requirements metadata sit outside `payload/`, with their hashes in the
manifest. Release-pinned package JSON generates a canonical additional runtime-packages
INI outside the payload; both policies are hashed and checked during staging.
Stage validation checks those hashes, AMD64 and OS-only probe imports.
No user-created voice file is harvested. The system probe opens registry keys
with read-only access, checks local paths/ancestors without following reparse
points and never changes runtime installation metadata.

## Visual C++ x64 detection and version floor

The probe reads `HKLM\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64`
explicitly through Registry64 and Registry32. `Installed=1`, numeric DWORD
`Major/Minor/Bld/Rbld` and the REG_SZ `Version` must agree for each discovered
installation. It selects the highest registered x64 v14 version; the two views
can legitimately retain different servicing records. Malformed/inaccessible
metadata is a detection failure. An x86/ARM registration alone does not satisfy
x64 requirements.

It additionally verifies AMD64 PE headers and minimum file versions of System32
`msvcp140.dll`, `msvcp140_atomic_wait.dll`, `vcruntime140.dll` and
`vcruntime140_1.dll`. Arbitrary DLL presence without installation metadata never
reports compatibility. This is an installation/version check, not a complete
cryptographic audit of the installed Microsoft package.

[prerequisites-common.ps1](prerequisites-common.ps1) derives the floor from the
actual CMake-selected x64 compiler, its matching VS redistributable pack and the
maximum of those four required runtime file versions. Current verified inputs:

| Input | Version |
| --- | --- |
| Selected MSVC compiler | 19.44.35229.0 |
| MSVC tools directory | 14.44.35207 |
| Matching redistributable pack directory | 14.44.35112 |
| Required VC v14 x64 runtime floor | **14.44.35211.0** |

The directory's label is not the runtime files' version. A compiler version in
the 19.x numbering scheme is not itself a VC runtime version. Rebuilding with a
different toolchain recomputes the recorded floor; an unidentified/mismatched
toolchain fails staging instead of keeping a guessed minimum. Newer compatible
v14 x64 packages are accepted. A different major family is not assumed compatible.

## .NET 10 Desktop x64 detection

The published framework-dependent application requests **Microsoft.WindowsDesktop.App
10.0.0** and **Microsoft.NETCore.App 10.0.0**. Its runtimeconfig currently omits
`rollForward`, giving the default Minor policy with patch servicing. The apphost
pack pin **10.0.12** describes the executable stub, not an invented application
framework minimum. In practice the selected Desktop runtime can require a newer
matching Core patch through its own runtimeconfig.

The probe follows x64 apphost root precedence: `DOTNET_ROOT_X64`, `DOTNET_ROOT`,
the x64 `InstallLocation` in the Registry32 .NET setup location, then the default
native Program Files dotnet directory. It does not search PATH or invoke the
SDK's `dotnet` command. As in apphost, a nonexistent environment directory is
skipped; an existing selected directory with an invalid installation fails rather
than borrowing frameworks from another root. It checks the host's PE architecture and
separately inventories stable Desktop and Core frameworks. Standard Core alone,
.NET Framework or an SDK directory alone cannot satisfy Desktop. A real SDK
installation that also supplies compatible Desktop/Core frameworks can pass
because those frameworks exist, not because the SDK exists.

The installed hostfxr resolves the **actual published runtimeconfig** via
`hostfxr_resolve_frameworks_for_runtime_config`, including roll-forward and
transitive framework compatibility. Resolved versions/paths are copied during
the callback, and required native Desktop/Core images are checked for AMD64.
The probe does not initialize the CLR; it also checks that coreclr was not loaded.
Unresolved framework policy, malformed configs, missing framework files and
inaccessible roots never report compatible. Preview-only directories are not
accepted as stable production runtimes. Host API reference:
[reviewed .NET 10 hostfxr header](https://github.com/dotnet/runtime/blob/v10.0.12/src/native/corehost/hostfxr.h).
See Microsoft's [version selection](https://learn.microsoft.com/en-us/dotnet/core/versions/selection)
and [app launch/runtime discovery](https://learn.microsoft.com/en-us/dotnet/core/runtime-discovery/troubleshoot-app-launch).

## Structured diagnostics

`OutLoudPrerequisites --requirements <absolute.ini> --runtime-config
<absolute.runtimeconfig.json> [--report <new-absolute.ini>]` writes INI-format
results to stdout. Optional report output is UTF-16 with BOM, using `CREATE_NEW`;
an existing file is never overwritten. Windows, VC and Desktop have separate
status/detail fields; installed VC/Desktop versions and hostfxr result are recorded.

| Status | Meaning |
| --- | --- |
| `compatible` | Required architecture and version/policy checks passed |
| `missing` | Required package/framework absent |
| `version_too_old` | Installed runtime below the required family/version |
| `incorrect_architecture` | Selected installation is x86/ARM instead of x64 |
| `incompatible_version` | Present runtime cannot meet framework/family policy |
| `detection_failed` | Identity, access, metadata, files or resolution cannot be trusted |
| `unsupported` | Windows edition/build/native architecture is outside support |

Exit codes: 0 all compatible, 1 absent/outdated/wrong-architecture/incompatible
runtime, 2 detection/report failure, 3 unsupported Windows. Invalid command or
requirements input returns 2 with a diagnostic; if no valid report is produced,
Inno treats that as detection failure rather than interpreting stale output.

## Official acquisition and release pins

| Prerequisite | Official source | Selection |
| --- | --- | --- |
| VC v14 Redistributable x64 | [Microsoft supported download page](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170), linked [x64 package](https://aka.ms/vc14/vc_redist.x64.exe) | Reviewed supported v14 x64 release >= the recorded floor; accept installed newer v14 |
| .NET 10 Desktop Runtime x64 | [Microsoft .NET 10 download page](https://dotnet.microsoft.com/en-us/download/dotnet/10.0), Desktop Runtime / Windows / x64 Installer | Reviewed stable 10.0 servicing release satisfying the published config and its Core dependency; not SDK/Core-only/ARM64/x86 |

The implemented [runtime-packages.json](runtime-packages.json) pins:

| Package | Version / bytes | SHA-256 |
| --- | --- | --- |
| VC x64 | 14.51.36247.0 / 18,731,856 | `843068991daaa1f73ad9f6239bce4d0f6a07a51f18c37ea2a867e9beca71295c` |
| .NET Desktop x64 | 10.0.12 / 60,032,984 | `55a67d8476cde95a9cc43a95803b4f54446e7c9251ed22aa6421d4908174ae84` |

Workspace-only copies were downloaded from the exact Microsoft URLs, read and
verified, never executed. VC discovery followed the official permalink once to
its content-addressed download.visualstudio.microsoft.com URL; the mutable
permalink is not the runtime acquisition URL. .NET uses a version-specific
builds.dotnet.microsoft.com Desktop x64 URL; its bytes match the published
SHA-512 in Microsoft's 10.0 release metadata. Both pass native signature,
publisher, file version and product verification. The .NET bundle's file version
is 10.0.12.50000, separate from its runtime's 10.0.12 version.

Both official x64 bundles have **x86 PE bootstrap executables**. Native verification
checks the pinned bootstrap machine, exact file/product versions and `(x64)`
product identity in addition to whole-file SHA-256 and Microsoft trust. It does
not mistake the bootstrap PE header for the installed framework architecture.
Post-install runtime detection still requires AMD64.

Use official version-specific Microsoft package URLs after selection. The VC
permalink changes over time and is a discovery source, not an immutable release
pin. Do not make an unreviewed latest package a reproducible build input. Record
exact package version, official URL, architecture, size and SHA-256 (and the
published .NET release-metadata checksum), retaining provenance with release
metadata. Review security servicing updates and refresh pins before release.
If an installed runtime already satisfies detection, avoid downgrade/reinstallation.

Before executing a downloaded package, the gated A03.4 workflow validates HTTPS acquisition,
Microsoft Authenticode publisher/trust/code-signing signature and timestamp,
exact approved content hash/size, architecture and version. A matching hash alone
does not authenticate a package; a Microsoft signature alone does not select the
approved version. Every failed/mismatched check blocks vendor execution.
Vendor packages are not re-signed and trust stores are not altered. The controller
captures vendor outcomes/reboot requirements and reruns the existing probe.

Microsoft's [VC redistribution requirements](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files?view=msvc-170)
restrict redistribution to appropriately licensed Visual Studio users and the
applicable license/REDIST list. Verify entitlement and selected package terms
before shipping it; Out Loud's MIT license does not grant Microsoft rights.
The .NET runtime has [MIT source licensing](https://github.com/dotnet/runtime/blob/v10.0.12/LICENSE.TXT),
but the exact Desktop installer, WindowsDesktop components and third-party notices
must be reviewed/preserved as distributed; the existing apphost notice alone
does not represent the complete installed runtime. Packages were acquired only
into ignored validation artifacts; no redistribution clearance is claimed by
this task. These package-specific legal checks
remain release work. Existing Nendis MIT, third-party, CC BY model/
tokenizer and Unicode materials are unchanged.

## Validation and limits

```powershell
cmake --build build-installer-prerequisites --config Release --target outloud-prerequisite-tests
.\build-installer-prerequisites\Release\outloud-prerequisite-tests.exe
cmake --build build-installer-prerequisites --config Release --target outloud-runtime-install-tests
.\build-installer-prerequisites\Release\outloud-runtime-install-tests.exe
.\installer\inno\verify-stage.ps1 -StageDirectory "$PWD\generated\installer\stages\candidate-01"
.\installer\inno\test-stage.ps1 -StageDirectory "$PWD\generated\installer\stages\candidate-01"
.\installer\inno\test-prerequisites.ps1 -StageDirectory "$PWD\generated\installer\stages\candidate-01"
.\installer\inno\test-runtime-acquisition.ps1 -StageDirectory "$PWD\generated\installer\stages\candidate-01"
```

The stage build must run first to configure the probe and establish its SDK header
path. A03.4 results: **34 native detection assertions**, **47 native acquisition
assertions**, **27 detection integration checks**, **30 acquisition integration
checks** (using existing local packages), and **30 staging checks** passed.
Without `-PackageDirectory`, acquisition integration runs controlled and unsigned-image
checks only, with no network requirement. Controlled tests cover every requested OS/runtime
state, Core/SDK-only roots, newer servicing, actual roll-forward Disable/LatestPatch,
invalid config, wrong-architecture PE inspection and non-overwritten reports.
Live read-only detection found Windows build **26300**, native AMD64 client,
VC **14.51.36247.0**, Desktop/Core **10.0.12**, all compatible.

Unsupported OS tests use controlled facts, not actual Windows 10/ARM/Server VMs.
Inno compilation/UI and built-in download callbacks, clean-machine runtime
installation, servicing repairs and actual reboot handling remain unverified.
Real package authenticity was verified read-only; installer execution, network
interruption and reboot outcomes use test doubles. There is no
new inference test suite or benchmark. Source changes are confined to root README
and installer infrastructure; application/native engine sources and current legal
materials are unchanged. Installed development/library files are checked by
before/after hashes; no runtime installation, registry mutation, certificate,
signing, deployment, application/process stop or Git staging/commit/push occurs.

Remaining blockers are live consent-wizard validation, installation-owned certificates/
signing, live production SAPI registration validation, locked-resource upgrade/repair/rollback
and confirmed uninstall, plus live VM prerequisite testing and package distribution
entitlement review. A03.5 adds the [product license and separate Kyutai policy
acknowledgment](CONSENT.md) before runtime consent, retaining all gates until
the reviewed lifecycle is complete. Signing/certificate work is the next stage.

## Gated wizard and native installation controller

[runtime-install.iss](runtime-install.iss) integrates with InitializeWizard,
ShouldSkipPage, PrepareToInstall and CancelButtonClick. It reuses the existing
probe and its `vc_action`/`dotnet_action` fields. Compatible runtimes are skipped;
missing, too-old and wrong-architecture runtimes can acquire x64 packages.
An incompatible .NET major can receive 10.0 side by side. Uncertain detection
and an unknown VC family block automatic repair/downgrade. Pascal does not
reimplement compatibility rules.

An unchecked checkbox explicitly authorizes the named shared Microsoft packages;
names, required versions and detected states appear on the prerequisite page.
Silent setup cannot silently grant consent. This is permission to install
prerequisites, separate from the preceding A03.5 license/policy page. Native redetection
skips execution if another process updates the runtime during acquisition.

Inno Setup **6.7.3+** is required, retaining the supported CreateDownloadPage /
DownloadTemporaryFile machinery. HTTPS URLs are restricted to the pinned Microsoft
endpoint families. The page shows progress, checks SHA-256, supports abort and
restores the prior page in a finally block. Packages stay in Inno's private
temporary directory. The reviewed [temporary-directory security](https://jrsoftware.org/is6help/topic_securitymeasures.htm)
uses unpredictable names and restricts unelevated writes to elevated local
temporary directories. Native local-path/reparse checks refuse unsafe source/
report paths. Pin updates require explicit review; there is no runtime latest
package selection or fallback mirror. Redirect/TLS/proxy behavior must also be
validated with the selected compiler in the later disposable-VM campaign.

Before execution the bootstrap rechecks exact size/SHA-256, WinVerifyTrust's
generic Authenticode policy with chain revocation, signer common name and
Microsoft organization, product/file version and bundle architecture. VC expects
CN Microsoft Corporation; Desktop expects CN .NET, both O Microsoft Corporation.
Invalid/unsigned/unexpected packages and inability to complete trust verification
are refused; revocation/network errors are not bypassed. Standard Windows trust
handles signature/timestamp validity. No SignTool, PowerShell, developer SDK or
external runtime is needed on the target system.

The same native read handle excludes writes and deletion while hashing, checking
trust/resources, creating the vendor process and waiting for completion. There
is no verified-file-close/execute interval. Both packages use absolute paths,
fixed `/install /passive /norestart /log`, administrator checks and an explicit
`--consented` controller invocation. Vendor UI provides installation progress.
Space preflight requires 512 MiB temporary and 1536 MiB system free space;
vendor checks remain authoritative and real disk-full errors still stop setup.

Production C++ `ensure_runtime` is instantiated with Windows operations and with
test doubles. Every vendor outcome reruns the original runtime detection; Pascal
then requests the complete probe again. Zero vendor exit alone is insufficient.
Success requires compatible detection; 1638 (another/newer installation) is
accepted only after compatible detection. All other unexpected errors fail.
Execution/acquisition/hash/trust/architecture failures, malformed reports,
detection failure and cancellation prevent Out Loud installation. Status,
verification result, vendor exit and a vendor log location are reported.

3010 and 1641 always stop setup with a manual restart/rerun instruction, even
if the immediate probe passes. `/norestart` is always supplied; 1641 is treated
as an unexpected vendor reboot outcome. Out Loud calls no reboot API and
schedules no RunOnce. NeedsRestart stays false. Rerunning setup starts fresh
detection and consent, skipping compatible shared components; no mutable resume
receipt is trusted.

During an active vendor operation, cancellation waits for it to finish or be
cancelled in its own UI; it never kills a system installer. Out Loud stops
afterwards. Declining cancellation does not latch it. Normal success/error/
cancellation removes only the known package in the private temporary directory;
the controller pin closes first. Inno removes session files on exit. Forced
process/power interruption can leave temporary artifacts; no separate crash
recovery service is added.

VC++ and .NET are **shared system components**. Successful prerequisite installation
may persist after a later prerequisite or Out Loud failure. There is no rollback
or uninstall of these packages; Out Loud uninstall must never add one. This
does not change the confirmed policy for Out Loud-owned voices/data.
THIRD_PARTY_NOTICES was reviewed and retained: it already distinguishes system
runtime prerequisites and separate terms from the apphost/project MIT grant.

Changed A03.4 files: root README; installer/inno README, PREREQUISITES, OutLoud.iss,
prerequisites.iss, prerequisites-common.ps1, build-stage.ps1, production-common.ps1,
verify-stage.ps1, package.json and existing stage/detection tests. Added
runtime-packages.json, runtime-install.iss and test-runtime-acquisition.ps1;
native probe.cpp/CMakeLists updated, with new packages.inc, runtime_install.hpp
and runtime_install_tests.cpp. No application payload/source or existing legal
document changed. Evidence and read-only package copies are ignored under
generated/installer/a034 and excluded from every application stage.
