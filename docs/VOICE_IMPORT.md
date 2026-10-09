# Step 3A: native Pocket voice importer

Prepared voice import, [managed library storage](VOICE_LIBRARY.md) and
[managed SAPI tokens](MULTI_VOICE_SAPI.md) are separate operations. Importing
never registers a Windows voice.

`outloud_voice_import` is the reusable, offline C++20 Windows x64 boundary for
importing a **prepared Pocket voice state** into an Out Loud Voice (`.olv`).
The CLI is `outloud-voice-import.exe`. This conversion API writes an OLV but does
not install a library entry or register a token; current coordinated import is
described in [VOICE_MANAGER_GUIDE.md](VOICE_MANAGER_GUIDE.md). The SAPI engine does not link the importer
or its source-header parser; its loader and synthesis behavior are unchanged.

The separate [managed voice library backend](VOICE_LIBRARY.md)
calls this API and stores human-facing metadata outside the unchanged `.olv`.
This document retains the importer contract and historical Step 3A evidence.
Current production/development build commands are in
[A11/A12](BUILD_AND_VALIDATION_IMPROVEMENTS.md).

## Audited source and established conversion

The installed reference environment was inspected locally: Python 3.12.10,
Pocket TTS 3.3.0, PyTorch 2.14.1+cpu and Safetensors 0.8.0. Relevant primary
source under `.venv/Lib/site-packages/pocket_tts/`:

- `models/model_state.py`: `export_model_state` flattens nested state names
  into `module_name/tensor_key` and calls `safetensors.torch.save_file`, without
  adding model/version metadata. `_import_model_state` reconstructs the nesting.
- `models/tts_model.py`: `get_state_for_audio_prompt` loads an existing
  Safetensors state directly. For raw audio, the separate upstream path reads,
  optionally truncates, converts to 24 kHz mono, runs Mimi `encode_to_latent`,
  widens latents to FP32, projects with `speaker_proj_weight`, adds the learned
  BOS when configured, and runs FlowLM prefill/increments the offsets.
- `config/english.yaml`: the pinned original model revision and
  `insert_bos_before_voice: true`.
- `modules/attention.py`: RoPE is applied to K **before** appending K/V to the
  cache. Its batch-1 linear-cache state contains `cache`, `offset` and `pad`.

The known source is `voices/gnome_male.safetensors`, **5,506,872 bytes**, SHA256
`2118ea60dfe87bd515b27e051e1d677a7fe15663875740599147bde37a9c4cea`.
It has an 8-byte little-endian header length of 1744, a UTF-8 JSON tensor
directory, then 5,505,120 bytes of packed tensor data. There is no magic,
pickle, PyTorch archive, embedded code, model identity or metadata entry.
The directory contains exactly these fields for each layer 0..5:

| Tensor suffix after `transformer.layers.<layer>.self_attn/` | Dtype | Shape | Value |
| --- | --- | --- | --- |
| `cache` | F32 | `[2,1,112,16,64]` | Ready K/V prefix |
| `offset` | I64 | `[1]` | 112 |
| `pad` | I64 | `[1]` | 0 |

The actual source data stores the 12 integer tensors first (96 bytes), then
the six caches. Directory order and physical tensor order are not required
by the importer: names and validated offsets determine the bytes.

The established writer is `tools/assets/export_voice.py`, using
`SafeTensorSource`, `header` and `finish_asset` in `tools/assets/asset_format.py`.
It verifies the expected original source hash, checks the complete schema and
finite values, and copies layer caches **verbatim** in layer order. Offset/pad
are represented by the shared prefix length and zero-padding invariant.
It adds the fixed compatibility ID, source/payload/container hashes and v1
header. No normalization, dtype conversion, model evaluation, RoPE or cache
recomputation occurs. The existing script has callable helpers but is a
development exporter: Python/NumPy, an expected source hash, and outputs
restricted to `generated/assets/`. It has no frontend error contract.

The ignored `generated/inspect_step2a.py` and `generated/write_step2a_snapshot.py`
inspect and record the original state; they do not create `.olv` files.
`tools/assets/export_test_fixtures.py` and
`tools/neural/export_flowlm_prefill.py` independently compare source bytes.
Existing loaders, fixtures and checks are described in
[NATIVE_ASSETS.md](NATIVE_ASSETS.md).

The original audio recording and exact historical export invocation are
**unknown**. The inspected upstream code establishes the preparation path,
but this particular source has no metadata proving which model/preparation
created it. Known-good native synthesis and exact conversion are established
for the local Gnome reference; arbitrary prefixes cannot gain model provenance
merely by matching its tensor shapes.

## Supported boundary and dependencies

The backend supports the complete finite, unpadded batch-1 state above with
any positive bounded N, six equal prefix lengths, I64 offsets equal to N and
zero pads. Source filename extensions are not used to infer the container.
Legacy `current_end`, extra/missing tensors, expanded caches with unused NaNs,
other architectures/languages, other dtypes and padded/batched states are
rejected. Upstream supports a legacy `current_end` branch; its conversion is
deliberately outside this first production contract.

