# Step 2H: resident performance and bottleneck investigation

Measured on this development host on 2026-10-05. These numbers describe this
host and workload; they are not runtime requirements or correctness baselines.
Step 2G was committed as `d2a0b0c` and the tree was clean before this work.
This step adds measurement tools and diagnostic counters only. Model arithmetic,
reduction order, layouts, tolerances, CPU dispatch and dependencies are unchanged.

Portable native sequential synthesis is **1.597 times slower than upstream
sequential**, and **1.943 times slower than upstream production**. The largest
measured stage difference is Mimi: 1446.372 ms native versus 673.097 ms upstream.
The ideal native worker-overlap replay gives 1626.115 ms, still above upstream
production's 1424.587 ms. Workers alone cannot reach that target under the
unchanged-kernel assumptions below.

## Workload and revised methodology

Text: `The watch captain needs your help before the next caravan arrives.`
Voice: `gnome_male`; 20 tokens; 112-position voice prefix; seed 73. Every mode
produces 46 latent frames / 88320 float32 samples / 3.680 s at 24000 Hz.
Scheduling is discarded prefill sampling, 47 candidates, accepted EOS at 43,
46 emitted frames, 48 noise draws, final FlowLM offset 179, Mimi offset 736.
Native uses the existing captured noise; upstream uses its actual seeded RNG.

Each main mode loads and validates local assets, runs **one untimed warm-up**,
then **five timed generations** with fresh seed/state before every run.
Minimum, median and maximum retain all five measurements. Median is the primary
comparison; no mean or additional repeated benchmark runs were needed. Total
max-minus-min ranges were 3.01%, 2.04%, 5.70%, 2.34% and 4.24% of the respective
U1/U2/U3/N1/N2 medians. First-PCM variation, especially U3, is recorded as-is.
There are no millisecond/RTF CTest gates or added performance CTests.

All scheduling/output checks, fixture comparisons/hashes, report writing and
console output occur outside timed regions. No benchmark writes WAV files.
Production scheduling hooks verify the single warm-up and are removed before
the five measurements. U2/U3 PCM hashes match the protected Segment 1 float32
hash exactly; U3 also matches the captured latent hash. U1 frame coalescing can
change PCM rounding, so its checks require the correct sample/frame count and
finite float32 output, with the hash recorded rather than forced to U2's hash.
Native checks actual stop decisions, offsets, noise consumption and output counts
after timing; unchanged CTests validate numerical parity with the frozen fixtures.

Python uses `time.perf_counter_ns()`; native uses `std::chrono::steady_clock`.
RTF follows the previous requested convention **audio seconds / wall seconds**,
so a larger RTF is faster. Asset loading/hashing is excluded. U3/N1/N2 start
at text prefill with prepared tokens and reusable state; state reset/allocation
is outside neural total. U1/U2 time the official streaming request, including
its normal text preparation, state creation and worker/queue lifecycle. This
boundary difference is retained to measure production honestly.

U3 calls the untouched installed prefill and autoregressive functions with a
synchronous development sink; no worker or producer/consumer queue runs in U3.
The installed scheduling and mathematics generate each latent before the sink
denormalizes and calls installed Mimi once. N1/N2 use the existing `_into`
native APIs, stop policy and decoder to feed their own generated latents into
Mimi immediately. No model observers or diagnostic counters run in benchmarks.
Stage-boundary timers and a development C++ new/new[] audit are present in the
native harness. Captured-noise retrieval is included in prefill/AR timing.

Prefill includes the discarded sampler in all three sequential modes. AR
includes sampler calls, the final discarded candidate and scheduling. U3 AR
subtracts synchronous decoder intervals from the installed AR-loop duration;
native AR sums candidate intervals, leaving small dispatch/PCM-copy gaps in
total. Sampler alone, Mimi transformer and SEANet are separated only in the
diagnostic runs; they do not have uninstrumented five-run stage medians.
Independent stage medians need not add to the total median.

No affinity, priority, power, thread-environment or backend tuning was done.
Assets were local and Hugging Face loading was forced offline. No network
access was required, and no packages were installed or edited.

## Main measurements

Modes: U1 = official production/default coalescing; U2 = official production
with `max_decoder_frames_per_call=1`; U3 = upstream sequential/one frame;
N1 = native portable sequential; N2 = native optional AVX2/FMA sequential.

First PCM ms

