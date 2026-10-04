# Native Out Loud Pocket assets (Step 2C)

`outloud_assets` is a C++20 Windows x64 library for the fixed Pocket TTS 3.3.0
English, CPU, batch-1, 24 kHz mono synthesis asset contract. `.olp` holds the
shared model; `.olv` holds one ready-to-synthesize Out Loud Voice. The native
library uses local read-only file mappings and Windows BCrypt SHA256. It has
no Python, PyTorch, Hugging Face, safetensors, JSON/YAML or network dependency.
No neural backend or inference is implemented in this step.

## Source and fixed model schema

The original local model is `kyutai/pocket-tts`, revision
`983151f13aaeab1b13c1e5e3c2c383d49a9edf3f`,
`languages/english/model.safetensors`, SHA256
`fb0dc01b0d4d2e1c905b7a3e0676e3d9c96d5ae460e24e3ab94981805babf997`.
The exporter independently verifies all 214 BF16 tensors / 109,502,146 elements.
The retained set is 169 tensors / 99,719,330 elements; 45 tensors / 9,782,816
elements are omitted.

The complete omission rule, checked against the installed pinned
`pocket_tts/models/tts_model.py`, `models/mimi.py` and the English configuration,
is these prefixes and two exact names:

- `mimi.encoder.`
- `mimi.encoder_transformer.`
- `mimi.downsample.`
- `flow_lm.speaker_proj_weight`
- `flow_lm.bos_before_voice`

These components prepare conditioning from reference audio. Synthesis from an
already computed voice prefix does not invoke them. Decoder, decoder
transformer, upsample, FlowLM and sampler weights remain. In particular,
`mimi.quantizer.output_proj.weight` remains: the synthesis path uses its
projection despite not running the audio quantization path. See
[POCKET_PIPELINE.md](POCKET_PIPELINE.md) for the source mapping.

[pocket-model-schema.json](../reference/pocket-model-schema.json) records every
included ID/name/dtype/shape and every omitted name/dtype/shape. IDs 0..168 are
assigned by the original included names' lexical order and frozen for format
1. The generated [pocket_model_schema.hpp](../engine/include/outloud/pocket_model_schema.hpp)
compiles the same mapping into C++; no runtime name lookup or configuration
parser is required. The binary table uses IDs. Names remain available for
diagnostics. Architecture schema 1 also pins the non-weight constants exposed
by `PocketArchitecture` in [assets.hpp](../engine/include/outloud/assets.hpp),
including FlowLM 1024/6/16/64, latent width 32, sampler width 512, Mimi decoder
512/2/8, decoder ratios 6/5/4 and 1920 samples per frame.

## Version 1 binary headers

Both formats use the following 256-byte header. All integers are unsigned
little-endian; hashes are 32 raw bytes, not hexadecimal strings. Reserved
fields must be zero. No native struct serialization is used.

| Offset | Type | `.olp` model | `.olv` Pocket voice |
|---|---|---|---|
| 0 | 8 bytes | `OLPMOD01` | `OLVPKT01` |
| 8 | u32 | Format version 1 | Format version 1 |
| 12 | u32 | Header bytes 256 | Header bytes 256 |
| 16 | u32 | Endian marker `0x01020304` | Same |
| 20 | u32 | Family 1 = Pocket | Same |
| 24 | u32 | Architecture 1 = fixed English 3.3.0 synthesis | Same |
| 28 | u32 | Dtype 1 = BF16 | Dtype 2 = IEEE-754 FP32 |
| 32 | 32 bytes | Model compatibility ID | Required compatible model ID |
| 64 | 32 bytes | Original model SHA256 | Original voice safetensors SHA256 |
| 96 | 32 bytes | Whole-container SHA256 | Same |
| 128 | 32 bytes | Payload SHA256 | Same |
| 160 | u64 | Exact total file bytes | Same |
| 168 | u64 | Record table starts at 256 | No records; 256 |
| 176 | u64 | Payload starts at 11072 | Payload starts at 256 |
| 184 | u64 | Aligned payload bytes | K/V payload bytes |
| 192 | u32 | Tensor count 169 | Layer count 6 |
| 196 | u32 | Record bytes 64 | Prefix length N |
| 200 | u32 | FlowLM width 1024 | Heads 16 |
| 204 | u32 | FlowLM layers 6 | Head dimension 64 |
| 208 | u32 | Latent width 32 | Batch size 1 |
| 212 | u32 | Sample rate 24000 | Same |
| 216 | u32 | Frame samples 1920 | Same |
| 220 | u32 | Flags 0 | Flags 1: K is already post-RoPE |
| 224 | u64 | Total elements 99719330 | Total elements `6 * 2 * N * 16 * 64` |
| 232 | u32 | Voice schema 0 (unused) | Pocket voice schema 1 |
| 236 | 20 bytes | Reserved, zero | Same |

