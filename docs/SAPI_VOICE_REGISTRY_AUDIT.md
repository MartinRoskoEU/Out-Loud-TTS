# SAPI registry and Voice Library consistency audit

Audit date: 2026-10-09. Source baseline: `2b1ee6f` (Added voice tester).
Production library/registry snapshots were captured at approximately
**11:52–11:55 UTC**. Later native enumeration and tests confirmed the findings.
This is an audit, not a repair.

**VERIFIED** means directly supported by inspected source, system reads or an
executed test. **INFERRED** identifies a plausible explanation, not a demonstrated
cause. **UNVERIFIED** identifies information not established in this audit.

## 1. Findings and historical limits

**VERIFIED:** the two current machine library entries have their own managed
HKLM Registry64 tokens, matching library IDs, names, ownership, CLSID and actual
managed voice paths. Fresh Windows SAPI5 enumeration includes both:
**Out Loud - Undead Male** and **Out Loud - Undead Female**. No managed HKLM token
without a current machine library entry was found.

**VERIFIED:** other Out Loud registrations remain: the older development Gnome
token in HKLM64, and a Gnome token in HKCU pointing to an existing older user
library entry. Machine Voice Manager removal targets neither of those entries.
Their presence is not evidence that removal of a selected machine library ID
failed.

**VERIFIED:** the production coordination code removes the owned HKLM64 token as
part of machine-library removal. The temporary import/remove/reimport cycle
passed, and injected registry errors did not report complete success.

**VERIFIED:** a separate diagnostic defect was reproduced: runtime-aware CLI
inspection reports `conflict` when helper-generated model/tokenizer strings use
`runtime/filename`, while the CLI constructs the same paths with backslashes.
The paths resolve to the same locations; token comparison uses raw registry
strings. This is a false diagnostic conflict, not evidence that the new tokens
are absent or excluded from SAPI enumeration.

**UNVERIFIED:** the exact latest historical removal sequence cannot be recovered
from current state. There are no supplied before/after snapshots, removed-ID
receipts, helper outcomes or WoW enumeration captures for that operation. The
historical IDs in the request are comparison candidates only. Current metadata
dates the new imports, not the identities or successful removal of every earlier
voice. No running process named `Wow`, `WowClassic` or `WowClassicT` was found;
WoW was not launched or inspected internally.

## 2. Actual current production Voice Library

Root: `C:\ProgramData\OutLoud\VoiceLibrary`.

**VERIFIED:** there are exactly two entries under `voices`. Both native
`outloud-voice-library list` health results are `valid`. Their actual OLV
SHA-256 hashes match metadata. `.staging` and `.trash` were empty. The read-only
library operation opens the existing lock for reading and uses a shared lock;
it does not create the library or run cleanup.

| Field | Undead Male | Undead Female |
| --- | --- | --- |
| Library ID | `61e96426345e59d3806447bf9357b812` | `b5db3e52d26308112791a9c3e396d3d1` |
| Display name | `Undead Male` | `Undead Female` |
| Library health | `valid` | `valid` |
| OLV hash, independently read and matching metadata | `b2aad5d3c6c9627a3d04fc84a5655498d1008c144c31bcb6da70a467ab0e0ebf` | `599608840ab512d276f17e1cafa97858111cf9e8ef507b00e074bd0cc7d71212` |
| Recorded source hash | `6f68c1a292152e541ea43f844c6c1d380f2a5c5b7f93e369310964dda80caebb` | `2d621e2df47edbcbced6e91b654474c667a4f5106af582ff29bcd10d3c32d466` |
| Source filename | `reference-audio-undead-male.safetensors` | `reference-audio-undead-female-2.safetensors` |
| Import timestamp UTC | `2026-10-09T11:38:53Z` | `2026-10-09T11:39:05Z` |
| Import kind | `pocket_prepared` | `pocket_prepared` |
| OLV bytes | 5,161,216 | 7,323,904 |
| Metadata / OLV schema | 1 / 1 | 1 / 1 |
| Model provenance verified | `false` | `false` |

Actual managed paths:

```text
C:\ProgramData\OutLoud\VoiceLibrary\voices\61e96426345e59d3806447bf9357b812\voice.olv
C:\ProgramData\OutLoud\VoiceLibrary\voices\b5db3e52d26308112791a9c3e396d3d1\voice.olv
```

Both declare compatibility ID
`993a3a9749dc64d60815aa0e71fa4c18f53d478d05d0c4da8c7c77164387c59a`.
**VERIFIED:** native inspection validates strict metadata, OLV integrity and
metadata/header identity, as well as machine-library security checks. A recorded
source hash is provenance metadata verified against the OLV header; it does not
prove the original source file is still available or establish its original
directory. No attempt was made to reconstruct the user's original import files.

## 3. Actual registrations grouped by hive and view

All four locations were opened explicitly with
`.NET RegistryKey.OpenBaseKey(hive, RegistryView.Registry64/Registry32)` and
`OpenSubKey(..., writable: false)`. Values were read with their actual types and
without expanding environment-variable strings. No Registry Editor display was
used as evidence.

Path in each view: `SOFTWARE\Microsoft\Speech\Voices\Tokens`.

