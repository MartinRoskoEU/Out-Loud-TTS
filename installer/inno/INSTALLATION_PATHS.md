# Installation directory and SAPI identity

## Volume-root ancestor validation (0.3.0)

The ancestor replacement check excludes self-`DELETE` only for a positively
verified actual volume root. It opens the ancestor without following a final
reparse point, resolves its normalized volume GUID path through the handle,
opens that GUID root independently and compares volume serial/file identities.
The ACL used for the exception comes from the verified handle. Failed proof or
a drive alias to an ordinary directory retains the original DELETE check.
Ownership, delete-child, security-modification, generic-all, inheritance and
reparse checks remain enforced; destination/full-directory checks do not receive
this exception. The check is shared by preflight and the existing native lifecycle.

Focused regression mode in the existing test executable:
`outloud-install-maintenance-tests.exe <private-fixture-parent> security`.
It checks the captured Sandbox ACL in memory, prohibited permissions, failed
proof, ordinary directories and a temporary drive alias, without changing system
ACLs or installing/uninstalling Out Loud.

## Installation inspection diagnostics (0.3.0)

Setup always enables Inno logging and captures the installation inspection
helper's stdout/stderr. A failed preflight displays the exact persistent log
path. Setup also attempts a non-overwriting copy of that log to
`%TEMP%\OutLoud-Setup-0.3.0-is-*.log`, outside its disposable extraction directory.
If copying fails, the message points to Inno's original retained `Setup Log`
file. Copy the displayed file out before closing Windows Sandbox. For a chosen
filename, launch `OutLoud-Setup.exe /LOG="C:\path\OutLoud-diagnostics.log"`.

Native inspection reports retain schema 1 and the `secure_candidate` success
contract. Failures additionally emit `inspection_*` fields to stderr and attempt
a rejected report: category, phase, exact reason, responsible path, Windows API
and error (or `not_applicable` for a policy refusal), reparse attributes/tag or
query error, and applicable owner SID/SDDL/ACE index, type, flags, mask and SID.
Non-ASCII/control characters are escaped as `\uXXXX`; `inspection_path_utf16`
retains the exact UTF-16 path. A secondary report-writing failure uses
`report_failure_*` and never replaces the original validation reason. No private
key or certificate material is logged. Logging does not alter validation,
certificate handling, signing, SAPI registration or uninstall authorization.

Focused native integration checks are in `test-installation-diagnostics.ps1`.
To build the same 0.3.0 product with the unversioned delivery filename, pass
`-InstallerFilename OutLoud-Setup.exe -PreserveHistoricalInstallers` to
`build-installer.ps1`; the normal verified publication workflow writes into
`release/` and retains existing versioned artifacts. Version metadata and the
default versioned build filename remain unchanged.

**Current status:** [A03.12](BUILD.md) supersedes earlier foundation/compiler-gate
statements below. Shared native maintenance/uninstall and Inno lifecycle are
integrated for a controlled-test build. Standalone legacy signing/registration
commands remain blocked. Historical phase evidence below does not establish
live qualification; A03.13 functional testing is still required. Path budgeting
now uses the complete vault layout, rather than the former 100-character root cap.


Implemented 2026-10-10 for **Out Loud 0.1.0**. Existing installer work was
preserved. This removes directory restrictions and deployment-specific conflict
handling; it does **not** complete A03 transaction recovery or enable installation.
All compiler, wizard, signing and registration lifecycle gates remain closed.

## Directory selection and discovery

Fresh setup uses `{autopf}\OutLoud` as a default and exposes the standard
destination page. `DisableDirPage=auto` and `UsePreviousAppDir=yes` retain Inno's
normal discovery behavior. The stable **AppId remains `OutLoud`**. Machine-wide
Registry64 uninstall metadata at
`Software\Microsoft\Windows\CurrentVersion\Uninstall\OutLoud_is1` supplies the
existing release's `InstallLocation`. An update uses that location, disables
directory editing, and checks it again before preparation; `/DIR` cannot move
an existing instance. Missing/inaccessible recorded locations fail closed for
future checked repair rather than creating another installation.