| Mode | Minimum | Median | Maximum |
| --- | --- | --- | --- |
| U1 | 120.213 | 134.900 | 139.327 |
| U2 | 118.985 | 121.557 | 123.992 |
| U3 | 96.913 | 100.286 | 116.867 |
| N1 | 207.360 | 208.817 | 209.758 |
| N2 | 222.750 | 225.001 | 226.304 |

Total ms

| Mode | Minimum | Median | Maximum |
| --- | --- | --- | --- |
| U1 | 1392.123 | 1424.586 | 1434.950 |
| U2 | 1383.839 | 1406.896 | 1412.593 |
| U3 | 1710.654 | 1733.871 | 1809.495 |
| N1 | 2737.012 | 2768.577 | 2801.871 |
| N2 | 2820.355 | 2841.630 | 2940.900 |

RTF (audio / wall)

| Mode | Minimum | Median | Maximum |
| --- | --- | --- | --- |
| U1 | 2.5645 | 2.5832 | 2.6434 |
| U2 | 2.6051 | 2.6157 | 2.6593 |
| U3 | 2.0337 | 2.1224 | 2.1512 |
| N1 | 1.3134 | 1.3292 | 1.3445 |
| N2 | 1.2513 | 1.2950 | 1.3048 |

Uninstrumented stage medians

| Stage ms | U3 | N1 | N2 | N1 / U3 |
| --- | --- | --- | --- | --- |
| prefill_ms | 64.735 | 153.434 | 168.577 | 2.370 |
| ar_including_sampler_ms | 991.005 | 1167.663 | 1242.635 | 1.178 |
| mimi_ms | 673.097 | 1446.372 | 1431.179 | 2.149 |
| total_ms | 1733.871 | 2768.577 | 2841.630 | 1.597 |
| first_pcm_ms | 100.286 | 208.817 | 225.001 | 2.082 |

Native integrated first PCM is measured from prefill through discarded sampling,
BOS sampling and first decode until 1920 float32 samples are ready. Step 2G's
independently summed estimates remain in [NATIVE_MIMI.md](NATIVE_MIMI.md):
207.537 ms portable and 241.139 ms AVX2/FMA. Step 2H's actual integrated medians
are 208.817 and 225.001 ms; loading, playback and eventual application work are
excluded. This is an actual request-compute measurement, not a sum estimate.

U2 is 17.691 ms (1.24%) faster than U1 by total median and its first chunk is
13.343 ms earlier. Default U1 returned 45, 44, 46, 46 and 45 chunks, versus
46 in every U2 run. All calls decoded one or two frames in these U1 runs.
Thus the short canonical segment supplies **no observed coalescing gain**;
the small total difference is comparable to run spread and does not establish
that coalescing generally slows synthesis. U3 / U2 is 1.232: production with
workers uses 18.86% less wall time than sequential one-frame compute here.
U3 / U1 is 1.217 (17.84% lower production wall time). These compare complete
pipelines, not isolated queue overhead. Backend acceleration versus native is
not separated experimentally from layout/math-policy/interpreter effects.

N2 / N1 is 1.026: current AVX2/FMA took 2.64% longer overall by median. Its Mimi
median is slightly lower, while prefill and AR are higher. This dot-product path
does not demonstrate an end-to-end gain or justify raising the CPU minimum.

## Upstream environment and actual backend evidence

Windows AMD64 (`Windows-11-10.0.26300-SP0`), Python 3.12.10, Pocket TTS 3.3.0,
PyTorch 2.14.1+cpu. Installed Pocket sets intra-op threads to **1** at import;
interop threads were **12**. MKLDNN was enabled; MKL, MKLDNN/oneDNN and OpenMP
availability were all true. No relevant thread environment variables were set.
The captured `torch.__config__.show()` reports MSVC 194234444, C++ 202002,
Intel oneAPI MKL 2026.1 Product Build 20260612, MKL-DNN v3.12.0,
OpenMP 2019, LAPACK enabled, CPU capability AVX2, `BLAS_INFO=mkl`,
`USE_MKL=ON`, `USE_MKLDNN=ON`, `USE_OPENMP=ON`, `USE_CUDA=0`, Release.
Native was built Release x64 with MSVC 19.44 and existing `/fp:precise` settings.
The full configuration is preserved in ignored `upstream-benchmark.json`.

