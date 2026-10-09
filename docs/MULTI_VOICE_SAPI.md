# Managed machine SAPI voices

Out Loud supports one production Voice Library under ProgramData and managed
SAPI5 tokens in HKLM Registry64. [Legacy management was retired](LEGACY_ARCHITECTURE_REMOVAL.md).
The [historical implementation record](MULTI_VOICE_SAPI_HISTORY.md) preserves
prior experiments and validation; its commands are not current instructions.

## Identity and registration

Each immutable 32-character lowercase hexadecimal library ID selects exactly:

```text
HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Speech\Voices\Tokens\OutLoud_Pocket_<library_id>
```

`(Default)`, `409` and `Attributes\Name` contain the exact library display name,
with no application prefix. `Attributes\Vendor` remains `Out Loud` and
`Attributes\Language` remains `409`. Equal names are legal; IDs select voices.
The fixed shared-engine CLSID is `{F38C3398-3552-43B9-8350-7DD301327D70}`.
`LibraryId` and `OutLoudManagedOwner=OutLoud.ManagedVoice.1` identify owned tokens.
Paths and generation defaults retain their existing strict REG_SZ schema.

`SapiVoiceManager` and its registry interface have no scope parameter. The real
backend reads/writes HKLM with `KEY_WOW64_64KEY`, validates ownership, types,
allowed fields/subkeys and expected snapshots, and commits bounded token changes
using native registry transactions. Failure does not fall back to another hive
or nontransactional writes. COM engine verification uses its fixed HKLM64 server
registration and pinned file identity, never an effective HKCR fallback.
Generic unrelated tokens are ignored or diagnosed without special voice names.

## Frontend and diagnostics

Voice Manager is `asInvoker`. Import/Replace/Remove use the existing authenticated
one-shot elevated helper; only that helper mutates the machine library/tokens.
Import publishes a new ID before registration; registration failure reports
partial success with the retained entry. Replacement retains the ID/name/token
and validates the current token before publishing changed content. Removal
coordinates journaled reversible detach, fingerprint-checked owned token removal,
rollback and cleanup warnings. An authorized Remove retry first recovers that ID.
The read-only snapshot bridge and Voice Tester remain separate from elevation.

The snapshot's `correct` status still checks library identity/path/name when no
intended shared runtime is supplied; it does not certify playback or visibility
in WoW. The existing detail explains that limit. Voice Tester assigns an exact
owned HKLM token, never a display-name match or default voice.

Both CLIs are fixed to the default ProgramData library. SAPI installation/update
and engine verification require an explicit deployment root:

```powershell
.\build-production\Release\outloud-sapi-voices.exe list
.\build-production\Release\outloud-sapi-voices.exe reconcile
.\build-production\Release\outloud-sapi-voices.exe verify-engine --runtime-root '<deployment>'
.\build-production\Release\outloud-sapi-voices.exe inspect '<library_id>' --runtime-root '<deployment>'
```

`install ID`, `update ID`, and `remove ID` remain explicit administrative token
operations; they do not import/delete library files. The library CLI likewise
operates on files independently; use Voice Manager for coordinated mutations.
Scope/root overrides and the special-token registration workflow no longer exist.

Explicit recovery maintenance, from an administrator shell after reviewing the
pending state:

```powershell
.\build-production\Release\outloud-sapi-voices.exe recover-removals
```

This queries exact owned HKLM64 tokens and reconciles removal journals under the
exclusive library lock. It restores unchanged-token entries or finishes verified
absent-token removals, preserves ambiguous data, reports paths/problems and exits
nonzero while pending work remains. It also processes the library's existing safe
staging/replacement cleanup. It does not modify tokens, recreate missing tokens,
or elevate automatically. Generic library-only cleanup cannot reconcile a
coordinated removal. See [Stabilization Phase 2](STABILIZATION_PHASE_2.md).

`test-managed-voices.ps1` uses default x64 `SpVoice.GetVoices()`, reports its
heading, matches exact HKLM token IDs, checks owner/LibraryId/CLSID/description,
and speaks only with explicit `-Speak` and requested IDs. It has no alternate
category or scope switch. This is a manual diagnostic, not an installer.

Current build and fast/opt-in correctness procedures are centralized in
[A11/A12](BUILD_AND_VALIDATION_IMPROVEMENTS.md). Its production/WPF Release builds
and 4/4 fast checks passed; the 31 broader entries were listed, not executed.
Use [Voice Manager](VOICE_MANAGER_GUIDE.md) for ordinary coordinated mutations.
All CLI writes/recovery above are deliberate administrative maintenance; read-only
`reconcile` reports differences and does not repair them.

## Runtime spelling and old display names

Known shared-runtime fields compare safe local slash/case/dot spellings as
equivalent; VoicePath, ownership, types, schema and unrelated values stay strict.
Equivalent existing runtime values are not rewritten by inspection/idempotence
or a deliberate name-only update. Different or unsafe paths remain conflicts.

An old prefixed owned machine token reports `stale_name` with an explanation in
the existing details pane. Voice data remains valid, but replacement still
requires a correct token. After reviewing the exact ID and deployment, an
administrator may explicitly run `update ID --runtime-root <deployment>` to
update only stale name fields. No automatic reconciliation or cross-scope
migration occurs. See the stabilization report for maintenance and test limits.
