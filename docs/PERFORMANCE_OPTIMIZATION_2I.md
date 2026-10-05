# Step 2I: Pocket CPU kernels

This step retains three measured, single-threaded AVX2/FMA optimizations using
the existing BF16 model and FP32 arithmetic. Portable remains the default and
fallback. There are no new dependencies, workers, RNG, model-format changes,
quantization, frame coalescing, multi-segment orchestration or SAPI changes.
Results are host-specific development observations, not runtime requirements.

## Starting state and measurements

Step 2H was committed as `4fc5ea6`; the working tree was clean before this step.
The required native/performance documents and current kernels were inspected.
The initial Release build passed and the existing 12/12 CTests ran once and
passed (39.61 s). The existing Mimi checker wrote baseline listening artifacts
during that initial sanity suite; subsequent experimental checks do not write
audio. Final listening artifacts are regenerated explicitly once.

Every initial path had one untimed warm-up and three timed generations. Each
focused candidate had one warm-up and two timed generations; neither needed
a third. Each final path had one warm-up and five timed generations. Minimum,
median and maximum retain every measurement; median is primary. There are no
performance CTests or hard performance gates. Intermediate runs used only
relevant existing subsystem checkers; the full suite ran again only on the
final retained state, once per build.

The workload and timing boundaries are unchanged from Step 2H: prepared
20-token canonical text, resident English model and `gnome_male`, captured
seed-73 noise, discarded prefill sampler, 47 candidates, EOS latch at 43,
46 emitted one-frame Mimi calls, 88320 float32 samples, 3.680 s at 24000 Hz.
Seed/noise/state reset, validation and report writing are outside timing.
Captured-noise retrieval remains inside measured prefill/AR. First PCM is
measured directly through prefill, discarded sampler, BOS and first decode.
No profiler/hooks, file output or console output run inside benchmark timing.
No upstream Python benchmark was rerun: comparisons use Step 2H U3 1733.871 ms
and U1 1424.587 ms. No affinity, priority, thread-count or power tuning occurred.

Fresh starting medians

| Stage / metric | Portable | Old AVX2/FMA |
| --- | --- | --- |
| first_pcm_ms | 208.145 | 222.501 |
| prefill_ms | 153.208 | 165.728 |
| ar_including_sampler_ms | 1160.303 | 1251.608 |
| mimi_ms | 1437.722 | 1441.251 |
| total_ms | 2750.351 | 2864.830 |
| rtf | 1.338 | 1.285 |

## Retained experiments

1. **Dense BF16 conversion in registers.** Previously every dense projection
   widened an entire BF16 output row into FP32 scratch, then the AVX2 dot kernel
   read it again. The candidate loads eight BF16 elements, zero-extends to eight
   32-bit words, shifts left 16 bits, reinterprets as exact FP32, and accumulates
   using the existing eight-lane FMA sequence. No native BF16 instruction is
   required. This removes FP32 row materialization on the optional path; the
   portable widening path remains intact. FlowLM, sampler, generation and Mimi
   checkers passed, including all existing path-agreement and EOS gates.

2. **Dense reuse across independent outputs/positions.** Single-token FlowLM
   and sampler calls process four output rows together, sharing activation
   loads and exposing four independent FMA chains. Multi-position projections
   process four activation positions per output row, sharing each register-
   widened weight vector; this directly covers Mimi's 16 positions and text's
   20 positions. Non-multiple row/output/width tails use the existing grouping.
   No model batch size, latent granularity or layout changes. All four subsystem
   checkers and path agreement passed again. The reuse materially improved
   prefill and Mimi in addition to the initial AR gain.

3. **Convolution reuse across time positions.** Conv1d and non-depthwise
   ConvTranspose retain their exact existing windows and phase rows, but process
   four time positions together using one loaded FP32 weight vector and four
   independent FMA chains. This removes repeated one-dot function dispatch and
   reuses weights. Portable uses its existing dot routine through the same
   bounded wrapper. Mimi primitive, integration, carry, PCM and path-agreement
   gates passed. Depthwise upsample remains unchanged.

All three are retained: each was built, validated and measured before adding
the next. There were **no rejected measured kernel candidates**. The first
unblocked BF16 version was superseded by the measured blocking improvement.
Memory-heavy prepacking and alternative reduction orders were not prototyped;
the retained changes met the raw-compute goal with no additional resident memory.
No performance result is attributed to an experiment that was not run.

