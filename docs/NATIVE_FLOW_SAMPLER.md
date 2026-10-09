# Native supplied-noise flow sampler (Step 2E)

> Current runtime composition: [ARCHITECTURE.md](ARCHITECTURE.md). Build/checker instructions below are historical; use [A11/A12](BUILD_AND_VALIDATION_IMPROVEMENTS.md).

This records the original Step 2E implementation and measurements.
[A09](PERFORMANCE_OPTIMIZATION_A09.md) adds a **4 KiB** sampler-owned cache for
normalized `s=0`/`t=1` embeddings and evaluates combined-condition SiLU once per
sample. The normal activation-array payload falls from 23,552 to 18,432 bytes.
Historical measurements below predate those additions; numerical gates remain.

`outloud_flow_sampler` implements the fixed English Pocket TTS 3.3.0
`SimpleMLPAdaLN` and one-step LSD arithmetic. Its public boundary takes one
FP32 hidden row `[1024]` and an externally supplied FP32 noise row `[32]`, and
returns velocity and normalized latent, each `[32]`. Batch size is one.
`FlowSampler` defaults to the portable path. This establishes two canonical
arithmetic cases and one deterministic BOS candidate, not full TTS parity.

## Source contract and operation order

The implementation was checked against the untouched local
`.venv/Lib/site-packages/pocket_tts/models/flow_lm.py`, `modules/mlp.py`, the
English configuration and the prefill scheduling in `models/tts_model.py`.
The existing pinned BF16 model is widened to FP32 by upstream for reference
execution, just as in the Step 2D exporter. Site-packages source is unchanged.

The configured sampler is LSD, one decode step, temperature 0.3, no clamp.
Upstream draws noise with standard deviation `sqrt(0.3)` and then calls the
network with `s=0`, `t=1`, `x=noise`. Its LSD update is `noise + velocity / 1`.
Native code accepts the already scaled noise and computes `noise + velocity`.
It does not scale, clamp, regenerate or denormalize the supplied row. Seed 73
and PyTorch's RNG are reference information only.

1. Biased input projection: noise 32 -> 512. Biased condition projection:
   hidden 1024 -> 512.
2. Separate s/t embeddings: each scalar multiplies its **loaded BF16-rounded
   128 frequencies**, widened exactly. Concatenate **COS first, SIN second**,
   producing 256 FP32 features. Apply biased 256 -> 512 linear, SiLU, biased
   512 -> 512 linear, then the unusual upstream RMSNorm described below.
3. Combine as `condition_projected + (s_normalized + t_normalized) / 2`.
   Preserve addition before division and addition to the condition afterward.
4. Six residual blocks, each operating on width 512. For each block compute
   SiLU(condition), then biased 512 -> 1536 linear, split contiguous chunks
   in the order **shift, scale, gate**. Apply affine custom LayerNorm to x,
   modulate as `norm * (1 + scale) + shift`, apply biased 512 -> 512 linear,
   SiLU, biased 512 -> 512 linear, multiply by gate, then add to original x.
5. Final layer: SiLU(condition), biased 512 -> 1024 linear, split **shift,
   scale**; non-affine custom LayerNorm of x, the same modulation, and biased
   512 -> 32 projection. This returns velocity. Add the original supplied
   noise to obtain the normalized latent.

SiLU uses FP32 `x / (1 + exp(-x))`. All activations and neural accumulation
remain FP32, including the variance reductions. Reduction grouping and the
runtime transcendental functions can differ from PyTorch, so measured
floating tolerances apply. The optional dot product explicitly uses FMA;
portable scalar modulation retains the source expression order.

## Two distinct normalization formulas

The upstream class called `RMSNorm` computes **sample variance**, using
PyTorch's default correction 1. For width 512:

```
mean = sum(x) / 512
variance = sum((x - mean)^2) / 511
output = x * (alpha * (1 / sqrt(variance + 1e-5)))
```

There is **no mean subtraction in the output** and no mean-square RMS
normalization. Alpha is a loaded BF16 vector widened at use. The native
`sample_variance_norm` keeps the multiplication grouping shown above.
Analytical tests distinguish this from conventional RMS, a population
variance denominator, and centered output, including constant nonzero input.

