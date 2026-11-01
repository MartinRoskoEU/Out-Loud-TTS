# Pocket SAPI adapter and development deployment

`OutLoudPocketSAPI.dll` retains CLSID `{F38C3398-3552-43B9-8350-7DD301327D70}`
and the existing `DllGetClassObject`/`DllCanUnloadNow` exports. Registration uses
the established scripts; there is no self-registration/regsvr32 workflow.

## Runtime responsibilities

Rust is the only synthesis backend. Managed tokens contain `Backend=Rust`, the
trusted `RustRuntimePath`, protected native `VoicePath`, library identity and
ownership/display fields. They contain no model or revision binding. The adapter
uses the machine-wide explicit WinUI Settings model selection through the
existing verified `RustModelSelection` path. See
[production integration](RUST_RUNTIME_SESSION.md#production-integration-independent-native-profiles-and-settings-selection).

SAPI uses the runtime activated in Settings without a runtime-version allowlist.
Updated executables resolve from the existing protected installation receipt and
retain SHA-256 verification and resident file leases. An invalid selected update
is reported as an error; SAPI does not substitute the bundled runtime.

The C++ COM adapter, caller-thread SAPI controls, streaming/partial writes, PCM16
delivery, Sonic speech-rate processing, volume/bookmarks, cancellation, owned
process cleanup, diagnostics and resident leases are retained.
The shared mapping, SHA-256, strict native-container validation and library locks
remain. No existing deployment, registration or application data is changed by
building this source.

## Build and deployment

### Development updates after a production installation

`update-installed.ps1` also supports same-version updates of the current WinUI installer-managed application, using its recorded directory and the existing production maintenance/signing/inventory lifecycle. This route preserves the Inno uninstaller and application data, and never adopts an older installer or a standalone development bundle. See [the installed development update procedure](../installer/PRODUCTION.md#updating-an-installed-application-during-development) for Plan, Prepare and Deploy. No Setup executable is compiled for these updates.

### Repeated in-place WinUI development updates

For an existing clean development bundle, `update-installed.ps1 -DeploymentRoot`
updates that same directory. It requires no Inno ownership record and does not
write COM/SAPI registration or application data. Use elevated x64 Windows PowerShell 5.1 (`powershell.exe`) for
Prepare/Deploy and close Out Loud, WoW and other SAPI clients manually first.

```powershell
.\sapi\scripts\update-installed.ps1 -Action Plan -DeploymentRoot 'C:\ProgramData\OutLoud\Development\0.4.0-dev1'
.\sapi\scripts\update-installed.ps1 -Action Deploy `
    -DeploymentRoot 'C:\ProgramData\OutLoud\Development\0.4.0-dev1' `
    -CertificateThumbprint '<existing trusted code-signing thumbprint>' `
    -CertificateStoreLocation CurrentUser
```

Without `-PublishDirectory`, Prepare/Deploy configure/build the existing native
Release targets and security inspector, and publish WinUI with its existing
`NativeBuildDirectory` convention. An explicit PublishDirectory reuses already
built matching Release outputs. Prepare only stages; Deploy additionally signs,
verifies and publishes. Each invocation uses a fresh temporary sibling stage and
workspace diagnostic report, while the installed directory path remains unchanged.

The old bundle must pass its fixed reviewed inventory, complete hashes and valid
signatures; the prior 81-file WinUI inventory is accepted only for this source
inspection. Current staging/signing still requires the current 80-file inventory.
The existing certificate must match both bundles' project signers. The official
Rust executable is never re-signed. Native owner/ACL/independent-file inspection
is reused before and after publication. Existing COM registration must already
point to this exact DLL path, with its original owner and configuration.

Every existing inventory file is acquired exclusively before the first write,
including unchanged binaries, and rehashed through that held handle. Active
processes or sharing conflicts stop the update. Modified files preserve their
existing owner/ACL. Only obsolete files named by the verified old inventory are
removed; unknown files are refused. Inventory/resources and `deployment.json`
are updated together, with the manifest written last. Final full bundle/security
checks and the existing CLI's read-only `verify-engine` must pass before success
is reported. COM/SAPI trees are compared before/after without writing them.

There is no backup, automatic rollback, migration or version-history mechanism.
An I/O failure during publication can leave a partial update; the report is
marked failed and the script throws. Such a bundle requires explicit repair;
it is never reported as successfully verified. Staged inputs and diagnostics are
retained for inspection. Shared models/selection/Settings under
`%ProgramData%\OutLoud\ApplicationData` and profiles under
`%ProgramData%\OutLoud\VoiceLibrary` are outside the replacement inventory.

### Clean WinUI development deployment (no Inno Setup)

The existing `sapi/scripts/update-installed.ps1` entry point now has an explicit
clean branch: `-DeploymentRoot <fresh versioned directory>`. It requires the
absence of an Inno installation record. Retired installer installations are
refused; this workflow does not migrate, repair or uninstall them. Clean deployment
never fabricates uninstall records, installation IDs or installer signing receipts.
Existing development bundles use the in-place path above. Existing registrations
pointing to another deployment are refused.

Build the required production artifacts and publish WinUI using its existing
project (commands below build workspace files only):

```powershell
cmake -S . -B build-winui-native -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=OFF
cmake --build build-winui-native --config Release --target OutLoudPocketSAPI OutLoudVoiceManagerNative OutLoudVoiceImportHelper outloud-sapi-voices
cmake -S installer/native -B build-installer-prerequisites -G "Visual Studio 17 2022" -A x64
cmake --build build-installer-prerequisites --config Release --target OutLoudInstallSigning
dotnet publish ui/OutLoud.WinUI/OutLoud.WinUI.csproj -c Release -p:Platform=x64 --self-contained false -o generated/winui-deployment-preparation/app
```

The native build must have been configured from the current source. Shipping
`OutLoudPocketSAPI` enables production Rust Settings integration independently of
the opt-in private test DLL. Existing .NET 10 x64, Windows App Runtime 2.5.1 and VC
v14 x64 installations remain prerequisites; this workflow does not acquire them.

The following commands are **manual deployment operations, not validation**.
Use elevated x64 PowerShell for publication/registration, stop active Out Loud
voice management and SAPI clients, and close WoW. Supply an existing trusted
code-signing certificate; no certificate/trust/key creation is performed.

```powershell
# Plan performs read-only location/ACL inspection and writes only its diagnostic.
.\sapi\scripts\update-installed.ps1 -Action Plan -DeploymentRoot 'C:\Program Files\Out Loud Development\0.4.0-dev1'

# Deploy composes the existing stage -> sign -> verify -> register scripts.
.\sapi\scripts\update-installed.ps1 -Action Deploy `
    -DeploymentRoot 'C:\Program Files\Out Loud Development\0.4.0-dev1' `
    -BuildDirectory 'build-winui-native/Release' `
    -PublishDirectory 'generated/winui-deployment-preparation/app' `
    -CertificateThumbprint '<existing trusted code-signing thumbprint>' `
    -CertificateStoreLocation CurrentUser
```

`Prepare` stages only, without signing or registration. A prepared bundle is not
overwritten: finish it with the existing `sign-deployment.ps1`,
`verify-deployment.ps1 -RequireSignatures` and `register.ps1` entry points. An
explicit fresh destination is required for a complete `Deploy` invocation.
Signing/registration failures preserve staged files and registry rollback evidence
for review; no new cleanup or migration behavior is introduced.

Schema **3** in `sapi/deployment-inventory.json` describes the exact WinUI payload:
WinUI at the bundle root with its bridge/helper, `bin/OutLoudPocketSAPI.dll`, the
management CLI and existing security inspector, Microsoft dependencies, and the
official `runtime/pocket-tts/` executable/license. This retains both the app's
existing relative runtime path and the native registration layout. WinUI's compiled
XAML and PRI are copied from the matching Release build output (the published
assembly must match); use `-WinUIBuildDirectory` if that output is elsewhere.
No models, selections or managed profiles belong to the deployment inventory.

The reviewed inventory has a fixed, newline-normalized digest in
`deployment-common.ps1`; SDK dependency changes require an explicit inventory/pin
review. Microsoft dependency signatures and fixed package hashes are preserved.
The official Rust executable remains byte-for-byte pinned to
`813cabfc2f8fc7a4e5310deda6025426b3164610dd9f3c3098a6078e5cc1895a` and is **never
re-signed**. Project images use the existing explicit certificate/SignTool policy.
Unlisted files, altered inventories, missing or changed files, reparse paths,
wrong architectures and mismatched build/publish components are rejected.

Before clean publication and again before COM registration, the existing native
installation inspector's `--development-bundle` mode applies its original fixed
NTFS, owner, ACL, ancestor and independent-file checks without granting installer
ownership. COM registration still uses the existing HKLM Registry64 ownership,
HKCU-override, game-closed, signed-bundle and rollback checks. Registration remains
a separate operation from profile import and never changes user application data.

For an existing valid native profile with a missing token, explicitly use the
existing CLI after engine registration (replace the ID with the inspected target):

```powershell
& '<bundle>\bin\outloud-sapi-voices.exe' inspect '<existing-library-id>'
& '<bundle>\bin\outloud-sapi-voices.exe' install '<existing-library-id>' --runtime-root '<bundle>'
```

The CLI now supplies `<bundle>/runtime/pocket-tts/pocket-tts.exe`. The existing
production manager validates its trusted runtime identity, native profile and
machine registration before token creation. Model selection stays entirely in
Settings and the production SAPI resolver; no per-voice model binding is added.


The fixed schema-3 WinUI inventory is authoritative for standalone development
deployment. Production installation uses its separate verified receipt and
[maintenance workflow](../installer/PRODUCTION.md).
The deployment module contains the shared path/hash/PE/version helpers directly.
`OutLoudInstallSigning.exe` retains its existing read-only description, dependency,
and `--installation-inspect --development-bundle` operations and security predicates.
Standalone development signing uses an existing trusted certificate through
SignTool; COM/SAPI registration remains in the existing checked scripts. Production
setup instead provisions and manages its installation-owned certificate through
the production native lifecycle. These ownership routes are not interchangeable.

`unregister.ps1` still refuses while a machine voice references this engine and
never removes profiles. Signing, trusted certificates, ACL/owner validation,
registry ownership, HKCU override rejection and transaction/rollback rules remain.
No deployment or Windows registration is part of source validation.

### WinUI production installer

The self-contained WinUI/Rust production installer, installation-owned signing/uninstall lifecycle and installed development updates are documented in [installer/PRODUCTION.md](../installer/PRODUCTION.md). Their ownership contract is separate from the standalone development bundle workflow above. They do not adopt a development bundle or an older WPF installation. Build/fixture validation does not authorize running installation, uninstallation, certificate provisioning, or registration on the development PC; live acceptance testing belongs in Windows Sandbox.
