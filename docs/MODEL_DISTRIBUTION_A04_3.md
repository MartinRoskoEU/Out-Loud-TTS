# A04.3: model distribution verification

Date: **2026-10-10**. Entry HEAD: `f97900beacb95842f3824cf57dc5b831a66caec7`;
clean working tree. **Decision A — BUNDLE EXISTING MODEL.** The existing OLP's
169 inference tensors match the pinned public Kyutai checkpoint bit for bit.
The existing tokenizer is also publicly sourced. Their published terms support
bundling with attribution; no concrete bundling prohibition was identified.

## 1. Objective

Determine whether A03 can package the unchanged model and tokenizer, using the
existing A04/A04.1/A04.2 evidence and a focused source-to-OLP comparison. No
engine, converter, format, voice, installer or deployment change is made.
This decision concerns these two assets, not clearance of every installer component.

## 2. Existing model identity

Read-only hashes of both `generated/assets/pocket-english.olp` and the deployed
`C:\OutLoud\OutLoud-Pocket-development-01\runtime\pocket-english.olp` agree:

| Field | Verified value |
| --- | --- |
| File size | 199,449,856 bytes |
| SHA-256 | `30889426a3bed523e725baafe70ec9064044898aa203975980741be1f3d70406` |
| Included tensors / BF16 elements | 169 / 99,719,330 |
| Aligned payload SHA-256 | `dc804d42e457b49614f279d3656d81cf3f493df7459c8ecb9c4c423ece8feeac` |
| Compatibility ID | `993a3a9749dc64d60815aa0e71fa4c18f53d478d05d0c4da8c7c77164387c59a` |

Historical conversion input remains `kyutai/pocket-tts`, revision
`983151f13aaeab1b13c1e5e3c2c383d49a9edf3f`,
`languages/english/model.safetensors`, SHA-256
`fb0dc01b0d4d2e1c905b7a3e0676e3d9c96d5ae460e24e3ab94981805babf997`.
The original cached checkpoint was independently hash-checked. Its identity in
the OLP header and compatibility calculation is preserved; public-source
verification does not rewrite the actual conversion/download history.

## 3. Public Kyutai source and pinned revision

