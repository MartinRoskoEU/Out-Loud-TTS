# Stabilization Phase 2: A02 removal recovery

Date: 2026-10-09. A02 only. This extends the current ProgramData library, HKLM
Registry64 managed tokens and authenticated one-shot mutation helper. The working
tree already contained cleanup, legacy retirement and Phase 1 changes; those
changes and historical reports were preserved. No production recovery was run.

## Root cause verified in the current source

Previously, removal renamed the selected entry into trash, invoked the token
removal callback, and then deleted the files. Exceptions could restore the
directory in the same process, but termination after detach lost that rollback.
Generic cleanup accepted voice/metadata trash as disposable without evidence
that the registry operation committed. Consequently, a live token could point
at a missing directory and later cleanup could delete its only retained copy.
Replacement already had its own durable control records; removal did not.

## Journal and publication

Each removal has this bounded layout under the existing protected machine root:

```text
.trash/<random_32_lowercase_hex_transaction_id>/
  remove.json
  control.tmp       # exists only during journal publication/update
  retained/         # original voices/<library_id> directory, renamed intact
    voice.olv
    metadata.json
```

`remove.json` is strict JSON, capped at 4096 bytes with exactly these nine fields:

| Field | Verified meaning |
| --- | --- |
| `schema` | Integer 1, independent of unchanged OLV/metadata formats |
| `operation` | Exact `remove` |
| `phase` | Exact `prepared` or `committed` |
| `id` | Immutable 32-character lowercase hexadecimal library ID |
| `directory_id` | Original Windows 128-bit file ID, lowercase hexadecimal |
| `volume` | Original directory volume serial number |
| `voice` | Raw SHA-256 of original `voice.olv`, or empty if already absent |
| `metadata` | Raw SHA-256 of original `metadata.json`, or empty if already absent |
| `registration` | SHA-256 of the strict owned token tree, `absent`, or `library_only` |

No voice contents, arbitrary paths, registry keys, commands or deployment roots
are persisted. Recovery derives all paths from pinned library roots, validated
IDs and constant filenames. Raw hashes protect the original bytes even when
damaged metadata or a missing voice made the confirmed entry unhealthy; restoring
those bytes does not certify a healthy asset.

SAPI computes the token fingerprint only after checking managed owner, exact ID,
CLSID, permitted fields/subkeys and REG_SZ shape. It hashes deterministic
length-prefixed names, value types and bytes for all root and Attributes fields.
Removal checks that fingerprint again, then uses the existing transactional
snapshot compare-and-exchange. A newly created or changed token is not deleted
using stale preparation evidence. No runtime/model load is needed for recovery.

Publication reuses the existing protected control-file writer: complete write,
`FlushFileBuffers`, close, then atomic same-directory handle rename of
`control.tmp` to `remove.json`. Updates replace the prior journal atomically.
The prepared journal is published before any detach. The journal is removed
last, after restored or deleted data is settled. A leftover empty transaction
directory is harmless and can be cleaned later.

This provides recovery records for process interruption. Directory-entry
persistence across abrupt power loss has not been established or tested. There
is no combined atomic transaction across Windows filesystem and registry.

## State machine, commit point and decisions

Normal coordinated removal follows:

```text
confirmed identity + delete preflight + owned token fingerprint
  -> durable prepared journal
  -> detach original directory to retained
  -> revalidate retained identity and acquire file handles
  -> guarded registry transaction
  -> query current token and validate retained data
  -> durable committed journal
  -> delete bounded retained files/directory
  -> remove controls and transaction directory
```

The deletion commit point is **verified token absence plus validated retained
directory/file identities, followed by publication of the committed journal**.
Before that publication no retained file is deleted. Registry absence is the
relevant registry postcondition: a valid prepared journal plus unchanged retained
data and current token absence lets recovery publish the commit and finish an
operation interrupted after unregister. It does not attribute the absent token
to a particular process. A phase marker alone never proves current registry state.

| Current evidence | Recovery decision |
| --- | --- |
| Prepared; original present, retained absent; recorded token unchanged | Validate exact original directory/files, cancel preparation and remove controls. Healthy voice remains. |
| Prepared; retained present, original absent; recorded owned token unchanged | Validate full original identity/hashes, restore by handle rename with no overwrite, then remove controls. |
| Prepared; retained present, original absent; exact token absent | Validate full original identity/hashes, publish committed phase, finish deletion. Covers the unregister/phase-update interruption window. |
| Committed; original absent; exact token absent | Validate directory identity and every remaining file hash, continue bounded cleanup. Files already deleted by committed cleanup may be absent. |
| Committed; original and retained absent; token absent | Finish remaining controls; data cleanup already completed. |
| Original already restored but prepared journal remains | Verify original bytes/directory and unchanged registration again, finish control cleanup. |
| Token unreadable, malformed, foreign or fingerprint changed | Retain journal/data and report unresolved registration/conflict. No token mutation or guessed rollback. |
| Committed but a token now exists, or original and retained both exist | Retain evidence for reconciliation; no overwrite or deletion. |
| Uncommitted retained data missing/corrupt, wrong directory identity, invalid journal/schema/phase/ID/layout | Retain remaining evidence with a specific recovery problem. |
| Locked/in-use files or filesystem/security failure | Refuse unsafe work, retain pending data and report failure/warning. |
| Nonempty old trash without a removal journal | Preserve; no transaction outcome is invented. |

