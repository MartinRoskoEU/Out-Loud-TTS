# Step 3C.1: machine library and machine SAPI tokens

Production is now a machine runtime, ProgramData managed voices and HKLM
Registry64 SAPI tokens. The user's live HKCU test found the managed Gnome token
only through explicit HKCU enumeration; assignment succeeded but Speak returned
E_ACCESSDENIED (0x80070005). Default SpVoice enumeration omitted it. The existing
HKLM development token using the same engine spoke correctly. This motivates
the scope decision without claiming to diagnose the HKCU HRESULT's cause.
Machine-managed Windows/WoW playback remains manual validation.

## Audit before implementation

| Existing assumption | Finding/change |
| --- | --- |
| Default root | `voice_library.cpp::default_voice_library_root`, its public header comment and VOICE_LIBRARY.md used FOLDERID_LocalAppData. Both developer CLIs consumed that resolver. Now FOLDERID_ProgramData. |
| Root restrictions | Local drive, pinned ancestors, no reparse/hard links, traversal, ADS, ambiguous/device/remote paths; drive-root/OutLoud deployment, Windows/Program Files excluded. ProgramData was already permitted. These restrictions stay. |
| Read access | Session always opened/created the lock read/write and exclusively locked it, including list/inspect/export. Changed to existing read-only shared lock for readers; mutations/cleanup retain exclusive locking. |
| HKCU assumptions | Manager scope guard and six default arguments; native registry root and scope guard; CLI accepted only `--scope user`; PowerShell exact token ID was HKCU; documentation/manual examples. Now machine is the default with separate explicit legacy user scope. |
| COM validation | Earlier effective HKCR lookup supported user diagnostics. Machine operations now require the same DLL identity at the machine HKLM Classes registration. User diagnostics retain HKCR. Neither writes CLSID. |
| Tests | Filesystem tests already used private temporary roots, and registry tests already injected a fake. Their default-root expectation and unsupported-machine expectation changed; fake stores are now separated by scope. No real library/token installation is used. |
| Metadata | Schema 1's eleven fields contain ID/name/asset hashes/compatibility/provenance/import information, no storage scope or absolute managed path. No change is needed. |
| IDs/formats | Secure random 128-bit lowercase 32-hex IDs and `OutLoud_Pocket_<id>` are suitable in either hive. `.olv` v1 and `.olp` are unchanged. A new explicit import creates a new ID; no existing ID is rewritten. |
| Legacy helpers | Deployment scripts still describe the separate Gnome development bundle. Managed tokens remain excluded from legacy register/unregister paths. No helper is run to migrate, register or clean anything. |

## Root, creation and access control

The production resolver calls
`SHGetKnownFolderPath(FOLDERID_ProgramData, KF_FLAG_DONT_VERIFY, nullptr, ...)`
and appends `OutLoud/VoiceLibrary`. It does not create anything and does not
read an environment variable to choose production storage. The usual location:

```text
C:\ProgramData\OutLoud\VoiceLibrary\
    voices\<immutable-library-id>\voice.olv
    voices\<immutable-library-id>\metadata.json
    .staging\
    .trash\
    .library.lock
```

Explicit `VoiceLibrary(root)` remains supported for isolated tests, developer
diagnostics and the old LocalAppData library. Only the canonical production root
has `is_machine_storage()==true`; case/short-name aliases resolve to the same
classification. An explicit spelling of the production root retains all machine
checks. Other roots do not implicitly authorize production HKLM installation.

