# A08: immutable Mimi transpose phase weights

Date: 2026-10-09. Entry HEAD: `046385a`; the working tree already contained
cleanup, legacy retirement, stabilization and A06 changes. They were preserved.
This step implements A08 only. No asset/reference dataset was regenerated.

## Repeated work and root cause

The current dense-transpose branch in `MimiConvolutionKernels::run` still gathered
and widened immutable BF16 weights for every output channel and stride phase on
every decoded frame. Three configurations were affected:

| Configuration | Tensor / schema ID | BF16 shape | Prepared floats | FP32 bytes |
| --- | --- | --- | ---: | ---: |
| 2 | `mimi.decoder.model.2.convtr.weight` / 130 | `[512,256,12]` | 1,572,864 | 6,291,456 |
| 5 | `mimi.decoder.model.5.convtr.weight` / 136 | `[256,128,10]` | 327,680 | 1,310,720 |
| 8 | `mimi.decoder.model.8.convtr.weight` / 142 | `[128,64,8]` | 65,536 | 262,144 |
| Total | | | **1,966,080** | **7,864,320** |

Thus a 46-frame fixture repeated 90,439,680 scalar conversions/stores of these
values. The prior implementation already reused its row/window buffers: it had
no repeated heap allocation here. The redundant work was gathering, conversion
and rewriting phase rows. The historical Step 2I preparation measurement of
about 124 ms was motivation, not a current baseline or performance claim.

Audio-dependent input windows, dot products, output overlap, bias and tail/carry
calculations remain per frame. Ordinary convolutions still widen their original
contiguous rows into existing scratch. The depthwise upsampler is unchanged.

## Implementation, representation and ownership

`MimiConvolutionKernels` now receives and retains its immutable `PocketModelAsset`
at construction. Checked dimension/count arithmetic computes offsets into one
owned `std::vector<float>`. Construction prepares all three dense groups before
the object can decode audio. The model copy shares ownership of the same read-only
mapping; it copies neither BF16 tensors nor a mapped file.

Rows flatten `[Cout, phase, Cin, current/previous]`. With `width = 2*Cin`:

```text
row offset = group_offset + (channel*stride + phase)*width
w          = (input_channel*Cout + channel)*kernel + phase
row[2*input_channel]     = bf16_to_float32(weight[w])
row[2*input_channel + 1] = bf16_to_float32(weight[w + stride])
```

This is the original gather and conversion, moved without changing ordering.
The run path takes a const span of that row and calls the existing dispatched
`dot_rows` and `dot`, with the original bias addition, head overlap addition and
`tail - bias` carry update. Compiler `/fp:precise`, AVX2/FMA compilation and ISA
selection are unchanged. There are no new locks, synchronization, global caches,
background workers, format changes or approximate operations.

The model argument was removed from `run`: a caller cannot supply a different
model while using rows cached from construction. The cache belongs to the
convolution owner inside each `MimiDecoder`. The normal resident `WorkerPipeline`
contains one such decoder, built before its existing two compute threads start.
It has one cache, not one for each worker. Independent decoders each own their
own cache; there is no cross-model sharing.

`MimiDecoderState::reset` changes histories/KV positions, not this cache. Repeated
requests and cancellation keep the resident cache. Destruction releases it;
partially constructed objects use ordinary member RAII cleanup. No pointer into
an expired mapping is stored. Changing voice conditioning does not change model
weights; no prepared row contains voice or request data.

## Memory and initialization

The cache size was calculated before implementation. Both ISA test runs and the
benchmark report an actual vector capacity of **1,966,080 floats / 7,864,320 bytes
(7.5 MiB)**. One additional heap allocation holds the rows. Shape validation,
checked multiplication/addition, byte overflow checks and `vector::max_size`
checks precede allocation.

The new MSVC x64 convolution owner is 32,944 inline bytes; the decoder is 59,512
inline bytes. Added inline control storage is calculated as **136 bytes**:
a 16-byte shared model handle, 24-byte vector and twelve 8-byte offsets. Together
with the payload, the expected per-decoder storage increase is 7,864,456 bytes,
excluding allocator metadata and OS backing. Mutable scratch is unchanged.
`scratch_bytes()` continues to report mutable scratch; the new
`prepared_weight_bytes()` reports the immutable cache separately. Existing worker
scratch statistics therefore do not include the new cache.

Standard vector allocation supplies float alignment. The existing AVX2 dot
implementation uses unaligned loads; no 32-byte alignment or aligned allocator
is introduced. Allocator metadata, page rounding and process working-set/private
memory were **not measured**. Multiple independent live decoders multiply this
cache cost. The mapped model remains unchanged and resident.

Construction timings include existing scratch allocation/zeroing plus the new
allocation/preparation, with already loaded/validated assets. They exclude
destruction and asset loading. Convolution-owner timing isolates that owner,
not the conversion loop alone. Decoder timing includes all decoder buffers.