One separate CPU `torch.profiler` run of U3 records operator names, self/total
CPU time, calls and shapes. Profile-only module hooks tag the four major groups.
Those inclusive times were FlowLM transformer 1030.646 ms / 48 calls, sampler
266.139 ms / 48, Mimi transformer 350.800 ms / 46 and SEANet 382.390 ms / 46.
Native portable's instrumented sampler, Mimi transformer and SEANet were
173.523, 574.585 and 924.672 ms; AVX2/FMA's were 184.358, 562.601 and
887.695 ms. **These are diagnostic single runs, not comparable stage medians.**
Upstream FlowLM tag excludes native input/EOS/output normalization boundaries;
Mimi tags exclude denormalization, upsample/projection and dispatch outside the
tagged modules. No standalone AR FlowLM/sampler benchmark ratios are claimed.

The dominant upstream operators are `aten::mm`/`aten::addmm`. The largest
shape groups are the 282 single-token FlowLM FFN1, FFN2 and QKV matrix products:
210.052, 192.176 and 156.726 ms self CPU respectively. Mimi FFN1 is 69.675 ms
/ 92 calls; group-1 `aten::slow_conv_transpose2d` is 68.414 ms / 46 calls.
`aten::mkldnn_convolution` proves **oneDNN dispatch** for 184 Conv1d calls:
three channel-reducing residual convolutions and the final mono convolution.
Their profiled total CPU times sum to 58.807 ms. The initial convolution takes
`aten::_slow_conv2d_forward`; transpose convolutions show a mix of slow and
`aten::_convolution` paths, so not every convolution uses oneDNN.
The build is MKL-linked, but an `aten::mm` event alone does not identify its
underlying BLAS routine. Exact vendor-routine attribution for mm/transpose is
**UNVERIFIED**; OpenMP availability does not prove a particular operator used
multiple intra-op threads. No backend is guessed from a CPU brand.

| Family | Self CPU ms |
| --- | --- |
| dense linear / matrix multiply | 1017.733 |
| other operators | 328.160 |
| convolution (all dispatch paths) | 278.366 |
| module wrapper / Python overhead | 272.546 |
| attention-related | 82.304 |
| activations | 63.275 |
| normalization / variance | 22.612 |

The CPU profiler collected the U3 calling thread. A distinct U1 observation
wrapped existing instance methods only for interval/thread-ID recording:
FlowLM/sampling on native thread IDs 32068 (prefill) and 15016 (AR), Mimi on
10416, with **1261.686 ms of intersecting FlowLM/Mimi intervals**. This proves
concurrent model work on different threads, not merely thread existence.
The caller plus two workers were observed (Python active threads 1 -> 3).
Uninstrumented U1/U2 CPU/wall medians were 1.826/1.846; U3/N1/N2 were
0.994/0.999/0.999. U1 process CPU median was 2578.125 ms versus 1424.587 ms
wall. Missing worker events in a thread-local profiler must not be treated as
evidence that the worker did no computation. Observation times are not an
extra benchmark measurement.

## Native profiling and attribution limits

Each path has one separate normal diagnostic generation following one warm-up.
The standalone `outloud-perf-profile` target compiles the existing engine sources
with `OUTLOUD_PERF_DIAGNOSTICS`; ordinary engine libraries and `outloud-perf`
compile all diagnostic macros away. Instrumentation uses enum IDs, fixed
inclusive/exclusive counters and a fixed nesting stack. Callbacks create no
labels or heap allocations. There are no diagnostic timers in ordinary kernels.

Profile total was 2975.187 ms portable and 3033.274 ms AVX2/FMA: 1.075 and
1.067 times their uninstrumented benchmark medians. Profiling is useful for
ranking and identifying repeated work, **not** assigning exact uninstrumented
percentages. In particular 4,139,827 row-widening calls and 111,872 transpose
weight-preparation scopes incur significant timer/counter overhead. Their
measured percentages include diagnostic cost and must not be interpreted as
the savings obtainable by eliminating conversion. No overhead subtraction or
additional profiling repetition was attempted.

Top-ten operation groups below use inclusive function intervals, including
their child weight preparation. Roots, nested attention aggregates and separate
widening counters are omitted from this ranking to avoid duplicates. Full
counters show inclusive intervals, so parent and child rows must not be added.
Domain percentages use nonoverlapping time assigned to FlowLM, sampler or Mimi;
complete-synthesis percentages use the diagnostic run's wall total. Prefill's
FlowLM counter excludes its separate discarded sampler. Global families use
**exclusive** intervals to avoid double counting conversion or attention.
Dense-family time covers linear wrappers and their dot work; dot products in
convolutions and attention are classified by their owning family. Transcendental
work inside GELU/ELU/SiLU/softmax remains in activations/attention; the explicit
RoPE/time-feature family is not a count of every scalar transcendental.

