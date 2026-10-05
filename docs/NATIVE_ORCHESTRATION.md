# Step 2L: native full-text streaming orchestration

`outloud_orchestration` connects the unchanged native text pipeline, production
RNG and resident worker pipeline. It accepts UTF-8 English text and an explicit
seed, and exposes each 1920-sample FP32 PCM frame immediately. It has no Python,
PyTorch, network, captured noise/latent, WAV or external dependency requirement.
Step 2K was committed as `841c21c`; the tree was clean before work began.

## VERIFIED installed upstream lifecycle

Inspected unchanged Pocket TTS 3.3.0 sources under
`.venv/Lib/site-packages/pocket_tts/`:

- `models/tts_model.py`: `generate_audio_stream`,
  `_generate_audio_stream_short_text`, `_generate`, `_autoregressive_generation`,
  `_decode_audio_worker`, `_run_flow_lm_and_increment_step`, `_expand_kv_cache`.
- `models/text_chunking.py`: `prepare_text_prompt`,
  `split_into_best_sentences`, `_find_boundary_indices`,
  `_is_decimal_period_boundary`, `_segments_from_boundaries`.
- `modules/text_conditioner.py`: `JsonTokenizer`, `LUTConditioner.prepare`.
- `modules/stateful_module.py`: `init_states`, `increment_steps`.
- `models/flow_lm.py`: `FlowLM.forward` and its already-scaled noise call.
- `config/english.yaml`: pinned English preparation/model options.

Source hashes accompany ignored development metadata and are checked after
capture. Nothing in site-packages was edited. Existing protected
`reference/pipeline-baseline.json` already records the canonical split, exact
IDs, per-segment EOS/candidate/frame counts and historical full-PCM identity.
It is reused, not replaced with a second tracked baseline.

### Text and boundaries

Preparation strips Unicode whitespace, rejects empty/whitespace-only text,
replaces CR/LF and performs one non-overlapping double-space replacement pass.
English has no short-input padding or character translation; semicolons remain.
The first scalar is capitalized and terminal punctuation is ensured using the
existing native Unicode tables/rules. The entire prepared text is tokenized
before splitting; the neural model receives independently prepared segment IDs.

The default **50-token limit is soft**. Sentence boundary IDs are
`263/682/799/292`, obtained upstream from `.!...?`; consecutive boundary marks
stay with the preceding slice. Decimal periods are skipped using decoded
prefix/suffix digit checks. Oversized sentences are subdivided at comma,
semicolon or colon IDs `262/1230/3244` when possible. Pieces are greedily grouped
while their counted tokens fit 50, decoded, stripped, then prepared/retokenized
for generation. An unsplittable oversized sentence remains one segment;
upstream warns rather than hard-cutting tokens.

Empty and whitespace-only inputs throw before any generation. One short input
remains one segment. Source-backed probes with exactly 50 tokens remain one
segment; 51 tokens without usable internal punctuation also remain one segment.
For <=4 whitespace-delimited words, the tail guess is 3, otherwise 1; upstream
adds 2, yielding tail 5 or 3. The pinned recommended tail override is None.
Existing `process_text` supplies these exact strings/IDs/tails; no tokenizer or
split logic was duplicated or altered.

### Seed and neural state

The historical oracle calls `torch.manual_seed(73)` **once after loading** for
the whole utterance. Installed orchestration performs no segment seed derivation
or reseed. Every segment consumes one discarded prefill draw, all candidate
draws including the final computed/discarded stop candidate, then the next
segment takes the next row. Model/voice construction is outside this seeded
generation region. Mimi consumes no RNG on this path.

Default `copy_state=True` deep-copies the immutable voice prefix for each
segment. No suffix or feedback crosses a segment boundary; each begins with
text prefill then learned BOS. `_decode_audio_worker` creates fresh Mimi state
per segment. Convolution overlap is not flushed at segment end. The first
accepted EOS is latched only at candidate index >=6, and the stop candidate is
sampled then discarded when `index >= first_eos + frames_after_eos`.
The existing token-count maximum remains the safety bound.

Segments are processed in order and their PCM chunks are concatenated directly:
**no inserted silence, pause, crossfade, resampling or retained Mimi history**.
One-frame decoder mode is explicit for this reference; the installed default
can coalesce queued latents, which produces a different numerical PCM identity.
Upstream obtains all split chunk strings up front, but prepares/tokenizes and
dispatches later segments only when earlier segment streaming completes.