| Construction, ms | Old min / median / max | New min / median / max | Added median ms |
| --- | --- | --- | ---: |
| Portable convolution owner | 0.1831 / 0.1898 / 0.2653 | 2.3996 / 2.4196 / 2.4994 | 2.2298 |
| Portable complete decoder | 0.3140 / 0.3256 / 0.3293 | 2.5217 / 2.5729 / 2.6463 | 2.2473 |
| AVX2 convolution owner | 0.1780 / 0.1819 / 0.1888 | 2.4894 / 2.5055 / 2.6040 | 2.3236 |
| AVX2 complete decoder | 0.2807 / 0.2832 / 0.3693 | 2.5827 / 2.6015 / 2.6615 | 2.3183 |

This adds about 2.3 ms once per resident decoder. A cold application first-output
effect was not measured; warm first-frame figures below exclude construction.

## Correctness baseline and exact comparison

Before changing numerical code, a Release x64 harness captured the unchanged
decoder on both ISA paths. Its executable, original source, source hashes,
complete raw FP32/PCM16 output and checkpoint manifests were saved under ignored
`generated/perf/a08/baseline/`. The first harness build found a mistaken PCM
header include; correcting that test-tool include produced the baseline build,
before any numerical changes. No tracked reference baseline was altered.

The frozen normalized latent fixture has 46 frames. Old and new each emitted
**88,320 samples / 3.680 seconds at 24 kHz**, 1,920 samples per frame, advancing
Mimi offset to 736. Actual raw FP32 and standard PCM16 files were compared by
length and `memcmp`, and matched byte for byte for each ISA independently:

| Path | Old and new complete FP32 PCM SHA-256 |
| --- | --- |
| Portable | `f1a0a8d71636cc069ed8af93e5f09ea5c81f304863c0be5088a24229a51c5ca2` |
| AVX2/FMA | `9ec11e20d5a9b85b278553a827f9d6f1e0b176d5cc89cd4ed7177fd208555de5` |

All observed decoder checkpoint hashes at frames 0/1/2/23/45, final K/V hashes
and twelve final convolution-carry hashes also matched the captured original.
Intermediate equivalence here is hash comparison; full PCM equivalence is direct
byte comparison. Portable and AVX2 have distinct baseline FP32 hashes because
their existing arithmetic differs; no cross-ISA bitwise equality is claimed.

Focused regressions directly checked all **1,966,080 prepared float bit patterns**
against the original BF16 widening on each ISA. A minimal test-only original
dense-transpose primitive checked exact output and carry at five fixture frames
for all three groups: **15 cases per ISA**. Cache hashes remained unchanged after
execution. No alternate full decoder or old production fallback was retained.

The existing independent upstream fixture gates, convolution checks, path
agreement, context, reset, capacity, invalid input, observer failure and reentry
checks passed with unchanged tolerances. Both unobserved 46-frame decode loops
still made **zero C++ heap allocations**. Injecting failure at each of the
**14 decoder-construction allocations** returned the live-allocation count to
its starting value; partial construction released all owned buffers.

The short real resident-worker check used workspace Gnome Male and Undead Male
profiles, verified distinct source identities, seed 73 and three emitted frames
per normal request. Each profile/ISA combination passed exact repeat output,
pre-stopped cancellation (zero samples/draws), streaming cancellation after one
frame, recovery and injected producer failure/recovery on the same pipeline.
Each complete short request emitted 5,760 samples with offset 48; streaming
cancellation returned a 1,920-sample prefix. Workers quiesced before recovery
and destruction. These are separate profile pipelines with the same model;
live COM token reassignment was not exercised.

## Short performance comparison

Measured on the same AMD Ryzen 9 5900X, Visual Studio 2022 Release x64 build,
existing model and frozen 46-frame normalized-latent fixture. The old statically
linked baseline executable remained usable after the incremental new build.
Old/new benchmarks ran consecutively without concurrent build/test work.

Each version/ISA performed **one untimed full decode warm-up and three timed
repetitions**, resetting decoder state before every measured run. Construction
measurements separately used one warm-up and three repetitions per owner type.
No extra repetitions or profiler runs were performed. Assets were loaded and
validated before clocks; scheduling validation, JSON serialization and all
output comparison were outside clocks. There were no observers, profiling hooks,
allocation hooks or debug counters in benchmark timing. No WAV was written.

The workload is decoder isolation with fixed, identical latents, so it executes
neither a voice-conditioned FlowLM nor RNG during timing. Voice and seed cannot
differ between measured decode inputs. This is **not complete TTS synthesis**.
First PCM below means one Mimi frame from an already available normalized latent,
excluding asset loading, FlowLM prefill, worker queues, SAPI and playback.