N1 instrumented top ten

| Domain / operation | ms | % synthesis | Calls | ms/call |
| --- | --- | --- | --- | --- |
| flowlm / ffn_linear1 | 428.852 | 14.41 | 288 | 1.489069 |
| flowlm / ffn_linear2 | 384.370 | 12.92 | 288 | 1.334618 |
| flowlm / qkv | 319.632 | 10.74 | 288 | 1.109832 |
| mimi / group1_convtranspose | 193.212 | 6.49 | 46 | 4.200254 |
| mimi / ffn_linear1 | 157.406 | 5.29 | 92 | 1.710934 |
| mimi / ffn_linear2 | 151.803 | 5.10 | 92 | 1.650029 |
| mimi / group2_convtranspose | 139.873 | 4.70 | 46 | 3.040715 |
| mimi / group3_convtranspose | 136.296 | 4.58 | 46 | 2.962963 |
| mimi / initial_conv1d | 133.141 | 4.48 | 46 | 2.894367 |
| mimi / qkv | 116.794 | 3.93 | 92 | 1.269501 |

N1 exclusive families

| Family | ms | % synthesis |
| --- | --- | --- |
| BF16 widening / weight preparation | 1035.924 | 34.82 |
| dense linear (excluding child BF16 widening) | 957.827 | 32.19 |
| Conv1d (excluding child BF16 widening) | 379.941 | 12.77 |
| ConvTranspose (excluding child weight preparation) | 375.920 | 12.64 |
| activations | 108.252 | 3.64 |
| attention (scores, softmax, values) | 93.566 | 3.14 |
| miscellaneous | 17.875 | 0.60 |
| normalization | 3.272 | 0.11 |
| RoPE / time transcendentals | 2.609 | 0.09 |

N1 / flowlm complete counters

| Stage | Inclusive ms | Calls | ms/call | % domain | % synthesis |
| --- | --- | --- | --- | --- | --- |
| prefill | 154.464 | 1 | 154.464300 | 11.88 | 5.19 |
| backbone | 1297.175 | 48 | 27.024485 | 99.80 | 43.60 |
| embedding | 0.016 | 1 | 0.016000 | 0.00 | 0.00 |
| input_projection | 2.553 | 47 | 0.054326 | 0.20 | 0.09 |
| qkv | 319.632 | 288 | 1.109832 | 24.59 | 10.74 |
| attention | 35.319 | 288 | 0.122635 | 2.72 | 1.19 |
| attention_scores | 15.335 | 6432 | 0.002384 | 1.18 | 0.52 |
| softmax | 2.327 | 6432 | 0.000362 | 0.18 | 0.08 |
| attention_values | 17.114 | 6432 | 0.002661 | 1.32 | 0.58 |
| attention_output | 106.977 | 288 | 0.371449 | 8.23 | 3.60 |
| ffn_linear1 | 428.852 | 288 | 1.489069 | 33.00 | 14.41 |
| ffn_linear2 | 384.370 | 288 | 1.334618 | 29.57 | 12.92 |
| normalization | 1.204 | 624 | 0.001930 | 0.09 | 0.04 |
| gelu | 18.342 | 288 | 0.063686 | 1.41 | 0.62 |
| rope | 0.868 | 288 | 0.003014 | 0.07 | 0.03 |
| residual | 0.208 | 576 | 0.000362 | 0.02 | 0.01 |
| eos | 0.036 | 48 | 0.000752 | 0.00 | 0.00 |
| bf16_widening | 747.297 | 2702405 | 0.000277 | 57.50 | 25.12 |

N1 / sampler complete counters

| Stage | Inclusive ms | Calls | ms/call | % domain | % synthesis |
| --- | --- | --- | --- | --- | --- |
| sampler_total | 173.523 | 48 | 3.615054 | 100.00 | 5.83 |
| input_projection | 1.321 | 48 | 0.027523 | 0.76 | 0.04 |
| condition_projection | 7.748 | 48 | 0.161423 | 4.47 | 0.26 |
| normalization | 0.271 | 432 | 0.000627 | 0.16 | 0.01 |
| silu | 0.899 | 720 | 0.001249 | 0.52 | 0.03 |
| residual | 0.027 | 288 | 0.000093 | 0.02 | 0.00 |
| time_features | 0.086 | 96 | 0.000891 | 0.05 | 0.00 |
| time_mlp_linear | 14.687 | 192 | 0.076496 | 8.46 | 0.49 |
| adaln_linear | 92.155 | 336 | 0.274270 | 53.11 | 3.10 |
| block_linear | 55.474 | 576 | 0.096310 | 31.97 | 1.86 |
| final_projection | 0.291 | 48 | 0.006058 | 0.17 | 0.01 |
| modulation | 0.080 | 672 | 0.000118 | 0.05 | 0.00 |
| bf16_widening | 108.553 | 935424 | 0.000116 | 62.56 | 3.65 |