| Technique | Prefill ms | AR ms | Mimi ms | Total ms | Total time reduction |
| --- | --- | --- | --- | --- | --- |
| BF16 conversion in AVX2 registers | 163.727 | 584.156 | 1439.578 | 2187.515 | 23.64% |
| four output rows / four activation positions | 61.372 | 530.889 | 1181.442 | 1773.758 | 18.91% |
| four convolution time positions | 61.977 | 547.294 | 808.185 | 1417.508 | 20.08% |

Affected-stage reductions: direct BF16 reduced AR 53.33%; dense blocking
reduced prefill 62.52% and Mimi 17.93%; convolution blocking reduced Mimi
31.59% against the immediately preceding candidate. None adds resident memory.

Each candidate requires AVX2 + FMA only on the existing optional path and adds
**zero resident bytes**. Experiment percentages compare successive two-run
medians (the first against the three-run old AVX2 baseline), not independent
isolated-kernel microbenchmarks. The final five-run results below are the primary
performance result. Cross-run stage medians do not necessarily sum to total.

## Arithmetic and streaming semantics

Each output keeps the original eight FP32 lane accumulators, component order
and pairwise final reduction. Blocking interleaves independent outputs rather
than splitting one output into additional partial sums. FMA stays explicit on
the optional path; portable multiplication/addition and final bias addition
remain separate. BF16 widening is exact bit placement, not new rounding or
quantization. No tolerance or frozen checkpoint was modified.

Conv1d remains PyTorch cross-correlation with the same input history and channel-
major output. ConvTranspose remains its existing `kernel=2*stride` phase
decomposition: zero-padded boundary windows, bias addition, previous-head
overlap, explicit tail bias subtraction and saved tail are preserved. The
four-position kernel operates within a single latent's decode, introducing
no wait for another frame. It still returns 1920 float samples immediately.

## Final performance and upstream comparison

portable final measurements

| Stage / metric | Minimum | Median | Maximum |
| --- | --- | --- | --- |
| first_pcm_ms | 216.5748 | 218.6633 | 303.3823 |
| prefill_ms | 159.9869 | 162.3208 | 222.7240 |
| ar_including_sampler_ms | 1259.0699 | 1271.3488 | 1508.7273 |
| mimi_ms | 1473.5571 | 1498.8034 | 1758.9950 |
| total_ms | 2895.0080 | 2930.1999 | 3444.9632 |
| rtf | 1.0682 | 1.2559 | 1.2712 |

avx2 final measurements

| Stage / metric | Minimum | Median | Maximum |
| --- | --- | --- | --- |
| first_pcm_ms | 90.9084 | 92.7571 | 101.9324 |
| prefill_ms | 62.7725 | 63.6222 | 70.6936 |
| ar_including_sampler_ms | 592.9937 | 603.1229 | 613.8719 |
| mimi_ms | 821.7540 | 826.3302 | 846.4463 |
| total_ms | 1487.8109 | 1493.1159 | 1523.9963 |
| rtf | 2.4147 | 2.4646 | 2.4734 |

Final AVX2/FMA uses **47.88% less wall time** than the fresh old AVX2/FMA
baseline (2864.830 -> 1493.116 ms, 1.919x speedup); Mimi falls from
1441.251 to 826.330 ms (42.67% less time). First PCM falls from 222.501 to
92.757 ms. Relative to Step 2H, optimized sequential wall time is
**0.861146x U3** (13.89% less time) and **1.048104x U1** (4.81% more time).
The portable final ratios are 1.689976x U3 and 2.056877x U1. These compare
this host's measurements with the existing upstream reference; they are not
cross-host performance guarantees.

Portable remains available/default. Its final median is 6.54% higher than its
fresh starting median, and its five-run range spans 18.77% of its final median.
All five are retained; the two slower measurements are not discarded and no
extra benchmark campaign was run. Arithmetic/storage on portable are preserved,
but these observations do not isolate compiler/code effects from host variation
and do not demonstrate a portable performance improvement. The optimized result
comes from selecting the existing optional AVX2/FMA path, whose final total range
is 2.42% of its median. Future minimum-ISA policy is unchanged by this step.

## Final diagnostic profile and bottleneck shift