## Native API and implementation choices

[UtterancePipeline](../engine/include/outloud/orchestration.hpp) borrows one
immutable tokenizer and owns one resident worker pipeline plus one utterance
`ProductionNoiseSource`. Model/voice mappings remain shared read-only, and the
same two worker threads persist across segments and utterances. One coordinator
may use the instance; APIs and sinks are neither concurrent nor reentrant.
The tokenizer must outlive the pipeline, and sink/context must survive `run`.

```cpp
outloud::UtterancePipeline pipeline(model, voice, tokenizer,
    maximum_segment_tokens, maximum_segment_frames, kernel_path);
pipeline.prepare(utf8_text, explicit_seed);
const auto result = pipeline.run(sink, sink_context);
// sink(void*, uint64_t utterance_frame, size_t segment,
//      uint32_t segment_frame, span<const float> pcm)
```

`prepare` reuses `process_text`, stores all text/segment IDs and validates every
segment against the configured resident capacities before audio is exposed.
This is an implementation choice: native text preparation is eager and timed
separately. Later segments' **neural** prepare/reset/compute waits until the
previous segment has finished. No later segment's latent/PCM is precomputed.

Capacities bound each segment, not total utterance length/segment count. An
oversized unsplittable segment is rejected before streaming if it exceeds
configured capacity; it is not silently resegmented. Callers can reserve larger
capacities subject to the existing neural 4096-position limits. The development
tool reserves 51 tokens and the corresponding 238 maximum candidates to cover
the soft-limit edge probe; this is not a new mandatory segmentation limit.

`run` starts its clock, reseeds the private RNG once while workers are idle,
then passes that **same external NoiseSource** through every segment's existing
worker prepare/run path. Only the producer calls `next()` during computation;
the coordinator accesses RNG state only between completed requests. External
source mode performs no implicit reseed. The consumer remains RNG-independent.
Step 2K's explicit-seed one-segment overload and RNG arithmetic are unchanged.

The only worker output change permits an empty destination when a non-null
sink is supplied. In that case the sink views the existing decoder frame
directly; it expires when the callback returns. Existing destination-backed
calls still copy one frame into stable caller storage with the same semantics.
Queue capacity remains two; generation and decoder loops/math are unchanged.

The utterance sink runs on the consumer, receives zero-based global/segment/
local indices and one **1920-float / 80-ms** span, and can naturally backpressure
the bounded queue. There is no full-segment or full-utterance PCM buffer in the
production core. No final WAV or other publication is required. A development
sink alone accumulates complete PCM for exact reuse comparison/listening.

Failures from computation, noise, observers or sinks use established worker
failure publication/wakeup/join behavior. No later segment is dispatched, and
`run` returns no successful partial result. Failed utterances require fresh
`prepare`; the next `run` resets neural state and reseeds, permitting recovery
on the same mappings/threads. Already delivered PCM cannot be recalled.
Public cancellation/StopSpeaking is not implemented.

## VERIFIED canonical schedule and numerical result

Exact full text:

> The watch captain needs your help before the next caravan arrives. Follow the river to the abandoned tower, speak with the scout waiting at its gate, and bring his report back to the village. Keep your lantern burning, for the path is narrow.

The three exact prepared segments are:

1. `The watch captain needs your help before the next caravan arrives.`
2. `Follow the river to the abandoned tower, speak with the scout waiting at its gate, and bring his report back to the village.`
3. `Keep your lantern burning, for the path is narrow.`

RNG rows and EOS/candidate indices below are zero based; draw ranges include
discarded prefill and stopping samples.

| Segment | Tokens | Draw range | Draws | Candidates | First EOS | Frames | Samples | Audio seconds |
| --- | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 20 | 0..47 | 48 | 47 | 43 | 46 | 88320 | 3.680 |
| 2 | 39 | 48..133 | 86 | 85 | 81 | 84 | 161280 | 6.720 |
| 3 | 16 | 134..175 | 42 | 41 | 37 | 40 | 76800 | 3.200 |
| Total | **75** | 0..175 | **176** | **173** | Per segment | **170** | **326400** | **13.600** |