N1 / mimi complete counters

| Stage | Inclusive ms | Calls | ms/call | % domain | % synthesis |
| --- | --- | --- | --- | --- | --- |
| mimi_decode | 1501.721 | 46 | 32.646109 | 100.00 | 50.47 |
| mimi_transformer | 574.585 | 46 | 12.490980 | 38.26 | 19.31 |
| seanet | 924.672 | 46 | 20.101563 | 61.57 | 31.08 |
| projection | 1.267 | 46 | 0.027546 | 0.08 | 0.04 |
| qkv | 116.794 | 92 | 1.269501 | 7.78 | 3.93 |
| attention | 58.247 | 92 | 0.633123 | 3.88 | 1.96 |
| attention_scores | 29.061 | 11776 | 0.002468 | 1.94 | 0.98 |
| softmax | 6.087 | 11776 | 0.000517 | 0.41 | 0.20 |
| attention_values | 22.134 | 11776 | 0.001880 | 1.47 | 0.74 |
| attention_output | 39.495 | 92 | 0.429299 | 2.63 | 1.33 |
| ffn_linear1 | 157.406 | 92 | 1.710934 | 10.48 | 5.29 |
| ffn_linear2 | 151.803 | 92 | 1.650029 | 10.11 | 5.10 |
| normalization | 1.798 | 184 | 0.009769 | 0.12 | 0.06 |
| gelu | 38.299 | 92 | 0.416289 | 2.55 | 1.29 |
| elu | 50.712 | 460 | 0.110244 | 3.38 | 1.70 |
| rope | 1.656 | 92 | 0.017999 | 0.11 | 0.06 |
| residual | 2.103 | 322 | 0.006531 | 0.14 | 0.07 |
| initial_conv1d | 133.141 | 46 | 2.894367 | 8.87 | 4.48 |
| group1_convtranspose | 193.212 | 46 | 4.200254 | 12.87 | 6.49 |
| group1_residual_convs | 58.191 | 92 | 0.632516 | 3.87 | 1.96 |
| group2_convtranspose | 139.873 | 46 | 3.040715 | 9.31 | 4.70 |
| group2_residual_convs | 82.647 | 92 | 0.898342 | 5.50 | 2.78 |
| group3_convtranspose | 136.296 | 46 | 2.962963 | 9.08 | 4.58 |
| group3_residual_convs | 104.670 | 92 | 1.137722 | 6.97 | 3.52 |
| final_conv1d | 20.181 | 46 | 0.438724 | 1.34 | 0.68 |
| depthwise_upsample | 0.527 | 46 | 0.011461 | 0.04 | 0.02 |
| bf16_widening | 86.086 | 501998 | 0.000171 | 5.73 | 2.89 |
| weight_preparation | 93.988 | 111872 | 0.000840 | 6.26 | 3.16 |
| latent_denormalization | 0.009 | 46 | 0.000204 | 0.00 | 0.00 |

N2 instrumented top ten

| Domain / operation | ms | % synthesis | Calls | ms/call |
| --- | --- | --- | --- | --- |
| flowlm / ffn_linear1 | 455.445 | 15.01 | 288 | 1.581408 |
| flowlm / ffn_linear2 | 425.709 | 14.03 | 288 | 1.478157 |
| flowlm / qkv | 341.002 | 11.24 | 288 | 1.184035 |
| mimi / group1_convtranspose | 193.381 | 6.38 | 46 | 4.203937 |
| mimi / ffn_linear2 | 171.131 | 5.64 | 92 | 1.860125 |
| mimi / initial_conv1d | 156.381 | 5.16 | 46 | 3.399591 |
| mimi / ffn_linear1 | 144.176 | 4.75 | 92 | 1.567135 |
| mimi / group2_convtranspose | 128.533 | 4.24 | 46 | 2.794200 |
| mimi / group3_convtranspose | 124.996 | 4.12 | 46 | 2.717296 |
| flowlm / attention_output | 113.753 | 3.75 | 288 | 0.394975 |

N2 exclusive families

