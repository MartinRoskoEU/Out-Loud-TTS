# Managed voices and Windows SAPI (Steps 3C and 3C.1)

Production now uses **ProgramData managed assets + HKLM Registry64 tokens +
the shared machine COM engine**. The earlier LocalAppData/HKCU experiment is
legacy/development only. See [Step 3C.1](MACHINE_VOICE_LIBRARY.md) for the audit,
security contract, API changes, and exact elevated/non-elevated manual commands.
Nothing migrates or modifies existing real test data automatically.

One native engine CLSID `{F38C3398-3552-43B9-8350-7DD301327D70}` and one
`OutLoudPocketSAPI.dll` serve independent voice tokens. Model and tokenizer
paths are shared. Each token reads its own managed `.olv` in place; registration
never copies it into the deployment. Each engine object owns its configuration,
resident mappings, tokenizer and two workers. This shares files, not mutable
in-process inference state or a single worker pair across objects.

## Adapter and deployment audit

`read_configuration` requires `VoicePath`. `AssetRoot` defaults to the parent
of the DLL's directory; it must be absolute. Model/tokenizer default to
`runtime/pocket-english.olp` and `runtime/pocket-english-tokenizer.bin` under
that root. Relative paths resolve under AssetRoot; absolute paths remain
absolute, including managed voices outside the deployment. All resolved paths
must be regular local drive files. VoicePath also requires `.olv` (case
insensitive); device/UNC paths, alternate streams and embedded NUL are rejected.
Missing paths return a file-not-found HRESULT; invalid path/configuration returns
E_INVALIDARG. The unchanged production loader validates the `.olv` header,
hash, finite data and model compatibility during lazy resident construction on
first Speak. File extension/regular-file checks alone do not establish validity.

Seed defaults to 73, MaximumSegmentTokens to 128 (positive, subject to the
existing 4096-token/cache bound), KernelPath to Auto; Portable and AVX2 are
supported. Token display names, IDs and ownership metadata are not neural
configuration. Configuration is per Engine. DLL globals contain only module
HMODULE and atomic object/server-lock counts. No global voice selection exists.

SetObjectToken validates a new configuration before changing the old one. It
waits for an active request to finish. A successful different runtime selection
destroys the old resident only after quiescence; later Speak creates the new
resident. Invalid configuration leaves the old token/runtime intact. Display
name changes alone retain the resident. Do not call SetObjectToken reentrantly
from that same engine's site callback: callbacks must return for Speak to finish.

The existing inventory, prepare-deployment and deployment validation helpers
describe the Gnome development bundle, including `voices/gnome_male.olv`.
They remain development helpers, not managed-library installation. Legacy
register/unregister now reject managed ID keys. Legacy unregistration also
checks HKCU peers before deciding whether its shared HKLM COM class is unused.
Managed removal **never** removes COM registration.

The live development token `OutLoud_Pocket_GnomeMale_Development`, description
`Out Loud - Gnome Male (Development)`, remains outside management and unchanged.

## Identity and exact token contract

For immutable lowercase 32-hex library ID `id`, the key is exactly
`OutLoud_Pocket_<id>`. This is an injective mapping independent of name, file
content and enumeration order. Token scope is part of the Windows token ID.

Production writes only Registry64, under:

```text
HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Speech\Voices\Tokens\OutLoud_Pocket_<id>
```

All values below are REG_SZ. Paths are normalized absolute local paths.

| Location/value | Value |
| --- | --- |
| `(Default)` and `409` | `Out Loud - <current library display_name>` |
| `CLSID` | `{F38C3398-3552-43B9-8350-7DD301327D70}` |
| `OutLoudManagedOwner` | `OutLoud.ManagedVoice.1` |
| `LibraryId` | immutable library ID |
| `AssetRoot` | selected shared deployment root |
| `ModelPath` | shared `runtime/pocket-english.olp` absolute path |
| `TokenizerPath` | shared `runtime/pocket-english-tokenizer.bin` absolute path |
| `VoicePath` | exact healthy ProgramData library entry's absolute `voice.olv` path |
| `Seed` | `73` |
| `MaximumSegmentTokens` | `128` |
| `KernelPath` | `Auto` |
| `Attributes/Name` | same prefixed display name |
| `Attributes/Language` | `409`, English convention used by this pinned model/setup |
| `Attributes/Vendor` | `Out Loud` |

