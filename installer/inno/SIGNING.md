# A03.6: machine-wide local engine signing

**Current status:** [A03.12](BUILD.md) supersedes earlier foundation/compiler-gate
statements below. Shared native maintenance/uninstall and Inno lifecycle are
integrated for a controlled-test build. Standalone legacy signing/registration
commands remain blocked. Historical phase evidence below does not establish
live qualification; A03.13 functional testing is still required. Path budgeting
now uses the complete vault layout, rather than the former 100-character root cap.


Implemented 2026-10-10. All installer compilation, execution and native mutation
gates remain closed. This is implemented foundation code, not an installable
release. No certificate/key generation, trust change or real signing was executed.

## Compatibility reference and trust scope

The existing development scripts use SHA-256 Authenticode, verify with Windows
trust policy and require the expected signer. Read-only inspection confirmed the
working self-signed development certificate in CurrentUser My and Root, RSA 3072,
RSA-SHA256, Digital Signature and Code Signing EKU. The user confirms that this
setup works with WoW: Forever and that a public CA is unnecessary. No WoW files
were inspected and no game was launched or stopped.

Production preserves that local My/Root arrangement in **LocalMachine**, consistent
with machine-wide installation and HKLM64 SAPI. My holds the certificate's machine
key association; Root receives only the public certificate. No CurrentUser,
TrustedPublisher, TrustedPeople, additional CA, developer key or policy bypass is
added. A different authorized elevated administrator can maintain this installation.

Root's necessity has not been isolated experimentally. Preserving the user's
working configuration is the selected compatibility policy, not proof that a
smaller trust configuration would fail. Removing Root trust requires separately
authorized compatibility testing. This certificate establishes local code-signing
trust for all users, **not public publisher identity or SmartScreen reputation**.
The code-signing-only EKU, Digital Signature-only usage and non-CA basic constraint
limit the generated certificate's intended purposes; adding a local trust anchor
still allows code signed by that key to be trusted on the machine. Administrators
and SYSTEM must therefore remain the only key authorities.

## Certificate and key lifecycle

`OutLoudInstallSigning.exe` uses Windows CNG's Microsoft Software Key Storage
Provider. Fresh installation creates an OS-random 128-bit installation ID and
`OutLoud.Install.<id>` persisted **machine** RSA 3072 key. Export policy is zero,
key usage is signing only. The protected key descriptor sets Administrators as
owner and allows only Administrators and SYSTEM; it is checked again before use.
UAC-filtered administrators and ordinary users receive no key grant. There is
no private-key export, PFX, key file in the payload or secret diagnostic output.

The self-signed certificate has a unique `CN=Out Loud Local Signing <id>`, explicit
RSA-SHA256, Code Signing EKU `1.3.6.1.5.5.7.3.3`, Digital Signature usage,
non-CA basic constraint and five-year validity with leap-day handling. A fresh
installation does not search for or reuse a development certificate. Windows
APIs generate it; customer machines need neither SDK nor Visual Studio.

Owned state is under `<resolved installation directory>\configuration`. Administrators and
SYSTEM can write; users can read public ownership metadata. The native helper
requires the selected installation root, elevated machine authority, protected
ancestors/files, no reparse points and an exclusively write-pinned one-link engine.
Interactive user identity never determines certificate scope.

