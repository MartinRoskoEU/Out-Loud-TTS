# A06: Voice Manager refresh validation reuse

Date: 2026-10-09. A06 only; prior cleanup, legacy retirement and both stabilization
phases remain in place. This changes native read-only enumeration and its tests.
No A07/A08/A09 work, managed UI redesign, persistent cache or production mutation
is included.

## Original work and root cause

The current bridge called `VoiceLibrary::list_voices()` and then
`SapiVoiceManager::inspect(id)` for each returned row. `inspect` called `voice(id)`
and `VoiceLibrary::inspect_voice(id)` again. The first shared library lock ended
before the per-row SAPI inspections; list and token comparison could therefore
refer to different library mutation epochs.

For each healthy row, both passes performed:

1. Directory pin/type checks, production ACL/ownership validation and metadata
   read/strict schema parsing.
2. Managed-file checks and a preliminary OLV mapping/header-version check.
3. `validate_olv`, which maps the input, constructs `OutLoudVoice` with another
   mapping, checks structure, container and payload integrity, scans every FP32
   value for finiteness and calculates the full-file SHA-256.
4. Metadata/header identity/hash/compatibility comparison.
5. Replacement-pending and removal-pending discovery, each scanning its pending
   directory and reparsing journals per row.

The second pass repeated authoritative library work within the same logical
refresh instead of comparing the token against the first validated record.
SAPI also returned a copied optional `LibraryVoice` for that second inspection.
Those repeats were unnecessary if one library read epoch remained valid.

An initial validation is still necessary on every refresh. Validation after a
mutation and fresh checks at Import/Replace/Remove/install/update boundaries are
also necessary. They are not replaced by frontend snapshot trust.

## Implementation and snapshot lifetime

`SapiVoiceManager::read_snapshot()` now obtains all voices through the private
`VoiceLibrary::with_read_snapshot` friend boundary. That boundary creates one
shared `Session`, enumerates/validates rows, and keeps its lock while SAPI reads
each exact managed token and compares it against the const validated record.
Only this internally created row can reach the private `inspect_impl` reuse path;
there is no public overload accepting an arbitrary purportedly validated row.

The callback cannot escape the validation lock scope. On return, records are
display results rather than trusted capabilities. No lock, validation token,
mapping or cache survives in the WPF result. A later read starts a new Session
and validates again. The elevated helper accepts the same ID/name/hash/health
assertions and performs its unchanged fresh authoritative checks.

Within enumeration, `pending_index` enumerates staging/trash once and parses each
discovered journal once. It builds temporary ID sets, checks the existing strict
journal formats, validates relevant replacement commit markers and refuses
duplicate removal journals for an active ID. Existing root/directory pins and
production ACL checks apply. Unknown/corrupt evidence is not ignored or treated
as a successful recovery. Empty libraries with no valid active IDs do not need
pending scans. This discovery never writes or performs recovery.

Rows retain sorted directory-ID order and the existing health, token-status,
name/path comparison and diagnostics code. Removal-pending takes precedence over
replacement-pending as before. `inspect(id)` continues using fresh library
inspection; CLI reconciliation/orphan inspection and privileged mutations are
not redirected to a cached row.

Validated voice records and raw token trees move into snapshot/bridge storage
rather than duplicating voice metadata through a second inspection. Temporary
vectors reserve row capacity. The pending ID sets introduce short-lived control
allocations proportional to enumerated IDs; there is no payload copy, permanent
validation index or new allocation framework. SAPI error values are copied once
into their per-row result before the existing bridge error-field conversion.
C ABI buffers and managed string copying remain unchanged.

## Consistency, health and security

Supported import, replacement, removal, rename and recovery require the exclusive
library lock. They cannot publish changes between library validation and token
comparison in the new read epoch; overlapping operations retain the existing
nonblocking `library_busy` result. Existing writers similarly prevent refresh
from acquiring a read epoch. Locks end after native token comparison, before ABI
packing and managed processing. Registry access therefore extends the existing
read-lock duration, but OLV validation is performed only once per row.

