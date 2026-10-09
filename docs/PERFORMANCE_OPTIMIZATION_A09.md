# A09: FlowLM prefill reuse and invariant sampler work

Date: 2026-10-09. Entry HEAD: `046385a`, with previous cleanup, legacy removal,
stabilization, A06 and A08 changes already present in the working tree. Those
changes were preserved. This implements A09 only, using workspace assets.

## Confirmed redundant operations

The current implementation confirmed all three audit observations:

1. `FlowLMBackbone::prefill` called owning `embed`, allocating a token-embedding
   vector, then returned a freshly allocated `FlowLMResult::normalized` vector.
   Backbone activation and KV buffers were already reused; these two temporary
   allocations remained on every resident request. A pre-change allocation audit
   measured **two C++ allocations per warm 20-token prefill** on both ISA paths.
2. `FlowSampler::sample` computed two time embeddings for fixed scalar inputs
   **s=0 and t=1** on every call. Stored BF16 frequencies, two dense projections
   per embedding, SiLU and sample-variance normalization depend only on the
   immutable model and selected ISA. They depend on neither hidden state, noise,
   text, voice, request, acoustic frame nor RNG.
3. After adding time conditioning, `work.y` was unchanged throughout the six
   residual blocks and final modulation. Seven sites copied it and evaluated the
   same 512-element SiLU. This condition is invariant **within one sample**, not
   across samples; its projection depends on the current FlowLM hidden row.

## Implementation and lifetimes

### Prefill working storage

`prefill_into(tokens, state, result, observer)` provides the same numerical result
in caller-owned reusable storage. The existing `work_.norm` vector receives the
token embeddings. `backbone_into` copies those values into `work_.hidden` before
the first layer normalization overwrites `work_.norm`. No embedded input is read
after that copy. Token widening, hidden initialization, positional operations,
layer arithmetic, normalization, EOS projection and KV append order are unchanged.
The final implementation introduces **no separate embedding vector**.

When capacity must grow, prefill reserves the requested validated size before
resizing norm/output, avoiding geometric growth for these prefill allocations.
Shorter requests overwrite every live embedding element and resize logical output;
they retain storage for later requests. Existing token/suffix limits remain
1..4096 and existing worker request limits still apply. No global buffer, caller
pointer retention, new thread or per-request synchronization is introduced.

`FlowLMGenerator` and the worker producer reuse their existing
`candidate.backbone` result for prefill. The discarded prefill sample and its
observer are consumed before the candidate loop overwrites that result. The
generation-call header now explicitly states that spans and the sample reference
are borrowed only during the observer invocation. Capture code copies them there;
it retains no borrowed view. Producer/consumer ownership and cancellation safe
points are unchanged.

Owning `embed`, `prefill` and `backbone` APIs remain available. An owning `prefill`
still needs a result allocation when the caller asks for an independent result;
zero warm allocations apply to the new reusable path. Existing result semantics,
state validation, failed computation invalidation and reset requirements remain.

### Sampler time cache

Construction runs the original time-conditioning primitive once for each index
and stores the two normalized `[512]` FP32 arrays inside the sampler. The same
model mapping and selected portable/AVX2 kernels are used. The cache is private,
read-only during inference, model/ISA-bound and released with the sampler. There
is no cross-model sharing or invalidation at frame/request/reset boundaries.

The time primitive retains stored-frequency COS/SIN ordering, both original
linear operations, original SiLU, original sample-variance reduction and alpha
scaling. Normal sampling consumes the cached arrays in the original expression:

```cpp
work.y[i] = work.y[i] + (time_[0][i] + time_[1][i]) / 2.0f;
```

No averaged-time rewrite changes FP32 grouping. Sampling trajectory, one-step
integration, guidance behavior, noise arithmetic and draw ordering are unchanged.

Detailed development observers still receive all original time checkpoints.
When an observer is supplied, the small time primitive is recomputed into local
scratch for those checkpoints, while inference still consumes the cache. This
avoids retaining every intermediate MLP stage. Those observer runs are excluded
from benchmarks. There is no obsolete whole-sampler production fallback.