Exactly one final optimized diagnostic generation followed one unprofiled
warm-up. The normal benchmark compiles all diagnostic scopes away. Final
profiling and its comparison to the uninstrumented median are reported below;
the profile is not another benchmark run. Complete counters live in ignored
`generated/perf/2i-final-native-avx2-profile.json` and `2i-comparison.json`.

The profile total was **1501.857 ms**, 1.005854x the uninstrumented final
median. Instrumented sampler total was 80.737 ms / 48 calls, Mimi transformer
271.001 ms / 46 and SEANet 558.363 ms / 46.

Final AVX2/FMA diagnostic top ten

| Domain / group | Inclusive ms | % synthesis | Calls | ms/call |
| --- | --- | --- | --- | --- |
| flowlm / ffn_linear1 | 190.295 | 12.67 | 288 | 0.660748 |
| mimi / group1_convtranspose | 158.822 | 10.58 | 46 | 3.452661 |
| flowlm / qkv | 143.787 | 9.57 | 288 | 0.499260 |
| flowlm / ffn_linear2 | 134.181 | 8.93 | 288 | 0.465905 |
| mimi / initial_conv1d | 69.620 | 4.64 | 46 | 1.513472 |
| mimi / group3_residual_convs | 62.481 | 4.16 | 92 | 0.679140 |
| mimi / group2_convtranspose | 61.879 | 4.12 | 46 | 1.345200 |
| mimi / group3_convtranspose | 60.469 | 4.03 | 46 | 1.314537 |
| mimi / ffn_linear2 | 55.427 | 3.69 | 92 | 0.602472 |
| mimi / ffn_linear1 | 50.844 | 3.39 | 92 | 0.552649 |

Final AVX2/FMA exclusive families

| Family | ms | % synthesis |
| --- | --- | --- |
| dense linear (excluding child BF16 widening) | 753.487 | 50.17 |
| Conv1d (excluding child BF16 widening) | 200.075 | 13.32 |
| ConvTranspose (excluding child weight preparation) | 157.582 | 10.49 |
| BF16 widening / weight preparation | 145.168 | 9.67 |
| attention (scores, softmax, values) | 112.716 | 7.51 |
| activations | 107.566 | 7.16 |
| miscellaneous | 19.175 | 1.28 |
| normalization | 3.504 | 0.23 |
| RoPE / time transcendentals | 2.585 | 0.17 |

Dense direct conversion is included in the dense function counter; it is no
longer separately measured by the old `widen_bf16` child scope. Therefore the
drop in the separate BF16-widening family must not be mistaken for elimination
of all conversion. ConvTranspose's phase preparation still appears separately.
Inclusive top-ten groups contain their preparation children; exclusive family
totals avoid double counting. Domain/root groups should not be added to their
children. RoPE/transcendentals inside activations and softmax remain attributed
to those owning functions. Profile percentages are instrumented observations.

Dense linears remain the largest global family (~50% including their in-register
conversion), and SEANet remains about 67% of Mimi. Group-1 ConvTranspose is the
largest Mimi operation group. Its scattered phase-weight preparation remains
a useful next target: all three transpose groups still spend 124.109 ms in
explicit preparation. FlowLM FFN/QKV remain the largest transformer groups.
Attention and activations now have larger relative shares (~7.5% and ~7.2%),
while normalization and RoPE remain small. No further optimization or backend
integration was attempted after these retained experiments.

## Updated ideal worker-overlap replay

The same Step 2H recurrence replays all five final native per-candidate/per-frame
traces. A starts with measured prefill, produces 46 frames and also computes
the discarded final candidate. B finishes each frame at
`max(latent_ready, previous_decode_finish) + measured_decode_duration`.
The later final A/B completion is total. It assumes unchanged call durations,
independent resources, zero contention and zero synchronization cost, omitting
small dispatch/PCM-copy gaps. This is an optimistic theoretical bound; no worker
or queue exists, and no invented realistic cost penalty is added.

| Path | Ideal min ms | Ideal median ms | Ideal max ms | Ideal median RTF | Sequential / ideal | Meets U1 |
| --- | --- | --- | --- | --- | --- | --- |
| portable | 1662.266 | 1694.414 | 2023.075 | 2.1718 | 1.729 | False |
| avx2 | 900.192 | 906.354 | 922.824 | 4.0602 | 1.647 | True |

Optimized: 1493.116 ms sequential -> **906.354 ms ideal overlap**, modeled
RTF **4.0602** versus U1 2.5832. **Could workers alone from this optimized
state meet U1? YES in this ideal model**; no additional kernel speedup is
required by the bound. Actual worker performance remains unverified because
contention/synchronization were not measured. Portable remains above U1
even in its ideal model (1694.414 ms, RTF 2.1718).