| Hive / view | Out Loud token keys found |
| --- | --- |
| HKLM / Registry64 | `OutLoud_Pocket_61e96426345e59d3806447bf9357b812`; `OutLoud_Pocket_b5db3e52d26308112791a9c3e396d3d1`; `OutLoud_Pocket_GnomeMale_Development` |
| HKLM / Registry32 | None |
| HKCU / Registry64 | `OutLoud_Pocket_a4fb27ae6305b2c563711fdb512bcbb2` |
| HKCU / Registry32 | The same `OutLoud_Pocket_a4fb27ae6305b2c563711fdb512bcbb2` contents |

**VERIFIED:** HKCU `SOFTWARE` is shared under the documented modern WOW64 rules;
seeing this HKCU speech token through both requested views is not proof of two
independently installed registrations. HKLM `SOFTWARE` and COM CLSID locations
are redirected. See Microsoft's [registry-view table](https://learn.microsoft.com/en-us/windows/win32/winprog64/shared-registry-keys).

### Current managed HKLM64 tokens

All listed root and attribute values are **REG_SZ**, not DWORD or expandable
strings. The only subkey is `Attributes`.

| Value | Male token | Female token |
| --- | --- | --- |
| Token key | `OutLoud_Pocket_61e96426345e59d3806447bf9357b812` | `OutLoud_Pocket_b5db3e52d26308112791a9c3e396d3d1` |
| Default description and `409` | `Out Loud - Undead Male` | `Out Loud - Undead Female` |
| `LibraryId` | `61e96426345e59d3806447bf9357b812` | `b5db3e52d26308112791a9c3e396d3d1` |
| `VoicePath` | Male managed path in section 2 | Female managed path in section 2 |
| `Attributes\Name` | `Out Loud - Undead Male` | `Out Loud - Undead Female` |

Common values, including the **actual slash spelling**:

```text
CLSID                = {F38C3398-3552-43B9-8350-7DD301327D70}
OutLoudManagedOwner   = OutLoud.ManagedVoice.1
AssetRoot            = C:\OutLoud\OutLoud-Pocket-development-01
ModelPath            = C:\OutLoud\OutLoud-Pocket-development-01\runtime/pocket-english.olp
TokenizerPath        = C:\OutLoud\OutLoud-Pocket-development-01\runtime/pocket-english-tokenizer.bin
Seed                 = 73
MaximumSegmentTokens = 128
KernelPath           = Auto
Attributes\Language  = 409
Attributes\Vendor    = Out Loud
```

**VERIFIED:** both managed voice files, the shared model, tokenizer and engine
DLL exist and are readable at these paths. Token identity, display fields and
voice paths match the current library. The slash spelling is diagnostically
significant because token-tree equality compares strings; it is not an absent
runtime file.

### Legacy HKLM64 development token

```text
Key                  = OutLoud_Pocket_GnomeMale_Development
Default and 409      = Out Loud - Gnome Male (Development)
CLSID                = {F38C3398-3552-43B9-8350-7DD301327D70}
OutLoudPocketOwner    = OutLoudPocketSAPI-2M
AssetRoot            = C:\OutLoud\OutLoud-Pocket-development-01
ModelPath            = runtime/pocket-english.olp
TokenizerPath        = runtime/pocket-english-tokenizer.bin
VoicePath            = voices/gnome_male.olv
Seed                 = 73
MaximumSegmentTokens = 128
KernelPath           = Auto
Attributes\Name      = Out Loud - Gnome Male (Development)
Attributes\Language  = 409
Attributes\Vendor    = Out Loud
Attributes\Gender    = Male
Attributes\Age       = Adult
Attributes\Version   = 0.1
```

All are REG_SZ. `LibraryId` and `OutLoudManagedOwner` are absent. Its actual
resolved voice file is
`C:\OutLoud\OutLoud-Pocket-development-01\voices\gnome_male.olv`, which exists.
**VERIFIED:** this is a distinct legacy registration with its own ownership
marker and deployed voice file. It is not an orphan of a current machine
library entry and is intentionally excluded from managed-token listing/removal.

### Older HKCU managed token

```text
Key                  = OutLoud_Pocket_a4fb27ae6305b2c563711fdb512bcbb2
Default and 409      = Out Loud - Gnome Male
LibraryId            = a4fb27ae6305b2c563711fdb512bcbb2
CLSID                = {F38C3398-3552-43B9-8350-7DD301327D70}
OutLoudManagedOwner   = OutLoud.ManagedVoice.1
AssetRoot            = C:\OutLoud\OutLoud-Pocket-development-01
ModelPath            = C:\OutLoud\OutLoud-Pocket-development-01\runtime\pocket-english.olp
TokenizerPath        = C:\OutLoud\OutLoud-Pocket-development-01\runtime\pocket-english-tokenizer.bin
VoicePath            = C:\Users\rosko\AppData\Local\OutLoud\VoiceLibrary\voices\a4fb27ae6305b2c563711fdb512bcbb2\voice.olv
Seed                 = 73
MaximumSegmentTokens = 128
KernelPath           = Auto
Attributes\Name      = Out Loud - Gnome Male
Attributes\Language  = 409
Attributes\Vendor    = Out Loud
```

