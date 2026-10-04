# Native Pocket English text pipeline (Step 2B)

The C++20 `outloud_text` library implements the fixed Pocket TTS 3.3.0 English
text path described in [POCKET_PIPELINE.md](POCKET_PIPELINE.md). It has no neural
inference, model/voice loading, PCM, .olv, COM or SAPI code. Runtime execution
uses C++ and local tokenizer data only. No third-party dependency was added;
the asset loader uses Windows' built-in BCrypt SHA256 implementation.

## Build and run on Windows x64

Use the repository's existing Python 3.12 environment only for offline exports:

```powershell
.venv\Scripts\python.exe -B tools\tokenizer\export_tokenizer.py
.venv\Scripts\python.exe -B tools\tokenizer\export_test_fixtures.py
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
.\build\Release\outloud-tokenizer-check.exe
ctest --test-dir build -C Release --output-on-failure
```

The tokenizer converter reads the pinned tokenizer JSON already in the local
cache, verifies its SHA256, and never contacts Hugging Face. Use `--input` to
specify another local copy of the same file. `HF_HUB_OFFLINE=1` is forced.
Python 3.12's Unicode 15.0.0 data is required to export preparation tables.
Neither CMake, compilation, the native checker nor CTest invokes Python.

The fixture converter reads the tracked baseline and regression JSON; canonical
IDs are not maintained manually in C++ source. It emits a private header for
test executables. Rerun it after an intentional fixture change. CMake requires
the exported asset and header to exist and accepts overrides through
`OUTLOUD_TOKENIZER_ASSET` and `OUTLOUD_FIXTURE_HEADER` (the header's basename
must remain `native_text_fixtures.hpp`). All generated files remain Git-ignored.

The build copies the tokenizer asset beside each executable. Run the checker
with no text to execute canonical parity, or supply text:

```powershell
.\build\Release\outloud-tokenizer-check.exe --text "Where is the scout?"
.\build\Release\outloud-tokenizer-check.exe --text-file .\generated\quest.txt
.\build\Release\outloud-tokenizer-check.exe --asset .\generated\tokenizer\pocket-english-tokenizer.bin
.\build\Release\outloud-text-tests.exe .\generated\tokenizer\pocket-english-tokenizer.bin
```

Text files contain UTF-8 bytes; no additional BOM removal or cleanup is applied.
Command-line text arrives as UTF-16 on Windows and is explicitly converted to
UTF-8. A parity mismatch, invalid text or invalid/missing asset returns nonzero.
Custom input is printed for inspection; `RESULT: OK` does not claim parity
against a separately generated reference for that custom text.

## Native strategy and verified behavior

