# Out Loud voice library backend (Steps 3B and 3C.1)

Step 3C adds a separate [managed SAPI token layer](MULTI_VOICE_SAPI.md).
Imports and library operations remain independent of Windows registration.
Step 3C.1 changes production storage to ProgramData with administrator-only
modification and ordinary-user reads. See the
[machine architecture and manual validation package](MACHINE_VOICE_LIBRARY.md).
LocalAppData remains explicit legacy/development storage; no migration occurs.

`outloud_voice_library` is a reusable native C++20 Windows x64 library for
managed voices. It provides list/inspect, Pocket import, `.olv` import/export,
display rename, removal and explicit pending-work cleanup. It uses the
[Step 3A importer](VOICE_IMPORT.md) and existing production voice validation.
No frontend, SAPI integration, registration, installer or runtime voice
selection is introduced. `.olv` v1 and neural synthesis are unchanged.

## Audit and library location

The development voice is `generated/assets/gnome_male.olv`. Native canonical
checkers/synthesis tools select that filename in their configured asset
directory. `sapi/src/adapter.cpp::read_configuration` resolves required
`VoicePath` from the SAPI token: an absolute path stays absolute, otherwise it
is relative to `AssetRoot`. The root defaults to the DLL directory's parent;
model/tokenizer defaults are under `runtime/`. Resident runtime ownership is
per configured engine, with one selected voice, not a managed library lookup.

`sapi/scripts/prepare-deployment.ps1` accepts one input voice and always copies
it to `voices/gnome_male.olv`. `sapi/deployment-inventory.json` and
`deployment-common.ps1` require that single asset path. `register.ps1` defaults
to that path while allowing an explicitly selected `VoicePath`. These bundle
and canonical-test assumptions remain intact. No automatic Gnome migration:
the known-good development/deployment voice remains outside managed storage
unless the caller explicitly imports a copy.

There was no existing C++ per-user library/config/known-folder abstraction.
Step 3A offers read-only mappings, hashes and local import paths. The existing
`deployment.json` manifest describes a deployment/signing bundle; it is not
user voice metadata and is not reused or modified here.

The production default is obtained with
`SHGetKnownFolderPath(FOLDERID_ProgramData, KF_FLAG_DONT_VERIFY)`:

```text
%ProgramData%\OutLoud\VoiceLibrary\
    .library.lock
    voices\
        <library-id>\
            voice.olv
            metadata.json
    .staging\
        <operation-id>\       # unpublished work, never enumerated as installed
    .trash\
        <operation-id>\       # committed removal awaiting physical cleanup
```

Constructing `VoiceLibrary` creates nothing. Listing a nonexistent library is
empty and creates nothing. Import creates the managed directories lazily;
operations on an existing library use its lock file. An explicit root override
supports isolated tests or a caller-selected local location. It must be
separate from source repositories and registered deployments. The backend
rejects drive roots, `X:\OutLoud`, Windows and Program Files descendants,
network/device paths, and reparse points. Existing short-name aliases are
resolved before protected-root checks. No current deployment or registry is
queried to discover/migrate voices. All automated tests and CLI validation
used unique temporary roots, never the default library.

The normal production location is `C:\ProgramData\OutLoud\VoiceLibrary`;
the drive/path is resolved by Windows, never hardcoded. Production status is
determined from the canonical root, including case/short-name equivalence.
Passing the same production path explicitly still applies machine security.
Other explicit roots remain useful for tests and the old LocalAppData library,
but do not qualify for production HKLM token installation.

Production creation/mutation requires an enabled Administrators or SYSTEM SID.
Missing `OutLoud`, `VoiceLibrary` and internal directories are created with
Administrators ownership and a protected inheritable DACL: SYSTEM/Administrators
full control, BUILTIN Users read/execute/list, no write/delete/creator-owner
grant. The two production anchors and all accessed managed directories/files
are checked. Existing unsafe ACLs return `library_security_invalid`, with no
automatic ownership/ACL rewrite. Nothing changes ProgramData itself.
Metadata and assets receive the same access contract and trusted owner at
creation (or on newly converted unpublished output); production publication
checks ownership and ACLs before committing. See the linked security audit for
why raw ProgramData inheritance is insufficient on this machine.

## Identity, duplicate and replacement policies