All are REG_SZ; only the `Attributes` subkey exists. Its older user-library
directory and voice file still exist. Read-only user-scope native inspection
reports `correct` against that user library, with the same limited-runtime
diagnostic described in section 7. **VERIFIED:** it is outside the current
machine-library scope. It was not a missing-file orphan at audit time and does
not appear in the default x64 SAPI enumeration below.

### COM engine registration

**VERIFIED:** the engine's HKLM64 registration is:

```text
SOFTWARE\Classes\CLSID\{F38C3398-3552-43B9-8350-7DD301327D70}\InprocServer32
Default        = C:\OutLoud\OutLoud-Pocket-development-01\bin\OutLoudPocketSAPI.dll
ThreadingModel = Both
```

Both values are REG_SZ. Effective HKCR64 resolves to that same registration;
no HKCU64 override was found. HKLM32, HKCU32 and effective HKCR32 have no matching
server registration. The DLL is AMD64 (`IMAGE_FILE_MACHINE_AMD64`, `0x8664`),
260,912 bytes, SHA-256
`a131176aa831d9f3ac38665a7b31fdd3141c142b109c9541b277ca4d2eb0f9f4`.
The read-only `verify-engine --scope machine --runtime-root ...` returned
`engine_verified`, using file identity comparison. This checks the selected
registered DLL identity; it is not a synthesis, dependency-load or WoW test.

## 4. Library-to-token identity comparison

The machine library is the comparison root unless explicitly stated otherwise.
“Matches” below refers to verified ownership/identity/name/voice path; full
runtime-string diagnostic behavior is separately reported in section 11.

| Library ID | Voice name | Library exists | HKLM64 token exists | Registration matches | Other registrations |
| --- | --- | --- | --- | --- | --- |
| `61e96426345e59d3806447bf9357b812` | Undead Male | Yes, valid machine entry | Yes | Yes, current entry and actual managed file | None in other inspected hives/views |
| `b5db3e52d26308112791a9c3e396d3d1` | Undead Female | Yes, valid machine entry | Yes | Yes, current entry and actual managed file | None in other inspected hives/views |
| None; legacy development identity | Gnome Male (Development) | Not a managed machine entry; deployed legacy file exists | Yes | Legacy registration, not a managed pair | None found |
| `a4fb27ae6305b2c563711fdb512bcbb2` | Gnome Male | No in machine library; yes in older user library | No | Correct limited check against older user library | HKCU token visible through both shared views |
| `a72d678a0f01c0c9950e9947f076c2e5` | Unknown in this snapshot | No | No | Historical candidate absent | No token with this ID in the four inspected locations |
| `3600c69b783b9cdd5109200415b7d242` | Unknown in this snapshot | No | No | Historical candidate absent | No token with this ID in the four inspected locations |

**VERIFIED:** no current machine library entry is missing its token; no managed
HKLM64 token points to a removed machine directory or an unexpected voice file.
No independently duplicated current machine token was found across views. The
legacy development token and older user token point to existing, separate files.

**UNVERIFIED:** absence of the two historical candidate IDs now does not prove
that they were the entries removed in the user's latest WPF operation, nor when
or by which tool they were removed. Similar display names do not establish
token identity. New imports allocate new IDs even when a removed source is later
reimported, as the controlled test demonstrates.

## 5. Exact Remove call-path audit

Source locations are indexed in section 13. The current production path is:

```text
MainViewModel.RemoveAsync: selected immutable LibraryId/name/hash/health
  -> ImportDialogService.ConfirmRemovalAsync
  -> ElevatedVoiceImportService.ImportAsync
  -> version-3 framed request over authenticated local named pipe
  -> OutLoudVoiceImportHelper: decode_request
  -> mutation::import_and_install(Operation::remove)
  -> VoiceLibrary::remove_voice(id, options.remove_registration)
  -> SapiVoiceManager::remove(the SAME id, machine)
  -> WindowsRegistry::exchange: transactional HKLM Registry64 deletion
  -> helper result -> ImportProtocol.DecodeResult -> fresh backend snapshot
```

**VERIFIED identity and scope:** the WPF request includes the selected library
ID, display name, expected OLV hash and health. It has no source path, arbitrary
root, hive or registry-view input. Native decoding requires a 32-character
lowercase hexadecimal ID. The helper constructs the default ProgramData library
and production registry implementation. Token key calculation is exactly
`OutLoud_Pocket_` plus that ID. Registry opens/deletes use `KEY_WOW64_64KEY` and
machine scope resolves to HKLM. Display name is not a deletion selector.

**VERIFIED coordination:** `VoiceLibrary::remove_voice` rechecks the confirmed
identity/content/health and preflights files, including in-use and security
checks. It detaches the selected entry to a unique trash directory, revalidates
the files, then invokes the supplied registry callback. That callback calls
`manager.remove(request.target_id, SapiVoiceScope::machine)`; it is not omitted
from the production WPF path.

**VERIFIED token checks:** removal requires matching managed owner marker,
`LibraryId`, CLSID and the expected strict token shape. Conflicting ownership,
unexpected fields/subkeys or wrong value types cause refusal. An already missing
token yields `token_not_found`; the coordinating callback catches **only** that
case and proceeds because there is no registration left to remove.