The separate custom LayerNorm uses population variance, denominator 512,
epsilon **1e-6**, and `(x - mean) / sqrt(variance + eps)`. Residual blocks then
apply loaded affine weight/bias; the final norm has no affine parameters.
This differs from the existing backbone LayerNorm epsilon 1e-5. Both new
normalizations use two-pass FP32 moments with eight accumulators and a fixed
pairwise reduction. In-place operation computes statistics before writing.

## Model tensor mapping and CPU reuse

All 68 sampler records are already in the fixed `.olp` schema, IDs 4..71.
Names below are relative to `flow_lm.flow_net.`; weights use `[out,in]` order.
The tests independently assert every ID, name, dtype, shape and element count.

| ID | Parameter | Shape |
|---|---|---|
| 4 / 5 | `cond_embed.bias` / `.weight` | `[512]` / `[512,1024]` |
| 6 / 7 | `final_layer.adaLN_modulation.1.bias` / `.weight` | `[1024]` / `[1024,512]` |
| 8 / 9 | `final_layer.linear.bias` / `.weight` | `[32]` / `[32,512]` |
| 10 / 11 | `input_proj.bias` / `.weight` | `[512]` / `[512,32]` |
| b / b+1 | `res_blocks.i.adaLN_modulation.1.bias` / `.weight` | `[1536]` / `[1536,512]` |
| b+2 / b+3 | `res_blocks.i.in_ln.bias` / `.weight` | `[512]` / `[512]` |
| b+4 / b+5 | `res_blocks.i.mlp.0.bias` / `.weight` | `[512]` / `[512,512]` |
| b+6 / b+7 | `res_blocks.i.mlp.2.bias` / `.weight` | `[512]` / `[512,512]` |
| c | `time_embed.i.freqs` | `[128]` |
| c+1 / c+2 | `time_embed.i.mlp.0.bias` / `.weight` | `[512]` / `[512,256]` |
| c+3 / c+4 | `time_embed.i.mlp.2.bias` / `.weight` | `[512]` / `[512,512]` |
| c+5 | `time_embed.i.mlp.3.alpha` | `[512]` |

Here `b=12+8*i` for six blocks, `c=60+6*i` for two time embeddings. BOS
integration additionally uses existing `flow_lm.bos_emb` (ID 0, `[32]`) and
`flow_lm.input_linear.weight` (ID 72, `[1024,32]`, no bias).

Dense projections reuse Step 2D `cpu::Kernels::linear` and its one-row BF16
widening buffer. Common SiLU, both norms and time features are portable code.
Optional AVX2/FMA uses the existing isolated dot-product translation unit,
guarded by CPUID AVX/FMA/OSXSAVE, XGETBV XMM/YMM state and CPUID AVX2. Explicit
requests fail when unavailable; `automatic` falls back to portable. No host
vendor, topology, affinity, core count or threading choices were added.
Execution is single-threaded. No external compute backend or dependency was
added. No runtime Python, PyTorch, Hugging Face, network or GPU is involved.

## Offline reference fixtures

[export_flow_sampler.py](../tools/neural/export_flow_sampler.py) forces
`HF_HUB_OFFLINE=1` and reuses the pinned local upstream construction from the
Step 2D exporter without invoking its writer. Forward hooks on the untouched
modules capture the actual noise passed by `FlowLM.forward`, not a separately
constructed approximation. After model and voice load, reset seed to 73:

| Case | Source call | Noise draw (zero based) | Offset after call |
|---|---|---:|---:|
| `prefill` | Canonical 20-token Segment 1; empty latent input | 0 | 132 |
| `bos` | One all-NaN latent, replaced upstream with learned BOS | 1 | 133 |

The text is “The watch captain needs your help before the next caravan
arrives.” Voice is `gnome_male`, immutable prefix length 112. The prefill
latent is discarded by upstream TTS scheduling, but its noise draw is real.
The second draw belongs to the first BOS candidate. No generation loop is
used to export these two explicit calls.

Both source hidden hashes exactly match the protected Step 2D
`final.last_hidden` and `single.last_hidden`. Repeating both source calls from
seed 73 yields identical checkpoint bytes and RNG-state hashes. The exporter
also proves bitwise source residual reconstruction, the time averaging order,
and `latent == noise + velocity`.