| Family | ms | % synthesis |
| --- | --- | --- |
| dense linear (excluding child BF16 widening) | 1048.609 | 34.57 |
| BF16 widening / weight preparation | 1044.972 | 34.45 |
| Conv1d (excluding child BF16 widening) | 366.190 | 12.07 |
| ConvTranspose (excluding child weight preparation) | 355.509 | 11.72 |
| activations | 107.812 | 3.55 |
| attention (scores, softmax, values) | 86.768 | 2.86 |
| miscellaneous | 17.375 | 0.57 |
| normalization | 3.307 | 0.11 |
| RoPE / time transcendentals | 2.732 | 0.09 |

N2 / flowlm complete counters

| Stage | Inclusive ms | Calls | ms/call | % domain | % synthesis |
| --- | --- | --- | --- | --- | --- |
| prefill | 166.963 | 1 | 166.962800 | 11.96 | 5.50 |
| backbone | 1393.283 | 48 | 29.026727 | 99.81 | 45.93 |
| embedding | 0.017 | 1 | 0.017100 | 0.00 | 0.00 |
| input_projection | 2.589 | 47 | 0.055079 | 0.19 | 0.09 |
| qkv | 341.002 | 288 | 1.184035 | 24.43 | 11.24 |
| attention | 35.089 | 288 | 0.121835 | 2.51 | 1.16 |
| attention_scores | 14.519 | 6432 | 0.002257 | 1.04 | 0.48 |
| softmax | 2.373 | 6432 | 0.000369 | 0.17 | 0.08 |
| attention_values | 17.618 | 6432 | 0.002739 | 1.26 | 0.58 |
| attention_output | 113.753 | 288 | 0.394975 | 8.15 | 3.75 |
| ffn_linear1 | 455.445 | 288 | 1.581408 | 32.63 | 15.01 |
| ffn_linear2 | 425.709 | 288 | 1.478157 | 30.50 | 14.03 |
| normalization | 1.234 | 624 | 0.001978 | 0.09 | 0.04 |
| gelu | 18.236 | 288 | 0.063319 | 1.31 | 0.60 |
| rope | 0.949 | 288 | 0.003297 | 0.07 | 0.03 |
| residual | 0.230 | 576 | 0.000399 | 0.02 | 0.01 |
| eos | 0.036 | 48 | 0.000746 | 0.00 | 0.00 |
| bf16_widening | 756.453 | 2702405 | 0.000280 | 54.19 | 24.94 |

N2 / sampler complete counters

| Stage | Inclusive ms | Calls | ms/call | % domain | % synthesis |
| --- | --- | --- | --- | --- | --- |
| sampler_total | 184.358 | 48 | 3.840800 | 100.00 | 6.08 |
| input_projection | 1.339 | 48 | 0.027896 | 0.73 | 0.04 |
| condition_projection | 8.551 | 48 | 0.178137 | 4.64 | 0.28 |
| normalization | 0.271 | 432 | 0.000627 | 0.15 | 0.01 |
| silu | 0.934 | 720 | 0.001298 | 0.51 | 0.03 |
| residual | 0.021 | 288 | 0.000073 | 0.01 | 0.00 |
| time_features | 0.098 | 96 | 0.001021 | 0.05 | 0.00 |
| time_mlp_linear | 15.902 | 192 | 0.082822 | 8.63 | 0.52 |
| adaln_linear | 97.856 | 336 | 0.291239 | 53.08 | 3.23 |
| block_linear | 58.472 | 576 | 0.101513 | 31.72 | 1.93 |
| final_projection | 0.312 | 48 | 0.006508 | 0.17 | 0.01 |
| modulation | 0.080 | 672 | 0.000119 | 0.04 | 0.00 |
| bf16_widening | 110.823 | 935424 | 0.000118 | 60.11 | 3.65 |

N2 / mimi complete counters