There are no inferred gender, age, race or accent attributes. Duplicate display
names remain legal: Windows token ID and library ID identify the voice. The
manual playback helper requires both exact description and token ID.

## Scope findings and limits of the evidence

The Windows SDK's SPCAT_VOICES is an HKLM category. Microsoft's
[SAPI token documentation](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ms717036(v=vs.85))
permits an explicit HKCU fully qualified category; its documented default
enumeration walks the selected category and merges registered token enumerators.
It does not prove automatic HKCU/HKLM voice merging. This x64 machine accepted
`SpObjectTokenCategory.SetId(HKCU\SOFTWARE\Microsoft\Speech\Voices, false)`
and enumerated zero tokens. Read-only Registry64 inspection likewise found an
empty HKCU Tokens key. Normal SpVoice.GetVoices returned exactly the three HKLM
voices (Zira, Pocket development and WoWKokoro test). This was the initial
pre-install observation. Subsequent user live testing installed the HKCU Gnome
token: explicit enumeration found it, default GetVoices omitted it, token
assignment succeeded, and Speak returned E_ACCESSDENIED (0x80070005). The
machine development token using the same engine spoke correctly. This prompted
the deliberate ProgramData/HKLM architecture change; it does not establish the
underlying HKCU access-denial cause or prove machine-managed WoW integration.