The CLSID is not an installation discovery mechanism. No old deployment
directories are scanned. There is no migration mode or special deployment
classification. Ownership state without an installer record is refused rather
than adopted. A pre-existing nonempty destination without owned state is also
refused. Inno's normal uninstall metadata records the actual resolved location
for future maintenance; protected `configuration/signing.ini` supplies the
separate installation/certificate identity. Future development tools can use
those same records. Such tools are not implemented here.

Inno documents [AppId and its uninstall key](https://jrsoftware.org/ishelp/topic_setup_appid.htm),
[previous-directory reuse](https://jrsoftware.org/ishelp/topic_setup_usepreviousappdir.htm)
and [destination-page behavior](https://jrsoftware.org/ishelp/topic_setup_disabledirpage.htm).
The selected relative layout remains:

```text
<selected directory>/bin/           application and native components
<selected directory>/runtime/       unchanged model and tokenizer
<selected directory>/configuration/ protected ownership and recovery state
<selected directory>/licenses/      notices and full license texts
%ProgramData%/OutLoud/VoiceLibrary/  persistent managed voices, outside payload
```

Managed voices are not installer payload. Existing user settings/data locations
remain unchanged. Exports outside managed storage are outside installer ownership.

## Paths, security and signing

`installation_path.hpp` validates/normalizes local absolute directory paths,
rejects a volume root, UNC/device/stream paths and ambiguous components, and
provides ordinal case-insensitive directory comparisons. It contains no default
drive or product location. Native wide Windows APIs preserve spaces and Unicode.
Inno passes quoted `{app}`/`WizardDirValue` paths to the helper. The actual root
determines the DLL, ownership files, pending records and registration receipt.

The read-only `--installation-inspect` command checks the selected location before
prerequisite installation or file copying. It supports fixed local NTFS volumes
and refuses inaccessible or reparse-point paths. The existing destination or
nearest existing parent must have a trusted owner and no ordinary-user write
rights. Inherited write ACEs are checked for a new child; ancestor deletion,
replacement and security-modification rights are checked separately. SYSTEM,
Administrators and Windows TrustedInstaller are recognized for protected
ancestors. Creator-owner inheritance is safe only because the inspected parent
cannot be written by ordinary users and setup runs elevated.

ACL validation is conservative: deny ACEs do not relax another grant, unsupported
ACE forms are refused, and no unrelated ACL is rewritten. A secure custom parent
is accepted without testing whether it is in Program Files. An insecure parent
is refused even within Program Files. The installer does not automatically fix
user-owned or permissive ancestors. Runtime signing repeats these directory
checks, then retains protected-state, one-link file, pinned-engine and signature
checks. No normal-user write permission is added.

The machine-wide certificate lifecycle remains LocalMachine My/Root with the
existing protected signing-only key policy. Certificate reuse is keyed by owned
installation/certificate metadata, not directory spelling. The signing target is
`<resolved root>/bin/OutLoudPocketSAPI.dll`; vendor signatures and dependency
inventory policies are unchanged. No certificates were created or signed here.

## SAPI ownership and identity

**Authoritative identity:** [sapi/src/identity.hpp](../../sapi/src/identity.hpp).
The CLSID remains **`{F38C3398-3552-43B9-8350-7DD301327D70}`**, for every version
and installation. It is separate from Inno AppId, random installation ownership
ID, machine signing certificate and voice library IDs.

Native engine, token management, mutation helper, native installer and native
SAPI checkers include the header directly. The installer no longer produces or
handles the obsolete `migration_required` disposition. Missing installation or
certificate bindings are generic conflicts. Existing owner-marker spelling is
retained as a registry contract; the marker/CLSID alone never authorizes a write.
Strict typed-tree, matching ownership, override, transaction and pinned physical
server-file checks remain. `InprocServer32` derives from the resolved root and
uses machine Registry64. Unrelated registrations are never adopted/overwritten.

Cross-language consumers use these small mechanisms:

- [sapi/identity-common.ps1](../../sapi/identity-common.ps1) reads the one header
  definition. Production registration INI generation consumes it directly.
- Existing staged `installer-version.iss` also generates `EngineClsid` from the
  header. Inno registration verification consumes that definition; compiler
  overrides are refused. No additional staging format/file is needed.
- [ui/SapiIdentity.targets](../../ui/SapiIdentity.targets) generates a C# constant
  into each managed project's intermediate output. WPF uses it for exact token
  checks; the linked-service test project imports the same target.
- Package/development JSON inventories keep self-contained fixed identity fields;
  production policy loading and focused validation check them against the header.
- Existing development PowerShell bundles must run independently of repository
  headers. Their hardcoded CLSIDs remain deliberate portable exceptions. Those
  scripts are byte-for-byte unchanged; `test-paths-identity.ps1` checks their
  copies, mock fixtures and native numeric GUID representation against the header.
- Historical documentation preserves its recorded identity and observations.
  Zero/wrong GUIDs in negative tests are failure fixtures, not alternate engine
  identities. No new engine CLSID or installation mode has been introduced.

## Upgrade, rollback and uninstall limits

Scaffolding uses the selected root for files, shortcuts, signing, COM paths and
pending/receipt records. Update discovery fixes one existing directory; owned
certificates are reused. No code in this task deletes or recreates managed
voices, changes their IDs, changes user settings or adds an export cleanup path.

**Still pending:** transaction integration, complete release upgrade/repair,
locked-file handling, interruption recovery, validated rollback and full uninstall.
Future full uninstall must remove verified owned COM/tokens, managed voices and
application data, and safely remove the owned certificate/key, while preserving
shared runtimes and external exports. No such live lifecycle is claimed here.
The selected directory must be used for future cleanup/recovery; there is no
fixed Program Files product path in the current native lifecycle scaffolding.

## Files and focused verification

Changed installer files: `OutLoud.iss`, new `installation.iss`,
`production-common.ps1`, `registration-common.ps1`, `registration.iss`,
`version-common.ps1`, `package.json`, `test-registration.ps1`, `test-signing.ps1`,
new `test-paths-identity.ps1`; native `signing.cpp`, `registration.inc`,
`registration_policy.hpp`, `registration_tests.cpp`, new `installation_path.hpp`.
Added shared `sapi/identity-common.ps1` and `ui/SapiIdentity.targets`; updated
the WPF/test projects and `SapiTestSessionFactory.cs` to consume generated identity.
Updated root/installer README, signing/registration guides and this report.
Development deployment scripts, native engine/model/backend behavior, version,
AppId, authoritative identity header, LICENSE and THIRD_PARTY_NOTICES are unchanged.
No dependencies, models, licensing or distribution scope changed.

Validation evidence is under ignored `generated/installer/path-independence/`.
Final focused results:

| Check | Result |
| --- | --- |
| Native registration/path policy, in-memory registry | 305 assertions PASS |
| Native controlled signing/reuse policy | 124 assertions PASS |
| Path and cross-language identity validation | 47 checks PASS |
| Registration/stage integration, read-only inspection | 28 checks PASS |
| Signing/stage integration, read-only inspection and blocked mutation entry | 27 checks PASS |
| WPF Release x64 and linked-service test-project compilation | PASS, 0 warnings / 0 errors; frontend suite not run |
| Fresh production stage | PASS, 23 explicit files |
| PowerShell/XML parsing and local documentation links | 17 scripts and 47 local links PASS |
| Preservation | All eight existing development scripts plus version/header/license/notices match saved hashes; cleaned installation state remains absent |
| Whitespace | `git diff --check` PASS |

Release x64 native helper/affected tests and WPF builds passed. A new 23-file
production stage contains the generated identity, helper and frontend; no setup
was run. Focused tests cover registration conflicts/transaction errors, signing
reuse/failures, spaces/Unicode, non-default-drive path normalization, security
rejection, a real test-owned junction, preserved junction target and identity
consistency. Actual location probes are read-only: no destination is created.
The runtime prerequisite detector still uses Windows Program Files discovery to
find system-installed .NET. That is runtime discovery, not a product installation
directory constraint, and was not changed. Historical stages remain untouched;
the current validated stage is `generated/installer/stages/a03-path-independence-final`.

The **Inno compiler is unavailable** (PATH and standard machine/user locations
checked), so there is no Pascal compilation or live wizard validation. Its hard
compiler gate was not bypassed. Real custom-drive installation, release update,
uninstall, ACL creation under elevation, certificate reuse/signing at a custom
location, crash recovery and WoW remain unverified. Only focused installer checks
ran; no broad correctness suite, benchmark or game operation ran. No installed
registry/certificate/voice-library mutation, deployment, commit or push occurred.
