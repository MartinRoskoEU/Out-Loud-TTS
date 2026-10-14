# A03.7: production SAPI engine registration

**Current status:** [A03.12](BUILD.md) supersedes earlier foundation/compiler-gate
statements below. Shared native maintenance/uninstall and Inno lifecycle are
integrated for a controlled-test build. Standalone legacy signing/registration
commands remain blocked. Historical phase evidence below does not establish
live qualification; A03.13 functional testing is still required. Path budgeting
now uses the complete vault layout, rather than the former 100-character root cap.


**Current policy:** registration uses the selected installation directory and
requires a verified installation/certificate binding. Unbound registrations are
generic conflicts; there is no deployment detection or migration state. See
[INSTALLATION_PATHS.md](INSTALLATION_PATHS.md) for the current implementation.
The typed-tree serializer remains shared for production inspection/recovery.
The historical execution results below describe A03.7, not a newly enabled
installation or current lifecycle validation.

Implemented 2026-10-10. **Staging only, not installable or distributable.** The
unconditional Inno compiler error, InitializeSetup refusal, false foundation
lifecycle gate and separate false native registration gate remain. There is no
runtime override. No actual registry/certificate/trust operation is authorized
or performed by this validation. No development migration or uninstall is added.

## Registration and order

The existing static-CRT `OutLoudInstallSigning.exe` also implements registration;
there is no extra installer service, backend ABI, or engine export. It shares the
engine's fixed CLSID from `sapi/src/identity.hpp`. It reuses the manual checked
registration's class description/owner/ThreadingModel and refusal of another
deployment or current-user override. Writes use the existing managed registry
implementation's transactional compare-before-write principle, not the manual
PowerShell script's destructive tree restoration. Neither manual script is run.

The future gated order is:

1. Windows 11 native AMD64, interactive license/policy acceptance, read-only COM
   conflict inspection before runtime acquisition or local certificate creation.
2. Verified compatible Microsoft runtimes, payload copying, A03.6 certificate and
   engine signing, exact signer/code/signature and vendor dependency checks.
3. Recheck consent/runtimes and conflicts; native registration independently pins
   and validates the existing installation certificate, machine key/trust, exact
   signed engine hash and SIP digest. No certificate creation/signing occurs here.
4. Pin and hash every payload file from the package allowlist compiled by CMake
   from `package.json`. Canonical registration metadata binds the full payload,
   detector and requirements; stage verification rejects changed inputs even if
   their manifest hash is refreshed. The signed engine uses its unchanged SIP
   digest plus exact signed hash from protected `configuration/signing.ini`.
5. Rerun the hash-pinned static detector with only its read-only requirements/
   runtimeconfig/report command. Require actual compatible Windows, VC and .NET
   results, exit zero and a fresh report. It cannot invoke runtime installation
   through this path. An error/timeout stops registration.
6. Capture exact typed registry state, refuse conflicts, flush recovery state,
   create the fresh class in one checked registry transaction, verify the complete
   tree inside that transaction, then commit. No nontransactional fallback exists.
7. Read-only post-verification rechecks hive/view/fixed identity, strict values,
   overrides, pinned physical server identity, certificate and all required
   signatures. Only then publish the registration receipt and verified report.
   Inno requires that report and its installation/certificate/signed-file binding.

Consent is established by the interactive Inno wizard; the elevated native helper
accepts only its explicit `--consented` contract. Packaged policy inputs are trusted
installer inputs, not arbitrary public metadata or an authorization mechanism.
Publisher signing of the final installer remains later release work.

## Registry contract and ownership

Only this class is written, explicitly in **HKLM Registry64**:

```text
SOFTWARE\Classes\CLSID\{F38C3398-3552-43B9-8350-7DD301327D70}
    (Default)                  REG_SZ  Out Loud Pocket SAPI Engine
    OutLoudPocketOwner          REG_SZ  OutLoudPocketSAPI-2M
    OutLoudInstallationId       REG_SZ  <32 lowercase hex, signing ownership ID>
    OutLoudCertificateSHA256    REG_SZ  <64 lowercase hex, exact certificate DER>
    InprocServer32\(Default)   REG_SZ  <resolved installation directory>\bin\OutLoudPocketSAPI.dll
    InprocServer32\ThreadingModel REG_SZ Both
```

