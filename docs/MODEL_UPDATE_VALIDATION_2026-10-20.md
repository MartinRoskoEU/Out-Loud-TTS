# Model update validation — 20 October 2026

Historical initial model-update validation. The per-voice classifications described
below were subsequently removed; the current 0.2.0 behavior and follow-up checks
are documented in [VOICE_STRUCTURE_VALIDATION_2026-10-20.md](VOICE_STRUCTURE_VALIDATION_2026-10-20.md).
The release uses structural validation without Compatible/Legacy/Unknown labels.

This is the historical model-update audit. The voice catalog, status labels and
warnings described here were subsequently removed by the structure-only cleanup.
They are not current product behavior. See [current voice policy](MODEL_UPDATES.md#voice-structure-and-olv-v1).
Cleanup results: [structure-only validation](VOICE_STRUCTURE_VALIDATION_2026-10-20.md).
The recorded model identities and original validation results remain historical evidence.

The developer pipeline selected and integrated the official stable English update
without target overrides. Final development engine version: **0.2.0**, automatically
advanced from **0.1.0** using the authoritative product metadata. No release was
created or promoted. The user's corrected Legacy/Unknown playback policy applies.

## Selected identities

| Identity | Previous | Updated |
| --- | --- | --- |
| Model | Kyutai Pocket TTS English | Official stable English, matching `english_2026-09_2` |
| Immutable HF weight revision | `983151f13aaeab1b13c1e5e3c2c383d49a9edf3f` | `7f364c05d1be3faf2b234cdbe4cbb68a026d4218` |
| Source SHA256 | `fb0dc01b0d4d2e1c905b7a3e0676e3d9c96d5ae460e24e3ab94981805babf997` | `7767ce79b1b8db523d867e70855a00627aacfe44aaa5c1fd7ebe135fba71fff9` |
| OLP SHA256 | `30889426a3bed523e725baafe70ec9064044898aa203975980741be1f3d70406` | `fe8c40e3a773465974ce965e57f01c8904ac4cb2cf7086c42cbb151f8815ba3e` |
| Model compatibility ID | `993a3a9749dc64d60815aa0e71fa4c18f53d478d05d0c4da8c7c77164387c59a` | `11399b73577d746fa8b5a52a9a6893b1033343f4cbc6da7ad08b831b6d20eb36` |

The selected [official configuration](https://raw.githubusercontent.com/kyutai-labs/pocket-tts/9aa7a7f0e52a4cd81bcf8a0fc7c981cdc6d11e53/pocket_tts/config/english.yaml)
is pinned to GitHub commit `9aa7a7f0e52a4cd81bcf8a0fc7c981cdc6d11e53`.
Its SHA256 is `08fcb2c2a1e5a79416c3683eeb3e98e1684bf7c1a23f2fa2d194837535442ca8`.
The exact historical GitHub config commit for the old conversion was not recorded;
that fact is retained as unknown rather than invented. Its HF revision and actual
weight bytes were verified.

Selection followed official default configuration and matching HF hashes, not
repository naming order or modification times. The candidate has 214 BF16 tensors;
57 differ, including 55 of the 169 synthesis tensors and two excluded tensors.
Configuration, six-layer English architecture, tensor schema and tokenizer match
the native implementation. Immutable source ancestry/file history establishes a
forward update. No inference changes were necessary.

## Verification evidence

Final validated run: `generated/model-updates/run-20261020T000220Z-0bd3c4b4/`.
Its `result.json`, `check.json` and individual logs contain full comparison and
verification details. `reference/active-model.json` points to the isolated build.

| Check | Result |
| --- | --- |
| Official source file | Actual 219,029,196 bytes and SHA256 match immutable HF LFS metadata |
| Conversion | Existing BF16 algorithm; 169 retained / 45 excluded tensors, frozen numeric IDs |
| OLP integrity | 199,449,856 bytes; headers, table, dimensions, payload and whole-container hashes verified |
| Aligned OLP payload SHA256 | `ad0ddebb3daeeaf630f17a7836fe80a949cab6fcb1b5de56540c46665bb372ea` |
| Native development build | Release x64 engine, bridge, import helper and tools built in isolation; no registration |
| Product versions | Engine/bridge/helper/CLI/WPF file versions verified as `0.2.0.0` |
| Compatible speech fixture | Verified new-weight preparation; 86,400 finite samples, EOS reached |
| Legacy speech fixture | Verified old-weight preparation; 80,640 finite samples, EOS reached |
| Unknown speech fixture | No verified model provenance; 86,400 finite samples, EOS reached |
| Invalid profiles/model | Structural corruption and model integrity failures rejected natively |
| Existing asset regressions | Source-byte hashes, all 65,536 BF16 patterns, FP32 widening, two original voice payloads, malformed headers/bounds/dimensions/dtype/integrity checks passed |
| Safetensors importer | 157 structured rejection checks passed; original caches copied exactly; Unknown import allowed |
| Voice library | 236 assertions passed, including classification, export/rename, metadata, locking, unsafe paths and recovery |
| Frontend | 222 checks passed, including selection of all three statuses and rejection of unhealthy rows |
| Final WPF build | Zero warnings/errors; output under `generated/model-updates/ui-final/` |
| Updater tests | 11 offline tests passed: no-op/configuration block, divergent/retired revisions, versioning, partial publication, recovery, rollback and conflicting edits |
| Actual rollback | Restored previous model/pins and 0.1.0; automatic update then restored validated 0.2.0 with expanded checks |
| Final idempotence | Repeated CHECK and UPDATE preserved hashes of 400 source/model/voice files and timestamps of nine candidate native binaries |

The speech fixtures were newly prepared from original development reference audio
in scratch storage using strict loading of the verified old/new weights. Existing
voice profiles were not regenerated. These tests establish focused end-to-end
execution, not universal quality or speaker-similarity guarantees.

## OLV and Voice Manager behavior

OLV v1 remains unchanged. Engine version, serialization version and actual model
weights are independent. A compiled content-bound catalog records verified
payload/source/serialized-ID tuples and exact preparing weight hashes/revisions.
New developer test profiles retain preparation sidecars. Historical self-declared
compatibility IDs and arbitrary Safetensors metadata are not treated as proof.

Verified active-weight profiles are Compatible. Verified different-weight profiles
are Legacy. Unverifiable preparation is Unknown, including historical profiles
whose original model cannot be established. The original model is displayed only
when reliably known; the original serialized ID remains visible and unchanged.
Legacy and Unknown are selectable/playable when structurally valid, with a
nonmodal quality/behavior warning. Only invalid, unsupported or unsafe data is
blocked. OLP model integrity pins remain mandatory.

## Preservation and upgrade audit

All seven existing developer `.safetensors`/`.olv` files retained their exact
hashes. OLV import/export preservation was verified. Old builds were not rebuilt
or registered. The previous OLP and metadata remain in the active run's
`previous/` backup, with an integrity-checked rollback manifest.

The public `release/OutLoud-Setup-0.1.0.exe` remains unchanged and matches its
existing SHA256: `60c9d3c9be09e52ddc8bf0f9217cead6054511a300ad70b1a85ecc8bf60aec31`.
No installed application, actual machine/user voice library, COM registration,
release artifact, Git tag or remote repository was modified.

Inno chooses repair or release maintenance for an in-place upgrade, independently
of complete uninstall. Native maintenance excludes the voice library from package
payload. The isolated inventory-upgrade test passed 99 checks with private files
and simulated signing/registry, including preservation. Evidence is under
`generated/u/maintenance-fa78c9cc83bf22157cb7e2dab045d8ba/`.
The initial audit path exceeded an existing Win32 maintenance path budget; the
same audit passed from a shorter workspace path. That safeguard was not changed.
Complete uninstall's explicit confirmation/deletion policy is unchanged.
`update-installed.ps1` retains its version-equality safeguard and may refuse a
cross-version developer deployment; it was not invoked.

No destructive upgrade path was found in the inspected code and isolated audit.
Live upgrade/uninstall validation was deliberately not performed on the actual
installation. Before public release, validate on disposable machines with copied
libraries, listen to and assess candidate speech across representative old/unknown
profiles, refresh model-specific neural quality/EOS baselines where appropriate,
and complete existing packaging, license, signing and release checks. Safe optional
voice regeneration requires original reference audio; availability is not assumed.

Commands, provenance details and rollback instructions: [MODEL_UPDATES.md](MODEL_UPDATES.md).
