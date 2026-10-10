# Pocket TTS model attribution

Publisher: **Kyutai**. Credited authors recorded in A04: Manu Orsini, Simon Rouard,
Gabriel De Marmiesse, Václav Volhejn, Neil Zeghidour and Alexandre Défossez.
This credit does not invent a copyright assignment or give Nendis ownership of
the original weights.

## Source and license

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
[A04, sections 6–7](../docs/LICENSING_AND_REDISTRIBUTION_A04.md).
No asset was regenerated or modified when this attribution was added.

## Unresolved additional policy/access terms

**Overall model bundling permission is not finally cleared.** The pinned metadata
declares CC BY 4.0 and also publishes prohibited-use wording. A04 did not establish
the legal effect of that wording, any applicable accepted access agreement, or its
redistribution/downstream implications. The pinned raw model card was gated; the
exact-revision API supplied the verified license/policy evidence.

The CC grant permits commercial and free reuse under its conditions. This document
does not reinterpret the separate policy as a new CC restriction, dismiss it as
ineffective, or claim it has been resolved. Attribution and inclusion of the full
CC text do not settle that uncertainty. The model is an intended installer asset
only after the remaining applicable conditions are established.