Thus **not committed** means a valid preparation whose unchanged token still
requires its original data; **committed** means the verified absence/identity
postcondition permits deletion and the durable marker permits interrupted cleanup;
**unknown** means required evidence cannot be established. Unknown never authorizes
discarding retained voice data. Recovery does not re-register absent tokens.

An in-process registry failure restores exact original data when safe and propagates
the original error. Failed restoration remains `removal_partial`. A post-unregister
exception that recovery resolves to committed deletion reports a warning explicitly
identifying the verified commit/cleanup outcome, rather than falsely claiming that
data remains unresolved. Successful unregister with unfinished deletion reports
the existing cleanup warning. Completed recovery removes unnecessary backups.

The standalone library-only API/CLI retains its file-only semantics. Its journal
uses `library_only`; prepared data is restored, while its explicitly published
commit allows cleanup without registry inspection. It is not a substitute for
coordinated SAPI removal and can leave a separately installed token.

## Ownership, security and invocation

The storage library owns the exclusive lock, journal, protected directories,
identity/hash checks and filesystem recovery. It accepts a native token-evidence
callback; it contains no privileged registry implementation. SAPI owns exact HKLM64
lookup, strict token ownership/fingerprints and transactions. The elevated coordinator
composes them for the same confirmed immutable ID. Helper authentication, protocol
version, operation codes, COM CLSID, managed UI and mutation outcome vocabulary
remain unchanged. The C++ library API adds recovery structures/callbacks and
appends `removal_pending` health/error values; consumers are rebuilt. The read-only
C ABI and helper wire format are unchanged.

`recover_removal(id, callback)` uses an exclusive, noncreating session. The existing
authorized Remove operation invokes it before attempting a new removal for that ID.
If recovery completes an already committed removal, it returns that outcome. If it
restores the entry, normal confirmation assertions still apply before a fresh remove.
If the UI confirmed a `removal_pending` health snapshot, restoration changes that
health; the ensuing stale-confirmation refusal requires refresh and confirmation
again rather than silently weakening the selected-entry checks.
Read-only list/inspect can expose `removal_pending` on an active entry; they never
repair it, delete files, query for recovery authority or trigger elevation. Detached
entries are absent from the active list, so explicit maintenance is available.

From an administrator shell, after reviewing the pending state, the supported
maintenance command is:

```powershell
.\build\Release\outloud-sapi-voices.exe recover-removals
```

This command was **not executed** here. It uses the fixed ProgramData library and
HKLM64 token inspector, reports recovered/pending counts plus problem codes and
paths, and returns nonzero if work remains pending. It does not elevate itself or
modify tokens. It also invokes the existing safe staging/replacement cleanup.

`cleanup_pending()` without token evidence preserves every coordinated journal,
even one marked committed. Nonempty unjournaled trash is always retained. Empty
trash directories and recognized staging/replacement work can still be cleaned.
Replacement recovery code and its original guarantees are unchanged; existing
replacement tests remain in the focused coordinator/library checks.

Existing protected ownership/ACL checks, local-root/ancestor pins, directory/file
identity checks, hard-link/reparse/junction refusal and exclusive mutation locks
remain enforced. Children are a fixed allowlist, not recursively deleted arbitrary
content. Original and retained directory coexistence is a conflict. Duplicate
journals for one ID are refused. Invalid journals encountered while scanning can
conservatively block another mutation until administrator review. No journal can
supply an external target path or registry key.

## Files and functions changed for A02