This source can be safely parsed and converted natively: it is a bounded JSON
directory and raw tensors, with no executable deserialization. A small private
parser handles this header; no third-party JSON library or Safetensors runtime
is shipped. It rejects duplicate keys, invalid UTF-8/escapes/surrogates and
ambiguous/out-of-range numeric fields. Header limit is 64 KiB, nesting limit
8, total JSON nodes 1024, object members 128, array elements 32. Payload limit
is the existing 1 GiB `.olv` limit, so N is at most **21,845**. Source size is
bounded by that payload plus the header, its length field and 96 scalar bytes.

Import needs neither the `.olp` file nor Python/PyTorch/NumPy. The shared model
and tokenizer are required for synthesis and runtime pairing validation.
Creating a prepared voice **from raw audio** remains a separate upstream
operation requiring its original preparation weights/tooling. The synthesis
`.olp` omits Mimi encoder components, `speaker_proj_weight` and
`bos_before_voice`; this importer cannot recreate that audio preparation path
using `.olp`. No Python embedding, LibTorch, ONNX or other neural dependency
is added. The native importer uses the existing Windows BCrypt/asset libraries.
Python was used only to inspect local reference files and rerun the old writer
for an independent comparison during development. No network was accessed.

## Compatibility and `.olv` v1

**The existing v1 bytes and loader are preserved. No format migration.** The
full field table remains in [NATIVE_ASSETS.md](NATIVE_ASSETS.md). In brief:

- Magic `OLVPKT01`, format version 1, header size 256, endian marker
  `0x01020304`, Pocket family/English synthesis architecture 1 and FP32 dtype 2.
- Compatibility/source identities, SHA256 for payload and whole container;
  container hashing substitutes zeros only for its own bytes 96..127.
- Exact total size, payload offset 256, payload length, six layers, prefix N,
  16 heads, head dimension 64, batch 1, 24 kHz/1920 samples, post-RoPE flag,
  total elements, voice schema 1, and 20 reserved zero bytes.
- Six typed fixed-schema cache sections, each `[2,1,N,16,64]` in row-major
  order, K followed by V; total payload `49152*N` bytes. The existing dimensions
  and arithmetic determine every section's offset/length, so a new tensor
  table would duplicate this contract.

The original source SHA provides a content/provenance identifier; the whole
file hash identifies the serialized `.olv`. Neither is publisher authentication
or proof of model origin. No new UUID or display-name field is required for
this conversion. Display metadata/library identity can be designed separately
before frontend/library work. Source metadata is returned as an opaque string
map and is not silently inserted into `.olv` or interpreted as a model version.
The pinned upstream exporter defines no such metadata keys.

Unknown versions, schemas, flags, sections/reserved fields fail closed under
v1. There is no promise to skip unknown sections. Future extensions require an
explicit version and migration/reader policy that preserves existing v1 files.
Integers and IEEE-754 FP32 bytes are little-endian, for the existing Windows
x64 runtime. Truncation, trailing bytes, bad bounds, hashes and non-finite
cache entries are rejected by the production loader.

Conversion **requires** `declared_model_compatibility_id` in its options. Only
the pinned ID is supported:
`993a3a9749dc64d60815aa0e71fa4c18f53d478d05d0c4da8c7c77164387c59a`.
Its derivation binds English 3.3.0 synthesis-v1, original model hash and original
tokenizer hash (see the asset document). A different declaration is rejected.
This explicitly records the caller's knowledge/claim about the preparing
model; it does **not** establish missing provenance. All inspection/import
results retain `model_provenance_verified=false`. Frontend code must expose
that limitation and obtain/retain an informed compatibility declaration for
new sources. An optional independently known source SHA detects a source
identity mismatch, but likewise cannot establish model origin.

## Public API, errors and destination handling

[voice_import.hpp](../engine/include/outloud/voice_import.hpp) exposes:

```cpp
auto source = outloud::inspect_pocket_voice(source_path);
auto validated = outloud::validate_pocket_voice(source_path);

outloud::VoiceImportOptions options;
options.declared_model_compatibility_id = outloud::pocket_model_compatibility_id;
// Optional: options.expected_source_sha256 = independently_known_sha;
// Optional: options.replace_existing = true;
auto result = outloud::convert_pocket_voice_to_olv(source_path, output_path, options);
auto voice = outloud::validate_olv(output_path); // default: pinned pairing
```

Inspection and source validation both perform complete schema, bounds,
contiguous/non-overlapping coverage, finite-value and hash scans. There is no
unsafe header-only success result. `PocketVoiceInfo` returns prefix, source/
payload sizes and hashes, metadata, and provenance status. `VoiceImportResult`
adds `OlvInfo`: format version, prefix, sizes, source identity, compatibility
and final file hash. The future GUI need not parse tensors.