[flow-sampler-baseline.json](../reference/flow-sampler-baseline.json) tracks
only stable metadata: compatibility/model/voice hashes, case inputs and
outputs, source configuration, RNG-state hashes, checkpoint names, flat
shapes, dtype, byte offsets and SHA-256. Batch-one rows are flattened in the
fixture, so a 512-wide activation is represented as `[512]`. All floats live
in ignored `generated/neural/flow-sampler-f32.bin`: **192 checkpoints**, 96 per
case, **373,520 bytes**, SHA-256
`efc2786d369de30d423fbe3ae3843b402b55c0648cbe812f28144826b64faeb1`.
The generated ignored C++ header carries the same hashes and bounds. The
reader validates whole-file hash/length, contiguous bounds, each checkpoint
hash, and absence of trailing bytes. Regeneration refuses changed frozen
metadata before replacing the blob. Prior baseline writers are never called.

## Checker, regression limits and validation

The checker verifies all 192 direct checkpoints using exact captured hidden
and noise, then 192 more with native prefill/BOS hidden states. The latter
include the established backbone error. It also compares **130 isolated
primitive outputs**, using captured upstream inputs to separate each linear,
SiLU, norm and AdaLN split error from accumulated network error. Each supported
CPU path runs both cases, repeat-bit checks, one-step addition checks and BOS
integration; portable/AVX2 path agreement covers all 384 execution checkpoints.

Reference inputs, noise bytes and s/t scalars must be exact. Every floating
checkpoint reports max absolute error, RMS, unrestricted nonzero-reference
relative error, zero-reference absolute error, and meaningful relative error.
For the meaningful metric, include elements with
`abs(reference) >= 1e-6 * max(1, max(abs(reference)))`. The unrestricted metric
is retained as diagnostic information; small denominators are not concealed.

Measurements were made **before** defining numerical gates. The 96 stage
entries in [flow_sampler_tolerances.hpp](../tests/flow_sampler_tolerances.hpp)
take the maximum of both cases, both paths, direct/integrated execution,
isolated primitive measurements and path agreement. Each of absolute, RMS
and meaningful-relative limits is twice its observed maximum, rounded upward
to one significant decimal digit; observed zeros stay zero. All three limits
must pass independently. Thus limits are specific to each block and stage,
and do not change the frozen PyTorch data. Example limits:

| Stage | Max absolute limit | RMS limit | Meaningful relative limit |
|---|---:|---:|---:|
| supplied noise / s,t scalars | 0 | 0 | 0 |
| projected noise | 6e-8 | 8e-9 | 1e-4 |
| normalized s embedding | 3e-6 | 3e-7 | 1e-6 |
| normalized t embedding | 2e-6 | 2e-7 | 3e-6 |
| block3 modulation | 3e-5 | 3e-6 | 1e-3 |
| velocity | 3e-6 | 9e-7 | 8e-4 |
| normalized latent | 3e-6 | 9e-7 | 9e-4 |

The normal checker exits nonzero on a failed structural/numerical check;
only a successful normal run prints `RESULT: PASS`. `--measure` preserves
structural/hash checks but bypasses numerical gates and prints `MEASURED`.
Ignored `sampler-portable-parity.json`, `sampler-avx2-parity.json` and
`sampler-path-agreement.json` contain every comparison separately and current
informational timing. The checker requires a writable fixture directory for
these development reports; this is not a production asset-reader requirement.

CTest adds `canonical-flow-sampler` and `flow-sampler-regressions`. Coverage
includes all tensor IDs/shapes, analytical SiLU and norm cases, affine and
non-affine norm behavior, loaded frequency mutation, cosine/sine ordering,
all actual time MLPs, all six residual blocks and final layer, both velocities
and latents, path agreement, invalid dimensions/nonfinite inputs, exhausted
cache capacity, incomplete backbone state, portable default and automatic
fallback. Intentional failed numerical metrics verify gate rejection.
All eight Step 2B/2C/2D/2E tests pass with AVX2 enabled and in an independent
build with `OUTLOUD_ENABLE_AVX2=OFF`.

With the prior local assets/fixtures already exported:

```powershell
$env:HF_HUB_OFFLINE = '1'
$env:PYTHONDONTWRITEBYTECODE = '1'
.\.venv\Scripts\python.exe tools/neural/export_flow_sampler.py
cmake -S . -B build -A x64
cmake --build build --config Release
.\build\Release\outloud-flow-sampler-check.exe
ctest --test-dir build -C Release --output-on-failure
cmake -S . -B build-portable -A x64 -DOUTLOUD_ENABLE_AVX2=OFF
cmake --build build-portable --config Release
ctest --test-dir build-portable -C Release --output-on-failure
```