| File | Change |
| --- | --- |
| `engine/include/outloud/voice_library.hpp` | Recovery result/state/token-evidence API, guarded removal callbacks, cleanup problems and appended pending diagnostics. |
| `engine/src/voice_library_remove.inc` (new) | Strict journal publication/loading, evidence validation, `remove_prepared`, `recover_remove` and transaction discovery. |
| `engine/src/voice_library.cpp` | Journaled removal wrapper, explicit recovery, safe cleanup branches and read-only pending diagnosis. |
| `engine/src/replacement_audit.hpp` | Test-only removal interruption flag; production has no new probe. |
| `engine/include/outloud/sapi_voices.hpp`, `sapi/src/voice_tokens.cpp` | Strict owned-token fingerprint and expected-fingerprint removal guard. |
| `frontend/mutation/mutation.cpp` | Selected-ID recovery before authorized remove, token callbacks and existing warning mapping. |
| `tools/sapi-voices/main.cpp` | Explicit `recover-removals` maintenance. |
| `tools/voice-library/main.cpp` | Display structured cleanup problems; generic cleanup remains file-only. |
| `frontend/mutation/removal_recovery_tests.hpp` (new), `frontend/native/tests.cpp` | Deterministic interrupted-state tests in the existing executable. |
| `frontend/mutation/remove_tests.hpp`, `tests/voice_library_test.cpp` | Existing partial cleanup uses token evidence; old unjournaled trash is asserted retained. |
| `docs/VOICE_LIBRARY.md`, `docs/MULTI_VOICE_SAPI.md`, `docs/VOICE_MANAGER_REMOVE.md`, this report | Current recovery/maintenance contract and historical-step annotation. |

No import/replacement implementation, inference/audio path, managed UI, format,
dependency or Windows registry transaction implementation was changed by A02.

## Focused tests and executed builds

The new test group reconstructs interruptions through the existing separately
compiled audit seam. It does not terminate real processes. Private scratch
libraries and fake registry maps are used; actual file bytes, token trees and
remaining controls are checked, including repeated recovery and unrelated tokens.

**26 scenarios passed:** normal removal; journal creation failure; detach failure;
registry transaction failure; interruption after journal publication, detach,
unregister, commit and partial cleanup; resolved post-unregister exception;
unreadable registry; foreign owner; changed owned token; mapped/locked retained
voice; interruption after restore; corrupt JSON; traversal ID; arbitrary extra path;
unknown phase; wrong directory identity; missing voice; hardlinked journal; missing
journal; corrupt retained bytes; old unjournaled trash; authorized selected-ID retry.
Existing tests additionally cover active in-use voices, stale confirmations,
damaged metadata, rollback failure, partial cleanup, absent tokens and replacement
recovery/security interactions. No new CTest registration was added.

Incremental Release x64 build succeeded (exit 0), using the existing build tree:

```powershell
cmake --build build --config Release --parallel --target `
  outloud_voice_library outloud_sapi_voices `
  OutLoudVoiceImportHelper OutLoudVoiceManagerNative `
  outloud-voice-library-tests outloud-voice-manager-tests `
  outloud-sapi-voice-tests outloud-sapi-voices outloud-voice-library
```

One focused CTest selection passed **3/3**, total **23.37 seconds**:

```powershell
ctest --test-dir build -C Release --output-on-failure -R '^(voice-library-regressions|voice-manager-bridge-regressions|sapi-token-regressions)$'
```

| Executed test | Initial result |
| --- | --- |
| `voice-library-regressions` | PASS, 232 assertions, 1.43 s |
| `voice-manager-bridge-regressions` | PASS, 448 checks including 25 new recovery scenarios, 14.57 s |
| `sapi-token-regressions` | PASS, 152 assertions, 7.31 s |

Final source review found one truthful-outcome error-message edge case after
successful recovery of a post-unregister exception. That branch was corrected,
affected binaries incrementally rebuilt, and only the coordinator regression was
rerun with the additional 26th scenario: **PASS, 455 bridge checks, 26 recovery
scenarios, 14.73 s** (CTest total 14.76 s). The other two passing tests were not
repeated. Both incremental build commands completed with exit 0.

No managed code changed, so no WPF build or frontend test run was needed. No clean
rebuild, full suite, inference campaign, reference export, download, profiler,
benchmark or playback occurred. Build outputs stay in the workspace `build/Release`;
nothing was copied to the running WPF application or deployment directories.

## Limits and unchanged production state

Process-crash states are deterministic simulations, not real helper crashes or
verified abrupt power-loss tests. Registry and filesystem are separate stores;
external administrators do not share the library lock and can change registration
after inspection. A changed/reappearing token is retained for reconciliation when
observed, but this does not promise consistency under arbitrary concurrent external
registry edits. No automatic foreign-token removal or registration recreation exists.

Corrupt/missing journals and old unjournaled trash require explicit review; there
is no speculative legacy migration. A failed physical cleanup retains the journal
and whatever data remains. If the originally confirmed entry was already damaged,
exact rollback restores that original condition. An unresolved state may remain
unselectable because the active directory is detached; the maintenance command
provides the explicit recovery route without adding a new UI or helper opcode.

Real HKLM/HKCU, ProgramData library data, production voices, deployed binaries,
certificates and speech settings were untouched. No real elevated helper,
installation, deployment, registry repair, signing or game action was performed.
No process was terminated. Nothing was staged, committed or pushed. Prior working
tree changes were retained.