Library IDs are independent **128-bit cryptographically random identifiers**,
stored as 32 lowercase hexadecimal characters. They are not content hashes,
model identities or display-name-derived filenames. An ID remains stable
through rename and identifies the directory/metadata pair. Random identity
lets identical assets represent distinct user entries; a content-derived ID
would merge that distinction. Input-to-ID generation is intentionally random,
while the assigned/persisted identity is stable. Destination creation and
directory publication both check collisions and never replace an entry.

Duplicate `.olv` content means identical **whole-file SHA256**, not merely
similar audio or equal K/V tensors with different source/container identities.
The default import policy throws `duplicate_content` with matching existing
IDs. Explicit `allow_separate_entry` imports a new ID and reports the matches.
Detection validates safe, compatible managed assets, even if their display
metadata is corrupt. Missing/corrupt/unsafe assets cannot establish a verified
duplicate. No automatic merge or overwrite occurs.

Duplicate display names are allowed. IDs disambiguate them. Names are UTF-8,
trimmed at Unicode whitespace boundaries, nonempty, at most 256 Unicode
scalars/1024 bytes, without control or line-separator characters. Names are
never used as physical filenames; punctuation/path-like labels cannot change
an asset path. No case folding or Unicode normalization is imposed.

Managed imports never replace an existing entry or its content. A caller must
not pass Step 3A's `replace_existing=true` to a library Pocket import. Export
overwrites only when its separate `replace_existing` argument is explicitly
true. Rename is an explicit metadata update; remove is an explicit deletion.

## Metadata schema 1

`metadata.json` is UTF-8 JSON, deterministically serialized in a fixed field
order with a trailing newline. Exactly these eleven fields are accepted:

| Field | Purpose / contract |
| --- | --- |
| `schema_version` | 1; unknown future versions fail, no automatic migration |
| `library_id` | Must match the canonical directory ID |
| `display_name` | Editable, validated Unicode label |
| `olv_version` | Recorded container version; new imports are version 1 |
| `olv_sha256` | Exact whole-file identity for integrity and duplicate/export checks |
| `source_sha256` | Original source identity retained by the `.olv` header |
| `compatibility_id` | Declared runtime/model pairing, checked against the asset |
| `imported_utc` | UTC `YYYY-MM-DDTHH:MM:SSZ`; distinguishes imports/troubleshooting history |
| `source_filename` | Basename only, for import diagnostics; never an asset locator |
| `import_kind` | `pocket_prepared` or `olv` |
| `model_provenance_verified` | Always `false` for the currently supported inputs |

Hashes must be 64 lowercase hexadecimal digits, timestamps must be valid,
unknown/duplicate fields are rejected, and no path field is accepted. Metadata
is limited to 16 KiB. The existing bounded JSON parser is reused (depth 8,
1024 nodes, 128 object members, 32 array elements); it validates UTF-8,
escapes/surrogates and numeric fields. Its literal-token handling now retains
`true`/`false`/`null` spelling, permitting strict validation of the required
false provenance flag; Step 3A source rules remain unchanged.

Metadata never determines a filesystem location. The caller's validated ID
and fixed `voice.olv`/`metadata.json` names determine paths. A record claiming
another ID, an arbitrary asset path, or verified provenance is corrupt.

Pocket sources contain no model provenance. Their Step 3A compatibility
declaration stays a caller assertion; it is not upgraded to verification.
Standalone `.olv` likewise proves structure/integrity and declared pairing,
not the original preparing model or recording rights. Optional notes, tags,
rights acknowledgements and UI state are not added without a concrete product
contract. No library metadata is inserted into or exported with `.olv`.

## Operations and recovery

Pocket import pins the local source read-only, requires Step 3A's supported
compatibility declaration, and calls `convert_pocket_voice_to_olv` inside a
unique staging directory. `.olv` import pins and validates its source through
`validate_olv`, then copies exact bytes into staging. Both validate metadata
and the resulting production-loadable voice, compare identities, enforce the
duplicate policy, and atomically rename the entire staging directory to
`voices/<id>`. Successful publication contains both complete, consistent files.
Ordinary failures discard owned staging files; failure to clean them leaves
unpublished pending work, never a falsely installed entry.

Export requires a healthy entry, pins its source, and copies to a uniquely
created temporary file in the user-selected destination directory. The copy
is flushed, production-validated and hash-compared. The temporary's volume/
file identity is checked across validation/reopening before handle-based
finalization. Cleanup deletes only the original owned temporary identity.
No sidecar is exported. Destination must be local, end in `.olv`, have an
existing parent, and be outside this library (physical parent identities also
catch short-path aliases). Existing targets require explicit replacement;
concurrent target creation cannot defeat a non-replacing finalization. Export
leaves library metadata/voice unchanged.