Build and native checks consume existing local assets and fixtures without
fetching anything. A missing generated asset causes a configure error. Python
and PyTorch are used only for development export.

## Deterministic BOS integration

`FlowSampler::bos_candidate` widens learned BOS directly, projects it through
the existing bias-free 32 -> 1024 input linear, calls the existing backbone
and EOS projection for exactly one position, then samples with supplied
noise. This is equivalent to upstream NaN -> learned BOS substitution without
requiring public callers to construct NaNs. The result carries the normalized
hidden row, EOS logit, velocity and normalized latent. EOS stopping is absent.

Tests prefill the canonical text to offset 132 and assert one successful
candidate advances all six layers to 133; its sampler hidden is checked
against the BOS reference. Malformed/nonfinite noise is rejected before any
cache mutation. Exhausted capacity and incomplete state are rejected by the
existing backbone. Sampler observer spans are valid only during each callback.
Observers must not reenter the same mutable sampler. If a sampler observer
throws after the backbone succeeds, the candidate's cache position has already
been consumed; reset/re-prefill before retrying that logical candidate.
Backbone failures retain the existing reset-required state semantics.

No normal-frame helper or autoregressive loop is added. Future feedback uses
the previous **normalized** latent; denormalization belongs later at the Mimi
boundary.

## Memory and timings

Sampler weights occupy **9,759,008 BF16 elements / 19,518,016 bytes** in the
existing resident read-only `.olp`. These are 68 records already included in
the model, not an additional asset or duplicated allocation. The asset handle
shares ownership of that mapping. There is **no permanent FP32 sampler-weight
copy and no complete FP32 model copy**.

| Native sampler allocation | Bytes | Lifetime |
|---|---:|---|
| Eight 512-wide activation arrays | 16,384 | Per-call stack scratch |
| AdaLN parameter buffer, max 1536 floats | 6,144 | Per-call stack scratch, reused |
| Sinusoidal feature buffer, 256 floats | 1,024 | Per-call stack scratch, reused |
| Total main activation scratch | **23,552** | Per-call stack scratch |
| Existing one-row widening buffer | **16,384** | Sampler kernel context, reused |
| Velocity + latent | 256 | Returned `FlowSample` |

The sampler's widest input row is 1024 (4096 used bytes); it keeps the existing
fixed 16 KiB context buffer rather than adding a different conversion path.
Norm weights/biases/frequencies widen individual scalars at use. Small local
reduction arrays, object headers, observer labels and call-stack overhead are
additional to the main scratch figures. Test observers copy checkpoint arrays
only in the checker, not in ordinary sampling.

BOS projection uses an additional 32+1024 float stack arrays (4224 bytes).
The existing backbone has its own widening buffer, a returned 1024-float
hidden row, and single-position workspace. At offset 132 that reported
backbone workspace is `(12*1024+133)*4+16384 = 66,068` bytes. Main backbone
temporary activations are freed before sampler execution. The test state with
capacity 21 uses `6*2*21*1024*4 = 1,032,192` mutable suffix bytes and shares the
unchanged 5.5 MB voice prefix. See [NATIVE_FLOWLM.md](NATIVE_FLOWLM.md) for the
existing state and workspace accounting. Timings and observed parity follow.

## Remaining unverified work

Production RNG and its consumption scheduling are deliberately postponed:
these tests prove arithmetic for captured supplied noise, not PyTorch RNG
compatibility. Other noise vectors, text/voices, long generation, physical
CPUs without AVX2 and broader compiler/libm combinations remain unverified.
The AVX2-disabled build verifies the portable build/dispatch behavior on the
available host. Timings are single-call informational Release measurements
after asset load, without observers; they exclude prefill, loading/hashing
and exporter execution and are not a benchmark or a cross-CPU speed promise.

No generation/stopping loop, Mimi, latent denormalization, PCM, SAPI, registry
or WoW integration is implemented. No dependency is added. Previous protected
baselines are unchanged. Step 2E ends here.

## Observed numerical measurements

The following tables preserve the measured errors used to establish gates.

| CPU path | Sampler only (ms) | One BOS candidate (ms) |
|---|---:|---:|
| Portable | 2.5442 | 22.6573 |
| AVX2/FMA | 2.7486 | 24.5232 |