Class keys inherit the machine CLSID parent's security. No registry parent,
HKCU, Registry32, Speech token, library, COM self-registration or other class is
written. No `regsvr32`, example voice or development token creation. Future real
voices continue through the existing Voice Manager import/registration workflow.

An already-owned class must have the exact installation/certificate identity,
description, owner, strict REG_SZ fields, sole server child and ThreadingModel.
Its server must reside in the expected installed bin directory and resolve to the
pinned installed file's volume/FileId. Successful repeat registration performs
zero registry writes. Early inspection's `owned_candidate` is not verification
and cannot authorize a write; final checks still apply.

A class without installation/certificate fields returns **`conflict`**, even if
its CLSID, marker or server path matches. It is never accepted as authority to
replace the class or rebind tokens. Wrong owner/identity, foreign paths, malformed types,
extra fields/children, missing server, registry symbolic links or access failure
are refused. A raced class creation is not overwritten. The current user's
Registry64/32 overrides and machine Registry32 class are inspected and refused.
Windows' standard HKCU Classes link is handled read-only by verifying its exact
`\Registry\User\<current-token-SID>_Classes` target and opening that physical
class hive directly. Arbitrary links and linked class/server keys remain refused;
machine write ancestors never use this exception.
Unloaded/other users' private registry hives are not loaded or comprehensively
audited; a broader per-user override inventory is outside this registration operation.

## Failure state and recovery boundary

Registry snapshots retain existence, names, raw value types/bytes and child trees
with strict depth/node/data bounds. Their `utf16le-typed-tree-v1` encoding is length
framed, sorted and lossless, without environment expansion or path rewriting.
The read-only `--registration-inspect <app> <new-report>` exposes classification
and exact before-state. Inspection never creates a class or overwrites a report.

Before fresh mutation, protected, flushed
`configuration/registration.pending.ini` records exact before/desired state,
scope, CLSID and installation/certificate identity. All six value writes are
inside KTM/Reg*Transacted; write, access, comparison or commit failure rolls back
that registry transaction. It leaves the journal for checked recovery rather
than silently retrying. Transactions unavailable means failure.

Post-commit verification or receipt failure leaves the committed registration and
pending state; **it does not delete or blindly restore a live class**. Only full
verification renames the journal to `configuration/registration.ini`. Existing
pending state blocks another registration. The receipt retains its before-write
phase as a record of the captured transaction intent; its publication follows
verification. The surrounding installer transaction is not implemented yet.

Later recovery must authenticate the installation, compare the actual current
class with the captured expected owned state and decide completion/restoration
under a checked transaction. Never delete voice tokens to resolve a conflict.
Loaded/locked-file handling, crash/power-loss recovery,
upgrade integration (including A03.6 previous signing journals), and uninstall
remain prerequisites to opening any gate. An existing development engine is
left completely intact by A03.7.

## Validation

Controlled native tests exercise the production registration controller with an
in-memory registry, six partial-write faults, commit/post-verification/receipt
failures, a concurrent foreign registration, access errors, malformed data,
missing Windows/runtimes/consent/payload/engine/certificate/signatures, wrong path/
physical identity, owned repeat, development/third-party conflicts, overrides,
exact before-state and preservation of voices/unrelated registry data.

```powershell
cmake --build build-installer-prerequisites --config Release --parallel --target OutLoudInstallSigning outloud-install-registration-tests outloud-install-signing-tests outloud-prerequisite-tests outloud-runtime-install-tests
.\build-installer-prerequisites\Release\outloud-install-registration-tests.exe
.\installer\inno\test-registration.ps1 -StageDirectory <new-verified-stage>
```

The integration check calls only the read-only inspector and the gated registration
command, which refuses before argument parsing or any registry access. Existing
stage, prerequisite, runtime-acquisition, consent and signing checks remain required.
No Microsoft installer, live registry mutation or certificate operation is used.
The helper still has only Windows SDK dependencies and a static CRT; KTM is an
existing Windows system dependency already used by managed token transactions.
No new third-party licensing obligation is introduced; notices remain unchanged.