Read-only Get-Acl inspection of ProgramData on this Windows installation found
SYSTEM/Administrators full control, Users read/execute plus a **container-
inheritable Write grant**, and an inheritable CREATOR OWNER grant. OutLoud did
not exist there. Blindly inheriting this DACL would permit ordinary directory
modification and make the initial creator security-sensitive. Windows describes
[inherited file descriptors](https://learn.microsoft.com/en-us/windows/win32/fileio/file-security-and-access-rights)
and [ACE propagation](https://learn.microsoft.com/en-us/windows/win32/secauthz/automatic-propagation-of-inheritable-aces).

The backend therefore creates missing production anchors and managed directories
with Administrators owner and this protected, inheritable descriptor:

```text
O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FRFX;;;BU)
```

| Principal | Access |
| --- | --- |
| SYSTEM | Full control |
| BUILTIN Administrators | Full control |
| BUILTIN Users | Read/execute/list and synchronization; no ordinary write/delete |

There is no Everyone or CREATOR OWNER grant. The policy is intentionally narrow:
exactly these three allow ACEs/masks; the root anchors also require protected
DACLs. Accessed child directories/files require the same grants and
Administrators/SYSTEM ownership. Directory creation explicitly supplies
Administrators ownership. New copied assets, metadata and lock files receive an
explicit protected file descriptor with the same grants and Administrators
owner. The unchanged Pocket importer produces unpublished staged output; only
that newly converted file has its owner/DACL set through a pinned handle before
publication. This avoids depending on a custom elevated token's default owner.
Existing installed files, anchors and ACLs are never rewritten automatically.

ProgramData itself is never hardened. Existing OutLoud/library objects are
verified, never recursively rewritten or taken over. Unsafe existing security
returns `library_security_invalid`; an administrator must review/provision it
separately. Failed creation may leave safe empty directories/lock for retry.
Publication verifies staged file ACLs and ownership before committing a voice.
Staging/trash, the lock, metadata and assets are within the same security
boundary. Existing filesystem/link/containment checks remain active.

Readers use GENERIC_READ/OPEN_EXISTING on the existing lock and a fail-fast
shared LockFileEx. Writers and explicit cleanup use a fail-fast exclusive lock.
[LockFileEx permits shared locks on read handles](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-lockfileex).
Listing a missing library returns empty; inspection returns voice_not_found.
Read calls create nothing. A partly provisioned existing library without its
lock reports library_unavailable; an elevated mutating operation can initialize
it. Ordinary users can list/inspect fully validated metadata/assets and run the
engine with read-only voice mappings. Cooperating readers block mutations.

Production mutation requires an enabled Administrators or SYSTEM SID before
creating storage. Filtered administrator tokens fail with
`library_access_denied`, native ERROR_ACCESS_DENIED. Filesystem access/creation
failures retain a structured permission error. Missing/unavailable and unsafe
security remain distinct. Rename's raw ACCESS_DENIED can also mean an open
child; its preexisting destination/removal error and native cause stay intact.

## Machine API and lifecycle

All six `SapiVoiceManager` operations support machine scope and now default to
`SapiVoiceScope::machine`: list, inspect, install, update, remove, reconcile.
Native operations open HKLM `SOFTWARE/Microsoft/Speech/Voices/Tokens` with
KEY_WOW64_64KEY. Managed key/schema/ownership/CLSID remain unchanged:

```text
HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Speech\Voices\Tokens\OutLoud_Pocket_<id>
CLSID = {F38C3398-3552-43B9-8350-7DD301327D70}
VoicePath = <known ProgramData>\OutLoud\VoiceLibrary\voices\<id>\voice.olv
```

The twelve root REG_SZ values, single Attributes child, Seed=73,
MaximumSegmentTokens=128, KernelPath=Auto, Language=409 and Vendor=`Out Loud`
are identical to Step 3C. No inferred gender/age/race/accent. See the complete
[token contract](MULTI_VOICE_SAPI.md#identity-and-exact-token-contract).
Engine validation pins the selected machine DLL and compares volume/file ID,
retaining the mixed-separator/case/short-name fixes.

Production install/update reject every non-production library root with
`invalid_machine_library_root` **before registry writes**. VoicePath is derived
only from a healthy exact managed entry under that library, never metadata or
an arbitrary GUI path. Consequently HKLM cannot silently reference LocalAppData
or any user's profile. `MachineVoiceLibraryPolicy::development_override` is a
separate constructor argument for tests/developer diagnostics. It permits an
explicit temporary root and does not change permissions or registry scope.
Production callers must retain the default policy. Reconciliation of a
non-production root is a diagnostic comparison, not permission to install it.

Explicit `SapiVoiceScope::user` retains all six legacy operations, including
mutation compatibility and exact-owned-token cleanup. User and machine stores
never merge/fall back. Existing real HKCU data is untouched. The frontend uses
machine scope; it must not expose legacy user install as production import.

Import and token install remain separately callable. If import succeeds but
registration fails, the valid library entry remains for retry/reconcile. Rename
updates metadata first and then synchronizes only the exact token's display
fields; ID, key, VoicePath and `.olv` bytes stay stable. Registry permission
failure does not roll back a committed library rename; reconcile reports the
stale name. Removal is exact token first, exact library entry second; neither
removes the shared COM engine, another voice or original imported source.
Orphan/unhealthy/wrong-path/conflicting/malformed tokens are advisory results,
never automatically deleted by reconciliation.

Registry mutations retain compare-snapshot transactions and bounded deletion.
Access denial is `registry_access_denied` with the original native code; no UAC,
scope fallback or COM repair occurs. New SAPI errors append
`invalid_machine_library_root` and `library_access_denied` without renumbering
old errors. Original VoiceLibraryError/VoiceImportError causes are retained;
library_security_invalid/library_unavailable are available in library_error.

The future WPF UI should run normally and read this library. A future small
explicitly elevated broker can perform machine mutations. No GUI, broker,
requireAdministrator application manifest, UAC launch or pseudo-transaction
spanning filesystem and registry is implemented here.

## CLI changes

Both CLIs use the ProgramData resolver when --root is omitted. Voice-library
syntax retains `--root DIR` before the command. The SAPI CLI accepts
`--scope machine|user`, defaults to machine, and offers
`--allow-development-library-root` only alongside an explicit --root and
machine scope. Production commands below do not use that override. Developer
root overrides do not self-elevate. `verify-engine` remains read-only; machine
scope checks HKLM Classes, user scope checks effective HKCR.

The manual helper defaults to an exact HKLM ID in default GetVoices enumeration.
`-ExplicitUserCategory` still opens HKCU with create=false and never calls
GetVoices; it implies user scope. Explicit conflicting `-Scope machine` is an
error. `-Scope user` retains default-enumeration legacy diagnostics. Playback
always requires exact identity/ownership/description and explicit `-Speak`.

## Manual validation commands (NOT EXECUTED)

After source review, open **x64 PowerShell as Administrator** in the repository.
Use the known-good development voice as a read-only source; this does not change
the deployment, COM registration or development token. Never pre-create machine
directories with inherited permissive ACLs: let the backend provision them.

```powershell
$lib = (Resolve-Path '.\build\Release\outloud-voice-library.exe').Path
$sapi = (Resolve-Path '.\build\Release\outloud-sapi-voices.exe').Path
$runtime = 'C:\OutLoud\OutLoud-Pocket-development-01'
$source = Join-Path $runtime 'voices\gnome_male.olv'
function Check-Exit { if ($LASTEXITCODE -ne 0) { throw "Native command failed: $LASTEXITCODE" } }

$imported = @(& $lib import-olv $source --name 'Gnome Male')
Check-Exit
$imported
if ($imported -notcontains 'health=valid') { throw 'Imported machine voice is not healthy' }
$machineId = @($imported | Where-Object { $_ -match '^id=[0-9a-f]{32}$' })
if ($machineId.Count -ne 1) { throw 'Expected one newly imported library ID' }
$machineId = $machineId[0].Substring(3)
& $lib inspect $machineId
Check-Exit # require health=valid
& $lib list
Check-Exit

& $sapi verify-engine --runtime-root $runtime --scope machine
Check-Exit
& $sapi install $machineId --runtime-root $runtime --scope machine
Check-Exit
& $sapi inspect $machineId --runtime-root $runtime --scope machine
Check-Exit # require status=correct and ProgramData VoicePath
& $sapi reconcile --runtime-root $runtime --scope machine
Check-Exit
$machineId # record this NEW ID for the next normal PowerShell session
```

Close the elevated shell. Open a **normal non-elevated x64 PowerShell** in the
repository, substitute the recorded new ID, and verify read access, default
enumeration and playback through the exact machine token:

```powershell
$machineId = '<recorded new 32-hex machine library ID>'
.\build\Release\outloud-voice-library.exe list
if ($LASTEXITCODE) { throw 'Machine library read failed' }
.\build\Release\outloud-voice-library.exe inspect $machineId
if ($LASTEXITCODE) { throw 'Machine voice read/health failed' }
.\sapi\scripts\test-managed-voices.ps1 -LibraryId $machineId -Scope machine
.\sapi\scripts\test-managed-voices.ps1 -LibraryId $machineId -Scope machine -Speak
```

Require the default heading, exact new HKLM token ID, `Out Loud - Gnome Male`
and audible `Hello traveler. This is the managed Out Loud voice test.` Do not
use explicit HKCU as a machine fallback. A failed normal read/playback is a
validation failure, not grounds to run WoW/UI permanently elevated.

Start WoW clean **manually**, enumerate, then use the newly printed numerical
voiceID (replace 123 below). Do not reuse cached voice IDs:

```text
/run for _,v in ipairs(C_VoiceChat.GetTtsVoices()) do print(v.voiceID,v.name) end
/run C_VoiceChat.SpeakText(123,"Hello traveler. This is the managed Out Loud voice test.",0,100)
/run C_VoiceChat.StopSpeakingText()
```

For a second distinct prepared voice, in the elevated repository shell:

```powershell
$lib = (Resolve-Path '.\build\Release\outloud-voice-library.exe').Path
$sapi = (Resolve-Path '.\build\Release\outloud-sapi-voices.exe').Path
$runtime = 'C:\OutLoud\OutLoud-Pocket-development-01'
$machineId = '<recorded new Gnome machine library ID>'
function Check-Exit { if ($LASTEXITCODE -ne 0) { throw "Native command failed: $LASTEXITCODE" } }
$secondSource = (Resolve-Path '.\generated\assets\undead_male.olv').Path
$second = @(& $lib import-olv $secondSource --name 'Undead Male')
Check-Exit
$secondId = @($second | Where-Object { $_ -match '^id=[0-9a-f]{32}$' })
if ($secondId.Count -ne 1) { throw 'Expected one second library ID' }
$secondId = $secondId[0].Substring(3)
& $sapi install $secondId --runtime-root $runtime --scope machine
Check-Exit
& $sapi list --runtime-root $runtime --scope machine
Check-Exit
```

In the normal shell, test both recorded IDs with the helper's
`-LibraryId $machineId,$secondId -Scope machine -Speak`. Then restart WoW
manually to refresh enumeration and confirm distinct names, IDs and speech.

Rename/remove are deliberately separate elevated operations:
Use the same elevated shell/setup as the second import, restoring the recorded
Gnome machineId if starting another session. These commands remove the NEW
Gnome machine voice after validation, leaving the second voice installed.

```powershell
& $lib rename $machineId 'Gnome Male Renamed'
Check-Exit
& $sapi reconcile --runtime-root $runtime --scope machine
Check-Exit # stale_name until explicit update
& $sapi update $machineId --runtime-root $runtime --scope machine
Check-Exit
& $sapi inspect $machineId --runtime-root $runtime --scope machine
Check-Exit # correct; same ID/key/VoicePath

& $sapi remove $machineId --scope machine
Check-Exit
& $sapi verify-engine --runtime-root $runtime --scope machine
Check-Exit # shared COM registration is still present
& $lib inspect $machineId
Check-Exit # library entry still valid until separately removed
& $lib remove $machineId
Check-Exit
& $lib list
Check-Exit # second voice remains
```

## Legacy cleanup (manual only, after successful production validation)

Use a **normal x64 shell under the original user** for the old HKCU token and
LocalAppData library. Never use regsvr32/unregister to remove a managed voice.
Keep the source/original library entry until the new machine voice is verified:

```powershell
$oldId = 'a4fb27ae6305b2c563711fdb512bcbb2'
$oldRoot = Join-Path $env:LOCALAPPDATA 'OutLoud\VoiceLibrary'
.\build\Release\outloud-sapi-voices.exe inspect $oldId --scope user --root $oldRoot
if ($LASTEXITCODE) { throw 'Legacy token inspection failed' }
.\build\Release\outloud-sapi-voices.exe remove $oldId --scope user --root $oldRoot
if ($LASTEXITCODE) { throw 'Legacy token removal failed' }
# Optional, separately chosen deletion of the legacy managed COPY:
.\build\Release\outloud-voice-library.exe --root $oldRoot remove $oldId
if ($LASTEXITCODE) { throw 'Legacy library removal failed' }
```

This preserves the development HKLM token, shared COM registration and original
import source. No library or token migration happens automatically.

## Validation and remaining decisions

Coverage is added to the existing library/token tests, without new performance
CTest gates. Tests use private temporary roots, restricted tokens, synthetic
descriptors and separately injected fake HKLM/HKCU stores. They cover known-
folder root resolution, schema/identity/bytes, ACL grants/inheritance and
ordinary reads/denied writes, read-lock concurrency, machine snapshots, all six
operations, separate legacy operations, root rejection/development override,
duplicates/Unicode, missing/corrupt voices, stale/orphan/wrong-path/ID/ownership
conflicts, peers/development tokens, shared engine lifetime, rollback/races and
existing link/traversal/deletion protections. The PowerShell regression uses
fake COM objects only, including default HKLM and explicit HKCU selection.

Registration-free multi-voice coverage retains distinct Gnome/Undead native-
exact PCM, alternating/concurrent engines, bookmarks, cancellation recovery,
reassignment/error preservation and DLL unload. No neural, RNG, tokenizer,
worker, format or scheduling implementation changes are made.

Final Release x64 `/W4 /WX` build passed. **16/16 CTests passed in 47.21 s**,
including **235 library assertions** and **127 managed-token assertions**.
The separate fake-COM PowerShell test passed **49 assertions**, and all nine
PowerShell scripts parsed. Tracked and untracked whitespace checks passed;
the index is empty. No new performance CTest or benchmark/profiler was added.

The full native utterance checker passed with original FP32 SHA256
`36788a2c1cfde712fb9ef68151a93b1b78eec40b1e440e99ff1b1b59d1a8bcf0`, all eight
cancellation barriers and same-pipeline exact recovery. The registration-free
SAPI host passed canonical PCM16 (170 frames / 652800 bytes), leading/trailing
bookmarks and real Windows parser/site event checks, all ten abort cases with
recovery, partial/error writes, residency, caller affinity and COM unload.
These tests use memory output, not audible playback or live installed voices.

Ignored workspace logs:
[build](../generated/perf/3c1-build.txt),
[CTest](../generated/perf/3c1-ctest.txt),
[native correctness](../generated/perf/3c1-native-check.txt),
[SAPI correctness](../generated/perf/3c1-sapi-check.txt).

Changed files for Step 3C.1 (other uncommitted Step 3C work is preserved):

| Area | Files |
| --- | --- |
| Build | CMakeLists.txt |
| Public API | engine/include/outloud/voice_library.hpp; engine/include/outloud/sapi_voices.hpp |
| Storage/security | engine/src/voice_library.cpp; engine/src/voice_library_fs.hpp; new voice_library_security.hpp/.cpp |
| Token/registry backend | sapi/src/voice_tokens.cpp; sapi/src/voice_registry.cpp; new voice_registry_location.hpp |
| CLIs | tools/voice-library/main.cpp; tools/sapi-voices/main.cpp |
| Manual helper | sapi/scripts/test-managed-voices.ps1 |
| Tests | tests/voice_library_test.cpp; sapi/tests/voice_tokens_test.cpp; sapi/tests/managed_voices_script_test.ps1 |
| Documentation | docs/VOICE_LIBRARY.md; docs/MULTI_VOICE_SAPI.md; docs/SAPI_ADAPTER.md; new docs/MACHINE_VOICE_LIBRARY.md |

Before WPF work, the user must validate actual elevated provisioning plus
ordinary Windows/WoW reads/playback/rename/removal. Broker/UAC UX and packaging
remain future work; storage/scope defaults no longer need a product decision.

No real HKLM/HKCU write, real library mutation, COM register/unregister,
deployment/signing change, UAC invocation or WoW process action was executed.
The LocalAppData Gnome entry, its HKCU token, development HKLM token, shared COM
registration and C:\OutLoud deployment remain untouched. Nothing was staged,
committed or pushed.