Rename validates the entry and pins/rechecks its voice. It writes and validates
new metadata in staging, then atomically replaces only `metadata.json`.
Library ID, `.olv` bytes/hashes, import kind, timestamp and source filename
remain unchanged. Locked metadata causes failure while preserving the old
record. Rename/export refuse unhealthy entries rather than laundering their
state into a successful operation.

Remove accepts only a validated directory ID, including entries with corrupt
metadata/content. It pins the directory identity and preflights all files for
delete access, regular single-link type and writable attributes. Only the two
fixed managed filenames may be present; unknown files are retained for review.
A failed preflight changes nothing. Windows may deny directory rename when
child files are open, even with delete sharing, so preflight handles are
released before the rename; the directory stays pinned and the files are
revalidated after publication of the removal.

The removal commit point is an atomic directory move to `.trash/<operation-id>`.
Before that move, failure leaves the active entry intact. Afterwards the entry
is removed from enumeration; cleanup failure returns `cleanup_pending=true`
and a reason. Deletion uses handles to the revalidated files, not metadata
paths. Only managed copies are removed, never imported original sources.
Unknown, read-only or locked files are retained. No recycle-bin integration.

`cleanup_pending()` explicitly discards flat unpublished staging jobs and
finishes already committed removals. It touches only canonical operation IDs
and the two fixed expected filenames under `.staging`/`.trash`. Unsafe links,
locks, unexpected names or files are retained and returned in `pending`.
It never deletes active entries. This supports ordinary failure and abandoned
work recovery without a transaction database. There is no power-loss durability
or process-crash cleanup journal guarantee; a crash may leave staging/trash
work. Atomic visibility and successful-file flushing do not establish a
multi-operation crash transaction.

## Safety and enumeration

Each operation pins directory ancestors one component at a time with
`FILE_FLAG_OPEN_REPARSE_POINT`, checks handle types, and denies delete sharing
to prevent directory replacement while used. Regular files must have one hard
link; junctions/symlinks, alternate streams, UNC/device/mapped-network paths (including DOS device aliases),
parent traversal, trailing-dot/space aliases and malformed IDs are rejected.
There is no recursive production deletion and no metadata-derived path access.
Rename/finalization uses handle-based Windows file information operations.

A fail-fast shared `LockFileEx` allows concurrent readers; mutations and cleanup
take an exclusive lock across threads, API instances and processes. Read calls
open the existing lock with GENERIC_READ/OPEN_EXISTING; they never create/write
it. Mutations use GENERIC_READ|GENERIC_WRITE. Contention returns `library_busy`;
there is no spin/sleep loop. Source snapshots deny write/delete sharing.
File IDs protect export temporary reopening. External programs that edit the
development/legacy user's writable files without using the lock can change
health. Production's protected ACLs prevent ordinary users from doing this;
administrators can still modify files outside the cooperating library API.
Files are revalidated at operation boundaries and again after
removal commit; link/type changes are never followed for cleanup.

List/inspect validate each entry independently and scan its voice; one broken
entry does not abort enumeration. Production security-contract violations and
permission errors are structured library errors. Available health states:

| Health | Meaning |
| --- | --- |
| `valid` | Metadata, production asset and recorded identities agree |
| `missing_voice` | Metadata exists but `voice.olv` is absent |
| `metadata_corrupt` | Missing/invalid/oversized/schema-inconsistent metadata |
| `unsupported_metadata_version` | Future metadata schema; no migration attempted |
| `unsupported_voice_version` | Unsupported actual `.olv` format version |
| `corrupt_voice` | Invalid container, truncation or production validation failure |
| `incompatible_model` | Asset or metadata declares an unsupported model pairing |
| `hash_mismatch` | Valid asset differs from recorded hash/source/version/identity |
| `unsafe_entry` | Bad directory identity, file type, junction/symlink or hard link |
| `unavailable` | Entry cannot currently be accessed, e.g. locked files |

`LibraryVoice` always includes the directory identity, health and explanation.
Metadata is optional when unreadable; `managed_olv_path` is supplied only for
healthy voices. A nonexistent requested ID raises `voice_not_found`, distinct
from a broken present entry. Missing/busy/unsafe library roots are library-wide
errors, not fabricated empty lists. Listing performs full validation; a future
frontend should schedule it appropriately for the size of its library.

## Public API and errors