The existing engine DLL is registered in HKLM Registry64 at
`C:\OutLoud\OutLoud-Pocket-development-01\bin\OutLoudPocketSAPI.dll`.
Machine token management reads HKLM SOFTWARE/Classes/CLSID/InprocServer32;
explicit legacy user diagnostics read effective HKCR registration. Each
requires the selected shared DLL's pinned file identity; it never changes
COM registration. See the engine-validation bugfix below for path equivalence.
Windows also supports per-user COM class registration through merged HKCR;
elevated-client caveats apply. See Microsoft's
[HKCR description](https://learn.microsoft.com/en-us/windows/win32/sysinfo/hkey-classes-root-key)
and [COM elevation behavior](https://learn.microsoft.com/en-us/windows/win32/com/the-com-elevation-moniker).
This step neither installs nor live-tests a per-user engine class. Production
requires existing machine registration, without creating or changing it.

The production backend explicitly uses KEY_WOW64_64KEY, following
[Microsoft's registry-view rules](https://learn.microsoft.com/en-us/windows/win32/winprog64/accessing-an-alternate-registry-view).
This is classic SAPI 5 Speech, not Speech_OneCore or WOW6432Node. The checker
and PowerShell observations ran as x64 processes. All six manager operations
now support `machine` (production default) and explicit `user` (legacy
diagnostics/cleanup, with mutation compatibility retained). There is no scope
fallback, self-elevation or automatic HKCU-to-HKLM migration. Production
install/update require the canonical machine library; a separate explicit
development override permits isolated temporary roots.

**WoW HKCU enumeration, voice IDs, playback, bookmarks, StopSpeakingText and
rename/cache refresh behavior all remain live-user validation.** Memory-output
Windows SAPI tests do not prove game integration or audible playback.

## API, operations and errors

`engine/include/outloud/sapi_voices.hpp` exposes `SapiVoiceManager`, independent
of VoiceLibrary's registry-free implementation. A GUI can compose import and
token installation, or token removal followed by optional library removal.
The manager accepts a VoiceLibrary, SapiVoiceRuntime and injectable
SapiVoiceRegistry. Runtime paths are explicit; no hardcoded deployment discovery.

| API | Behavior |
| --- | --- |
| `list(scope)` | read-only managed candidates, ignoring development/unrelated tokens |
| `inspect(id, scope)` | token snapshot plus library/configuration status |
| `reconcile(scope)` | union of library entries and candidates, advisory actions only |
| `install(id, scope)` | healthy entry, production `.olv`/model/tokenizer validation, existing effective COM DLL, complete token publication |
| `update(id, scope)` | explicit name synchronization only; identity/path/settings must already match |
| `remove(id, scope)` | exact owned token only; works for an orphan without needing a library entry/runtime |

Install returns installed or already_installed for an exact match; different
existing contents are token_conflict. Update returns updated or unchanged and
changes only the three name fields. Wrong paths/settings, ownership mismatches
or extra contents require review, not silent repair. Remove requires key ID,
LibraryId, marker, CLSID and exact known root/Attributes schema. Missing token
is token_not_found; unexpected children/types/values prevent removal.
The `.olv`, metadata and COM class are not modified by these operations.

Reconciliation rows include status, detail, library ID, key, raw snapshot and
suggested_action. Statuses are missing, correct, stale_name, orphan, wrong_path,
unhealthy_voice, malformed and conflict. Suggested actions include install,
update, explicit token removal, library repair and registry review. None runs
automatically. Without runtime configuration, inspection checks library
identity/path/name and fixed token settings while explicitly reporting that
the shared runtime paths were not compared; supply --runtime-root for a full
configuration comparison. Reconciliation is a read-only advisory snapshot,
not a transaction across files and registry; install/update revalidate.

SapiVoiceError exposes code, Win32 system_error, optional VoiceLibraryError and
optional VoiceImportError. Codes distinguish invalid ID, missing/unhealthy/
incompatible voice, missing/conflicting/malformed token, access denied, other
registry I/O, unsupported scope, missing/mismatched engine registration,
invalid runtime, library failure, reconciliation required and internal failure.
Engine validation additionally distinguishes engine_registration_malformed,
engine_dll_mismatch and engine_dll_inaccessible; engine_registration_missing
now specifically identifies an absent expected CLSID/InprocServer32 key.
Unhealthy inspection preserves the library's diagnostic detail. Importer/library
exceptions retain their original fields when propagated as operation errors.

## Registry implementation and safety

The production adapter uses native Unicode Windows registry APIs, Advapi32 and
KtmW32; no PowerShell, reg.exe or COM registration helper is spawned. The
transaction APIs are documented by Microsoft:
[create/open](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regcreatekeytransactedw)
and [bounded deletion](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regdeletekeytransactedw).
Only token mutations enter a transaction. They reopen/read a snapshot in the
transaction, compare it with the inspected expected tree, then commit the
complete change. A mismatch/Win32 error rolls back; no nontransactional fallback
or scope fallback occurs. Every opened child uses the same transaction.

Ancestor keys are traversed individually without following registry links.
The key suffix is validated as a library ID. Root and Attributes are the only
editable levels; removal makes two nonrecursive delete calls (Attributes then
the exact token). Unknown subkeys are represented without recursively opening
or deleting their contents. Values retain original types/bytes, including
invalid REG_SZ data; no normalization hides ownership conflicts. Bounds are
64 values per inspected key, 4096 enumerated children and 32 KiB per value.
Access denied is distinct from not found. The immutable legacy token is ignored.

The filesystem and registry are separate resources: another process can rename
or remove a library entry after validation/installation. Reconciliation detects
the resulting stale/orphan/unhealthy state. There is no claim of a combined
filesystem/registry transaction or access-control protection from the same user.

Real registry mutations were not exercised here. Transaction rollback/races,
ownership and schema behavior are tested through the injected fake. Native
write-path integration remains part of manual validation.

## Validation and the concrete filesystem fix

The initial managed-token test passed 51 assertions using a unique temporary Unicode library and a fake
registry. It covers stable IDs, independent duplicate content, idempotence,
Unicode names/paths, missing engine/library/token, denied/failed writes,
install/update/remove rollback, stale names, wrong paths, ownership/CLSID/ID
conflicts, unexpected keys/types, concurrent remove, external registry mutation,
orphans, corrupt `.olv`, unaffected peers and exact body hash preservation.

This exposed an existing variable-size FileRenameInfo allocation that sometimes
omitted the FileName terminator at an exact eight-byte boundary. A second
published directory could acquire an extra trailing character. The allocation
now always reserves the terminator; FileNameLength still counts the name bytes.
The new checker tests all four UTF-16 path-length alignments. See
[FILE_RENAME_INFO](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_rename_info).
This changes filesystem finalization, not the library layout or `.olv` format.

The registration-free multi-voice checker imports separate Gnome and Undead
voices into temporary Unicode managed paths and computes independent native
references with Auto kernels and seed 73. It loads one workspace DLL/factory.
Checks cover A/B/A/B, concurrent different engine instances, per-voice cancellation
and exact recovery, quiescent reassignment, reassignment waiting for an active
request, invalid assignment preserving the prior configuration, display-only
assignment, and actual Windows SpVoice/SpStream memory output with in-memory
tokens. Windows plain and leading-bookmark PCM equals each intended native
reference exactly. The DLL becomes unloadable after all objects/workers release.

The existing SAPI checker also passed unchanged canonical PCM16 (170 frames,
326400 samples, 652800 bytes), real Windows XML/bookmark queues, partial/error
writes, controls, ten abort/recovery cases and residency/module-lifetime checks.
These are correctness tests, not performance gates or benchmarks. No profiler.
Release x64 /W4 /WX build and all 16 CTests passed (44.08 seconds); raw output is in ignored
`generated/perf/3c-build.txt`, `3c-ctest.txt` and `3c-sapi-regression.txt`.

## Engine-validation bugfix: mixed separators in the real install

The first manual install failed before token mutation, despite matching
PowerShell paths. A read-only native trace reproduced the exact fault:

```text
REG_SZ type=1, bytes=134, decoded path length=66
registered: C:\OutLoud\OutLoud-Pocket-development-01\bin\OutLoudPocketSAPI.dll
selected:   C:\OutLoud\OutLoud-Pocket-development-01\bin/OutLoudPocketSAPI.dll
CompareStringOrdinal(..., TRUE) = 3 (different)
```

The CLI normalized the runtime root before appending the literal
`bin/OutLoudPocketSAPI.dll`; it did not normalize the completed DLL path.
Windows file access and filesystem::path equality accept these separators,
but WindowsRegistry::verify_engine compared their raw strings. Its single
missing/mismatch error then mislabeled this spelling difference as
engine_registration_missing. Registry64 lookup, strict REG_SZ decoding and
CLSID were correct. No workspace-DLL comparison, stale deployment assumption
or handle-final-path/prefix discrepancy caused this failure.

The original FakeRegistry::verify_engine returned success from a boolean and
never exercised production decoding or comparison. It now fakes only lookup
and calls the same validate_engine_registration helper as the real adapter.
The regression creates a temporary runtime/bin DLL and uses the exact former
mixed-separator construction, proving that the old raw comparison differs
while manager.install succeeds with the normalized registered path. Mutations
remain entirely fake; the real user library is not inspected or modified.

The CLI now normalizes completed runtime paths and strips a trailing runtime
directory separator. Validation also checks both absolute local `.dll` paths
using the existing alias policy, pins both ancestor chains and regular
single-link files, and compares FILE_ID_INFO volume serial plus 128-bit file ID.
It uses FILE_READ_ATTRIBUTES with read sharing, requesting no write/delete
access and denying replacement while handles are held. See Microsoft's
[file identity comparison](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_id_info).
Equal case/separator/8.3 representations refer to the same file. A separate
copy is rejected even when every DLL byte is identical. Reparse/hard-link
aliases, traversal, ADS, DOS devices, UNC/device prefixes and file-path trailing
separators remain rejected. Internally resolved canonical paths work; untrusted
`\\?\` input is not admitted by stripping its prefix. This establishes selected
file identity, not certificate/PE/export validation or a new deployment policy.

Three new error codes are appended, retaining existing enum values:
engine_registration_malformed (missing/bad default value or unsafe target),
engine_dll_mismatch (different existing file), and engine_dll_inaccessible
(file-open/identity failure, retaining native error and library cause where
available). Registry access-denied/I/O retain their existing classifications;
selected invalid runtime paths remain invalid_runtime. No registry repair,
scope fallback or registration change is introduced.

The checker now passes 92 assertions, including mixed paths in both directions,
case, canonical/short paths, loaded-DLL validation, same-content different files,
malformed REG_SZ/type/termination, missing key/expected CLSID simulation,
missing/non-regular targets, unsafe aliases, junctions, hard links and install
failure without writes. Release x64 /W4 /WX and all 16 CTests passed
(46.02 seconds); git diff --check and new-file whitespace checks passed.
Ignored logs are `generated/perf/3c-fix-build.txt`, `3c-fix-ctest.txt` and
`3c-fix-native-before.txt`. Temporary trace output was removed from production.

A new helper exercises the real effective HKCR Registry64 read and validator
without creating a VoiceLibrary instance or attempting registry mutation:

```powershell
.\build\Release\outloud-sapi-voices.exe verify-engine --runtime-root 'C:\OutLoud\OutLoud-Pocket-development-01'
```

That read-only check passed against the existing registration with
result=engine_verified, including a trailing runtime-root separator. No real
install was executed. After the build, manually rerun the original command in
the PowerShell session with the same `$cli`, `$a` and `$options`:

```powershell
& $cli install $a @options
```

Or use the equivalent explicit command with the reported healthy library ID:

```powershell
.\build\Release\outloud-sapi-voices.exe install a4fb27ae6305b2c563711fdb512bcbb2 --root "$env:LOCALAPPDATA\OutLoud\VoiceLibrary" --runtime-root 'C:\OutLoud\OutLoud-Pocket-development-01' --scope user
```

The bugfix performed no real registry/token/COM write, deployment/signing,
real-library change, WoW action or Git mutation. Neural synthesis, RNG, worker
pipeline, tokenizer, `.olp`, `.olv` v1, library layout and token identity are
unchanged. Default Windows/WoW HKCU visibility remains the separate manual test.

## Legacy HKCU diagnostic package — NOT EXECUTED BY CODEX

The commands below preserve the earlier experiment/cleanup route. They do not
install production voices. The current production procedure is
[the machine validation package](MACHINE_VOICE_LIBRARY.md#manual-validation-commands-not-executed).
HKCU playback has since failed in the user's live test; explicit enumeration
success alone is insufficient. For this legacy package pass `--scope user`
and the old root explicitly; the helper's default token scope is now machine.
Use `-Scope user` for a default-enumeration legacy diagnostic, or
`-ExplicitUserCategory` to select only HKCU.

Run from the repository in **x64 PowerShell**, under the same normal Windows
user that will run WoW. Close WoW manually before token changes. Use existing
healthy managed IDs; do not paste placeholder IDs unchanged. No import performs
registration. If needed, import voices separately using VOICE_LIBRARY.md first.

```powershell
$cli = '.\build\Release\outloud-sapi-voices.exe'
$lib = '.\build\Release\outloud-voice-library.exe'
$runtime = 'C:\OutLoud\OutLoud-Pocket-development-01'
$library = Join-Path $env:LOCALAPPDATA 'OutLoud\VoiceLibrary'
& $lib --root $library list
$a = '<first existing 32-hex library ID>'
$b = '<second existing 32-hex library ID>'
$options = @('--root', $library, '--runtime-root', $runtime, '--scope', 'user')
& $cli list @options
& $cli reconcile @options
& $cli inspect $a @options
```

Install/update require an already registered engine matching
`$runtime\bin\OutLoudPocketSAPI.dll` and production-validated shared assets. The
current live DLL can be used for this configuration-only experiment; deploying
a reviewed/signed rebuilt DLL later is a separate manual action. This step does
not copy, sign or register a new DLL. Every CLI command must finish with exit
code zero before continuing (`if ($LASTEXITCODE) { throw 'Command failed' }`).

Test A: install one per-user token and enumerate default SpVoice. The helper
lists IDs/descriptions from the selected category only and requires the exact
managed description/ID. Record an absent-default result as a failure of default
visibility, even if a separate explicit HKCU invocation works.

```powershell
& $cli install $a @options
if ($LASTEXITCODE) { throw 'Install failed' }
.\sapi\scripts\test-managed-voices.ps1 -LibraryId $a -Scope user
```

Test B: exact-token playback, no name substring/default-voice fallback:

```powershell
.\sapi\scripts\test-managed-voices.ps1 -LibraryId $a -Scope user -Speak
```

The helper speaks exactly `Hello traveler. This is the managed Out Loud voice test.`
If default enumeration failed, the following **separate diagnostic** explicitly
selects from HKCU. Success proves that explicit user-token activation/playback
works; it does not prove default visibility or WoW visibility:

```powershell
.\sapi\scripts\test-managed-voices.ps1 -LibraryId $a -ExplicitUserCategory -Speak
```

This mode prints `Explicit HKCU SAPI category enumeration:`, opens
`HKEY_CURRENT_USER\SOFTWARE\Microsoft\Speech\Voices` with `SetId(..., false)`
and uses `EnumerateTokens`. It never calls `SpVoice.GetVoices()` or falls back
to another category. Omitting the switch (or passing
`-ExplicitUserCategory:$false`) selects default enumeration instead.

The script-only regression checks use fake COM objects and enumerable token
collections without registry access or audio playback:

```powershell
powershell.exe -NoProfile -File .\sapi\tests\managed_voices_script_test.ps1
```

Test C: install a second distinct voice, select each exact description/identity
and listen for the intended distinct voice. Use different labels for the WoW
test because its voice table exposes name/voiceID rather than library IDs.

```powershell
& $cli install $b @options
if ($LASTEXITCODE) { throw 'Second install failed' }
.\sapi\scripts\test-managed-voices.ps1 -LibraryId $a,$b -Scope user -Speak
& $cli reconcile @options
```

Tests D/E: start WoW clean **manually** and enumerate in game. Do not reuse
previous-session numerical voice IDs. These commands follow the
[Blizzard generated API source mirrored by Gethe](https://github.com/Gethe/wow-ui-source/blob/live/Interface/AddOns/Blizzard_APIDocumentationGenerated/VoiceChatDocumentation.lua):
GetTtsVoices returns name/voiceID; SpeakText takes voiceID, text, rate, volume,
optional overlap. StopSpeakingText has no arguments. The installed game's build
must still be checked; a source signature is not a live validation result.

```lua
/run for _,v in ipairs(C_VoiceChat.GetTtsVoices()) do print(v.voiceID,v.name) end
```

For each managed name, confirm a distinct ID. Substitute that ID for 123 or
456 below, run separately, and listen to each voice:

```lua
/run C_VoiceChat.SpeakText(123,"Hello traveler. This is the managed Out Loud voice test.",0,100,false)
/run C_VoiceChat.SpeakText(456,"Hello traveler. This is the managed Out Loud voice test.",0,100,false)
/run C_VoiceChat.SpeakText(123,'<bookmark mark="Start"/>Hello traveler.',0,100,false)
/run C_VoiceChat.StopSpeakingText()
```

Record Windows default/explicit visibility separately from WoW visibility.
If WoW or default Windows enumeration omits user tokens, stop here and report
the enumeration results. No command automatically creates HKLM tokens. An
elevated machine-token strategy needs a later explicit decision/implementation.

Test F: close WoW manually; rename the library entry, inspect stale status,
explicitly update, then use a fresh PowerShell helper invocation and a fresh
WoW process to observe the new name. Identity/path remain unchanged.

```powershell
& $lib --root $library rename $a 'Managed voice renamed'
if ($LASTEXITCODE) { throw 'Rename failed' }
& $cli inspect $a @options
& $cli update $a @options
if ($LASTEXITCODE) { throw 'Token update failed' }
.\sapi\scripts\test-managed-voices.ps1 -LibraryId $a,$b
```

Test G: close WoW manually; remove only A's token, inspect missing status,
verify A's library entry remains healthy and B still enumerates. Verify the
shared engine registration and legacy development token read-only:

```powershell
& $cli remove $a @options
if ($LASTEXITCODE) { throw 'Token removal failed' }
& $cli inspect $a @options
& $lib --root $library inspect $a
.\sapi\scripts\test-managed-voices.ps1 -LibraryId $b
$machine = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, [Microsoft.Win32.RegistryView]::Registry64)
try {
    $engine = $machine.OpenSubKey('SOFTWARE\Classes\CLSID\{F38C3398-3552-43B9-8350-7DD301327D70}\InprocServer32')
    if ($null -eq $engine) { throw 'Shared COM engine missing' }
    try { $engine.GetValue('') } finally { $engine.Dispose() }
    $legacy = $machine.OpenSubKey('SOFTWARE\Microsoft\Speech\Voices\Tokens\OutLoud_Pocket_GnomeMale_Development')
    if ($null -eq $legacy) { throw 'Legacy development voice missing' }
    try { $legacy.GetValue('') } finally { $legacy.Dispose() }
} finally { $machine.Dispose() }
```

No GUI/installer/updater/addon, download/rights/cloning service or automatic
import-to-registration coupling is included. Neural math, RNG, workers,
tokenizer/splitting, model assets and `.olv` v1 are unchanged. Nothing was staged,
committed or pushed. No real registry/token/COM write, registration, regsvr32,
signing, certificate operation, C:\OutLoud deployment write, real-user library
mutation or WoW process action was performed. Stop after Step 3C.