The payload hash covers bytes from the payload offset through EOF. The
container hash covers the entire file, including header, records and padding,
with only bytes 96..127 substituted by zeros while hashing. Thus header
metadata and tensor records are also checked. These hashes provide integrity;
they do not authenticate a publisher.

## `.olp` tensor records and exact BF16 storage

Exactly 169 records follow the header, in ID order. Each 64-byte record is:

| Relative offset | Type | Meaning |
|---|---|---|
| 0 | u32 | Stable tensor ID |
| 4 | u32 | Dtype 1 = BF16 |
| 8 | u32 | Rank, exact compiled schema value |
| 12 | u32 | Flags 0 |
| 16 | 4 u32 | Dimensions; unused entries zero |
| 32 | u64 | Absolute payload offset |
| 40 | u64 | Raw tensor bytes = elements * 2 |
| 48 | u64 | Element count |
| 56 | 8 bytes | Reserved, zero |

Each tensor starts on a 64-byte boundary. Tensors retain source row-major
ordering and exact BF16 bytes. Zero padding rounds each tensor end to 64
bytes, including the last tensor. There is no quantization or permanent FP32
conversion. Raw weight bytes total 199,438,660; padding totals 124 bytes.
The payload is 199,438,784 bytes; header/table add 11,072 bytes; the complete
model is **199,449,856 bytes**.

BF16 widening constructs FP32 bits as `uint32_t(bf16_bits) << 16`, then uses
`std::bit_cast<float>`. It adds no arithmetic or rounding. The caller can use
`widen_bf16(input, output)` with its own allocation or a tensor view's
`to_float32()` for an explicit allocation. Tests compare the complete exported
model against independently generated PyTorch FP32 bytes, and all 65,536
BF16 patterns, including signed zeros, subnormals, infinities and NaN payloads.

## `.olv` ready-to-synthesize voice payload

The payload is six consecutive layers, numbered 0..5. Each layer contains the
original contiguous FP32 cache `[2,1,N,16,64]`: all K values followed by all V
values. Flattened indexing is `[K_or_V, batch, prefix_position, head, component]`.
K already includes RoPE and must not be rotated again during prefix import.
Each layer starts on a 64-byte boundary without extra padding, since its size
is `2 * N * 16 * 64 * 4` bytes. Payload bytes are `49152 * N`.

The converter requires the complete six-layer source schema: caches of that
shape and dtype, six I64 `[1]` offsets equal to N, and six I64 `[1]` pads equal
to zero. All layers must have the same positive prefix and finite caches with
no unused positions. It then copies the K/V bytes without conversion. The
duplicate offsets and zero pads are omitted: a future inference state can
reconstruct offset=N and pad=0 for every layer. No neural mutable state is
allocated by the current loader.

| Voice | Prefix N | Payload bytes | Complete `.olv` bytes |
|---|---|---|---|
| `gnome_male` (canonical) | 112 | 5,505,024 | **5,505,280** |
| `undead_male` | 105 | 5,160,960 | **5,161,216** |

No WAV, Mimi encoder/decoder state, RNG, Python objects or Hugging Face metadata
is stored. Version 1 has no optional voice display metadata. Future schema or
layout changes require a new explicit version; unknown versions/flags/reserved
fields fail closed. A future Voice Creator can produce this same contract
after computing a prefix using the corresponding model. It must export valid
post-RoPE K/V prefixes, the actual model compatibility ID and integrity fields.
The format represents a prepared voice for synthesis, not a training format.
Voice Creator and audio extraction are not implemented here.

## Compatibility and validation

Compatibility ID is SHA256 of the following byte concatenation:

```text
ASCII("OutLoud/Pocket/English/3.3.0/synthesis-v1") + 00
+ raw32(original model SHA256)
+ raw32(pinned original tokenizer SHA256)
```

The tokenizer SHA is
`f498428e1eafee50492f7be13dc9bfafcfc12e508cd0eb1b01c92ecd5d8c6687`;
the resulting compatibility ID is
`993a3a9749dc64d60815aa0e71fa4c18f53d478d05d0c4da8c7c77164387c59a`.
The synthesis schema in the domain string binds the omission/layout contract.
Changing the model, tokenizer or synthesis contract requires a new identity.

Model construction validates the pinned source, compatibility ID and pinned
payload identity, exact file length, every record/dimension/size/offset, zero
padding, reserved fields and both computed hashes. Voice construction validates
the fixed header/layout, nonzero source/compatibility identities, bounded
length, positive prefix, finite FP32 data and both computed hashes. Source
voice SHA is retained as provenance, rather than restricting all future voices
to today's two hashes. `voice.validate_against(model)` must be called before
using the pair; it rejects a different compatibility ID. Voice payloads are
limited to 1 GiB. Truncation, trailing bytes, invalid bounds, mismatched types,
dimensions, versions and integrity hashes are rejected before exposing views.