| Stage | Inclusive ms | Calls | ms/call | % domain | % synthesis |
| --- | --- | --- | --- | --- | --- |
| mimi_decode | 1452.812 | 46 | 31.582861 | 100.00 | 47.90 |
| mimi_transformer | 562.601 | 46 | 12.230452 | 38.72 | 18.55 |
| seanet | 887.695 | 46 | 19.297709 | 61.10 | 29.27 |
| projection | 1.295 | 46 | 0.028146 | 0.09 | 0.04 |
| qkv | 108.733 | 92 | 1.181880 | 7.48 | 3.58 |
| attention | 51.680 | 92 | 0.561737 | 3.56 | 1.70 |
| attention_scores | 23.449 | 11776 | 0.001991 | 1.61 | 0.77 |
| softmax | 6.124 | 11776 | 0.000520 | 0.42 | 0.20 |
| attention_values | 21.162 | 11776 | 0.001797 | 1.46 | 0.70 |
| attention_output | 36.444 | 92 | 0.396126 | 2.51 | 1.20 |
| ffn_linear1 | 144.176 | 92 | 1.567135 | 9.92 | 4.75 |
| ffn_linear2 | 171.131 | 92 | 1.860125 | 11.78 | 5.64 |
| normalization | 1.801 | 184 | 0.009790 | 0.12 | 0.06 |
| gelu | 38.077 | 92 | 0.413879 | 2.62 | 1.26 |
| elu | 50.565 | 460 | 0.109924 | 3.48 | 1.67 |
| rope | 1.684 | 92 | 0.018307 | 0.12 | 0.06 |
| residual | 1.787 | 322 | 0.005551 | 0.12 | 0.06 |
| initial_conv1d | 156.381 | 46 | 3.399591 | 10.76 | 5.16 |
| group1_convtranspose | 193.381 | 46 | 4.203937 | 13.31 | 6.38 |
| group1_residual_convs | 52.116 | 92 | 0.566483 | 3.59 | 1.72 |
| group2_convtranspose | 128.533 | 46 | 2.794200 | 8.85 | 4.24 |
| group2_residual_convs | 70.869 | 92 | 0.770317 | 4.88 | 2.34 |
| group3_convtranspose | 124.996 | 46 | 2.717296 | 8.60 | 4.12 |
| group3_residual_convs | 88.711 | 92 | 0.964251 | 6.11 | 2.92 |
| final_conv1d | 16.994 | 46 | 0.369437 | 1.17 | 0.56 |
| depthwise_upsample | 0.541 | 46 | 0.011754 | 0.04 | 0.02 |
| bf16_widening | 85.754 | 501998 | 0.000171 | 5.90 | 2.83 |
| weight_preparation | 91.941 | 111872 | 0.000822 | 6.33 | 3.03 |
| latent_denormalization | 0.009 | 46 | 0.000202 | 0.00 | 0.00 |

## Theoretical worker-overlap replay

For each of the five native traces, stage A starts after measured prefill
(including discarded sampling). For each emitted candidate, accumulate its
measured generation interval to get latent-ready time. Stage B finishes at
`max(latent_ready, previous_decode_finish) + measured_decode_duration`.
The 47th candidate is discarded but still runs on A; total is the later of
final A completion and final B completion. PCM-copy/dispatch gaps are omitted
from this ideal replay. It uses actual per-candidate/per-frame durations, not
just stage averages. No worker/queue code was implemented.

| Mode | Ideal min ms | Ideal median ms | Ideal max ms | Median ideal RTF | Sequential / ideal | Additional uniform speedup to U1 |
| --- | --- | --- | --- | --- | --- | --- |
| N1 | 1608.884 | 1626.115 | 1643.608 | 2.2631 | 1.703 | 1.141 |
| N2 | 1613.375 | 1625.148 | 1677.194 | 2.2644 | 1.749 | 1.141 |

Portable: current 2768.577 ms -> ideal 1626.115 ms, maximum modeled speedup
1.703x, theoretical RTF 2.2631 versus upstream U1's 2.5832.
**Could workers alone reach upstream performance? NO** under these assumptions.
Even the best measured portable replay (1608.884 ms) misses the U1 median.
At least `1626.115 / 1424.587 = 1.1415x` additional **uniform** stage speedup
would be required after ideal overlap, equivalent to about 12.39% less time.
This is not a claim that making one arbitrary kernel 14.15% faster is sufficient:
Mimi is currently the limiting stage and speeding it up can move the bottleneck
to FlowLM. AVX2/FMA's ideal result is essentially the same, requiring 1.1408x.

These optimistic bounds assume independent resources, unchanged per-call
durations, zero contention and zero synchronization cost. Real overlap can
increase cache/memory/core contention, as upstream's production CPU demand also
illustrates. Queue cost has not been measured, so no arbitrary cost percentage
or supposedly realistic estimate is supplied. `analyze.pipeline(run,
transfer_ms=...)` supports explicitly hypothetical sensitivity analysis: a
serialized B-side cost delta adds at most `46 * delta` ms to this replay.
This parameter is not a measured queue implementation.

## Smallest next investigation targets and CPU requirements