**VERIFIED registry execution/errors:** `WindowsRegistry::exchange` snapshots
inside a registry transaction, compares with the expected token tree, deletes
the owned `Attributes` and token keys with `RegDeleteKeyTransactedW`, then checks
`CommitTransaction`. Access, I/O, conflict or commit errors are propagated;
there is no silent nontransactional fallback. Removal does not unregister the
shared COM engine or query runtime assets.

Failure outcomes:

| Condition | Library/token state and reported outcome |
| --- | --- |
| Preflight/in-use/stale confirmation failure | **VERIFIED:** no token deletion; operation fails. |
| Registry read/delete/commit failure, directory restoration succeeds | **VERIFIED:** active library entry restored; owned token remains; original structured failure returned. |
| Registry failure plus directory restoration failure | **VERIFIED:** entry remains detached in trash and registration may remain; `removal_partial` maps to `removal_warning`, not complete success. Existing injected regression covers this case. |
| Successful registry removal, physical trash cleanup fails | **VERIFIED:** active library entry and owned token are absent; `cleanup_pending` maps to `removal_warning`. |
| Token already absent | **VERIFIED:** coordinated library removal can complete successfully. |

Therefore a directory can disappear from the active list while its token remains
in a reported **partial removal**; the code does not present that exception as
complete success. Current `.trash`/token state contains no such machine orphan.

**VERIFIED:** after any confirmed removal result, WPF requests a fresh snapshot,
including failures/conflicts. Selection is resolved by ID, not name. If snapshot
refresh itself fails, the ViewModel preserves the last good list and exposes a
refresh error; the retained list is not proof of current state.

**VERIFIED distinctions:** `VoiceLibrary::remove_voice(id)` without a callback
is a library-only API, and `outloud-voice-library remove` uses it. That standalone
command can leave a token; it is not the WPF production coordination path.
Conversely, `outloud-sapi-voices remove` removes only a token. WPF does not offer
removal of an orphan that has no selectable library row, or removal of the
legacy development token. Machine removal also does not clean older HKCU state.

## 6. Exact Import call-path audit

```text
MainViewModel.ImportAsync -> import file/name dialog
  -> ElevatedVoiceImportService.ImportAsync -> framed request
  -> helper decode_request -> mutation::import_and_install(Operation::import)
  -> VoiceLibrary::import_pocket_voice -> importer -> staged OLV + metadata
  -> publish new immutable LibraryId
  -> resolve_machine_runtime from HKLM64 COM registration
  -> SapiVoiceManager::install(that published LibraryId, machine)
  -> WindowsRegistry::exchange: transactional HKLM64 token creation
  -> result -> WPF notification and fresh backend snapshot
```

**VERIFIED:** `Stage` allocates a new 128-bit random lowercase-hex ID using
`BCryptGenRandom`. It checks collisions against staging/current entries. The ID
is written into metadata, determines the published directory, becomes
`result.library_id`, and is passed unchanged to SAPI installation. It is not
derived from a display name or source filename.

**VERIFIED:** the native importer validates the prepared Pocket voice and uses
the declared pinned model compatibility. Duplicate OLV content is rejected by
the import coordinator while an identical active entry exists. Reimport after
removal creates a new ID. If the UI finds an existing matching display name,
it asks for replacement instead; replacement retains the selected ID and token.
That route is separate from importing into an emptied library.

**VERIFIED:** `resolve_machine_runtime` reads the fixed HKLM Registry64 COM
server registration without HKCU/HKCR fallback. It derives deployment root,
model and tokenizer paths from the registered `bin/OutLoudPocketSAPI.dll` layout.
`SapiVoiceManager::install` validates production machine storage, healthy voice,
runtime files, OLV/model compatibility, tokenizer and registered DLL identity.
Its desired token uses the published managed voice path, fixed engine CLSID,
owner marker, ID, display fields, English language and vendor, plus generation
defaults. It refuses to overwrite a different existing token.

**VERIFIED truthful partial outcomes:** after library publication the result is
set to `partial`. Only successful SAPI installation changes it to `success`.
If runtime resolution or registry access/creation/commit fails, the imported
entry is kept and returned with its ID and structured error. WPF maps that to a
warning: the voice was imported but SAPI registration failed. A complete success
is not assigned merely because the library importer succeeded.

**VERIFIED transport behavior:** outcome types and operation identity are
validated. Transport failure after submission returns `outcome_unknown` and
requests reconciliation through refresh; it does not fabricate success. UAC
cancellation before submission is cancellation. No helper/UAC was invoked in
this audit; these branches were inspected and exercised through fake launchers.

## 7. UI status accuracy and limitations

**VERIFIED:** the read-only native bridge constructs
`SapiVoiceManager(library, {}, registry, production)` with **no intended runtime
configuration**, then inspects HKLM64 for each current library row. It does not
enumerate all registry tokens or scan the older user library. The managed WPF
model copies and displays the backend's `sapi_status`; it does not replace the
result with a name-only guess.

For a `correct` result this inspection verifies:

- The exact current library ID's HKLM64 token exists and is readable.
- The managed marker, LibraryId, CLSID and strict REG_SZ tree shape match.
- The library entry is healthy and uses production machine storage.
- VoicePath matches the actual managed library location.
- Name/description/English Attributes and fixed generation fields match.

