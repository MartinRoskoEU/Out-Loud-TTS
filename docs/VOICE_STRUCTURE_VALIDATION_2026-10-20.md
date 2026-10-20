# Structure-only voice support — 20 October 2026

Voice preparation model revisions no longer determine availability. The development
version remains **0.2.0**, and the active model identity, configuration, OLP hash,
integrity pins and model history are unchanged.

## Removed components

- Removed `reference/voice-provenance.json` and the generated
  `engine/include/outloud/voice_provenance.hpp`; their only consumers implemented
  voice-to-model tracking and classification.
- Removed native catalog lookup, per-voice model/status fields, bridge model
  exports, managed model properties, status column, preparation-model labels and
  compatibility warnings from the library, tester and import dialog.
- Removed provenance-only assertions and status-selection tests.
- Replaced `prepare_verified_voice.py` with `prepare_test_voice.py`. The helper
  still verifies the selected model/tokenizer and prepares one scratch voice for
  end-to-end model validation, but creates no provenance sidecar or catalog record.
  The updater no longer downloads/prepares the previous weights solely to classify
  fixtures. Existing old/external fixtures exercise playback instead.

No replacement classification system was introduced. The OLV identifier remains
visible as serialized format data. Normal health/registration diagnostics and
import, selection, rename, export and removal remain available.

## OLV v1 and safety

The OLV v1 binary format, existing identifiers, headers, source/payload/container
hashes and FP32 cache layout are unchanged. Existing files are not migrated or
rewritten. Imported Safetensors caches retain exact FP32 data, supported older
counter/padding layouts and the existing fixed serialized import marker. That
marker is not used to track or classify a preparing model.

Native architecture/layout/dtype/dimension/bounds/finite-value checks, integrity
validation, safe library paths, locking and memory-safety safeguards remain active.
OLP model source, compatibility and payload pins remain strict. Invalid data is
rejected; differing or unidentified preparation revision alone cannot block a
supported voice.

For existing library metadata, the retired schema-1 boolean is retained only as
opaque compatibility data and round-trips without affecting eligibility. Both
boolean values are accepted, while invalid JSON types are rejected. The retired
bridge field is reserved and zeroed, preserving ABI v1 record size/offsets without
exposing provenance in managed voice records.

## Focused results

Evidence: `generated/voice-simplification/`.

| Check | Result |
| --- | --- |
| Native Release x64 engine/bridge/helper/tools | Built in `build-voice-structure/`; unregistered |
| Voice Manager | Built with zero warnings/errors in `generated/voice-simplification/ui/` |
| Frontend fake-service tests | 214 checks passed; obsolete classification tests removed |
| Fresh scratch OLV speech | 86,400 finite samples; EOS reached |
| Existing OLV speech | 80,640 finite samples; EOS reached |
| Older-weight fixture playback | 80,640 finite samples; EOS reached, without reading preparation records |
| External Safetensors import/playback | 80,640 finite samples; EOS reached, no model declaration required |
| OLV import/export | v1 retained; original files and exported identity/payload bytes matched |
| Native asset regressions | All 65,536 BF16 patterns, model source/widening checks, original voice payloads and malformed-asset checks passed |
| Native importer | 157 rejection checks passed, including unsupported architecture/dtype/layout, bounds, malformed JSON and non-finite data |
| Voice library | 241 assertions passed, including metadata compatibility round-trip, export/rename, locks, unsafe paths and recovery |
| Scratch preparation | No `.provenance.json` created |
| Updater regression tests | 12 passed, including historical rollback without restoring retired tracking files |
| Official CHECK | `already-current`, engine 0.2.0 |
| Already-current UPDATE | `already-current`, engine 0.2.0; no conversion, build or publication |
| No-op/preservation audit | 399 source/model/voice file hashes and nine build timestamps unchanged by CHECK/UPDATE; 17 pinned metadata/history/original voice/release assets unchanged by cleanup |

The known older-weight fixture was reused from the prior isolated validation run;
it was not regenerated or assigned a new identity. Existing profiles with unknown
preparation history were imported and synthesized without tracking that history.

## Model updater and remaining scope

The two developer commands, official discovery, immutable revisions, verified
downloads, conversion, OLP pins, automatic engine versioning, model history,
transactional publication and recovery remain in place. Historical journals skip
only the exact retired voice-catalog/header paths, preserving model rollback
without resurrecting removed functionality. This was tested with private fixtures;
actual rollback was not invoked during cleanup, preserving version 0.2.0.
Old generated run snapshots/sidecars remain historical audit artifacts and are
not dependencies or inputs for the simplified engine.

No functional blocker was found. No new upstream model was applied because the
development weights are already current. Speech smoke checks establish execution,
finite output and EOS for these fixtures, not perceptual speech quality or a
guarantee for every external cache. Model updates may affect quality, speaker
similarity and EOS behavior; this does not automatically block supported voices.

The installed application, actual user voice library, public installer and 0.1.0
release artifacts were not modified. No deployment, installer rebuild, commit,
push, tag or publication occurred. The prior model-update build remains available
for model rollback; the simplified binaries are in the new build directory above.

Current workflow: [MODEL_UPDATES.md](MODEL_UPDATES.md).