### Combined-condition SiLU

After forming `work.y`, `sample` copies it into the existing stack-local
`condition_silu` array and calls the unchanged FP32 SiLU once. All six blocks and
the final projection read that same array. Their matrix arithmetic is unchanged.
Six repeated copies and **3,072 redundant scalar SiLU evaluations** are eliminated
per sample. A fresh transformation occurs for every acoustic frame, discarded
prefill sample, hidden row and request; none is retained in object state.

## Memory and initialization

| Storage | Before | Final A09 | Meaning |
| --- | ---: | ---: | --- |
| Sampler inline object, measured `sizeof`, MSVC x64 | 16,416 B | 20,512 B | **4,096 B additional**, two fixed `[512]` arrays; no cache heap allocation |
| Normal sampler activation arrays | 23,552 B | 18,432 B | **5,120 B fewer** stack-array payload bytes; not a process-stack measurement |
| Backbone workspace capacity, 20-token benchmark | 999,964 B | 999,964 B | Existing normalization scratch reused; no additional embedding storage |
| Separate benchmark result capacity | 81,920 B | 81,920 B | Before reallocated every call; after retained/reused |
| Resident worker/generator candidate normalized capacity after 20-token prefill | 4,096 B | 81,920 B | Calculated **77,824 B** retained increase versus old one-row candidate |

The calculated resident increase for that worker is **81,920 B (80 KiB)**:
77,824 B of retained result storage plus the 4,096 B sampler cache. At the usual
128-token segment bound this is **524,288 B (512 KiB)**; at the general 4096-token
API bound it is at most **16 MiB** of these payloads. Actual retention follows
the largest prefill used by that owner. Existing larger activation/KV buffers,
state limits and mapped BF16 weights are unchanged. Independent pipeline owners
have independent caches/results; the two-worker pipeline has only one sampler.

Capacity and `sizeof` observations are not process working-set/private-memory
measurements. Allocator metadata, page rounding and OS stack backing were not
profiled. First-request buffer growth remains possible; zero allocation is a
warm, previously sized result. Normal destruction and partial construction use
member/vector/mapping RAII. Cancellation retains reusable storage for recovery.

Cold-owner initialization was measured separately, with loaded/validated assets,
one untimed constructor warm-up and three constructor repetitions. Destruction
is outside the clock. This measures the complete sampler constructor, including
both new time embeddings, not model loading or the whole resident engine:

| Sampler construction, ms | Old min / median / max | New min / median / max | Added median ms |
| --- | --- | --- | ---: |
| Portable | 0.0001 / 0.0001 / 0.0001 | 0.1974 / 0.1981 / 0.1983 | 0.1980 |
| AVX2/FMA | 0.0015 / 0.0015 / 0.0017 | 0.0456 / 0.0546 / 0.0670 | 0.0531 |

The trivial old constructor approaches timer granularity; a percentage is not
useful. Complete cold application first-output latency was not measured.

## Exact baseline and numerical evidence

Before production numerical changes, the current Release x64 implementation was
captured in `generated/perf/a09/baseline/`. Frozen statically linked old normal
and allocation-audit executables, their harness, original FlowLM/sampler source
and source hashes were preserved. No tracked canonical reference changed and no
model/fixture export ran.

For each ISA, the baseline and final implementation executed the same workspace
Gnome Male and Undead Male assets, fixed canonical token IDs, seed 73 and model.
Each profile used one backbone across prefill lengths **1/20/7/20**. Every prefill
observer output/shape, normalized result, EOS scalar, offset and all six layers'
K/V suffixes were serialized immediately into the deterministic record.