**VERIFIED limitation:** with no runtime supplied, expected `AssetRoot`,
`ModelPath` and `TokenizerPath` are copied from the actual token before equality
comparison. The backend explicitly returns this detail:

```text
Library identity/path/name checked; shared runtime configuration was not supplied.
```

Thus `correct` is **not** full runtime validation: it does not establish model
file availability/current identity, tokenizer availability, effective COM DLL,
dependency loading, synthesis/playback, default SAPI enumeration or WoW
enumeration. Current paths and engine identity were independently read/checked
for this audit. A token pointing to invalid shared runtime paths could still
pass this limited status if its other fields/library entry match.

**VERIFIED:** a missing selected HKLM token returns `missing`, not `correct`.
An absent library paired with an owned token returns `orphan` when that ID is
inspected; wrong VoicePath returns `wrong_path`; ownership mismatch returns
`conflict`. Registry access errors become structured per-row SAPI errors,
without inventing a successful status. The bridge checks these cases in its
existing regressions.

**VERIFIED:** Voice Tester selects an exact full HKLM token with
`SpObjectToken.SetId(..., create=false)` and rechecks ownership/voice path before
assigning it to SpVoice. Successful direct-token playback can prove more about
that token's runtime, but does not prove that another process enumerates it or
refreshes its list. No playback was performed in this audit.

**INFERRED UX issue:** the terse green `correct` badge can be interpreted as
“fully installed and visible in every host” although the backend's actual
contract is narrower. Exposing its scope and diagnostic limit more prominently
would reduce confusion without changing result semantics.

## 8. Native Windows SAPI5 enumeration

**VERIFIED:** the existing `test-managed-voices.ps1`, without `-Speak`, found
both exact current machine IDs in default x64 `SpVoice.GetVoices()`. A fresh
read-only native C++ diagnostic independently used
`ISpObjectTokenCategory::SetId(SPCAT_VOICES, FALSE)` and `EnumTokens`, then read
each token ID/default description. It returned the same four voices. It did
not select the Pocket engine or call Speak.

Exact enumerated IDs and descriptions:

| Full native token ID | Display name |
| --- | --- |
| `HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Speech\Voices\Tokens\TTS_MS_EN-US_ZIRA_11.0` | Microsoft Zira Desktop - English (United States) |
| `HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Speech\Voices\Tokens\OutLoud_Pocket_b5db3e52d26308112791a9c3e396d3d1` | Out Loud - Undead Female |
| `HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Speech\Voices\Tokens\OutLoud_Pocket_GnomeMale_Development` | Out Loud - Gnome Male (Development) |
| `HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Speech\Voices\Tokens\OutLoud_Pocket_61e96426345e59d3806447bf9357b812` | Out Loud - Undead Male |

**VERIFIED:** explicit HKCU category enumeration, also with create=false,
returned exactly:

```text
HKEY_CURRENT_USER\SOFTWARE\Microsoft\Speech\Voices\Tokens\OutLoud_Pocket_a4fb27ae6305b2c563711fdb512bcbb2
Out Loud - Gnome Male
```

This older user token was not in default enumeration in this session. Microsoft
documents both [SpVoice.GetVoices](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ms723601(v=vs.85))
and [category enumeration](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ms722584(v=vs.85));
the observations above come from the local runtime, not an assumed merging of
HKLM and HKCU.

**VERIFIED:** the current managed tokens have the expected description, English
`Language=409`, Name, Vendor and COM CLSID, and are demonstrably suitable for
this unfiltered x64 SAPI enumeration. Age/Gender/Version fields exist on the
development token and not on the managed tokens; their absence did not prevent
default enumeration. **UNVERIFIED:** any extra attribute filters imposed by the
user's particular WoW client or addon.

## 9. What can explain the WoW mismatch

**VERIFIED:** the evidence does not support “the new voices are missing from
machine SAPI registration” in the captured state. Both appear in fresh native
SAPI enumeration. The older development Gnome voice appears too because its
independent registration remains present.

**INFERRED:** a client or addon could retain a list captured before imports,
retain a previously selected voice index, use a different enumeration source,
or filter the available voices. These are hypotheses, not observed causes.
An index is not an immutable library ID; current enumeration order itself must
not be assumed to equal an earlier order. No live WoW list, client/addon
configuration or initialization log was available here.

**VERIFIED:** only the x64 Pocket server is registered; a 32-bit host would not
find these HKLM64 tokens/server in its own redirected view. **UNVERIFIED:** the
bitness and enumeration route of the exact process used in the reported failed
observation. This audit did not launch a client to establish them.