The source-backed exporter reuses frozen scheduling metadata. The available
`generated/pocket_oracle_gnome.f32` was from the default coalescing decoder and
had another hash; a single offline one-frame correctness capture was necessary
for the requested upstream full listening artifact. It reproduced the exact
historical upstream FP32 SHA
`e4e1004ffda5a83b205a6d131d34209a88596012b249657702657c924ec529b9`.
Its instrumented normal run verified all 176 noise rows against Step 2K's
existing reference and observed all per-segment schedules above. No upstream
performance campaign was run or historical fixture overwritten.

Native AVX2 full PCM SHA is
`36788a2c1cfde712fb9ef68151a93b1b78eec40b1e440e99ff1b1b59d1a8bcf0`.
Its Segment 1 prefix matches the Step 2K native production PCM hash exactly.
Full native/upstream comparison has maximum absolute error **0.00400758**,
RMS **7.93275e-5**, correlation **0.999999005**. These are descriptive diagnostics,
not newly invented or weakened tolerance gates. Full native PCM is not required
to have upstream's hash: existing free-running neural drift remains documented.
All protected neural parity gates still run unchanged in the existing tests.

The standalone checker verifies all 75 full IDs, every segment's string/IDs,
all 176 RNG rows bitwise, voice-prefix reset offsets, latch/emission scheduling,
per-segment/final counts and finite PCM. Requests A/B on the same runtime have
bit-identical native PCM, including B after a first-frame sink exception on an
intervening full-text request. Its first frame arrives with only two producer
candidates completed, before the first segment or later segments finish.
No extra statistical, stress, soak or CPU-matrix campaign is introduced.

Listening-only artifacts, written outside timing:

- `generated/audio/upstream-full-canonical.wav`
- `generated/audio/native-full-canonical-avx2.wav`

They are mono PCM16, 24000 Hz / 326400 samples. The ignored raw upstream/native
FP32 files and schedules remain authoritative; no human listening evaluation
is claimed. Generated headers, captures and reports remain ignored.

## Performance and memory

**VERIFIED final AVX2 observation**, one warm-up plus three timed requests:

| Metric | Minimum | Median (primary) | Maximum |
| --- | ---: | ---: | ---: |
| Text preparation, ms | 0.1578 | **0.1704** | 0.1887 |
| Request first PCM, ms | 86.6338 | **92.2589** | 105.0207 |
| Request total wall, ms | 3852.9323 | **3860.1418** | 3862.1054 |
| RTF, audio / wall | 3.5214x | **3.5232x** | 3.5298x |

Each request generated **13.600 seconds** of audio. Total times were
3860.1418, 3852.9323 and 3862.1054 ms; no additional runs were made.
These host observations are not performance requirements. Separate segment
medians do not necessarily sum to the median total.

| Segment | Tokens | Frames | Audio seconds | Worker compute median ms | Segment wall min / median / max ms |
| --- | ---: | ---: | ---: | ---: | --- |
| 1 | 20 | 46 | 3.680 | 1035.3209 | 1017.3834 / **1035.3444** / 1055.0678 |
| 2 | 39 | 84 | 6.720 | 1915.7890 | 1897.1653 / **1915.8090** / 1930.6418 |
| 3 | 16 | 40 | 3.200 | 900.6777 | 896.1167 / **900.6970** / 926.9469 |

Raw reports are `generated/perf/2l-benchmark-avx2.json` and
`generated/perf/2l-summary.json`. The fixture-free `outloud-utterance-synth`
uses ordinary runtime libraries without
allocation audit, observer, trace or kernel profiler. Its timed output sink
only checks/counts streaming indices; it does not store PCM. One warm-up plus
three timed requests is the entire benchmark. Assets load/validate first.
Text preparation is measured separately on each request. Request first-PCM
and total clocks include RNG reseed, segment neural reset/prepare and the
producer/consumer execution. Per-segment worker compute excludes reset;
per-segment wall includes it. No WAV/JSON/hash/console output is inside timing.
RTF means audio duration divided by wall duration, so higher is faster.
Returned segment-result copies are allocated after the compute clock ends.

The historical upstream full-text timing used a different decoder batching and
process/thread lifecycle, so it is not a direct comparison for this observation.
No speedup claim or new kernel optimization follows from these host timings.

Orchestration adds an inline 2656-byte utterance RNG and small scalar/text/result
descriptors. Existing worker-owned single-segment RNG storage remains present
for its unchanged public seed overload; the utterance uses its own continuous
source. Prepared text strings, full IDs, segment descriptors/IDs and segment
results scale with text/segment count. Temporary normalization/split/decoded
strings, token-ID slices, Unicode/Viterbi state and the previous prepared text
can coexist during preparation; they are not PCM or per-frame allocations.
Their peak heap/allocator/OS overhead was not profiled.