[voice_library.hpp](../engine/include/outloud/voice_library.hpp) provides
`default_voice_library_root()` and `VoiceLibrary(root)` with:

```cpp
library.list_voices();
library.inspect_voice(id);
library.import_pocket_voice(source, name, step3a_options, duplicate_policy);
library.import_olv(source, name, duplicate_policy);
library.export_olv(id, destination, replace_existing);
library.rename_voice(id, new_name);
library.remove_voice(id);
library.cleanup_pending();
```

Import returns the installed `LibraryVoice` and duplicate IDs. Removal and
cleanup return explicit pending-work results. Methods own operation state;
independent calls contend through the OS lock, not a global in-memory index.
`VoiceLibraryError` supplies code, message, path, system error, duplicate IDs
and an optional intact `VoiceImportError` from Step 3A. Error codes include
invalid/unavailable/busy root, unsafe path, invalid/missing ID, corrupt/future
metadata, invalid display name, duplicate content, import failure, incompatible/
corrupt voice, replacement required, source/destination IO and removal failure.
Step 3C.1 appends `library_access_denied` and `library_security_invalid`, preserving
old enum ordinals and importer/native error causes. Access/creation permission
failures are distinguished from missing storage. Windows rename can report
ACCESS_DENIED for open children; its existing source/destination/removal code
and raw Win32 cause are retained rather than claiming every error 5 is UAC.
The GUI does not need to parse tensors, metadata, filesystem layout or hashes.

## CLI and validation

CLI root override precedes the command. Without it, operations target the
default machine library; the examples deliberately use an isolated directory:

```powershell
$root = Join-Path $env:TEMP 'outloud-library-manual-example'
.\build\Release\outloud-voice-library.exe --root $root list
.\build\Release\outloud-voice-library.exe --root $root import-pocket voices\gnome_male.safetensors --name 'Gnome Male' --model-compatibility pinned
.\build\Release\outloud-voice-library.exe --root $root import-olv generated\assets\gnome_male.olv --name 'Another Gnome' --allow-duplicate
# Use a returned 32-character ID in place of <id>:
.\build\Release\outloud-voice-library.exe --root $root inspect <id>
.\build\Release\outloud-voice-library.exe --root $root rename <id> 'New Name'
.\build\Release\outloud-voice-library.exe --root $root export <id> (Join-Path $env:TEMP 'exported-gnome.olv')
# Export accepts --replace only when replacement is explicitly intended.
.\build\Release\outloud-voice-library.exe --root $root remove <id>
.\build\Release\outloud-voice-library.exe --root $root cleanup
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
git diff --check
```

Pocket import also accepts an exact compatibility ID and optional
`--expected-source-sha256`. CLI is a developer key/value presentation, not a
GUI protocol. Exit 0 means completed; backend failures are 1, usage errors 2.
Cleanup with retained pending paths exits 1. A removal committed with pending
cleanup exits 0 and reports that state explicitly.

Release x64 `/W4 /WX` build passed. **14/14 CTests** passed in **31.80 seconds**:
the 12 original tests, Step 3A importer regressions, and new library regressions.
The new suite passed **157 assertions** in this run (concurrent contention can
vary the assertion count). It uses the original Gnome source/known-good `.olv`,
requires exact managed/imported/exported bytes and production validation, and
checks rename/IDs, duplicate names/content, metadata and provenance falsification,
malformed/future/oversized metadata, missing/corrupt/future/incompatible assets,
hash mismatch, Unicode, invalid names, replacement, locks, failed-preflight
handle release, hard links, real NTFS junction fixtures, traversal, staged/
committed-removal cleanup, unknown-file retention and concurrent API instances.
Original inputs and the default machine/legacy user libraries remain untouched.

An isolated CLI list/import/rename/inspect/export/remove/cleanup round trip
also passed. The exported Gnome SHA256 remained
`ff138495b7d3834c2eabb926ff7226a2983e7af17cf5a0c6b4cad05ae6a5bb00`.
Its temporary root was cleaned. Raw CTest/CLI evidence is ignored under
`generated/perf/3b-*.txt`. No performance gate, profiler or neural optimization.

Before frontend work, decide how duplicate identity/name selection, compatibility
declarations, rights acknowledgement and retained cleanup work are presented.
User notes/tags/rights metadata need a product contract before schema changes.
Multi-voice SAPI tokens, WoW selection, hot reload, GUI, signing, installer and
deployment remain separate future steps. No Git staging/commit/push, registry
mutation, registration, deployment modification or WoW process action occurred.
Stop after Step 3B.