`signing.ini` records product/schema, LocalMachine My/Root, KSP, random installation
ID, authority SID `S-1-5-32-544` (Administrators, not the installing user's SID),
key name, SHA-1 certificate locator, full certificate DER SHA-256 and final signed
file SHA-256. SHA-1 is only a Windows store lookup; SHA-256 pins the actual identity.
Upgrade opens precisely these owned objects, checks all properties, machine chain,
key association/access/ACL and validity, then reuses the original key/certificate.
Missing/expired/wrong/inaccessible objects fail; there is no subject-name fallback,
new certificate on ordinary upgrade or automatic renewal.

## Exact signing inventory and dependency verification

| Component | Policy |
| --- | --- |
| `bin/OutLoudPocketSAPI.dll` | Only project DLL loaded by the native TTS engine; locally sign with the installation-owned certificate. |
| `msvcp140.dll`, `msvcp140_atomic_wait.dll`, `vcruntime140.dll`, `vcruntime140_1.dll` | Shared system-installed VC runtimes; require valid Microsoft signatures and preserve every byte. |
| Windows COM/Shell/BCrypt/Kernel/UCRT and transitive OS components | Require valid embedded or Windows catalog signatures; preserve OS files. |
| Voice Manager managed DLL, native bridge, import helper, CLI and signing helper | Not engine DLL dependencies; no new local signature is applied by this stage. |

Project inference libraries are statically linked. Source inspection found no
dynamic third-party engine loading. Engine PE imports and recursive regular and
delay-import closure are checked by the native helper, with bounded PE parsing.
API-set aliases are resolved by Windows using `DONT_RESOLVE_DLL_REFERENCES` and
System32 search: no DLL entry points, imports or synthesis execute. Resolved vendor
images must be in System32 or Windows WinSxS and have valid Microsoft signatures.
OS hard links are accepted; the project signing target retains the stricter
one-link policy. Catalog verification checks actual file membership and signature,
not merely the existence of a `.cat` file. Missing required imports fail. Missing
optional delay imports are recorded; installed optional components are verified.

The read-only inventory on this Windows machine resolved **367 vendor images**,
all valid, including the four VC runtimes. This deliberately conservative graph
includes optional Windows components and one OS executable used as an import
provider; it is not a count of DLLs actively loaded during synthesis. No executable
was launched. Other Windows versions can have a different closure. Host/game
plugins, arbitrary COM activation and every possible runtime-discovered module
are outside a PE import graph; no WoW module inventory was taken. Future external
engine DLLs must receive an explicit reviewed inventory/signature policy, rather
than automatically acquiring a local signature.

The installed development engine signature is still valid. The staged production
engine remains intentionally unsigned; it is signed only during eventual authorized
installation. No vendor file is signed by the local certificate.

## Signing, verification and installer sequence

OS `Mssign32!SignerSignEx2` is loaded only from System32, with SHA-256 and the exact
owned certificate/key. No external unsigned utility, SignTool or SDK is payload.
The supported Microsoft API structures are declared privately because the SDK
does not provide their header/import library; x64 layout assertions guard them.
No new timestamp is added, matching the working development signature. A public
release publisher/timestamp policy remains separate work.

The helper requires the staged unsigned file hash and SHA-256 SIP code digest.
After signing, it requires successful Windows Authenticode verification, exact
certificate locator **and DER SHA-256**, unchanged SIP digest and complete valid
vendor closure. It then records the signed container hash. A signature's mere
presence never succeeds. Reusing an already owned signed upgrade target also
requires its stored signed hash. Machine certificate-chain policy explicitly uses
`HCCE_LOCAL_MACHINE`; no unknown signer or revocation failure is waived. URL
retrieval is cache-only, and explicit machine-chain construction disables automatic
root updates. Verification does not silently acquire new trust. A missing cached
chain/revocation requirement fails closed and needs live VM policy validation.

Future sequence: validate payload and consent -> verify/install compatible shared
runtimes -> verify dependencies -> generate/reuse owned certificate -> configure
local trust -> sign -> verify every required signature -> checked SAPI registration
-> commit installation. The Inno wiring calls signing after normal file copying
and has an explicit `SigningVerified` guard. Production registration is implemented
under its separate closed gate. Before
enabling any gate, a later transaction must secure old files, handle locked users,
coordinate production registration and restore/recover interrupted work.

Three independent gates remain: unconditional Inno `#error`, unconditional setup
refusal, plus `FoundationLifecycleReady=False`; the native helper additionally
has `signing_lifecycle_ready=false` with no runtime override. Direct `--prepare`
therefore refuses before parsing or Windows mutation. Read-only `--identity`,
`--describe` and `--dependencies` remain available for controlled verification.

## Failure, rollback and complete uninstall

Flushed, protected `signing.pending.ini` checkpoints precede key, certificate,
store/trust, signing and verification effects. Atomic publication uses
`signing.next.ini`; successful upgrade retains `signing.previous.ini`. Pending,
next or previous residue requires recovery; it never causes a new-key fallback.
Failures return a fixed error and never publish verified completion. A failed
signature cannot reach registration. Journals contain only public identifiers.

Later transaction recovery must reconcile exact owned IDs/key namespace, cert
DER/hash and public trust copy; restore binary/registration backups; remove only
newly created owned key/certificate/trust objects when safe; and consume previous
state after the full install commits. An upgrade must preserve the original owned
objects on failure. The phase before certificate creation can identify an owned
key without having a certificate hash yet. Do not delete arbitrary matching
subjects or unrelated store entries. [A03.11 uninstall](UNINSTALL.md) now checks
all enumerable machine stores for container/public-key sharing and allows deletion
of an expired but exactly owned certificate. Actual rollback/uninstall/key deletion is
deliberately not implemented in A03.6. Journal handling currently fails closed,
so repeated maintenance awaits the later transaction layer.

Five-year expiry requires explicit maintenance/renewal design before expiration;
un-timestamped signatures must not be promised permanent validity. Live fresh/
upgrade signing, cross-user trust and unauthorized-key denial need disposable
Windows 11 VM validation before gates open. The live CNG/store/signer APIs were
compiled but never executed during this task.

## Controlled verification

```powershell
cmake --build build-installer-prerequisites --config Release --parallel --target OutLoudInstallSigning outloud-install-signing-tests
.\build-installer-prerequisites\Release\outloud-install-signing-tests.exe
.\installer\inno\build-stage.ps1 -StageDirectory "$PWD\generated\installer\stages\new-candidate"
.\installer\inno\test-signing.ps1 -StageDirectory "$PWD\generated\installer\stages\new-candidate"
```

Native controller tests use doubles for every mutating operation. Integration
checks inspect unsigned/signed inputs, test a damaged private copy, inventory
vendor signatures read-only, and prove direct mutation-gate refusal. Existing
staging, consent and prerequisite checks remain required. No real certificate,
trust, signing, registry, installation, production voice or runtime-installer
operation is part of these commands.

Final Release x64 native helper/test builds and production/WPF staging passed,
with no compiler/linker warnings or errors. Final stage is
`generated/installer/stages/a036-02`, status `foundation-not-installable`, with
23 payload files. All 22 original A03.5 payload files retain identical sizes and
SHA-256; only the new signing executable is added. Stage 01 and early inspection
outputs remain ignored evidence, rather than replacing previous artifacts.

| Final check | Result |
| --- | --- |
| Native signing controller, only operation doubles | 124 assertions PASS |
| Signing integration, read-only inventory and damaged private copy | 30 checks PASS |
| Existing staging regressions | 44 checks PASS |
| Existing consent regressions | 55 checks PASS |
| Existing prerequisite integration / fact doubles | 27 / 34 checks PASS |
| Existing acquisition integration / operation doubles | 30 / 47 checks PASS |
| Complete final stage verification | PASS, including seven AMD64 payload PE images and exact original icon/assets/licenses |
| Read-only conservative dependency graph | 367 vendor images, all signatures valid |

The controller cases include generation/trust/signing/verification failures,
machine-key/ACL properties, unique identity, wrong signer/code digest, unelevated
refusal, missing/wrong/expired/inaccessible certificate/key, interrupted signing,
different-administrator upgrade reuse and unrelated-object preservation. Native
API compilation and fake results do not prove live CNG creation/signing or actual
Inno wizard behavior. An initial source-encoding build issue and inventory handling
for Windows publisher names, optional imports and WinSxS were corrected before
the successful final runs. No production application tests or benchmarks ran.

Read-only before/after integrity checks confirmed all **30 installed/library
files** unchanged, including the installed engine SHA-256
`e1c7115d42200f3f804bbcb5cd6e583cc15a51f895ce4d70e027d04e2956fd86`.
All **136 entries** in CurrentUser/LocalMachine My, Root and TrustedPublisher
inventories retained identity/private-key presence. Typed speech/engine registry
snapshots across machine/user 64/32 views were identical around final verification.
No test invoked a real mutation helper, certificate generation/signing or runtime
installer. Evidence/logs are ignored under `generated/installer/a036`.

Files added: `installer/native/signing.cpp`, `signing_policy.hpp`, `signer_api.hpp`,
`signing_dependencies.inc`, `signing_tests.cpp`; `installer/inno/signing-policy.json`,
`signing-common.ps1`, `signing.iss`, `test-signing.ps1` and this guide. Integration
updates: native installer CMake; Inno `OutLoud.iss`, `build-stage.ps1`,
`production-common.ps1`, `package.json`, `runtime-install.iss`, `test-stage.ps1`,
`test-prerequisites.ps1`, installer README and root README. Existing development
signing scripts, application/engine source, voices, legal notices, certificates
and installed binaries were preserved. No staging/commit/push to Git occurred.

API references: [SignerSignEx2](https://learn.microsoft.com/en-us/windows/win32/seccrypto/signersignex2),
[NCryptSetProperty](https://learn.microsoft.com/en-us/windows/win32/api/ncrypt/nf-ncrypt-ncryptsetproperty),
[CertCreateSelfSignCertificate](https://learn.microsoft.com/en-us/windows/win32/api/wincrypt/nf-wincrypt-certcreateselfsigncertificate),
[Microsoft catalog verification sample](https://github.com/microsoftarchive/msdn-code-gallery-microsoft/blob/master/Official%20Windows%20Platform%20Sample/WinVerifyTrust%20signature%20verification%20sample/%5BC%2B%2B%5D-WinVerifyTrust%20signature%20verification%20sample/C%2B%2B/codesigning.cpp).