The offline voice converter verifies the known original source SHA, or requires
an explicit expected `--source-sha256` for another local voice. It assigns this
fixed model's compatibility ID. **UNVERIFIED:** existing voice safetensors do
not independently establish which model originally created them. Validating
their source identity and cache schema cannot prove that provenance. A new
voice must actually have been prepared with this model; specifying a SHA does
not establish semantic compatibility. The checks here establish conversion
fidelity and declared pairing, not neural or waveform parity.

The historical undead hash in [UPSTREAM.md](../reference/UPSTREAM.md) has only
63 hex characters. That existing reference remains unchanged. The actual
64-character SHA256 recomputed from the local file is
`dce632cddb3a33203e140c73b2fa3970325bfbef3984813ddfe3e43e328037b6`,
recorded in the new [asset-baseline.json](../reference/asset-baseline.json).
Canonical gnome identity remains
`2118ea60dfe87bd515b27e051e1d677a7fe15663875740599147bde37a9c4cea`.

## Native API and memory

```cpp
outloud::PocketModelAsset model(model_path); // validates on construction
outloud::OutLoudVoice voice(voice_path);     // validates on construction
voice.validate_against(model);
auto weights = model.tensor(outloud::PocketTensorId::flow_lm_input_linear_weight);
auto fp32 = weights.to_float32();           // optional, explicit allocation
auto keys = voice.keys(0);                 // immutable [N,16,64] view
auto values = voice.values(0);
```

Model and voice own read-only mappings; views remain valid while an owning
asset lives. File handles disallow simultaneous writes while mapped. Model
validation scans the entire payload but does not copy it or widen all weights.
The model occupies about 190.210 MiB of mapped address space; canonical voice
adds 5.250 MiB. Residency depends on Windows paging, and hashing touches every
page. If a later backend elects to widen every weight, FP32 data alone would
require another 398,877,320 bytes (380.399 MiB), in addition to mappings and
future mutable state. This is not the current loader's allocation policy. The
development checker widens one tensor at a time for reference comparisons.

## Offline conversion and build

Run from the repository root using the existing local environment. No package
installation, download or model execution is needed. Tools force
`HF_HUB_OFFLINE=1`, read local files only and verify source hashes. The default
model input is its pinned local Hugging Face cache file; `--input` on schema
and model exporters can select another local copy with the same SHA256.
Model and voice exporters accept `--output` only under `generated/assets/`.
All generated binaries and fixture headers remain Git-ignored.

```powershell
# Step 2B tokenizer assets must also already exist; see NATIVE_TEXT_PIPELINE.md.
# Audit/regenerate the tracked numeric mapping only from the pinned source:
.venv\Scripts\python.exe -B tools\assets\export_schema.py
.venv\Scripts\python.exe -B tools\assets\export_model.py
.venv\Scripts\python.exe -B tools\assets\export_voice.py
# If this optional reference voice is present:
.venv\Scripts\python.exe -B tools\assets\export_voice.py --input voices\undead_male.safetensors
.venv\Scripts\python.exe -B tools\assets\export_test_fixtures.py
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
.\build\Release\outloud-asset-check.exe
ctest --test-dir build -C Release --output-on-failure
git diff --check
git status
```

Schema regeneration writes the two tracked schema files; review any change.
Fixture generation uses the existing NumPy/PyTorch installation only to
compute original source byte hashes and exact widening references. It writes
the tracked asset baseline and ignored `asset_test_fixtures.hpp` /
`bf16-f32-fixture.bin`. Those references are development inputs, not library
runtime requirements. CMake never invokes Python and requires the exported
fixture header to build the check/test executables. `OUTLOUD_ASSET_DIRECTORY`
can override their asset/fixture directory. The checker accepts an optional
directory argument; otherwise it uses `generated/assets` relative to the
current directory. The large model is not copied into the build directory.

Observed Release x64 validation with MSVC 19.44.35229 and `/W4 /WX`:

- Checker: PASS, including exact original model and canonical voice bytes,
  full-model PyTorch widening and all 65,536 BF16 patterns.
- Asset regressions: both voices, selected tensor dimensions, every model
  record, invalid view indices, corruption/truncation/trailing data, bad hashes,
  reserved fields, non-finite voice values and incompatible model pairing PASS.
- CTest: 4/4 PASS, including unchanged canonical tokenizer and text regressions.

Existing oracle/text baseline files are unchanged. No claims about inference,
RNG, PCM or SAPI behavior are made by these asset tests.