One measured Release x64 call per path on the development host, MSVC 19.44.
These optional kernels are not assumed to be faster on any particular CPU.

| 32-D latent boundary | Portable abs | Portable meaningful rel | Portable RMS | AVX2 abs | AVX2 meaningful rel | AVX2 RMS |
|---|---:|---:|---:|---:|---:|---:|
| `prefill` | 5.364418e-07 | 5.696732e-06 | 1.981770e-07 | 4.172325e-07 | 7.449572e-06 | 2.004573e-07 |
| `bos` | 4.768372e-07 | 4.062027e-04 | 2.200958e-07 | 6.556511e-07 | 3.655824e-04 | 2.137137e-07 |
| `integrated.prefill` | 1.013279e-06 | 6.573152e-06 | 4.161668e-07 | 1.072884e-06 | 4.796155e-06 | 4.073192e-07 |
| `integrated.bos` | 9.536743e-07 | 1.624811e-04 | 3.610650e-07 | 7.152557e-07 | 1.375390e-05 | 2.874545e-07 |

`prefill` and `bos` supply captured reference hidden rows directly;
`integrated.*` supplies hidden rows produced by the native backbone.
Maximum execution-checkpoint absolute errors are 1.430511e-5 portable and
1.144409e-5 AVX2/FMA. Maximum path-agreement absolute error is 7.629395e-6.

Each row below is the maximum for that stage across both cases and direct /
native-integrated execution. The three metrics are aggregated independently.
Full per-case and isolated-kernel measurements remain in the ignored reports.

