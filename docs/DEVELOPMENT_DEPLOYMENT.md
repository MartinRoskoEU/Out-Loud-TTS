# Development updates of an installed Out Loud instance

A03.10 now connects development deployment to the same native transaction used
by the production Inno installer. **A03.12 enables the shared lifecycle for controlled testing.**
A03.13 functional deployment testing belongs on disposable Windows. No production installation exists on
the cleaned development machine; these commands do not recreate one.

## Commands and responsibilities

From the repository in x64 PowerShell:

```powershell
.\sapi\scripts\update-installed.ps1 -Components Engine -Action Plan
.\sapi\scripts\update-installed.ps1 -Components VoiceManager -Action Prepare
.\sapi\scripts\update-installed.ps1 -Components Both -Action Prepare
.\sapi\scripts\update-installed.ps1 -Components Both -Action Deploy
```

Plan is the default. All modes first discover the one HKLM64 `OutLoud_is1`
installation. There is no install-root, certificate, CLSID, Force or fallback
argument. Missing installation refuses before building or creating workspace
reports. Plan/Prepare preserve existing ownership and COM candidate inspection.
Those observations are not a substitute for the native transaction's full
protected inventory, ACL, key, receipt and physical-file validation.

Prepare selectively builds the existing Release x64 production components and
copies checked candidates to a new ignored workspace directory. It also supplies
the existing prerequisite detector and toolchain-derived requirements as temporary
bootstrap inputs, plus the hash-bound `maintenance-input.ini`. Bootstrap inputs
are not replacement application files. The native helper must exist; preparation
never installs prerequisites or signs candidates.

Deploy checks the shared native availability gate before building. If eventually
authorized/enabled, it requires elevated x64 PowerShell, repeats discovery and
inspection, prepares candidates and invokes `OutLoudInstallSigning.exe --maintain
development ... --consented`. Only a matching native committed result is reported
as success. Missing installation, changed ownership or pending recovery still refuses. Neither PowerShell nor
this script publishes files, edits receipts or implements a separate rollback.

## Exact update sets

| Mode | Payload paths under bin |
| --- | --- |
| Engine | OutLoudPocketSAPI.dll |
| VoiceManager | OutLoud.VoiceManager.exe/.dll/.deps.json/.runtimeconfig.json, OutLoudVoiceManagerNative.dll, OutLoudVoiceImportHelper.exe |
| Both | All seven files above |

The production package allowlist remains authoritative. Models, tokenizer,
maintenance CLI/helper, vendor DLLs, license files, managed voices, library
metadata and settings are excluded from development publication. Adding a new
dependency or payload path requires package/lifecycle review. Ordinary development
updates preserve version 0.1.0, AppId, installation ID, directory, owned certificate,
COM registration, token IDs and voice registrations.

## Shared signing, publication and recovery

The [A03.10 controller](../installer/inno/MAINTENANCE.md) authenticates protected
installation metadata and the complete installed inventory, stages below a private
installation-owned vault, reuses the LocalMachine My/Root certificate and
nonexportable machine key, signs/verifies the staged engine and its required
vendor dependencies, then coordinates files and signing/registration/inventory
receipts. UI-only updates reuse the existing engine signature and key identity.
There is no CurrentUser certificate fallback or per-update certificate.

Restart Manager holder detection and native file pins refuse active/inaccessible
resources. Close Voice Manager and relevant SAPI clients manually, including WoW
when applicable. No process termination, forced unloading, reboot replacement
or weakened sharing retry is implemented. The script's earlier read-only sharing
probe remains preliminary; publication-time protection belongs to the native helper.

Publication is a logical multi-file transaction, not a global atomic filesystem/
registry operation. Flushed protected snapshots retain original/replacement hashes,
physical IDs, verified backups, moved originals and expected registry/resource
state. Recovery checks all affected rows before restoring anything; foreign or
corrupt state is preserved and refused. A durable committed marker is not undone
because a report was lost. Normal updates do not unregister/register the engine
or change voice tokens. Terminal archives/backups remain for inventory-aware
live A03.11 uninstall validation.

Pending native or older signing/registration journals stop ordinary planning.
Future elevated maintenance can use the same native recover/finalize controller;
there is no PowerShell journal cleanup or automatic legacy-state adoption. Missing
or expired keys, unverifiable ownership, bootstrap records without an authoritative
pointer and Inno host-state conflicts need checked inspection. A03.12 enables checked recovery/finalization and bootstrap reconciliation.
Awaiting-host Inno operations must resume with the matching setup version.

## Historical A03.10 verification and remaining boundaries

The current shared controller passed 86 private-file checks with simulated
certificate/registry/signature hooks; 30 focused integration checks covered exact
manifests/selections, PowerShell parsing, native gates and real Inno 7 Pascal unit
compilation through an always-refusing inert harness. The existing development
update checks passed 64 assertions without rebuilding components. These are historical A03.10 results; [A03.12](../installer/inno/BUILD.md)
records current compilation and focused verification.

Native Release x64 helper/checker builds passed. No inference/WPF rebuild, full
production-stage rebuild, complete project suite or benchmark was needed. Evidence
is under ignored `generated/maintenance-validation`, `generated/installed-updates`
and the user temporary maintenance-fixture directory.

Implemented but not live-tested: authentic installed-instance validation, elevated
ACL/resource handling, real staged signing/key reuse, publication and interruption
recovery. Inno host completion/cancellation, bootstrap interruption inspection,
real restart/power-loss behavior and coherent future uninstall remain release
qualification items for A03.13. A03.12 now integrates maintenance and A03.11 uninstall. This is not
certification of a deployable or production-ready installation.

During those A03.10 checks, no installation, registry/certificate operation, voice/library mutation, application
termination, game action, signing or deployment was executed. Nothing was staged,
committed or pushed.