Sampler cases alternated prefill/BOS/prefill hidden/noise fixtures. Every original
sampler checkpoint was recorded. Observed and unobserved velocity/latent outputs
were also directly checked for exact agreement, so the production cached path
was exercised. A three-frame real generation recorded hidden/noise/velocity/
latent values, offsets, EOS flags and emission/latch decisions. Its production
RNG performed **four draws**, including the discarded prefill draw. The final
seed, left count, position and all **624 RNG state words** were recorded.

The real resident worker repeated the three-frame workload with its owned RNG.
Its generation observations, noise rows/draw count, FlowLM suffixes, Mimi offset
and twelve carries, complete raw FP32 PCM and standard PCM16 conversion were
recorded. Each profile produced **3 frames / 5,760 samples / 0.240 seconds** at
24 kHz, four draws and Mimi offset 48. RNG words come from the standalone short
generation; worker draw count and actual row bytes were compared independently.

Old/final record files are **63,814,812 bytes per ISA** and matched by complete
length plus **`memcmp`**, not just a checksum. Thus all recorded deterministic
outputs, state and final short PCM bytes are bitwise identical. Aggregate hashes:

| ISA | Identical old/final record SHA-256 |
| --- | --- |
| Portable | `8c15802ba3af911ea3f73670943c7242aeb7454693f281a74b97a5d9e0dfe5db` |
| AVX2/FMA | `bf06a55b6fc98618b16dd550552e35cae35c69cbf9ce9f00069879ac5e23c22b` |

Each ISA is compared with its own baseline; portable and AVX2 need not be bitwise
identical to each other. Existing independent upstream FlowLM/sampler fixtures
and numerical tolerances were retained, and their canonical gates passed.
No compiler precision setting, FMA/reduction order, model tensor, text splitting,
Mimi code or production noise implementation was altered by A09.

Additional focused checks verify both cached time embeddings directly against
the original tiny time-MLP primitive on each ISA. They compare all seven condition
sites per sample against original SiLU of the current combined condition, with
alternating inputs and observer failure followed by exact recovery. Reusable
prefill is tested against independent owning results, changing lengths, capacity
retention and failed-observer/reset recovery.

The separate allocation-audit executable measured **2 -> 0 warm prefill C++
allocations** on each ISA. Failure injection at each of **25 construction/initial
prefill allocations per ISA** restored the starting live-allocation count. This
covers cold owner/state/result setup and buffer growth; the inline time cache
itself introduces no additional allocation. Existing generation tests retained
zero unobserved candidate allocations, canonical EOS/draw checks, early EOS,
discarded final candidate, malformed/exhausted noise, failure/reset and reentry.

The same resident pipeline was also reused with lengths 20/1/7/20, pre-stopped
cancellation, cancellation after the discarded prefill sample, cancellation after
the first PCM frame, and an injected producer failure. All four combinations of
two voices/two ISAs quiesced and recovered exact normal PCM. Callback observations
were consumed only during their valid lifetimes. Cancellation did not rebuild
the workers, cache or model mappings.

## Short performance comparison

Same AMD Ryzen 9 5900X, Visual Studio 2022 Release x64, model, canonical 20-token
input, Gnome Male voice and seed 73. A08 was already present in both versions.
The sampler isolation case uses the same fixed BOS hidden/noise fixture, so it
does not run RNG. Prefill and first-latent state, RNG seed and worker state were
reset appropriately before each repetition. Assets were validated before clocks.

Each reported version/ISA/mode used **one untimed warm-up and three timed
repetitions**. Scheduling/sample checks, reference comparisons and all JSON/file
writes were outside timing. The normal benchmark executable has no overridden
allocation functions, profiler, debug trace or observer. Allocation hooks exist
only in the separate audit executable, which refuses benchmark mode. No WAV was
written. Tests/builds did not run concurrently with measurements. No CPU affinity,
clock control or user-application termination was performed.

First acoustic latent includes actual text prefill, its discarded sampler/noise
draw and the first BOS backbone/sample, measured with production seed 73. First
PCM includes the real worker's FlowLM generation and Mimi decode. Short total is
the worker's own dispatch-to-completion clock for **three frames**, including
output copies, with prepare/reset and construction outside that clock.