| Checkpoint stage | Portable abs | Portable meaningful rel | Portable RMS | AVX2 abs | AVX2 meaningful rel | AVX2 RMS |
|---|---:|---:|---:|---:|---:|---:|
| `block0.condition_silu` | 2.146e-06 | 3.618e-04 | 5.035e-07 | 1.907e-06 | 5.694e-04 | 4.587e-07 |
| `block0.gate` | 9.537e-07 | 9.074e-05 | 2.225e-07 | 9.835e-07 | 3.947e-04 | 2.368e-07 |
| `block0.gated_update` | 4.768e-07 | 1.033e-03 | 5.328e-08 | 7.153e-07 | 7.407e-04 | 5.226e-08 |
| `block0.input` | 2.980e-08 | 4.756e-05 | 3.701e-09 | 2.980e-08 | 3.155e-05 | 3.880e-09 |
| `block0.linear1` | 2.861e-06 | 5.335e-04 | 3.339e-07 | 2.861e-06 | 9.557e-04 | 3.967e-07 |
| `block0.linear2` | 1.974e-07 | 1.033e-03 | 5.698e-08 | 2.980e-07 | 7.425e-04 | 7.349e-08 |
| `block0.modulated` | 1.431e-06 | 1.061e-02 | 2.731e-07 | 1.907e-06 | 4.545e-03 | 2.856e-07 |
| `block0.norm` | 2.384e-07 | 1.052e-04 | 2.414e-08 | 2.980e-07 | 2.893e-04 | 3.093e-08 |
| `block0.output` | 4.768e-07 | 1.032e-04 | 5.359e-08 | 7.153e-07 | 7.541e-05 | 5.262e-08 |
| `block0.scale` | 3.815e-06 | 6.823e-05 | 7.782e-07 | 4.768e-06 | 5.538e-05 | 8.623e-07 |
| `block0.shift` | 1.192e-06 | 2.708e-03 | 2.093e-07 | 1.431e-06 | 2.418e-03 | 2.295e-07 |
| `block0.silu` | 8.643e-07 | 5.336e-04 | 9.735e-08 | 9.537e-07 | 9.559e-04 | 1.017e-07 |
| `block1.condition_silu` | 2.146e-06 | 3.618e-04 | 5.035e-07 | 1.907e-06 | 5.694e-04 | 4.587e-07 |
| `block1.gate` | 1.431e-06 | 1.013e-03 | 2.480e-07 | 9.537e-07 | 4.746e-04 | 2.215e-07 |
| `block1.gated_update` | 8.345e-07 | 1.268e-03 | 7.606e-08 | 8.345e-07 | 1.395e-03 | 7.035e-08 |
| `block1.input` | 4.768e-07 | 1.032e-04 | 5.359e-08 | 7.153e-07 | 7.541e-05 | 5.262e-08 |
| `block1.linear1` | 2.146e-06 | 6.247e-03 | 3.841e-07 | 2.384e-06 | 5.090e-03 | 4.054e-07 |
| `block1.linear2` | 4.172e-07 | 1.268e-03 | 7.176e-08 | 3.576e-07 | 1.395e-03 | 7.594e-08 |
| `block1.modulated` | 4.530e-06 | 1.292e-04 | 5.570e-07 | 4.768e-06 | 1.380e-04 | 5.276e-07 |
| `block1.norm` | 1.101e-06 | 9.483e-04 | 8.430e-08 | 7.991e-07 | 1.074e-03 | 7.368e-08 |
| `block1.output` | 8.345e-07 | 3.528e-04 | 8.693e-08 | 7.153e-07 | 5.033e-04 | 8.634e-08 |
| `block1.scale` | 7.629e-06 | 2.506e-04 | 1.529e-06 | 7.629e-06 | 2.242e-04 | 1.491e-06 |
| `block1.shift` | 1.431e-06 | 1.257e-03 | 2.037e-07 | 1.907e-06 | 1.257e-03 | 2.152e-07 |
| `block1.silu` | 1.252e-06 | 6.247e-03 | 1.533e-07 | 1.609e-06 | 5.090e-03 | 1.613e-07 |
| `block2.condition_silu` | 2.146e-06 | 3.618e-04 | 5.035e-07 | 1.907e-06 | 5.694e-04 | 4.587e-07 |
| `block2.gate` | 9.537e-07 | 2.271e-04 | 2.420e-07 | 1.073e-06 | 4.391e-04 | 2.309e-07 |
| `block2.gated_update` | 5.662e-07 | 2.424e-04 | 9.084e-08 | 7.749e-07 | 4.397e-04 | 9.136e-08 |
| `block2.input` | 8.345e-07 | 3.528e-04 | 8.693e-08 | 7.153e-07 | 5.033e-04 | 8.634e-08 |
| `block2.linear1` | 2.623e-06 | 2.033e-03 | 6.890e-07 | 2.861e-06 | 1.886e-03 | 6.266e-07 |
| `block2.linear2` | 4.172e-07 | 3.717e-03 | 9.288e-08 | 3.427e-07 | 2.230e-03 | 9.598e-08 |
| `block2.modulated` | 8.583e-06 | 4.026e-04 | 9.886e-07 | 8.583e-06 | 2.460e-04 | 1.051e-06 |
| `block2.norm` | 9.537e-07 | 3.347e-04 | 1.122e-07 | 1.073e-06 | 1.987e-04 | 1.087e-07 |
| `block2.output` | 8.345e-07 | 1.380e-03 | 1.067e-07 | 1.132e-06 | 1.558e-03 | 1.131e-07 |
| `block2.scale` | 9.537e-06 | 1.518e-05 | 1.725e-06 | 9.537e-06 | 1.902e-05 | 1.797e-06 |
| `block2.shift` | 1.431e-06 | 3.844e-04 | 2.772e-07 | 1.907e-06 | 1.781e-04 | 2.628e-07 |
| `block2.silu` | 1.073e-06 | 2.033e-03 | 1.989e-07 | 1.788e-06 | 1.886e-03 | 2.174e-07 |
| `block3.condition_silu` | 2.146e-06 | 3.618e-04 | 5.035e-07 | 1.907e-06 | 5.694e-04 | 4.587e-07 |
| `block3.gate` | 1.371e-06 | 8.978e-05 | 2.499e-07 | 9.537e-07 | 1.318e-04 | 2.403e-07 |
| `block3.gated_update` | 9.537e-07 | 8.232e-04 | 1.598e-07 | 8.345e-07 | 7.199e-04 | 1.482e-07 |
| `block3.input` | 8.345e-07 | 1.380e-03 | 1.067e-07 | 1.132e-06 | 1.558e-03 | 1.131e-07 |
| `block3.linear1` | 5.007e-06 | 1.449e-04 | 1.030e-06 | 3.338e-06 | 1.323e-04 | 8.389e-07 |
| `block3.linear2` | 7.972e-07 | 8.204e-04 | 1.433e-07 | 5.960e-07 | 7.196e-04 | 1.280e-07 |
| `block3.modulated` | 1.431e-05 | 3.114e-04 | 1.389e-06 | 1.144e-05 | 4.541e-04 | 1.318e-06 |
| `block3.norm` | 1.311e-06 | 1.231e-04 | 1.320e-07 | 1.311e-06 | 1.281e-04 | 1.270e-07 |
| `block3.output` | 1.073e-06 | 8.711e-04 | 1.719e-07 | 1.431e-06 | 1.195e-03 | 1.686e-07 |
| `block3.scale` | 5.722e-06 | 2.001e-05 | 1.529e-06 | 7.629e-06 | 2.449e-05 | 1.578e-06 |
| `block3.shift` | 1.907e-06 | 8.123e-04 | 2.935e-07 | 1.907e-06 | 7.290e-04 | 2.663e-07 |
| `block3.silu` | 3.099e-06 | 1.440e-04 | 3.617e-07 | 2.325e-06 | 1.314e-04 | 3.170e-07 |
| `block4.condition_silu` | 2.146e-06 | 3.618e-04 | 5.035e-07 | 1.907e-06 | 5.694e-04 | 4.587e-07 |
| `block4.gate` | 1.907e-06 | 3.087e-04 | 2.863e-07 | 9.537e-07 | 2.116e-04 | 2.702e-07 |
| `block4.gated_update` | 1.431e-06 | 4.529e-04 | 1.861e-07 | 1.431e-06 | 4.463e-04 | 1.921e-07 |
| `block4.input` | 1.073e-06 | 8.711e-04 | 1.719e-07 | 1.431e-06 | 1.195e-03 | 1.686e-07 |
| `block4.linear1` | 3.636e-06 | 4.928e-04 | 8.524e-07 | 4.053e-06 | 1.408e-03 | 9.838e-07 |
| `block4.linear2` | 6.258e-07 | 4.528e-04 | 1.621e-07 | 8.047e-07 | 4.465e-04 | 1.585e-07 |
| `block4.modulated` | 9.060e-06 | 1.709e-03 | 1.449e-06 | 9.537e-06 | 6.031e-04 | 1.558e-06 |
| `block4.norm` | 1.028e-06 | 2.155e-03 | 1.247e-07 | 9.537e-07 | 9.516e-04 | 1.219e-07 |
| `block4.output` | 1.431e-06 | 2.653e-04 | 2.367e-07 | 2.384e-06 | 3.017e-04 | 2.239e-07 |
| `block4.scale` | 7.629e-06 | 1.806e-05 | 1.659e-06 | 5.722e-06 | 2.332e-05 | 1.615e-06 |
| `block4.shift` | 1.431e-06 | 1.049e-03 | 3.032e-07 | 1.431e-06 | 1.349e-03 | 2.785e-07 |
| `block4.silu` | 2.742e-06 | 4.932e-04 | 3.288e-07 | 1.669e-06 | 1.409e-03 | 3.021e-07 |
| `block5.condition_silu` | 2.146e-06 | 3.618e-04 | 5.035e-07 | 1.907e-06 | 5.694e-04 | 4.587e-07 |
| `block5.gate` | 1.192e-06 | 1.704e-03 | 2.847e-07 | 2.861e-06 | 1.498e-03 | 2.959e-07 |
| `block5.gated_update` | 2.861e-06 | 9.576e-04 | 3.673e-07 | 3.815e-06 | 5.789e-04 | 3.815e-07 |
| `block5.input` | 1.431e-06 | 2.653e-04 | 2.367e-07 | 2.384e-06 | 3.017e-04 | 2.239e-07 |
| `block5.linear1` | 3.338e-06 | 3.097e-03 | 9.504e-07 | 3.189e-06 | 3.735e-03 | 9.598e-07 |
| `block5.linear2` | 2.861e-06 | 9.885e-03 | 5.239e-07 | 2.146e-06 | 5.023e-03 | 4.656e-07 |
| `block5.modulated` | 9.537e-06 | 2.083e-03 | 1.515e-06 | 7.987e-06 | 3.208e-03 | 1.467e-06 |
| `block5.norm` | 1.464e-06 | 4.010e-03 | 1.299e-07 | 1.401e-06 | 3.837e-03 | 1.240e-07 |
| `block5.output` | 3.815e-06 | 1.854e-03 | 4.060e-07 | 3.815e-06 | 6.865e-04 | 4.161e-07 |
| `block5.scale` | 7.629e-06 | 4.592e-05 | 1.918e-06 | 9.537e-06 | 7.417e-05 | 1.880e-06 |
| `block5.shift` | 9.537e-07 | 6.598e-04 | 2.437e-07 | 1.907e-06 | 4.838e-04 | 2.370e-07 |
| `block5.silu` | 3.338e-06 | 3.098e-03 | 5.064e-07 | 2.861e-06 | 3.736e-03 | 4.942e-07 |
| `condition.combined` | 1.907e-06 | 3.617e-04 | 6.330e-07 | 1.907e-06 | 5.693e-04 | 5.705e-07 |
| `condition.projected` | 1.907e-06 | 5.051e-04 | 6.272e-07 | 1.937e-06 | 3.281e-04 | 5.689e-07 |
| `final.condition_silu` | 2.146e-06 | 3.618e-04 | 5.035e-07 | 1.907e-06 | 5.694e-04 | 4.587e-07 |
| `final.modulated` | 4.172e-06 | 4.929e-04 | 5.386e-07 | 5.245e-06 | 2.596e-04 | 5.343e-07 |
| `final.norm` | 2.861e-06 | 1.652e-03 | 3.070e-07 | 3.815e-06 | 3.375e-04 | 3.038e-07 |
| `final.scale` | 5.722e-06 | 3.519e-04 | 8.005e-07 | 5.722e-06 | 5.083e-04 | 8.978e-07 |
| `final.shift` | 1.192e-06 | 7.704e-04 | 1.469e-07 | 1.192e-06 | 6.644e-04 | 1.361e-07 |
| `input.hidden` | 1.520e-06 | 3.450e-03 | 4.708e-07 | 1.431e-06 | 8.023e-04 | 4.545e-07 |
| `input.noise` | 0.000e+00 | 0.000e+00 | 0.000e+00 | 0.000e+00 | 0.000e+00 | 0.000e+00 |
| `input.projected` | 2.980e-08 | 4.756e-05 | 3.701e-09 | 2.980e-08 | 3.155e-05 | 3.880e-09 |
| `latent` | 1.013e-06 | 4.062e-04 | 4.162e-07 | 1.073e-06 | 3.656e-04 | 4.073e-07 |
| `time.s.features` | 0.000e+00 | 0.000e+00 | 0.000e+00 | 0.000e+00 | 0.000e+00 | 0.000e+00 |
| `time.s.linear1` | 0.000e+00 | 0.000e+00 | 0.000e+00 | 0.000e+00 | 0.000e+00 | 0.000e+00 |
| `time.s.mlp` | 1.788e-07 | 3.906e-07 | 4.472e-08 | 1.788e-07 | 3.906e-07 | 4.465e-08 |
| `time.s.normalized` | 1.192e-06 | 4.323e-07 | 9.707e-08 | 1.431e-06 | 4.793e-07 | 1.294e-07 |
| `time.s.scalar` | 0.000e+00 | 0.000e+00 | 0.000e+00 | 0.000e+00 | 0.000e+00 | 0.000e+00 |
| `time.s.silu` | 2.980e-08 | 1.309e-07 | 2.089e-09 | 2.980e-08 | 1.309e-07 | 2.089e-09 |
| `time.t.features` | 5.960e-08 | 1.103e-07 | 1.070e-08 | 5.960e-08 | 1.103e-07 | 1.070e-08 |
| `time.t.linear1` | 1.192e-07 | 1.203e-04 | 6.869e-09 | 1.192e-07 | 1.404e-04 | 7.109e-09 |
| `time.t.mlp` | 1.863e-08 | 1.144e-06 | 5.200e-09 | 1.863e-08 | 9.805e-07 | 5.314e-09 |
| `time.t.normalized` | 8.941e-07 | 1.147e-06 | 6.017e-08 | 7.749e-07 | 9.945e-07 | 5.380e-08 |
| `time.t.scalar` | 0.000e+00 | 0.000e+00 | 0.000e+00 | 0.000e+00 | 0.000e+00 | 0.000e+00 |
| `time.t.silu` | 2.235e-08 | 1.203e-04 | 2.409e-09 | 2.235e-08 | 1.404e-04 | 2.492e-09 |
| `velocity` | 1.013e-06 | 3.688e-04 | 4.174e-07 | 1.043e-06 | 1.801e-04 | 4.042e-07 |
