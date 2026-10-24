# Pocket TTS model attribution

Publisher: **Kyutai**. Credited authors recorded in A04: Manu Orsini, Simon Rouard,
Gabriel De Marmiesse, VĂˇclav Volhejn, Neil Zeghidour and Alexandre DĂ©fossez.
This credit does not invent a copyright assignment or give Nendis ownership of
the original weights.

## Historical conversion source and license

- Repository: [`kyutai/pocket-tts`](https://huggingface.co/kyutai/pocket-tts).
- Revision: `983151f13aaeab1b13c1e5e3c2c383d49a9edf3f`.
- Original asset: `languages/english/model.safetensors`.
- Declared asset license: **Creative Commons Attribution 4.0 International**,
  SPDX **CC-BY-4.0**, as verified in the
  [exact-revision repository metadata](https://huggingface.co/api/models/kyutai/pocket-tts/revision/983151f13aaeab1b13c1e5e3c2c383d49a9edf3f?blobs=true).
- Full official terms: [CC BY 4.0 legal code](https://creativecommons.org/licenses/by/4.0/legalcode.en),
  with an unmodified offline copy in [CC-BY-4.0.txt](CC-BY-4.0.txt).

The model is separately licensed; Out Loud's [MIT license](../LICENSE) does not
apply to the original model weights. Retain this attribution, supplied upstream
notices, license identification/link and modification information with any permitted
redistribution. The official legal code governs and includes its warranty disclaimer;
this document adds no endorsement or additional restriction to the CC grant.

## Conversion and exact artifact identity

Out Loud's [exporter](../tools/assets/export_model.py) places an inference subset
of the original checkpoint in OLP version 1. It retains **169 of 214 tensors**
(99,719,330 BF16 elements) and omits 45 encoder/conditioning-related tensors.
Included BF16 tensor bytes and row-major layout are preserved; numeric records,
container metadata and 64-byte alignment/padding are added. This is a subset/container
conversion, not retraining, quantization or a new independently owned model.
No standalone OLV voice or voice recording is added to the OLP.

| Artifact | Size | SHA-256 |
| --- | ---: | --- |
| Original checkpoint | 219,029,196 bytes | `fb0dc01b0d4d2e1c905b7a3e0676e3d9c96d5ae460e24e3ab94981805babf997` |
| `pocket-english.olp` | 199,449,856 bytes | `30889426a3bed523e725baafe70ec9064044898aa203975980741be1f3d70406` |
| Aligned OLP payload | 199,438,784 bytes | `dc804d42e457b49614f279d3656d81cf3f493df7459c8ecb9c4c423ece8feeac` |

Compatibility ID:
`993a3a9749dc64d60815aa0e71fa4c18f53d478d05d0c4da8c7c77164387c59a`.
These identities and changes were verified in
[A04, sections 6â€“7](../docs/LICENSING_AND_REDISTRIBUTION_A04.md).
No asset was regenerated or modified when this attribution was added.

## Independently verified public matching payload

[A04.3](../docs/MODEL_DISTRIBUTION_A04_3.md) verified the existing OLP against:

- Public repository: [`kyutai/pocket-tts-without-voice-cloning`](https://huggingface.co/kyutai/pocket-tts-without-voice-cloning).
- Pinned revision: `00eac05ed3d16bdc3f6b5d598874019c34a89214`.
- File: `languages/english/model.safetensors`, 219,029,196 bytes.
- SHA-256: `916ccd2686e9311cb40054893a3c4284393d658825ffc714a276f3e9b152344f`.
- [Exact metadata](https://huggingface.co/api/models/kyutai/pocket-tts-without-voice-cloning/revision/00eac05ed3d16bdc3f6b5d598874019c34a89214?blobs=true)
  and [card](https://huggingface.co/kyutai/pocket-tts-without-voice-cloning/raw/00eac05ed3d16bdc3f6b5d598874019c34a89214/README.md):
  **CC-BY-4.0**, nongated; anonymous checkpoint access verified.

**169/169 included tensors match bit for bit; zero mismatches or missing IDs.**
Kyutai's [published transformation](https://huggingface.co/kyutai/pocket-tts-without-voice-cloning/raw/00eac05ed3d16bdc3f6b5d598874019c34a89214/remove_voice_cloning_and_push.py)
zeros 42 `mimi.encoder`-prefix tensors, all excluded from OLP. Other source
tensors match the historical checkpoint. This is additional verified payload
provenance, not a claim that the original conversion downloaded this public file.
The existing OLP bytes, header source hash and compatibility ID remain unchanged.

## Redistribution assessment

**A04.3 decision A: bundle the unchanged OLP with attribution.** The public CC
grant supports redistribution; no concrete bundling restriction was identified.
Retain the credits, source links, license/disclaimer and conversion notice above.
Preserve the public card's prohibited-use policy reference: lawful use and consent
remain relevant, including for imported voices. The native engine does not
implement audio-to-voice cloning, but that does not make all impersonation impossible.

The policy is not treated as a redistribution prohibition or declared ineffective.
No separate agreement preventing bundling was evidenced in public access; actual
separate agreements remain independent under CC section 7(b). This assessment
supersedes the earlier pending asset conclusion without rewriting gated-source
history or claiming that past agreements are cancelled. Installer component
licensing beyond this asset remains separate.

## Active development checkpoint (engine 0.2.0)

Official stable English: kyutai/pocket-tts@7f364c05d1be3faf2b234cdbe4cbb68a026d4218.
Source SHA-256: `7767ce79b1b8db523d867e70855a00627aacfe44aaa5c1fd7ebe135fba71fff9`.
Configuration: https://raw.githubusercontent.com/kyutai-labs/pocket-tts/9aa7a7f0e52a4cd81bcf8a0fc7c981cdc6d11e53/pocket_tts/config/english.yaml.
OLP SHA-256: `fe8c40e3a773465974ce965e57f01c8904ac4cb2cf7086c42cbb151f8815ba3e`.
Same synthesis-only BF16 conversion and license/policy obligations. Historical release artifacts are retained.

## Active checkpoint (Out Loud 0.3.0)

Official Kyutai `english_2026-10`, selected by Pocket TTS v4.0.1
(`a922b912dfa366975294afb568d815b305acde5d`). Source repository: `kyutai/pocket-tts`;
file: `languages/english/model.safetensors`; immutable revision:
`db04f0e653473e5c7333c69686ac92b7460b0c1d`.

Source SHA-256: `bc96b82436c5ec00d3118ef3b01bc3358423e1766ecae3dca0aeea84789e7d74`.

OLP SHA-256: `b7befdd6647031154a490856b7b59bac3a0d813d9e1f65c621928d2640e0d5c4`.

The checkpoint model card at this revision declares CC BY 4.0. Its prohibited-use
paragraph matches the already bundled policy. Existing license and consent
behavior is preserved. The OLP includes the same 169 synthesis tensors as exact
BF16 bytes; 45 preparation-only tensors are excluded. No quantization or
precision reduction is applied. See `docs/releases/0.3.0.md` for verification,
tokenizer identity and voice-quality limitations. Historical provenance above
describes earlier assets and is retained.