Actual privileged registration, signed machine certificate verification in a fresh
VM, real KTM fault behavior and Inno wizard compilation/execution remain unverified.
The intentionally blocked Inno source is not an installable build. All live tests
require a later completed production transaction design and explicit authorization.

## Executed A03.7 results

Release x64 installer-native targets and production/WPF staging succeeded with
no compiler/linker warning or error. Final workspace stage is
`generated/installer/stages/a037-02`; all 23 payload files remain explicit and no
voice is included. Against A03.6 `a036-02`, **22 payload files have identical
sizes/hashes**; only `bin/OutLoudInstallSigning.exe` changed. Registration policy
is additional bootstrap metadata outside the installed payload.

| Final check | Result |
| --- | --- |
| Native registration controller and standard user class-hive link policy | 295 assertions PASS; in-memory registry only |
| Registration integration, direct gate and actual read-only inspection | 27 checks PASS |
| Existing A03.6 signing controller | 124 assertions PASS |
| Existing prerequisite facts | 34 assertions PASS |
| Existing runtime acquisition/controller | 47 assertions PASS |
| Stage tamper/inventory checks, including two new registration-input cases | 46 checks PASS |
| Prerequisite integration | 27 checks PASS |
| Runtime acquisition integration with previously cached pinned Microsoft packages | 30 checks PASS; no downloads or runtime execution |
| Mandatory license/policy consent checks | 55 checks PASS |
| Signing integration, actual vendor closure, valid installed reference and damaged scratch copy | 30 checks PASS; no signing |
| Installer PowerShell parsing and Git whitespace | PASS |

The initial metadata generator failed on PowerShell's property sorting of ordered
dictionaries; the explicit path expression now handles both dictionaries and
parsed manifest objects. The initial override guard rejected Windows' standard
HKCU Classes REG_LINK. A read-only SDK API diagnostic established its actual
type/target; the exact-current-SID physical-hive handling and controlled rejection
tests corrected that defect. Both failed attempts are superseded by the final
passing checks; no live mutation occurred in either attempt.

The native inspector reported **`migration_required`** for the unchanged existing
development class and captured its complete four raw REG_SZ values. It did not
load the engine, activate COM, create a key or change a token. Direct `--register`
was refused by the hard native gate before parsing or registry operations.

Read-only before/after snapshots of speech tokens and this COM class in HKLM/HKCU
Registry64/32 are byte-identical. Inventories of **30 installed/development/library
files** are identical, including their hashes/sizes; inventories of **136
certificate entries** in CurrentUser/LocalMachine My/Root/TrustedPublisher are
identical, including private-key presence. No trust, signing, registration,
installed DLL, model/tokenizer, user voice, runtime, game or speech-setting action
occurred. Evidence/logs are under ignored `generated/installer/a037/`.

The Inno compiler is unavailable in this environment; its deliberately blocked
source was not compiled or executed. Inno integration is validated by controlled
source/metadata checks, not a successful wizard run. This is a live-validation
limit, not a reason to remove the compiler or execution gates. No required
controlled check remains failing. Whole-install recovery remains an intentional
release blocker; development migration is now explicitly unsupported.
Nothing was staged, committed or pushed.

## Task file changes

All pre-existing A03.2-A03.6 work is preserved. The seven new files and ten
modified files for this task are:

| Area | New files | Modified files |
| --- | --- | --- |
| Installer-native | `installer/native/registration_policy.hpp`, `registration.inc`, `registration_tests.cpp` | `installer/native/signing.cpp` (read-only verification option and command dispatch), `CMakeLists.txt` (test target and package-derived payload header) |
| Inno/staging | `installer/inno/registration-common.ps1`, `registration.iss`, `test-registration.ps1` | `installer/inno/OutLoud.iss`, `production-common.ps1`, `build-stage.ps1`, `test-stage.ps1`, `test-prerequisites.ps1`, `test-signing.ps1` |
| Documentation | `installer/inno/REGISTRATION.md` | `README.md`, `installer/inno/README.md` |

The generated payload header and stage/reports/logs remain ignored build artifacts.
There are no engine/backend/frontend source, ABI, protocol, voice-management,
licensing material or project-format changes.