| Metric, ms | Old min / median / max | Final min / median / max | Old minus new median, ms | Observed reduction |
| --- | --- | --- | ---: | ---: |
| Portable prefill | 144.6922 / 145.1877 / 145.4700 | 142.7136 / 142.8889 / 143.4631 | 2.2988 | 1.58% |
| Portable sampler | 2.5711 / 2.5727 / 2.5845 | 2.3449 / 2.4278 / 2.4600 | 0.1449 | 5.63% |
| Portable first latent | 169.8823 / 170.8170 / 172.6170 | 167.4370 / 168.0397 / 169.0358 | 2.7773 | 1.63% |
| Portable first PCM | 198.3383 / 198.8948 / 198.9895 | 196.4386 / 196.9392 / 196.9586 | 1.9556 | 0.98% |
| Portable short total | 251.0026 / 251.7066 / 251.9026 | 248.0685 / 248.6594 / 248.9131 | 3.0472 | 1.21% |
| AVX2 prefill | 57.6965 / 57.7336 / 58.5689 | 57.7399 / 58.2306 / 58.6556 | -0.4970 | -0.86% |
| AVX2 sampler | 0.6249 / 1.1132 / 1.1402 | 0.5355 / 1.0073 / 1.0660 | 0.1059 | 9.51% |
| AVX2 first latent | 67.3390 / 67.4522 / 67.9911 | 66.2651 / 66.3541 / 67.7685 | 1.0981 | 1.63% |
| AVX2 first PCM | 87.9392 / 88.6938 / 97.2306 | 81.5347 / 81.9106 / 87.3499 | 6.7832 | 7.65% |
| AVX2 short total | 115.8409 / 116.8727 / 124.6790 | 107.1355 / 108.2429 / 115.0889 | 8.6298 | 7.38% |

Percentages divide the median difference by the old median; a negative reduction
means a larger new median. The portable sampler reduction exceeds its measured
range variation. Portable prefill and short latency also improved in this small
session. The AVX2 sampler range is unusually wide in both versions (roughly half
the median), so **9.51% is an observed median difference, not a reliable speedup
bound**. AVX2 prefill ranges overlap and its small regression is inconclusive.
AVX2 first-output/short-total measurements also vary substantially; their larger
median reductions cannot be attributed entirely to the removed sampler work.

Proven contributions are two avoided warm allocations, four avoided time-MLP
linears plus associated time features/normalizations per sample, and six avoided
condition copies/SiLU passes. Timing measures the two sampler optimizations
together; no ablation separates their individual savings. Do not extrapolate
isolated sampler or three-frame results to a complete utterance. **Full synthesis
time was not measured.** The small cache/retention cost, exact output and portable
benefit justify retaining this optimization, while AVX2 performance remains
limited evidence rather than a release guarantee.

## Build and focused validation

The minimal pre-change build compiled only the two new isolated tools and existing
dependencies. The initial implementation then received an incremental Release
x64 build and the five focused CTests below, all passing (9.61 s). Final memory
review replaced a separate embedding vector with existing normalization scratch.
That justified one further affected-target rebuild/revalidation and one new
optimized timing set. The original timing/correctness baseline was retained.
Superseded optimized JSON is preserved outside the final `optimized` folder;
measurements were not pooled or cherry-picked.

Final Release x64 targets built successfully: `outloud-flowlm-check`,
`outloud-flowlm-tests`, `outloud-flow-sampler-check`, `outloud-flow-sampler-tests`,
`outloud-generation-tests`, `outloud-flow-cache-check`, `outloud-flow-cache-alloc`
and `OutLoudPocketSAPI`, including `outloud_flowlm`, `outloud_flow_sampler`,
generation/noise/worker/orchestration dependencies. No clean build, WPF build,
dependency download or production deployment occurred.

Final **5/5 CTests passed in 9.62 seconds**:

| Existing CTest | Final time |
| --- | ---: |
| `canonical-flowlm-prefill` | 0.99 s |
| `flowlm-regressions` | 2.40 s |
| `canonical-flow-sampler` | 0.78 s |
| `flow-sampler-regressions` | 0.59 s |
| `generation-regressions` | 4.76 s |

Exact baseline comparison, allocation/failure audit and four short resident
lifecycle cases also passed on the final sources. Only these relevant checks ran;
no new CTest or performance gate was registered. No whole-project suite, extra
canonical generation campaign, registry/library/frontend test or profiler ran.
The existing generation regression includes its own canonical scheduling check;
it was not substituted with a shortened or weakened test. Git whitespace passed.

```powershell
cmake --build build --config Release --parallel --target outloud-flow-cache-check outloud-flow-cache-alloc outloud-flowlm-check outloud-flowlm-tests outloud-flow-sampler-check outloud-flow-sampler-tests outloud-generation-tests OutLoudPocketSAPI
ctest --test-dir build -C Release --output-on-failure -R '^(canonical-flowlm-prefill|flowlm-regressions|canonical-flow-sampler|flow-sampler-regressions|generation-regressions)$'
.\build\Release\outloud-flow-cache-check.exe compare generated/assets generated/neural generated/perf/a09/optimized generated/perf/a09/baseline
.\build\Release\outloud-flow-cache-alloc.exe alloc generated/assets generated/neural generated/perf/a09/optimized
.\build\Release\outloud-flow-cache-check.exe lifecycle generated/assets generated/neural generated/perf/a09/optimized
.\generated\perf\a09\baseline\old-flow-cache-check.exe bench generated/assets generated/neural generated/perf/a09/baseline
.\build\Release\outloud-flow-cache-check.exe bench generated/assets generated/neural generated/perf/a09/optimized
```

The old baseline capture used the frozen executable's `capture` mode before source
changes. Old allocation audit was also captured before edits. Tool source and
executables, raw output records, per-run JSON, source/asset hashes, comparison
data and initial/final CTest logs live under ignored `generated/perf/a09/`.

## Files changed and remaining limits

- `engine/include/outloud/flowlm.hpp`, `engine/src/flowlm.cpp`: `embed_into`,
  `prefill_into`, scratch/result reuse; existing owning interfaces retained.
- `engine/include/outloud/flow_sampler.hpp`, `engine/src/flow_sampler.cpp`:
  constructor time cache, preserved time observer primitive, one condition SiLU.
- `engine/src/generation.cpp`, `engine/src/worker_pipeline.cpp`: existing candidate
  output reused for prefill on its original compute owner.
- `engine/include/outloud/generation.hpp`: explicit borrowed-observer-view contract.
- `tests/flowlm_test.cpp`, `tests/flow_sampler_test.cpp`: small targeted checks.
- `tools/flow-cache-check/main.cpp`, `CMakeLists.txt`: exact capture/compare, short
  lifecycle/benchmark and separate allocation-audit targets; no new CTests.
- `docs/NATIVE_FLOWLM.md`, `docs/NATIVE_FLOW_SAMPLER.md`: historical report notes.
- This report: current design, memory, verified output and timing limits.

Bitwise evidence covers the recorded two-profile workloads and existing fixtures;
it does not certify every text, voice or long stream. Full utterance PCM equality,
physical non-AVX2 hardware, an AVX2-disabled build, other compilers/platforms,
process memory, live COM/SAPI playback and WoW were not exercised. General maximum
token capacities were bounded in code, not tested with expensive maximum-length
inference. Timing has three measurements and ordinary scheduling variation.
Broader validation remains appropriate before release.

A07 and unrelated kernel/attention/segmentation/RNG/Mimi changes remain deferred.
No native frontend C ABI or managed behavior changed. Real HKLM/HKCU, ProgramData,
installed voices/model files, deployed DLLs, certificates and speech settings were
untouched. No elevated production helper, registration, signing, installation,
game launch/termination or user-application stop occurred. Nothing was staged,
committed or pushed.
