# Pocket tokenizer attribution

Publisher: **Kyutai**. The learned vocabulary originates from the separately
identified repository below, not the model checkpoint's repository revision.
It is not owned by Nendis merely because Out Loud converts or uses it.

## Source and license

- Repository: [`kyutai/pocket-tts-without-voice-cloning`](https://huggingface.co/kyutai/pocket-tts-without-voice-cloning).
- Revision: `00eac05ed3d16bdc3f6b5d598874019c34a89214`.
- Original asset: `languages/english/tokenizer.json`.
- Declared asset license: **Creative Commons Attribution 4.0 International**,
  SPDX **CC-BY-4.0**, verified through the
  [exact-revision metadata](https://huggingface.co/api/models/kyutai/pocket-tts-without-voice-cloning/revision/00eac05ed3d16bdc3f6b5d598874019c34a89214?blobs=true)
  and [pinned repository card](https://huggingface.co/kyutai/pocket-tts-without-voice-cloning/raw/00eac05ed3d16bdc3f6b5d598874019c34a89214/README.md).
- Full official terms: [CC BY 4.0 legal code](https://creativecommons.org/licenses/by/4.0/legalcode.en),
  copied unmodified into [CC-BY-4.0.txt](CC-BY-4.0.txt).

Retain supplied creator/source notices, license identification/link and the conversion
notice with any permitted redistribution. The legal code, including its warranty
disclaimer, governs; this document adds no endorsement or downstream restriction.
The learned vocabulary is **not MIT**, and software-tool Apache/MIT licenses do not
replace its asset terms.

## Conversion and exact artifact identity

[export_tokenizer.py](../tools/tokenizer/export_tokenizer.py) serializes the pinned
4,000-piece Unigram vocabulary, FP64 scores, special/byte-fallback/configuration
data into Out Loud's binary tokenizer. It adds generated Unicode property/case
tables for native text preparation. The binary does not embed the Python interpreter,
SentencePiece or Hugging Face Tokenizers implementation.

| Artifact | Size | SHA-256 |
| --- | ---: | --- |
| Original `tokenizer.json` | 245,020 bytes | `f498428e1eafee50492f7be13dc9bfafcfc12e508cd0eb1b01c92ecd5d8c6687` |
| `pocket-english-tokenizer.bin` | 93,325 bytes | `45ffaf8c702dbb6c954bd54280d83ea00f8bf1d47692aca37e078c8deb26935a` |

These source/output identities and transformations were verified in
[A04, section 8](../docs/LICENSING_AND_REDISTRIBUTION_A04.md).
No tokenizer data was modified or regenerated for this notice.

## Separately licensed Unicode data

The binary includes Unicode **15.0.0** whitespace/uppercase/digit property ranges
and uppercase mappings generated through Python 3.12's Unicode database. Preserve
the historical Unicode copyright/permission notice in [Unicode-Data.txt](Unicode-Data.txt),
copyright 1991–2022 Unicode, Inc.; the relevant license is **Unicode-DFS-2016**.

That notice was extracted verbatim from the Unicode data/software section of
[ICU release 72.1's license](https://github.com/unicode-org/icu/blob/release-72-1/icu4c/LICENSE).
ICU is a source preserving the historical notice; **Out Loud does not ship ICU**.
The Pocket vocabulary, Unicode-derived tables and Python conversion tooling have
distinct roles and terms. Neither table generation nor storage in this binary makes
the Unicode data MIT or bundles the Python toolchain.

## Redistribution assessment

**A04.3 decision A: the existing binary tokenizer may be bundled unchanged with
the required license and attribution materials.**
[A04.3 verification](../docs/MODEL_DISTRIBUTION_A04_3.md) established public access
to the pinned source and verified the existing source/binary identities above.
CC BY 4.0 permits redistribution subject to its attribution and other license
conditions. No concrete condition preventing bundling or requiring individual
user downloads was found. No tokenizer conversion or rebuild is required.

Retain Kyutai/source credits, license/disclaimer and conversion notices, together
with the separate Unicode notice. Preserve the
[pinned upstream card's prohibited-use policy](https://huggingface.co/kyutai/pocket-tts-without-voice-cloning/raw/00eac05ed3d16bdc3f6b5d598874019c34a89214/README.md)
reference; this assessment does not authorize harmful or unauthorized use.
No separate agreement preventing bundling was evidenced in A04.3. Actual separate
agreements remain independent under CC section 7(b); CC BY 4.0 does not override
them. This current assessment supersedes the earlier pending tokenizer-policy
conclusion while preserving the original source and conversion history.