`Tokenizer` builds a small byte trie from the fixed 4000-piece vocabulary.
Encoding uses double-precision Viterbi scores at UTF-8 scalar boundaries,
shortest-first prefix traversal and strict `>` replacement (equal scores keep
the existing path). Unknown scalars score `minimum_vocabulary_score - 10`,
adjacent unknowns fuse, then their bytes map to the pinned fallback IDs.
These decisions were checked against the
[tokenizers v0.23.2 Unigram source](https://github.com/huggingface/tokenizers/blob/v0.23.2/tokenizers/src/models/unigram/model.rs)
and its [prefix traversal](https://github.com/huggingface/tokenizers/blob/v0.23.2/tokenizers/src/models/unigram/trie.rs).
The wheel contains bindings, not those Rust implementation sources; reading
the matching official tag filled that local-source gap. No Rust runtime or
source dependency is included in the build.

The four literal special tokens are isolated before ordinary text normalization,
matching the pinned flags. Each remaining nonempty span receives the
normalizer's U+2581 prefix. Only ASCII spaces become additional markers;
Metaspace splits each marker with following content. There is no automatic
BOS/EOS, NFKC or lowercase conversion. Decode skips special IDs by default,
removes all markers in the first remaining token, converts later markers to
spaces, decodes consecutive fallback bytes and fuses the results. Invalid byte
groups become one U+FFFD per byte. Unknown decoder IDs are skipped. These rules
follow the matching [Prepend](https://github.com/huggingface/tokenizers/blob/v0.23.2/tokenizers/src/normalizers/prepend.rs),
[Metaspace](https://github.com/huggingface/tokenizers/blob/v0.23.2/tokenizers/src/pre_tokenizers/metaspace.rs),
[added vocabulary](https://github.com/huggingface/tokenizers/blob/v0.23.2/tokenizers/src/tokenizer/added_vocabulary.rs)
and [ByteFallback](https://github.com/huggingface/tokenizers/blob/v0.23.2/tokenizers/src/decoders/byte_fallback.rs)
implementations, and local wheel probes.

Preparation and splitting were ported directly from the untouched installed
`pocket_tts/models/text_chunking.py`, with the executed English options:
no short-input padding, no character rewrites, semicolons preserved,
capitalization and terminal punctuation enabled. The space replacement is
one non-overlapping pass; it does not fully collapse runs. Unicode whitespace,
uppercase classification/full first-scalar uppercase expansion and digit
classification come from exported Python 3.12 tables, avoiding locale-dependent
`toupper`, `iswspace` or regex behavior. Invalid UTF-8 is rejected.

Splitting retains the exact token boundary IDs, grouped marks, decoded-prefix/
suffix decimal checks, fallback punctuation subdivision, greedy token-count
grouping, and decode/strip/reprepare/retokenize flow. The default limit is 50
and remains soft for an oversized sentence without usable fallback boundaries.
`frames_after_eos` is metadata only: upstream's word-count guess plus 2.

The public boundary is:

```cpp
outloud::Tokenizer tokenizer(local_asset);
auto ids = tokenizer.encode(utf8_text);
auto decoded = tokenizer.decode(ids);
auto prepared = outloud::prepare_text(utf8_text, tokenizer);
auto result = outloud::process_text(utf8_text, tokenizer); // max_tokens=50
// result.prepared_text, full_token_ids, segments{text, token_ids, frames_after_eos}
```

The tokenizer is immutable after load; calls use their own temporary state.
The library does not contain fixtures, Python launchers or network code.

## Fixed tokenizer asset, version 1

The converter emits `generated/tokenizer/pocket-english-tokenizer.bin` (93,325
bytes in this run). This is a tokenizer asset, not the future .olv voice format.
All integers are unsigned little-endian; scores are IEEE-754 binary64. No HF
paths, JSON parser or generic tokenizer configuration are stored.

| Offset / order | Field |
|---|---|
| 0 | Eight magic bytes `OLPTOK01` |
| 8 | u32 format version = 1 |
| 12 | u32 vocabulary count = 4000 |
| 16 | u32 unknown ID = 0 |
| 20 | u32 payload byte count |
| 24 | 32 raw bytes of original tokenizer JSON SHA256 |
| 56 | 32 raw bytes of payload SHA256 |
| 88 | Payload begins |

Payload order:

1. 4000 vocabulary records in ID order: u32 UTF-8 byte count, exact token
   bytes (no terminator), f64 score. JSON scores are preserved as binary64.
2. Unicode version string: u32 byte count and UTF-8 bytes (`15.0.0`).
3. Three property tables in order: Python `isspace`, `isupper`, `isdigit`.
   Each has u32 range count, then inclusive u32 start/end pairs.
4. Full uppercase mappings: u32 record count; each record has u32 source
   scalar, u32 replacement byte count, and UTF-8 replacement bytes.

The loader verifies magic/version/count/unknown ID, pinned original SHA,
payload size/hash, bounds, UTF-8, finite scores, fixed special/byte mappings,
Unicode version, ordered properties/mappings, duplicate pieces and trailing
data. It widens no scores to an approximate representation. Hashes provide
identity/integrity checking; this is not an asset signing system.

## Regression fixtures and validation

[text-pipeline-cases.json](../reference/text-pipeline-cases.json) contains twelve
stable cases generated by the installed Pocket 3.3.0 / tokenizers 0.23.2 path:
simple sentence, question, exclamation, grouped sentence punctuation, CR/LF/
tabs/spaces and Unicode edge whitespace, weak trailing punctuation with a
closer, straight/curly apostrophes, decimal numbers, a long quest with fallback
splitting, UTF-8 with accented text/emoji/CJK, literal special tokens, and
Unicode uppercase expansion/combining characters.

Each checks raw native encoding/decoding, exact prepared text, full prepared
IDs and all generation segment strings/IDs/tails. The canonical fixture is
read from the unchanged [pipeline baseline](../reference/pipeline-baseline.json).
To regenerate regression data deliberately, using untouched upstream code:

```powershell
.venv\Scripts\python.exe -B reference\generate_text_cases.py
.venv\Scripts\python.exe -B tools\tokenizer\export_test_fixtures.py
```

No neural model, voice or audio generation is involved in this command.
`--output` can write a comparison fixture under `generated/` instead of changing
the tracked corpus. Only development tooling imports the installed package.

Observed Release x64 validation with MSVC 19.44.35229:

- Canonical: exact 75 full IDs, exactly three segment strings and IDs, counts
  20 / 39 / 16, all PASS.
- Canonical plus twelve regression cases: 13/13 PASS.
- Additional native checks: empty prompt, invalid/overlong/surrogate/out-of-range
  UTF-8, exact single-pass spaces, terminal/weak punctuation and closers,
  special-token skipping/preservation, invalid fallback byte decoding,
  missing decode IDs, embedded NUL, invalid token limit and corrupt/truncated
  assets: PASS.
- CTest: 2/2 PASS; compilation uses `/W4 /WX /utf-8 /permissive- /fp:precise`.
- One canonical preparation/splitting/tokenization measurement: 0.153 ms,
  excluding load, output and reference comparison. This is an informational
  single run, not a benchmark guarantee.

**UNVERIFIED:** arbitrary Unicode and arbitrary text parity have not been
exhaustively established by these thirteen input fixtures. In particular,
all possible score ties, special-token placements and byte-fallback range
slices are not individually covered. The algorithms and property tables are
implemented from verified sources; there are no oracle-specific shortcuts.
Invalid UTF-8 is intentionally rejected rather than emulating Python strings
containing unpaired surrogates, which are outside the UTF-8 input contract.
No claims about later neural, RNG, waveform or SAPI parity are made here.
