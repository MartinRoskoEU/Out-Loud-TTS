# Developer model updates

Out Loud 0.3.0's `english_2026-10` update and focused validation are recorded in
[the release notes](releases/0.3.0.md). The first CHECK refused newly enabled
character replacement settings; they were reviewed and integrated explicitly,
then the existing conversion and transactional publication helpers were reused
with only the validation required for this release. The full automatic UPDATE
and its broader import/library suites were not run for that preparation.

Run from the repository root with the existing reference environment:

```powershell
.venv\Scripts\python.exe -B tools\assets\update_model.py check
.venv\Scripts\python.exe -B tools\assets\update_model.py update
```

Neither mode accepts a target model, revision, hash or engine version. CHECK
writes only isolated diagnostic downloads/reports under `generated/`. UPDATE
checks again, stages conversion/builds, validates, then publishes the development
pins and version together. Identical verified source weights return
`already-current` without conversion, rebuilding or version changes. Reports are
`generated/model-updates/run-*/result.json`. `check --no-download` requires cached
assets while still querying official metadata.

Prerequisites are the existing Windows x64 MSVC 2022/CMake/.NET toolchain,
reference Python dependencies, HF access to official gated weights, existing
tokenizer/build fixtures, and `samples/gnome_male.wav` for scratch test preparation.
Missing reference audio blocks validation without publishing. No recording is
inferred from a prepared cache. No new dependencies are installed by this tool.

## Selection and integration

`compare_model.discover_official()` is shared by the diagnostic and updater.
It resolves immutable official Kyutai GitHub configuration and HF weight
revisions, verifies the default language, and requires the official stable English
alias, canonical HF default and configuration pin to agree by weight hash.
Directory order, timestamps and apparent versions do not select weights. There
is no experimental, drifting, 24-layer, multilingual or community fallback.

Actual development OLP bytes are verified against JSON/C++ source pins, complete
container/payload integrity, numeric tensor IDs, layout and packaging hash.
Source downloads are verified against size and published LFS SHA256, or Git blob
identity for small non-LFS files. The existing diagnostic compares the complete
source schema, including excluded tensors. Configuration, tokenizer, tensor set,
shape or dtype changes block automation with differences. Forward HF ancestry
and a forward canonical weight-file change are required; retired weights and
divergent/ambiguous history are refused instead of guessing a downgrade.

The existing Safetensors-to-OLP algorithm and native inference are reused. New
source/payload pins and the existing weight-plus-tokenizer compatibility hash
are generated automatically. Model integrity verification stays mandatory.
The source snapshot includes existing uncommitted work; nothing is committed.

`ProductVersion.props` remains authoritative. Weight identity changes increment
MINOR, reset PATCH and preserve MAJOR within existing Windows version bounds.
CMake PE resources, WPF versions and future Inno staging derive from this source.
The updater checks native/managed PE versions. The proposed version is confined
to staging until validation passes. OLV stays at serialization version 1.
Published release artifacts and historical release examples are retained.

Validation includes conversion headers/table/payload, fresh scratch profiles
prepared from original reference audio using the selected weights, native Release
and WPF builds, existing OLV and imported Safetensors speech through EOS with
finite PCM, malformed asset rejection, and focused asset/import/library tests.
The existing fixture exporter verifies BF16 source bytes and PyTorch widening.
Historical numerical neural/utterance baselines remain historical; they are not
silently reinterpreted for new weights.

Source model/asset history is preserved under `reference/model-history/`.
Active model identity, schema, package pins and attribution are synchronized.
Builds, test profiles, listening WAVs and logs remain in the isolated run.
`release_promoted=false` records development validation only. No installer,
release, tag, registration, signing, deployment, commit or push occurs.

## Rollback and recovery

```powershell
.venv\Scripts\python.exe -B tools\assets\update_model.py update --rollback
```

Rollback restores the active run's previous model, metadata, fixtures and version
from integrity-checked `previous/` backups and `rollback.json`. Builds are kept.
Keep the run directory while rollback is needed. Concurrent edits are detected
before restoration; unknown files are retained rather than overwritten.

An OS lock prevents overlapping model operations. Source hashes detect conflicting
edits. A flushed journal precedes publication, and publication failure restores
affected files. After interruption, CHECK refuses partial state and UPDATE
recovers the previous state before discovery. Fault tests cover partial copies,
validation failure, crash recovery and conflicting edits. Neither recovery nor
rollback accesses installed software or user libraries.