`VoiceImportError` provides a stable enum code, descriptive `what()`, path,
tensor name where relevant, and a Win32/system error number where available.
`voice_import_error_name` supplies stable machine-readable names:
`file_not_found`, `source_io`, `unsupported_format`, `corrupt_source`,
`malformed_tensor`, `invalid_shape`, `unsupported_dtype`, `incompatible_model`,
`compatibility_required`, `source_identity_mismatch`, `invalid_destination`,
`destination_io`, `olv_validation`, `internal_failure`. No preprocessing-error
case is invented because import performs no preprocessing.

Sources are mapped read-only without write/delete sharing throughout
conversion. Unicode local-drive paths are supported; UNC/device namespaces,
alternate streams and mapped network drives are rejected. Destination must
end in `.olv`, have an existing parent, and not alias the source (including
hard links). Replacement requires an explicit option. No directory creation
or overwrite is implicit.

Conversion writes a uniquely created same-directory temporary file, flushes
and closes it, then validates it through **the existing `OutLoudVoice` loader**
and pinned pairing before calling `MoveFileExW` to finalize. A failure retains
the previous destination and the owned temporary is cleaned up on ordinary
exception unwinding. No delete-then-rename gap is introduced. A concurrent
destination creation causes finalization failure unless replacement was
explicitly requested. There is no process-crash/power-loss cleanup or recovery
journal guarantee; a crash can leave a temporary file, never an intentionally
published incomplete container. No complete output buffer is allocated:
cache bytes stream directly from the source mapping.

## CLI, validation and evidence

The production-only build supplies this CLI without reference fixtures.
Run from the repository root after the [documented build](BUILD_AND_VALIDATION_IMPROVEMENTS.md).
`pinned` is an explicit assertion that the source was prepared with this project's
model, not automatic provenance/compatibility discovery:

```powershell
.\build-production\Release\outloud-voice-import.exe inspect '<prepared-source.safetensors>'
.\build-production\Release\outloud-voice-import.exe '<prepared-source.safetensors>' '<new-output.olv>' --model-compatibility pinned
.\build-production\Release\outloud-voice-import.exe validate-olv '<new-output.olv>'
```

For repeated CLI import, either choose a new destination or deliberately add
`--replace`. `--model-compatibility` also accepts the exact 64-digit ID.
CLI status is 0 on success, 1 for backend failures, 2 for CLI usage errors.
API output/errors are structured; CLI output is developer-readable key/value
text, not a GUI integration protocol.

### Historical Step 3A execution evidence

The following results and closing decisions belong to Step 3A, before the later
Voice Manager workflows. They are not a new build, test run or pending frontend
implementation requirement.

Release x64 `/W4 /WX` build passed. All **12 original CTests plus one focused
importer CTest (13/13)** passed, 31.65 seconds. No performance CTest, benchmark
or profiler was added. The focused harness passed 68 structured rejection
checks, covering corrupt/truncated/unsupported source, shape/dtype, duplicate
keys (including escaped aliases), malformed/overflowing numeric offsets,
overlap/trailing bytes, unequal offsets/padding, NaN/Inf, Unicode/metadata,
required/wrong model declarations, source identity, missing paths, invalid
destinations, hard-link aliases, locked destination finalization, temporary
cleanup, explicit replacement, deterministic bytes, and corrupt/truncated/
version-incompatible `.olv` validation. No incompatible source-version metadata
test is asserted: the audited source defines no source-version/model fields.

The native CLI output, original known-good file, and a fresh run of the
unmodified Python exporter are all **5,505,280 bytes**, SHA256
`ff138495b7d3834c2eabb926ff7226a2983e7af17cf5a0c6b4cad05ae6a5bb00`.
The harness requires entire-file equality and exact bytes for all six caches.
Payload SHA256 is
`728da46f323788972bcdd89141468e6bb570e8601c7f39c9ab9400137b50ebb4`.

The optional runtime harness loads the newly converted equivalent through
`OutLoudVoice`, calls `validate_against(PocketModelAsset)`, and uses the same
production `UtterancePipeline` as SAPI, seed 73/AVX2 and the existing canonical
tokenizer/text/capacities. Only the voice path changes. It verifies every FP32
frame against the prior canonical capture, all segment frames/draws/EOS,
170 frames, 326,400 samples, 176 draws and 173 candidates. Exact FP32 SHA256:
`36788a2c1cfde712fb9ef68151a93b1b78eec40b1e440e99ff1b1b59d1a8bcf0`.
No playback or live WoW test is needed for this import boundary.

Ignored evidence is under `generated/perf/3a-*.txt`; CLI/imported and independent
legacy comparison files are under `generated/assets/3a-gnome-*.olv`. Original
source and known-good voice were not overwritten.

Before frontend work, decide how model provenance declarations, display names,
library identity, replacement confirmation and user-provided voice rights will
be presented/stored. The source supplies no permission/provenance metadata;
redistribution rights for recordings/model/voice assets are not established by
these conversion tests. No new third-party code/runtime dependency was added.
No Git staging/commit/push, registry mutation, SAPI registration, signing,
deployment copy or WoW process action was performed. Stop after Step 3A.