Repository: [`kyutai/pocket-tts-without-voice-cloning`](https://huggingface.co/kyutai/pocket-tts-without-voice-cloning).
Distribution provenance is pinned to **`00eac05ed3d16bdc3f6b5d598874019c34a89214`**,
file **`languages/english/model.safetensors`**, 219,029,196 bytes, SHA-256
**`916ccd2686e9311cb40054893a3c4284393d658825ffc714a276f3e9b152344f`**.
[Exact-revision metadata](https://huggingface.co/api/models/kyutai/pocket-tts-without-voice-cloning/revision/00eac05ed3d16bdc3f6b5d598874019c34a89214?blobs=true)
reports that LFS checksum, **CC-BY-4.0**, and **gated=false**.
An anonymous HEAD request to the
[pinned checkpoint](https://huggingface.co/kyutai/pocket-tts-without-voice-cloning/resolve/00eac05ed3d16bdc3f6b5d598874019c34a89214/languages/english/model.safetensors)
returned HTTP 200 and the expected size, without downloading the body.

The [public history](https://huggingface.co/kyutai/pocket-tts-without-voice-cloning/commits/main)
and revision metadata show an August 25 English checkpoint with a different
checksum (`be9c6b48…`). The
[September 17 upload](https://huggingface.co/kyutai/pocket-tts-without-voice-cloning/commit/e7205b6ee50e654a5ea19f0e9df2b0813b05e921)
introduced the selected checksum; the
[September 21 revision](https://huggingface.co/kyutai/pocket-tts-without-voice-cloning/commit/00eac05ed3d16bdc3f6b5d598874019c34a89214)
added tokenizer JSON files and retained that exact model blob. Later multilingual
and voice updates are outside this pin. Neither an older nor the latest model
was assumed equivalent.

The already cached September 17 file has the published checksum at both revisions
and was reused directly: **zero checkpoint downloads**. Its 214 BF16 tensor
names/shapes match the reviewed English schema, including FlowLM layers **0–5**,
width 1024 and embedding shape `[4001, 1024]`. There is no safetensors
`__metadata__` entry; architecture evidence comes from the tensor schema, not
invented container metadata.

## 4. Official no-cloning transformation

Kyutai's [pinned `remove_voice_cloning_and_push.py`](https://huggingface.co/kyutai/pocket-tts-without-voice-cloning/raw/00eac05ed3d16bdc3f6b5d598874019c34a89214/remove_voice_cloning_and_push.py)
applies `torch.zeros_like` to names starting with **`mimi.encoder`** and retains
other tensors. That prefix includes both `mimi.encoder.` and
`mimi.encoder_transformer.`. It processes the legacy checkpoint and recursively
all `model.safetensors` files in a temporary copy before publishing to the public
repository. Script SHA-256:
`38176436a24020ee5fdc015325bbfad63b21f0b215b70b483f00b7e34d2e97b4`.
The script was read, not executed.

## 5. Included/excluded encoder tensors

The [reviewed mapping](../reference/pocket-model-schema.json) excludes:

| Source group | Excluded tensors |
| --- | ---: |
| `mimi.encoder.` | 22 |
| `mimi.encoder_transformer.` | 20 |
| `mimi.downsample.` | 1 |
| `flow_lm.speaker_proj_weight` | 1 |
| `flow_lm.bos_before_voice` | 1 |
| **Total** | **45** |

**A: all 42 encoder-prefix tensors are completely absent from the OLP**, rather
than present but unused. The public checkpoint contains zeros for all 42. Direct
comparison against the historical source found exactly those 42 tensors changed;
the other 172 are byte-identical. Zeroing excluded tensors cannot change the OLP
inference payload. Synthesis decoder tensors remain included.

The native engine imports prepared voice states; it does not turn reference
recordings into new speaker embeddings. This does not establish that imported
voices cannot be misused for impersonation.

## 6. Tensor comparison method

Read the existing [exporter](../tools/assets/export_model.py),
[format helpers](../tools/assets/asset_format.py) and schema. One-off Python
standard-library inspection reused the existing read-only safetensors parser
and memory maps; no dependency installation or asset conversion occurred.

Verified both source hashes, complete 214-key schema, BF16 dtypes, shapes,
source bounds and contiguous data. For each of the **169 OLP records, IDs 0–168**,
verified its schema name, ID, dtype, rank, dimensions, size, element count,
offset and reserved fields. Compared all raw tensor bytes directly, without
floating-point decoding or tolerances: little-endian, row-major BF16 is copied
unchanged. There is no numerical conversion, retraining or quantization.

Also verified the 256-byte header, historical source/compatibility fields,
whole-container integrity field, payload hash, 64-byte alignment and zero
padding. The OLP adds its own table/header/padding and omits 45 tensors; its
whole-file hash is not expected to equal the safetensors hash. Comparison covers
**199,438,660 tensor bytes**, not just aggregate checksums or output audio.

## 7. Comparison results

| Result | Count |
| --- | ---: |
| Compared OLP tensors | 169 |
| Bit-exact matches | **169** |
| Mismatches | **0** |
| Missing source names / OLP IDs | **0 / 0** |
| Unexpected source tensors outside the complete reviewed schema | **0** |
| Encoder-prefix tensors included in OLP | **0** |

**MATCH.** Source and OLP identities are recorded in sections 2–3. All comparisons
completed; none relied on approximate tolerances or synthesis. Temporary summary
evidence is in `%LOCALAPPDATA%\Temp\outloud-a043-comparison.json`; no permanent
tool or large duplicate checkpoint was added to the repository.

## 8. Tokenizer status

The tokenizer uses the same public repository/pin, file
**`languages/english/tokenizer.json`**. Anonymous retrieval matched the cached
245,020-byte source exactly: SHA-256
**`f498428e1eafee50492f7be13dc9bfafcfc12e508cd0eb1b01c92ecd5d8c6687`**.
Its Git blob identity `fe3e7fe6185e4a3bc218fa8f5ed993ecb098201c` also matches
the published metadata. It contains the expected 4,000-piece Unigram vocabulary.
The pinned repository declares **CC-BY-4.0** for these assets.

Workspace and deployed binary tokenizer both remain 93,325 bytes, SHA-256
**`45ffaf8c702dbb6c954bd54280d83ea00f8bf1d47692aca37e078c8deb26935a`**.
Existing [tokenizer attribution](../licenses/Pocket-Tokenizer-Attribution.md) and
[Unicode notice](../licenses/Unicode-Data.txt), Unicode-DFS-2016, copyright
1991–2022 Unicode, Inc., are unchanged. No rebuild was needed.

## 9. Redistribution conditions

[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/legalcode.en) sections
2(a)(1)/(4) allow sharing whole or partial material and format changes, including
free/commercial distribution. Section 3 requires supplied creator/copyright,
license/disclaimer and source notices, plus modification information. Keep the
Kyutai/author credits, pinned links, conversion/subsetting description, prior
change indications and full CC text. Do not imply endorsement or relabel the
weights/vocabulary MIT. Section 2(a)(5) preserves recipients' licensed rights;
do not impose incompatible downstream restrictions or DRM.

The [pinned card](https://huggingface.co/kyutai/pocket-tts-without-voice-cloning/raw/00eac05ed3d16bdc3f6b5d598874019c34a89214/README.md)
also publishes a prohibited-use policy in its body and `extra_gated_prompt`.
It addresses unlawful/harmful/deceptive/unauthorized uses, including nonconsensual
cloning or impersonation, misleading recordings, harassment and privacy abuse;
it does **not** prohibit redistribution or require each recipient to download
weights, seek approval, or use audio-based cloning. Retain its source reference
and respect that policy for intended lawful narration and authorized voice use.
Encoder exclusion alone does not discharge consent obligations for imported voices.

Public retrieval required no login, access application or assent screen despite
the card's inherited gate-form fields. No separate applicable agreement preventing
this bundling was evidenced. CC section 7(b) leaves actual separate agreements
independent: this review does not cancel any historical agreement or claim that
CC invalidates the policy. The earlier gated acceptance history was not recovered;
that is not evidence of a redistribution ban on the independently published
matching payload. No material unresolved fact was identified that requires a
new model conversion or user-only download for this decision.

## 10. Final decision

**A — BUNDLE EXISTING MODEL.** Exact public payload provenance and the published
redistribution grant support packaging the current OLP and tokenizer with the
notices above. B is unnecessary because no included weight differs; C has no
identified blocking condition; D is unnecessary because the decisive comparison
and public-source terms were obtained.

Added this report and updated only [model attribution](../licenses/Pocket-Model-Attribution.md)
and [third-party notices](../THIRD_PARTY_NOTICES.md) with the public matching source
and current decision. Historical A04/A04.1/A04.2 reports remain unchanged. This
assessment supersedes their pending asset-policy conclusion, including the
earlier pending paragraph retained in the unchanged tokenizer attribution.

## 11. Practical implications for A03 installer

A03 **can bundle both existing files unchanged**. No conversion and no mandatory
end-user weight download are needed. Pin their existing hashes and package the
model/tokenizer attributions, CC legal code, Unicode notice, third-party notices
and this current assessment. Preserve the original OLP source binding and add
the public revision as independently verified matching payload provenance.
Continue excluding all standalone voices/recordings and development fixtures.
Microsoft runtime/host packaging decisions remain separate A03 work.

Lightweight verification: documentation links checked, entry-file hashes reviewed,
workspace/deployed asset hashes rechecked and `git diff --check` passed. No builds,
CTest, automated application tests, PCM campaign or benchmarks ran. No engine,
converter, production asset, registry, certificate or deployment file changed.
No installer implementation, signing, installation, staging, commit or push occurred.