Each token is still read freshly. A foreign or changed token produces its current
conflict/status; registry failures retain per-row structured errors, while
unexpected exceptions keep the bridge's existing sanitized failure boundary.
There is no atomic snapshot of all Windows registry tokens, no guarantee that a
token remains unchanged after its read, and no certification of playback or WoW
visibility. Direct external administrator edits do not honor the library lock;
they remain outside the supported mutation consistency guarantee.

Preserved checks include ProgramData root resolution/authority, ACL/ownership,
local-path and ancestor pins, reparse/junction/hard-link/type checks, immutable
IDs, metadata schema, OLV structure/integrity/finite values, metadata/header
identity, strict REG_SZ/token shape/ownership, exact display names and A05 runtime
path equivalence. A02 removal and existing replacement recovery semantics remain
in place. Missing, corrupt, inaccessible and pending entries are not promoted to
healthy/registered results by validation reuse.

## Demonstrated work reduction

The existing private checker compilation supplies counters; production Release
uses empty inline event functions with no counter state or legacy path. A
test-only switch reproduces the exact previous per-row enumeration, and the
baseline calls the original bridge composition (`list_voices` plus `inspect`
per ID). Both paths use the same three healthy OLV records and fake owned tokens.
Setup, imports and token creation are outside the counted work.

| Operation, one three-voice refresh | Before | After |
| --- | ---: | ---: |
| Metadata read/schema-validation attempts | 6 | 3 |
| Preliminary library OLV mapping attempts | 6 | 3 |
| Full `validate_olv` passes from library inspection | 6 | 3 |
| Replacement directory scans | 6 | 1 |
| Removal directory scans | 6 | 1 |
| Replacement journal parses, with one pending replacement | 6 | 1 |
| Removal journal parses, with one pending removal | 6 | 1 |
| Fresh token reads | 3 | 3 |

The first three counts establish **50% less duplicate library validation**.
Pending scans/parses fall by **83.3%** for this three-row fixture. Generally,
healthy validation drops from `2N` passes to `N`; each pending directory scan
drops from `2N` to one per nonempty enumeration. Journal parsing no longer scales
with both voice count and journal count.

The unchanged full-validator source performs three OLV mappings per healthy
inspection (including the preliminary map), three SHA-256 passes (container,
payload, full-file) and one finite-value scan. Thus total mappings and those
hash/finite passes also halve for healthy records. These internal counts are
**derived from the unchanged validator implementation**, not separate low-level
counter measurements. Corrupt/header/metadata failures can short-circuit work;
the healthy formula is not claimed for every malformed row.

No latency timing was collected. Less hashing/mapping/parsing is demonstrated;
a wall-clock refresh speedup, allocation-byte reduction and speech-generation
improvement are not claimed. Benefits depend on voice sizes, pending contents,
OS cache and registry access time.

## Files and functions changed

| File | A06 responsibility |
| --- | --- |
| `engine/include/outloud/voice_library.hpp` | Private SAPI friend callback for one locked read epoch. |
| `engine/src/voice_library.cpp` | Locked snapshot consumption and test-counter sites; enumeration body relocated to the private snapshot include. |
| `engine/src/voice_library_snapshot.inc` (new) | Single pending index and enumeration with original row classifications/order. |
| `engine/src/voice_library_replace.inc`, `engine/src/voice_library_remove.inc` | Count existing discovery/journal work only; mutation/recovery decisions unchanged. |
| `engine/src/voice_refresh_audit.hpp` (new) | Private checker counters and legacy-enumeration switch; no production state. |
| `engine/include/outloud/sapi_voices.hpp`, `sapi/src/voice_tokens.cpp` | Snapshot row/result method and shared status comparator; fresh standalone inspection retained. |
| `frontend/native/voice_manager.cpp` | Consume the combined native snapshot; unchanged ABI packing/error conversion. |
| `frontend/native/tests.cpp`, `frontend/native/refresh_tests.hpp` (new) | Fake token-read barrier, work counts and focused consistency/health tests. |
| This report, `docs/VOICE_LIBRARY.md` | Current read-snapshot contract. |