1. Mimi's SEANet ConvTranspose groups, then initial/residual Conv1d: SEANet
   totals roughly 62% of profiled Mimi, on the limiting stage in the ideal pipeline.
   Inspect phase/window dot work and repeated weight preparation first.
2. Shared BF16-backed dense linear/row-widening path: FlowLM FFN1/FFN2/QKV,
   followed by Mimi FFN/QKV. FlowLM's three projections account for about 87%
   of profiled FlowLM; inspect conversion and single-token versus 16-token
   reuse before selecting a future optimization.
3. Sampler AdaLN/block linears only after those dominant families; attention,
   normalization and RoPE have much smaller native totals in this workload.

No optimization, packing, external backend, SIMD rewrite or ISA requirement
change is implemented. Current evidence does not justify requiring AVX2/FMA:
the existing optional path was slower overall. Future wider/vectorized kernels
may benefit from AVX2/FMA, but any minimum-ISA recommendation remains preliminary
until a measured implementation demonstrates it. No vendor/model-specific
hardware requirement is inferred from this host.

## Memory, reproduction and validation

Resident shared `.olp`/`.olv` storage and bounded BF16 row buffers remain as in
Step 2G; there is no full FP32 model copy. Reusable native candidate storage is
reserved before timing, matching existing Step 2F generation. A preallocated
88320-sample harness output vector permits after-timing validation. Each native
warm-up, timed run and profile observed **zero C++ new/new[] requests in the
candidate/decoder hot loop**. Prefill/context/fixture allocations are outside
that audit; internal CRT/OS allocation is not measured. Normal engine allocation
behavior is unchanged. Profiling counters are fixed-size only.

With the previously exported local assets/fixtures present, from the repo root:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
.venv/Scripts/python.exe -B tools/perf/benchmark_upstream.py
./build/Release/outloud-perf.exe portable
./build/Release/outloud-perf.exe avx2
.venv/Scripts/python.exe -B tools/perf/profile_upstream.py
./build/Release/outloud-perf-profile.exe portable
./build/Release/outloud-perf-profile.exe avx2
.venv/Scripts/python.exe -B tools/perf/analyze.py
ctest --test-dir build -C Release --output-on-failure
cmake -S . -B build-portable -A x64 -DOUTLOUD_ENABLE_AVX2=OFF
cmake --build build-portable --config Release
ctest --test-dir build-portable -C Release --output-on-failure
git diff --check
git status
```

Run timing and profiling commands serially; do not overlap CPU-heavy work with
measurements. Scripts require only the existing venv; normal native tools remain
independent of Python/PyTorch. Generated JSON, text profiles and comparison
tables live under already ignored `generated/perf/`. They are not frozen or
used by CTest. `comparison.json` contains min/median/max, both profiles, families
and overlap traces; raw reports retain all five samples and per-frame times.

Release x64 build passed and all **12/12 existing CTests passed** (39.40 s).
The AVX2-disabled Release build passed and the same **12/12 CTests passed**
(21.89 s). Tests and tolerances are unchanged; no performance test was added.
All five modes and all three profile runs validated the canonical sample/frame
counts. All 51 installed Pocket source/configuration files with wheel RECORD
hashes matched. Source-token comparison against Step 2G confirmed the seven
engine files contain diagnostic additions only. Object-symbol inspection of
both ordinary builds confirmed no diagnostic/clock symbols in those seven
engine objects; the separate profile target contains them as intended.
Both benchmark executables import Windows/MSVC/CRT/BCrypt only. Protected
references and tests have no diff; `git diff --check` passes, generated reports
are ignored, and the Git index is empty. No network access was required.

Created: this document, `tools/perf/benchmark_upstream.py`,
`tools/perf/profile_upstream.py`, `tools/perf/analyze.py`,
`tools/perf-check/main.cpp`, `engine/include/outloud/perf_diagnostics.hpp`.
Modified: `CMakeLists.txt`, `engine/src/assets.cpp`, `engine/src/flowlm.cpp`,
`engine/src/flow_sampler.cpp`, `engine/src/mimi.cpp`, `engine/src/cpu/kernels.cpp`,
`engine/src/cpu/sampler_kernels.cpp`, `engine/src/cpu/mimi_kernels.cpp`.
Engine changes are diagnostic includes/scopes only; normal compilation removes
them. No reference baseline, tolerance or existing test was edited. Nothing was
staged, committed or pushed. Step 2H stops here; kernels, workers, production
RNG, multi-segment native orchestration and SAPI remain deferred.