| Decode metric, ms | Old min / median / max | New min / median / max | Median reduction ms | Reduction |
| --- | --- | --- | ---: | ---: |
| Portable, first PCM | 27.3769 / 27.3773 / 27.4571 | 25.2563 / 25.2959 / 25.3687 | 2.0814 | 7.60% |
| Portable, 46 frames | 1326.4454 / 1334.2409 / 1337.0435 | 1225.9615 / 1228.4712 / 1238.0352 | **105.7697** | **7.93%** |
| AVX2/FMA, first PCM | 13.9287 / 13.9599 / 14.4081 | 12.3110 / 12.3242 / 12.4760 | 1.6357 | 11.72% |
| AVX2/FMA, 46 frames | 726.1552 / 731.5846 / 734.3452 | 630.4191 / 638.9095 / 639.1315 | **92.6751** | **12.67%** |

Reduction is `old median - new median`, percentage divided by old median.
Full-decode ranges do not overlap: the benefit exceeds observed run variation
in this short session. Three measurements are not a broad statistical or
cross-machine guarantee. The tradeoff is retained: this fixture saves about
93–106 ms per 46-frame decode for 7.5 MiB and about 2.3 ms initialization.
**Complete synthesis time and production first-output latency were not measured;
no end-to-end speedup is claimed.**

## Build, focused checks and reproduction

One post-implementation incremental Release x64 build completed successfully for
`outloud-mimi-cache-check`, `outloud-mimi-tests`, `outloud-mimi-check` and
`OutLoudPocketSAPI`, including affected `outloud_mimi`, `outloud_workers` and
`outloud_orchestration` dependencies. No clean rebuild or WPF build was performed.

Only the existing two relevant CTests were executed: **2/2 PASS in 11.23 s**:
`canonical-mimi-decoder` (8.40 s) and `mimi-regressions` (2.78 s).
The exact baseline comparison and short worker lifecycle modes each passed
once. No new CTest/performance gate was added. Git whitespace checks passed.

```powershell
cmake --build build --config Release --parallel --target outloud-mimi-cache-check outloud-mimi-tests outloud-mimi-check OutLoudPocketSAPI
ctest --test-dir build -C Release --output-on-failure -R '^(canonical-mimi-decoder|mimi-regressions)$'
.\build\Release\outloud-mimi-cache-check.exe compare generated/assets/pocket-english.olp generated/neural generated/perf/a08/optimized generated/perf/a08/baseline
.\build\Release\outloud-mimi-cache-check.exe worker generated/assets/pocket-english.olp generated/neural generated/perf/a08/optimized
.\generated\perf\a08\baseline\old-mimi-cache-check.exe bench generated/assets/pocket-english.olp generated/neural generated/perf/a08/baseline
.\build\Release\outloud-mimi-cache-check.exe bench generated/assets/pocket-english.olp generated/neural generated/perf/a08/optimized
```

The original baseline was obtained with the old executable's `capture` mode,
before the source change. The executable/source are local ignored artifacts,
not portable tracked historical baselines. Raw per-run JSON and the calculated
comparison are under `generated/perf/a08/`; CTest output was copied there too.

## Changed files, limits and deferred work

- `engine/include/outloud/cpu/mimi_kernels.hpp`, `engine/src/cpu/mimi_kernels.cpp`:
  model-bound cache ownership, checked one-time layout and const-row consumption.
- `engine/include/outloud/mimi.hpp`, `engine/src/mimi.cpp`: construction wiring
  and separate cache-byte reporting.
- `tests/mimi_fixture_checks.hpp`, `tests/mimi_test.cpp`,
  `tests/mimi_transpose_checks.hpp`: updated kernel interface and focused checks.
- `tools/mimi-cache-check/main.cpp`, `CMakeLists.txt`: isolated capture/compare,
  short lifecycle and benchmark tool; no CTest registration.
- `docs/NATIVE_MIMI.md`: annotation linking the historical Step 2G report to A08.
- This report: design, evidence, measured tradeoff and boundaries.

The exact PCM claim covers this complete deterministic 46-frame fixture, not
every possible utterance. Two short voice-profile lifecycle checks add coverage
without certifying all profiles. No full 170-frame synthesis campaign, physical
non-AVX2 machine, separate AVX2-disabled build, broad compiler/platform validation,
process memory profiling, live SAPI/COM playback or WoW integration was run.
The SAPI DLL was rebuilt only in the workspace, not loaded through registration.
Full validation remains appropriate before release.

A07, A09, ordinary convolution widening, attention/KV changes, broader performance
campaigns and deployment remain deferred. No unrelated optimization or public
native frontend ABI change was made. Production registry, ProgramData, installed
voices/model files, deployed binaries, certificates and speech settings were
untouched. No elevation, signing, dependency installation or game/process control
was performed. Nothing was staged, committed or pushed.