The [client-generated VoiceChat API definitions](https://github.com/Gethe/wow-ui-source/blob/live/Interface/AddOns/Blizzard_APIDocumentationGenerated/VoiceChatDocumentation.lua),
distributed through a community mirror of Blizzard's UI source, expose
`C_VoiceChat.GetTtsVoices()`. **VERIFIED source surface:** this is a place to
collect the actual client-visible list later. **UNVERIFIED:** those Lua API
definitions do not reveal the internal Windows enumeration, cache lifetime or
refresh guarantees. They cannot prove that restarting WoW is the necessary fix.

The smallest next diagnostic is to compare that actual client-visible list with
the exact native names/IDs above during a user-controlled session, noting client
start time relative to the two import timestamps. No game command, restart,
speech-setting change or addon change was executed for this audit.

## 10. Controlled regression testing

No automated mutation used real ProgramData, HKLM, HKCU or UAC. Existing tests
use private scratch libraries and injected fake registries; frontend tests use
fake services/dialogs/elevation/COM peers. Temporary audit executables were built
in ignored `generated/registry-audit`, linked to existing backend libraries and
current coordination source. No production SAPI DLL was rebuilt or deployed.

### Import/remove/reimport cycle

**VERIFIED:** an audit-only harness reused the existing `FakeRegistry`,
`Scratch`, assertion helpers and the production
`mutation::import_and_install` function. It compiled current mutation/VoiceLibrary
code with the existing test seams. The library was a fresh child of the checked
unique scratch directory; registry mutation calls used only the in-memory map
and asserted machine scope. The resolver supplied workspace runtime files,
with the existing explicit development-library policy. No runtime inference
or production helper execution occurred.

Sequence and result:

| Step | Verified outcome |
| --- | --- |
| Import A from Gnome source; B from Undead Male source | Both complete `success`; healthy entries and matching machine-scope fake tokens. |
| Remove A and B through the production coordinator | Both return `removed`; library empty and both managed tokens absent. |
| Import C from Undead Female source; D from the formerly removed Gnome source | Two different voice contents with new immutable IDs; reimporting a removed source does not reuse its old ID. |
| Check registry after the second pair | Only the second pair's managed IDs remain; both old IDs absent. |
| Check unrelated and development tokens | Their in-memory trees remain byte-for-byte unchanged. |
| Inject registry transaction failure during removal | `failed` with `registry_io`; active library entry restored and token unchanged. |
| Inject registry transaction failure during import | `partial` with `registry_io`; healthy imported entry retained, no token and no complete success. |
| Test equivalent slash spellings in an in-memory token | Raw helper-style runtime reports `correct`; normalized CLI-style runtime reports `conflict`; runtime-unspecified inspection reports `correct`. |

The cycle passed **72 assertions**. IDs allocated in this isolated test only:

```text
Old A: 07ebee881a20939c29692a34d2873221
Old B: 9b7ef9ca9801ecaee7066ba0dea64cad
New C: a3ba96bbc08699abb0ca66ae4c8f9108
New D: e2815885a264123cd8a593ee9baff5af
```

C and D are distinct source profiles; D intentionally reimports A's removed
source to exercise immutable identity rather than merely a different filename.
The four IDs above are not production registrations.

### Existing regressions

**VERIFIED:** the focused existing CTest selection passed **4/4**, total
17.56 seconds:

- `voice-import-regressions`
- `voice-library-regressions`
- `voice-manager-bridge-regressions`
- `sapi-token-regressions`

The existing removal tests cover in-use voices, changed name/hash/health,
registry read and transaction failure, ownership conflict, malformed token,
library detach failure, trash cleanup failure, directory restoration failure,
already-missing token, damaged metadata and protocol identity. In particular,
the restoration-failure case verifies the remaining token plus detached library
files and `removal_partial`; the UI tests verify the warning text and refreshed
empty list instead of a success notification.

The existing .NET test executable passed **155 frontend assertions**, including
request identity, protocol outcome/severity, fake helper transport, refresh,
selection and removal partial/error presentation. No real elevation, SAPI
playback or production native read occurred in those frontend tests.

Commands used for the focused existing checks:

```powershell
ctest --test-dir build -C Release --output-on-failure -R '^(voice-import-regressions|voice-library-regressions|voice-manager-bridge-regressions|sapi-token-regressions)$'
dotnet run --project ui/OutLoud.VoiceManager.Tests/OutLoud.VoiceManager.Tests.csproj -c Release -p:Platform=x64 --no-build
```

Audit-only sources/artifacts retained in ignored `generated/registry-audit`:
`CMakeLists.txt`, `audit_cycle.cpp`, `enumerate.cpp`, and their isolated Release
x64 build. Existing production tests were not modified, weakened or replaced.
No new CTest or performance suite was added. Model-math/synthesis tests were not
needed for this registry/coordination audit and were not rerun.

## 11. Confirmed bugs and recommended fixes, not implemented

### Confirmed: equivalent runtime path strings produce a false conflict

**VERIFIED system reproduction:** this read-only command reports both current
rows as `correct` with the limited diagnostic:

```powershell
.\build\Release\outloud-sapi-voices.exe reconcile --scope machine
```

This read-only command reports both as `conflict`:

```powershell
.\build\Release\outloud-sapi-voices.exe reconcile --scope machine --runtime-root 'C:\OutLoud\OutLoud-Pocket-development-01'
```

**VERIFIED code/test cause:** helper `runtime_from_registration` constructs
`root / L"runtime/pocket-english.olp"` and the tokenizer equivalent without
normalizing their stored spelling. `tools/sapi-voices/main.cpp` applies
`lexically_normal()` to its constructed runtime paths. `desired()` serializes
the path spellings into REG_SZ values; `inspect()` then compares the complete
tree by raw equality. The isolated test reproduced the disagreement for the
same normalized filesystem paths and otherwise matching token fields.

**Impact:** misleading runtime-aware CLI conflict diagnostics for these
helper-created tokens, and potentially conflict/refusal when another caller
expects byte-different equivalent path strings. This does not affect the
demonstrated default enumeration and does not explain missing new token keys.
No import/remove synchronization failure was reproduced in the WPF coordination
path, which uses its own consistent runtime construction.

**Recommended targeted code correction, deferred:** align runtime path
construction and make diagnostic comparisons of the known path fields account
for normalized equivalent local paths. Preserve strict REG_SZ/schema/ownership,
allowed-root and reparse-point protections; do not solve this by ignoring
unexpected fields, accepting arbitrary paths or blindly rewriting existing
tokens. Any future normalization change must handle already stored helper-style
paths, and should include the small regression reproduced here. No live registry
repair is needed to make the currently enumerated voices appear in fresh SAPI.

### Status scope and visibility are limited by design

**VERIFIED:** current `correct` is an identity/path/name diagnostic, not a full
runtime or host-visibility certification. The bridge preserves that explanation.
It reads only library rows, so independent legacy/user entries and machine
orphans without library rows are outside the UI list.

**Recommended UX correction, deferred:** expose “machine HKLM64 token matches
library identity/path/name” and its runtime-validation limit more clearly, for
example through the existing details/tooltip. A separate read-only registry
inventory can distinguish legacy, user and orphan entries if later requested.
Do not silently merge scopes or make “Remove all library voices” delete all
Out Loud registrations across the machine/user hives.

### No confirmed production coordination defect

**VERIFIED within tested scope:** the production coordinator uses the same ID for
library publication/registration and for removal; machine Registry64 scope is
explicit; ordinary failures are truthful; partial removal is surfaced. The
current state is consistent with that implementation. **UNVERIFIED:** the
outcomes and exact binaries used in the user's prior operation; this is not a
retrospective proof that every historical removal succeeded.

The smallest safe immediate action is **diagnostic**, not deletion/reinstallation:
retain the current good registrations, preserve this inventory and collect the
client-visible voice list during the user's next WoW session. A later explicit
legacy/user cleanup would need a precise target and separate authorization; it
is not an automatic consistency repair.

## 12. Explicit answers to the seven requested questions

1. **Did Remove leave managed SAPI tokens behind?** **VERIFIED current state:**
   no orphaned managed HKLM64 tokens remain. **UNVERIFIED history:** the latest
   exact remove sequence cannot be proven without prior IDs/results/snapshots.
   The legacy development and older user tokens remain outside machine removal
   scope. The fake production-path cycle removed both selected tokens correctly.
2. **Did Import create the correct new machine tokens?** **VERIFIED current
   state:** both new IDs have correctly owned HKLM64 tokens pointing to their
   actual current files, and native SAPI enumerates them. Their metadata dates
   the imports. The exact caller/history is unverified, but production-path
   import/registration coordination passed the controlled test.
3. **Are visible registrations old, new, or mixed?** **VERIFIED:** mixed. HKLM64
   contains the two current Undead voices plus the older development Gnome;
   HKCU separately retains the older user Gnome.
4. **Why does Voice Manager report `correct`?** **VERIFIED:** its limited
   identity/path/name check genuinely passes for each current machine row. It
   substitutes actual shared runtime fields when no intended runtime is supplied;
   it does not certify playback or WoW visibility.
5. **Why might WoW not enumerate the expected voices?** **UNVERIFIED actual
   cause:** fresh native SAPI does enumerate them. Cached client/addon state,
   different enumeration/filtering or an older observation are possible
   explanations. There is no current client-visible list to distinguish them.
6. **Bug or token-identity misunderstanding?** **VERIFIED:** no import/remove
   coordination defect was reproduced; a separate path-spelling diagnostic bug
   was confirmed. **INFERRED:** treating old development/user Gnome registrations
   as removed machine IDs can explain the apparent inconsistency. This cannot
   establish what the user actually saw in WoW.
7. **Smallest safe correction?** **VERIFIED need:** no current machine-token
   cleanup is justified. First collect the client's list; later correct the
   diagnostic path comparison and clarify status scope. No fix was implemented.

## 13. Source references and audit boundaries

| Source | Exact inspected responsibility |
| --- | --- |
| [MainViewModel.cs](../ui/OutLoud.VoiceManager/ViewModels/MainViewModel.cs) | `ReadSnapshotAsync`, line 89; `ImportAsync`, 127; `RemoveAsync`, 157: selected identity, confirmation, outcomes and refresh. |
| [ImportDialogService.cs](../ui/OutLoud.VoiceManager/Services/ImportDialogService.cs) | `ConfirmRemovalAsync`, 32: selected record shown in confirmation. |
| [ImportProtocol.cs](../ui/OutLoud.VoiceManager/Services/ImportProtocol.cs) | `EncodeRequest`, 16; `DecodeResult`, 53–73: version-3 operation/ID and truthful success/partial/error mapping. |
| [ElevatedVoiceImportService.cs](../ui/OutLoud.VoiceManager/Services/ElevatedVoiceImportService.cs) | `LaunchAsync`, 28; `ImportAsync`, 40: one-shot runas transport, process authentication and outcome uncertainty. Inspected, never invoked against the real helper. |
| [helper.cpp](../frontend/mutation/helper.cpp) | `wWinMain`: authenticates parent/session/pipe, decodes request, calls production default library/registry coordinator and returns its encoded result. |
| [mutation.cpp](../frontend/mutation/mutation.cpp) | `decode_request`, 48; `runtime_from_registration`, 83; `resolve_machine_runtime`, 94; `import_and_install`, 120; removal callback, 130; partial-before-install, 155–158. |
| [mutation.hpp](../frontend/mutation/mutation.hpp) | `Operation`, `Outcome`, request fields and private injected coordination seam. |
| [voice_library.cpp](../engine/src/voice_library.cpp) | `Session`, 172; `inspect`, 200; `Stage`, 295; metadata/publish, 338–367; `list_voices`, 409; `import_pocket_voice`, 419; coordinated `remove_voice`, 508–588: health, IDs, staging and rollback/cleanup. |
| [voice_library_fs.hpp](../engine/src/voice_library_fs.hpp) | `random_id`, 164: BCrypt-generated 128-bit ID; safe local paths/handles. |
| [voice_tokens.cpp](../sapi/src/voice_tokens.cpp) | `owned`, 25; `shape`, 29; `desired`, 111; `validate_runtime`, 123; `inspect`, 142; runtime-unspecified branch, 163–165; `list/reconcile`, 183/200; `install`, 209; `remove`, 235. |
| [voice_registry_location.hpp](../sapi/src/voice_registry_location.hpp) | Explicit HKLM/HKCU and Registry64 scope, no fallback. |
| [voice_registry.cpp](../sapi/src/voice_registry.cpp) | `open`, 29 onward; `WindowsRegistry::verify_engine`, 141; `exchange`, 160–176; transaction commit, 27: real view, transactional deletion and propagated errors. |
| [identity.hpp](../sapi/src/identity.hpp) | Fixed Pocket CLSID and separate development token identity. |
| [tools/sapi-voices/main.cpp](../tools/sapi-voices/main.cpp) | Read-only list/reconcile/inspect and engine verification; normalized runtime path construction causing diagnostic discrepancy. |
| [frontend/native/voice_manager.cpp](../frontend/native/voice_manager.cpp) | Snapshot factory and library-row inspection with empty runtime; immutable result storage and structured row failures. |
| [NativeVoiceManagerService.cs](../ui/OutLoud.VoiceManager/Services/NativeVoiceManagerService.cs), [NativeVoiceManager.cs](../ui/OutLoud.VoiceManager/NativeInterop/NativeVoiceManager.cs) | WPF service/bridge call and copying native metadata/status into managed records. |
| [VoiceSnapshot.cs](../ui/OutLoud.VoiceManager/Models/VoiceSnapshot.cs), [VoiceImport.cs](../ui/OutLoud.VoiceManager/Models/VoiceImport.cs) | Direct status/detail display and warning/error/removal result messages. |
| [SapiTestSessionFactory.cs](../ui/OutLoud.VoiceManager/Services/SapiTestSessionFactory.cs) | `Initialize`, 44: exact token SetId/create=false and ownership/path checks; playback path not exercised here. |
| [test-managed-voices.ps1](../sapi/scripts/test-managed-voices.ps1) | Actual default/explicit category enumeration and exact managed ID checks; run without `-Speak`. |
| [frontend/native/tests.cpp](../frontend/native/tests.cpp) | Existing fake registry, production coordination/bridge regression entry point and read-only bridge assertions. |
| [remove_tests.hpp](../frontend/mutation/remove_tests.hpp), [tests.hpp](../frontend/mutation/tests.hpp) | Injected remove rollback/partial/cleanup and import partial/transport cases. |
| [scratch.hpp](../sapi/tests/scratch.hpp) | Existing unique temporary-library containment and cleanup, used only for test-owned scratch. |
| [voice_tokens_test.cpp](../sapi/tests/voice_tokens_test.cpp) | Machine/user separation, legacy preservation, malformed/ownership/registry failures. |
| [ImportTests.cs](../ui/OutLoud.VoiceManager.Tests/ImportTests.cs) | Selected immutable-ID removal, helper transport, partial/error notification and refresh tests with fake peers. |

Read-only access was sufficient; no elevation was requested. The inspected
current source and executed tests do not identify which helper executable was
used for past operations, and no game-process cache or prior registry snapshot
was available. Those are evidence limits, not missing permission to repair.

**VERIFIED final checks:** at 12:10:47 UTC the same two machine entries retained
their original directory timestamps and OLV hashes; staging/trash remained
empty and the installed engine DLL hash was unchanged. Report metadata and local
source links were checked, and Git whitespace checks found no errors.

The only new tracked-content candidate is this report. The pre-existing
untracked `docs/POCKET_PROSODY_AUDIT.md` was preserved. Audit diagnostics and their
build outputs are ignored workspace artifacts. No production code/tests,
ProgramData files, real registry registrations, installed DLLs, certificates or
speech settings were changed. Nothing was staged, committed or pushed.