## Focused validation

The new checker group verifies identical row order/IDs/names, every health/detail/
token status, metadata/content fields and immutable old-result storage. Three
healthy records exercise the before/after counts; every new refresh is shown to
validate again. Real prepared replacement and removal publication interruptions
create private journals. The bridge reports both pending health values, parses
their journals once, preserves the evidence byte-for-byte and does not recover
anything during read. Explicit cleanup then proves both original recovery
guarantees remain operational.

A deterministic token-read barrier runs a separate thread attempting six mutation
classes (import, replace, remove, rename, removal recovery and generic cleanup);
all must receive `library_busy` with unchanged files/tokens. An exclusive-writer
fixture verifies truthful busy refresh. A token changed at its read becomes
conflict, and a renamed library entry remains `stale_name` until explicit update.

Private corrupt JSON, missing OLV, corrupt integrity, hash-correct nonfinite FP32
cache values, exclusive file locks and hardlinked voice fixtures retain the same
old/new classifications. Install, replacement and removal reject corruption after
a previously healthy refresh with fresh validation counters. Existing coordinator
tests continue covering authoritative imports, replacement/removal and A02 cases;
existing token tests cover A05 path spelling, ownership/schema and stale names.

Managed code, ViewModel, Voice Tester rules and the native C ABI have no changes.
Selection preservation still resolves the same immutable ID using the unchanged
managed code. No frontend/WPF validation or live desktop test was required or run.

One incremental Release x64 build completed successfully, exit 0, with the
existing build tree and no warnings/errors:

```powershell
cmake --build build --config Release --parallel --target `
  OutLoudVoiceManagerNative outloud-voice-manager-tests `
  outloud-voice-library-tests outloud-sapi-voice-tests OutLoudVoiceImportHelper
```

This also rebuilt the affected `outloud_voice_library` and `outloud_sapi_voices`
libraries/dependencies. The authenticated helper was built, not executed. No
WPF build/output copy was needed for an unchanged managed/C ABI contract.

One focused CTest cycle passed **3/3**, total **25.50 seconds**:

```powershell
ctest --test-dir build -C Release --output-on-failure -R '^(voice-manager-bridge-regressions|voice-library-regressions|sapi-token-regressions)$'
```

| Executed test | Result |
| --- | --- |
| `voice-library-regressions` | PASS, 232 assertions, 1.52 s |
| `voice-manager-bridge-regressions` | PASS, 630 checks including A06 and the retained 26 A02 scenarios, 16.83 s |
| `sapi-token-regressions` | PASS, 152 assertions, 7.09 s |

The A06 group passed the exact work-count assertions above. Test durations are
not refresh benchmarks. No failed-test rebuild/rerun was necessary. Existing
export/version/null/error/result-ownership checks loaded the rebuilt bridge DLL
without invoking its production read entry point. Git whitespace checks passed.
No CMake target/CTest registration was added or changed for A06.

## Limits and deferred work

This is a fresh native read snapshot, not a persistent trusted cache or a combined
filesystem/registry transaction. Strict full pending discovery may surface corrupt
evidence that a former early per-ID match skipped; it fails closed rather than
guessing health. Files/registry edited directly outside supported locking are not
made atomic by this optimization. Slow registry reads can delay writers while the
shared lock is held.

Multiple mappings/hashes inside one full `validate_olv` call, standalone CLI
list/reconcile repeated work and ABI serialization allocations remain as before.
They require separate evidence/scope before optimization. No A07 neural workspace,
A08 mapped-asset redesign or A09 unrelated optimization was implemented. Every
refresh and every privileged mutation deliberately retains fresh validation.

No real HKLM/HKCU access, installed-voice inspection, ProgramData data mutation,
elevated helper execution, deployment/signing/certificate action, process
termination or game action occurred. Workspace build outputs were not copied to
deployed or running WPF locations. Nothing was staged, committed or pushed.