Measured canonical retained capacities on this MSVC build:

| Added orchestration storage | Bytes |
| --- | ---: |
| Impl plus public handle, including RNG and container/control headers | **2864** |
| Three segment descriptors | **192** |
| Full and per-segment token vector capacities | **732** |
| Prepared/full and segment string capacities including terminators | **653** |
| Three retained segment results | **264** |
| Total above, excluding existing worker backing/allocator overhead | **4705** |

The existing worker inline object is **97056 bytes**, unchanged by sink-only
support. Returned results own an additional 264-byte three-segment payload
copy in this request. Capacity figures are storage reports, not heap-allocation
counts; short strings on other inputs can use inline string storage.

Production **PCM buffering is zero bytes** beyond the existing decoder's reused
7680-byte frame. Model/voice mappings, existing neural scratch and configured
KV/carry storage remain the worker's responsibility. The tool's 51-token /
238-frame limits reserve 14204928 FlowLM suffix bytes and 31195136 Mimi KV bytes,
plus existing carry/scratch; capacities affect storage, not scheduling.
Checker-only PCM copies are 326400 floats each, 1305600 bytes per copy.

## Build, validation and remaining work

Development references are optional for the production library/synthesis tool;
CMake adds the standalone checker only when its generated header is present.
No new CTests or performance CTests are added. Existing local asset/tokenizer/
fixture prerequisites remain unchanged. Neither build nor runtime invokes Python.

**VERIFIED final checks:** initial Release sanity passed the existing 12/12
CTests once (27.44 s). Final Release x64 build and 12/12 CTests passed once
(27.16 s); final AVX2-disabled Release build and 12/12 CTests passed once
(20.02 s). Both full-text checkers passed exact text/RNG/lifecycle/counts,
immediate streaming, A/B bit equality, first-frame sink-error recovery and zero
audited producer/consumer hot-loop C++ allocations. Both production worker
checkers and RNG checkers passed; the existing fixture-backed worker checker
also passed both failure directions. The portable full-native PCM SHA is
`2215967a28dc81e98098793a80e6d987a891687090f63e4a46effb34ca76bd90`;
different neural-path PCM bits are expected, while canonical scheduling matches.

The six inspected Pocket source/config hashes and prior ten RNG source hashes
remain unchanged. Both WAV headers/sample counts were verified. Protected
kernel/model/RNG arithmetic, reference baselines, fixtures, text logic and
existing tolerance/test definitions are unchanged. `git diff --check` and
new-file whitespace/newline checks passed. Generated artifacts are ignored.
HEAD remains `841c21c`; nothing staged, committed or pushed.

```powershell
.\.venv\Scripts\python.exe -B tools/neural/export_orchestration_reference.py
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\outloud-utterance-check.exe avx2
.\build\Release\outloud-worker-check.exe rng-check avx2
.\build\Release\outloud-utterance-synth.exe avx2 bench
cmake -S . -B build-portable -DOUTLOUD_ENABLE_AVX2=OFF
cmake --build build-portable --config Release
ctest --test-dir build-portable -C Release --output-on-failure
.\build-portable\Release\outloud-utterance-check.exe portable
```

Portable and optimized neural paths retain their existing arithmetic differences;
segmentation, RNG, reset/lifecycle and counts select no vendor/core/SIMD policy.
Only this host's normal and AVX2-disabled builds are checked; other physical
CPUs/compilers, arbitrary text/voice neural outcomes and perceptual drift remain
UNVERIFIED. Text parity still has Step 2B's documented coverage/limitations.

The reusable full-text -> streaming PCM core is ready as the synthesis boundary
for a thin SAPI adapter. SAPI-specific StopSpeaking cancellation, PCM conversion/
output, COM lifecycle, registration and integration remain later work. No
COM/SAPI/registry/WoW implementation is added here. Stop after Step 2L.

Created files: `docs/NATIVE_ORCHESTRATION.md`,
`engine/include/outloud/orchestration.hpp`, `engine/src/orchestration.cpp`,
`tools/neural/export_orchestration_reference.py`, `tools/utterance-check/main.cpp`.
Modified files: `CMakeLists.txt`, `engine/include/outloud/worker_pipeline.hpp`,
`engine/src/worker_pipeline.cpp`. Nothing staged, committed or pushed.