## Memory, ISA and validation

The canonical `.olp` remains **199449856 bytes** and `gnome_male.olv`
**5505280 bytes**, read-only and shared. Added packed/prepared weight storage:
**0 bytes**. Total additional resident engine storage: **0 bytes**. Existing
16 KiB context row buffers and Mimi windows remain; AVX2 dense bypasses its
row buffer. Temporary vector accumulators and four scalar results use registers/
bounded stack only. No full FP32 model or subset expansion was retained.
The candidate and decoder hot-loop audit still reports zero C++ new/new[]
requests in every benchmark/profile run; internal CRT/OS allocation is not
measured. The harness's output storage and counters are unchanged from Step 2H.

ISA dispatch is unchanged: AVX/FMA/OSXSAVE, XGETBV XMM/YMM state, CPUID AVX2,
and the existing optional compilation boundary. `portable` always uses baseline
code, `automatic` falls back when unavailable and explicit `avx2_fma` fails when
unavailable. Only the isolated existing AVX2 translation unit gets `/arch:AVX2`.
There are no vendor, topology, core-count, cache-size or AVX-512 assumptions.
The optimized path is now useful enough to consider AVX2/FMA as a **future**
minimum candidate, but that recommendation is preliminary and no requirement
or default changed. Physical CPU coverage beyond this host remains unverified.

Final Release x64 build and **12/12 CTests passed once** (30.89 s). Final
AVX2-disabled Release build and the same **12/12 passed once** (22.39 s).
All 46-frame / 88320-sample / 48-noise-row counts and the full EOS timeline
passed unchanged gates; both paths passed primitive, generation and Mimi Case A
checks plus path agreement. Final Case A PCM max absolute/RMS errors remain
1.400709e-6 / 6.931622e-8 portable and 1.475215e-6 / 6.827520e-8 AVX2/FMA,
matching the prior reported measurements. Listening artifacts were regenerated
once with `--write-audio` and have the expected 24000 Hz mono PCM16 / 88320
frames. Protected baselines, tolerances and tests are unchanged. No runtime
dependency or network access was added; no upstream benchmark was rerun.
`git diff --check` passes and the index remains empty.

Listening PCM16 remains informational. The final artifact command writes the
same three files under ignored `generated/audio/` from the existing frozen
upstream PCM and native portable Case A / free-running Case B. Those native
files continue to use portable as before; the final optimized path is covered
by the unchanged float32 gates and path agreement. No claim of new human
listening evaluation is made.

## Reproduction and files

The native harness now accepts an optional 2/3/5 repetition count and a safe
report tag. Defaults remain one warm-up/five timed repetitions. Tags preserve
Step 2H reports. The profiling executable always runs once regardless of count.
The Mimi checker now writes audio only with explicit `--write-audio`; ordinary
CTest and experimental parity runs keep every numerical check but skip WAV I/O.

```powershell
# Existing exported local model, voice and fixtures are prerequisites.
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --build build-portable --config Release
ctest --test-dir build-portable -C Release --output-on-failure
./build/Release/outloud-perf.exe portable 5 2i-final
./build/Release/outloud-perf.exe avx2 5 2i-final
./build/Release/outloud-perf-profile.exe avx2 5 2i-final
./build/Release/outloud-mimi-check.exe --write-audio
.venv/Scripts/python.exe -B tools/perf/analyze_2i.py
git diff --check
git status
```

Run timings/profiling serially without concurrent CPU-heavy work. The analyzer
uses the captured initial/experimental reports; it performs no generation or
upstream imports. Performance data remain ignored under `generated/perf/`.
No new performance or correctness CTest was added.

Created: this document and `tools/perf/analyze_2i.py`.
Modified: `engine/include/outloud/cpu/kernels.hpp`, `engine/src/cpu/dot.hpp`,
`engine/src/cpu/dot_avx2.cpp`, `engine/src/cpu/kernels.cpp`,
`engine/src/cpu/mimi_kernels.cpp`, `tools/perf-check/main.cpp`,
`tools/mimi-check/main.cpp`.
No reference baseline, test, tolerance, CMake ISA flag or installed package was
edited. Nothing was staged, committed or pushed. Step 2I ends here.
