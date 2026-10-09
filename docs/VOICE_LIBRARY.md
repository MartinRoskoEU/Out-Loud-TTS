# Voice Library backend

The supported production library is `%ProgramData%\OutLoud\VoiceLibrary`.
[Machine-wide security](MACHINE_VOICE_LIBRARY.md) and
[managed SAPI registration](MULTI_VOICE_SAPI.md) describe the product boundaries.
Prior format/design/validation evidence is [archived](VOICE_LIBRARY_HISTORY.md).
The old alternate-root CLI instructions in that record are retired.

`outloud_voice_library` provides list/inspect, prepared Pocket import, OLV
import/export, display rename, replacement, coordinated removal and pending-work
cleanup. It uses the existing [voice importer](VOICE_IMPORT.md), strict asset
validation and metadata schema 1. Native import preserves the OLP/OLV contract;
it does not implement a separate inference path.

## Storage and identity

```text
%ProgramData%\OutLoud\VoiceLibrary\
  .library.lock
  voices\<library_id>\voice.olv
  voices\<library_id>\metadata.json
  .staging\<operation_id>\
  .trash\<operation_id>\
```

The Windows known-folder resolver supplies the root. Reads create nothing;
mutations require enabled machine authority and preserve protected ownership/ACLs.
Files and ancestors are pinned and checked for local path/type/identity and
reparse/hard-link violations. Metadata cannot redirect file access.

Library IDs are immutable cryptographically random 128-bit lowercase hexadecimal
values; names never select paths. Display names retain Unicode validation/trimming
and duplicate names remain legal. Duplicate content means exact OLV whole-file
SHA-256; default import rejects it and reports matching IDs. Metadata and OLV
schema versions and source/hash/compatibility provenance are unchanged.
Read-only health also reports `removal_pending` for an active entry with an
interrupted removal journal; it performs no recovery.
Constructing `VoiceLibrary(path)` is retained for isolated native tests;
the product callers and CLIs do not expose alternate production locations.

## Operations and recovery

Imports validate the source and staged asset/metadata before atomic publication
under a new ID. Export validates/copies only the selected asset and does not export
metadata. Rename changes validated metadata while retaining ID/content. Replacement
retains the existing identity and uses the existing validation callback, journal
and rollback/recovery rules. WPF confirms snapshot name/hash/health assertions
before replacement/removal.

Removal preflights the expected owned entry and publishes a removal journal before
reversible detach into `.trash/<operation_id>/retained`. The coordinating helper
removes that same ID's owned HKLM64 token with the captured token fingerprint.
Recovery restores exact original data when that token remains unchanged, or
finishes removal when token absence and retained identities are verified.
Unknown, unsafe, locked or changed state is retained with a recovery problem.
Partial restoration/cleanup failures remain warnings.

Exclusive library locking and interrupted-replacement recovery remain in place.
Generic `cleanup_pending()` preserves coordinated removal journals unless given
the token-inspection callback; it also preserves nonempty unjournaled trash.
An authorized Remove retry recovers its selected ID first. Explicit administrator
maintenance is `outloud-sapi-voices recover-removals`; read-only refresh never
repairs files or elevates. See [Stabilization Phase 2](STABILIZATION_PHASE_2.md)
for the state machine, security checks and process-interruption durability limits.
The snapshot preserves health/error causes and optional damaged metadata.

Voice Manager refresh now validates each row once within one shared library-lock
epoch, then compares freshly read SAPI tokens against those internal records.
Pending replacement/removal journals are indexed once for that enumeration.
Validation is never cached across refreshes or trusted by privileged mutations.
See [A06 refresh optimization](PERFORMANCE_OPTIMIZATION_A06.md) for measured work
counts, consistency boundaries and focused validation.

## CLI and application boundaries

`outloud-voice-library` is fixed to ProgramData, with no root/scope option:

```powershell
.\build-production\Release\outloud-voice-library.exe list
.\build-production\Release\outloud-voice-library.exe inspect '<library_id>'
```

Import-pocket/import-olv/export/rename/remove/cleanup remain explicit library-only
operations with their existing validation options and result meanings. They do
not coordinate registry changes. Use Voice Manager's elevated helper for normal
Import/Replace/Remove; the normal WPF process remains asInvoker and read-only.
Use the separate managed-token CLI for deliberate token diagnostics/reconciliation.

Canonical generated Gnome conditioning assets remain numerical test inputs; the
shared engine deployment neither bundles a voice nor creates a special token.
User-scope/legacy maintenance is not a supported workflow or a prerequisite in
this guide. Earlier installation inventories remain historical evidence.
Current app procedures are in [VOICE_MANAGER_GUIDE.md](VOICE_MANAGER_GUIDE.md);
build/validation is centralized in [A11/A12](BUILD_AND_VALIDATION_IMPROVEMENTS.md).