## Voice structure and OLV v1

Voice import and playback depend only on supported data format, structure and
integrity. Individual voices have no model-version classifications, model history,
preparation sidecars or compatibility warnings. Older prepared caches and profiles
with no known preparation history remain usable when technically supported.

The OLV serialization version, engine version and active model weights are
independent. OLV remains version 1. Existing compatibility IDs, voice-source and
payload/container hashes remain format data and are preserved byte for byte on
import/export. A voice's compatibility ID is not compared to the active model to
classify or block it. New Safetensors imports retain the existing fixed serialized
marker and copy the supported FP32 caches exactly; no model revision is inferred.

Architecture, tensor layout, dimensions, dtype, bounds, finite values, file and
payload integrity, safe paths, library locking and native memory-safety checks
remain enforced. Invalid or unsupported profiles are rejected. Library health and
SAPI registration still describe actionable file/registration problems, not voice
model versions. Normal import, selection, rename, export and removal are preserved.
The old schema-1 metadata boolean and ABI slot are retained only as reserved
compatibility data; they are not used to track a preparing model or affect playback.

The updater's offline `prepare_test_voice.py` creates one scratch profile for
end-to-end validation of each selected model. It verifies preparation inputs but
does not emit per-voice provenance records. Existing fixtures test playback of
older and externally prepared caches without model classification. The active
model's identity, SHA256, integrity/compatibility pins and model history remain
mandatory. Historical rollback journals skip retired voice-catalog entries so
model recovery cannot restore the removed feature. Historical run artifacts are
kept as audit/rollback evidence and are not consumed for voice classification.

Model updates can affect speech quality, speaker similarity, prosody and EOS
behavior. This does not automatically block structurally supported voices.
No existing voice is deleted, rewritten, migrated or regenerated. Optional fresh
preparation needs original reference audio; a KV cache is not a substitute.

## Upgrade preservation and public release

Model CHECK/UPDATE does not invoke `update-installed.ps1`. Same-version installed
development updates retain the version-equality guard. An intentional forward
development upgrade must supply the exact observed previous version:

```powershell
sapi\scripts\update-installed.ps1 -Components Both -Action Deploy -UpgradeFromVersion 0.1.0
```

The target comes from `ProductVersion.props`; missing/mismatching expectations,
equal versions, downgrades and partial component selections are rejected. The
explicit upgrade builds/stages the complete allowlisted package without compiling
an installer. Native preflight independently binds `upgrade_from`/`upgrade_to` to
the protected installed inventory, installer record and current helper version.
Signing, prerequisite, integrity, lock and publication checks remain mandatory.
The installation-owned certificate/key/trust, COM registration and voice library
are reused. Only the install record's version and version-bearing display name
may change; the exact typed before/after host trees are journaled alongside the
payload/receipts. Upgrade journals use maintenance schema 3; existing schema-2
journals and the inventory/OLV formats remain supported and unchanged.

Explicit developer upgrades return `awaiting_validation`, retaining the protected
pending transaction. Close test clients normally after validation, then use the
workspace `OutLoudInstallSigning.exe --maintenance-finalize INSTALL_ROOT REPORT`
from elevated x64 PowerShell. On failed validation use `--maintenance-recover
INSTALL_ROOT REPORT` instead; it restores both payload and host version after
checking all surviving identities. Never overwrite foreign edits or bypass locks.
Each report path must be new. Interrupted upgrades require the same checked
recovery; do not rerun publication against a pending transaction. Finalization
archives the journal and retains backups, removing the pending pointer. A terminal
committed journal is not undone by `--maintenance-recover`; do not describe it as
a general post-commit downgrade command.

Machine-specific installed-update reports remain local; generated deployment
evidence belongs under `generated/installed-updates/`, outside Git.
Inno release upgrade remains separate.
Inno uses repair for equal versions and release maintenance for changed versions,
without running complete uninstall. Native maintenance excludes the voice library
from package payload and preserves its identity during publication/recovery.
Intentional complete uninstall retains its existing explicit confirmation and
managed-library deletion policy.

The isolated inventory-upgrade audit uses private files and simulated registry/
signing. No library-destruction path was found in in-place upgrade. Live upgrade
and uninstall validation on disposable machines remains required before release.
Also listen to speech, assess quality/EOS across representative caches, refresh
model-specific neural baselines as appropriate, and complete existing package,
license, signing and release gates. This workflow does not bypass those gates.
