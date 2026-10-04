# Native deterministic FlowLM backbone (Step 2D)

`outloud_flowlm` implements Pocket TTS 3.3.0's fixed English, batch-1 FlowLM
text embedding, six transformer layers, final LayerNorm and deterministic EOS
projection. It uses the existing `.olp` and `.olv` loaders. It has no sampler,
noise/RNG, generation loop, EOS stopping policy, Mimi, PCM or SAPI code. This
step establishes canonical deterministic backbone parity, not full TTS parity.

## CPU boundary and portability

[kernels.hpp](../engine/include/outloud/cpu/kernels.hpp) exposes small typed
row-major FP32 views and the operations this backbone uses. There is no generic
tensor, model graph or neural backend. All computation is single-threaded.
The default is the portable path, built with the Windows x64/MSVC baseline ISA
and `/fp:precise`. Its dot product has eight independent FP32 accumulators and
a fixed pairwise reduction; tails are handled explicitly. MSVC can use baseline
SSE2 automatically. Correctness does not require AVX2, FMA or BF16 instructions.

An optional AVX2/FMA dot product occupies one separate translation unit/library,
compiled with `/arch:AVX2`. It uses eight FP32 lanes, explicit FP32 fused
multiply-add and the same final lane-reduction grouping. Dense linears and
attention Q/K dot products share this boundary. LayerNorm, GELU, RoPE, softmax,
V accumulation and residual operations use the common portable implementation.
This small optional path is not a promise of a speedup.

`KernelPath::portable` always selects the baseline. `KernelPath::automatic`
opts into capability dispatch, and `KernelPath::avx2_fma` requests that path
explicitly. Before selecting AVX2/FMA, `cpu_features()` checks CPUID leaf 1 for
AVX, FMA and OSXSAVE, XGETBV for both XMM/YMM state bits, and leaf 7 for AVX2.
XGETBV is executed only after its prerequisite checks. Automatic selection
falls back to portable if the implementation or CPU/OS support is missing;
explicit AVX2/FMA requests fail when unavailable. `OUTLOUD_ENABLE_AVX2=OFF`
omits that library entirely. No AVX2 option is applied to the backbone or
portable code, and no whole-program ISA optimization is enabled.

The engine is not tied to this development CPU, AMD or Intel. There are no
vendor/model, core count, SMT, cache size, affinity or thread-pool decisions.
The native default stays portable. Optimized-path policy and broader performance
work can be revisited after measurements across representative CPUs.

## BF16 storage and FP32 computation

All model data remains exact BF16 in the read-only model mapping. Embedding
lookup widens only selected rows. A dense projection widens one BF16 weight
row into a fixed 4096-float (16 KiB) buffer, then reuses that row for all input
positions. It never allocates a permanent FP32 matrix, packed full-model copy
or reduced-precision accumulator. Activations and all neural accumulations
are FP32. Only test error statistics use double precision.

There is no complete FP32 model duplicate. This strategy bounds conversion
memory independently of output width and preserves the Step 2C exact widening
helper. Native parameter selection uses the frozen `.olp` numeric schema:
embedding; eight records per transformer layer; final norm; EOS weight/bias.
The sampler and Mimi weights in `.olp` remain unused. See
[NATIVE_ASSETS.md](NATIVE_ASSETS.md) for the verified source and asset contract.

## Operation order and cache layout

The implementation was checked against the untouched installed
`pocket_tts/models/flow_lm.py`, `modules/transformer.py`, `modules/attention.py`,
`modules/rope.py`, `modules/text_conditioner.py` and the English configuration.
The LayerNorm modules are PyTorch `nn.LayerNorm`, epsilon 1e-5.

1. Look up token IDs in BF16 `[4001,1024]` embedding rows and widen exactly to
   FP32 `[T,1024]`. No additional BOS/EOS token or embedding scaling is inserted.
2. At each of six layers: LayerNorm -> bias-free packed QKV linear
   `[T,3072]` -> adjacent-coordinate RoPE on new Q/K -> append post-RoPE K and
   unrotated V -> causal scaled dot-product attention -> bias-free output
   projection -> attention residual.
3. LayerNorm -> bias-free linear 1024-to-4096 -> tanh-approximate GELU ->
   bias-free linear 4096-to-1024 -> FFN residual.
4. Final affine LayerNorm, select the last hidden row `[1024]`, then apply
   the biased 1024-to-1 EOS linear. Return the logit without a stopping decision.

LayerNorm computes FP32 mean and population variance, adds 1e-5, and applies
the widened affine weights. GELU is
`0.5*x*(1+tanh(sqrt(2/pi)*(x+0.044715*x*x*x)))`, with FP32 intermediates.
Attention uses scale `1/sqrt(64) = 0.125`, stable max-subtracted softmax, no
dropout and no finite context cutoff. Reduction order can differ from PyTorch.

`FlowLMPrefillState` shares the original `.olv` mapping and owns separate K/V
suffix arrays for each layer, each laid out `[suffix_capacity,16,64]`. There is
no per-segment copy of the 5.5 MB prefix. The immutable prefix and appended
suffix are read as two spans by attention. All six offsets initially equal the
voice prefix length. Offsets advance together only after a successful call.
A computation/observer failure marks the state incomplete; `reset()` is
required before reuse. Reset restores prefix offsets and hides old suffix
contents without copying or rotating the prefix.

For query row i of a call at absolute offset O, keys at absolute positions
`0..O+i` are visible, including the current key. Later keys are not read.
Canonical text positions are 112..131 and the resulting offset is exactly
**132** in every layer. Imported K is already post-RoPE and is never passed to
the RoPE helper. New Q/K pair d uses frequency
`exp(float(d) * float(-log(10000)*2/64))`, angle `frequency * absolute_position`,
and the upstream adjacent real/imaginary rotation. Only new post-RoPE K is
stored in the suffix. V is copied directly from its packed projection slice.

The current API bounds suffix capacity and per-call token count at 4096;
these are allocation limits, not an attention context cutoff. Cache views,
shapes, capacity, finite inputs, token IDs, compatible model/voice identity and
state voice identity are validated. A state cannot be reused with another voice.
The state and compute context are intended for one caller at a time.

```cpp
outloud::PocketModelAsset model(model_path);
outloud::OutLoudVoice voice(voice_path);
outloud::FlowLMBackbone backbone(model, voice); // portable by default
auto state = backbone.make_state(20);
auto result = backbone.prefill(segment_token_ids, state);
// state.offset() == voice.prefix_length() + segment_token_ids.size()
// result.normalized [T,1024], result.last_hidden() [1024], result.eos_logit
```

An optional observer receives ephemeral checkpoint views, with shapes, for
development comparisons. The library does not store fixtures or launch Python.
`backbone()` also accepts supplied deterministic embeddings. The checker uses
it for one learned-BOS position after prefill, without sampling or a generation
loop; that standalone probe advances the test state from 132 to 133.

## Offline reference and fixture structure

[export_flowlm_prefill.py](../tools/neural/export_flowlm_prefill.py) forces
`HF_HUB_OFFLINE=1`, verifies the original local model/tokenizer/gnome hashes,
and instantiates the installed unmodified FlowLM with local tokenizer data.
Meta-device construction avoids CPU RNG initialization; original BF16 source
parameters are assigned after exact widening. The exporter calls the upstream
conditioner, `backbone()` and EOS projection, never `FlowLM.forward()` or the
usual TTS prefill wrapper, because those also execute noise/sampling.
Neither the flow network nor Mimi is executed.

Safe forward/pre-forward hooks capture the canonical first segment, using the
unchanged pipeline baseline's twenty IDs. All layers share one upstream RoPE
module, so its hook is installed once and labeled by the active layer. Captured
post-RoPE K and V are checked against the actual upstream cache append. The
complete capture repeats with bit equality and CPU RNG state remains unchanged.
Only development tooling uses the existing PyTorch/NumPy/safetensors packages.

[flowlm-prefill-baseline.json](../reference/flowlm-prefill-baseline.json) stores
identity, text, IDs, offsets, dtypes, shapes, checkpoint offsets/sizes/hashes and
EOS scalars. It contains no large float arrays. There are **92 checkpoints** in
one **10,989,704-byte** little-endian contiguous FP32 fixture under ignored
`generated/neural/flowlm-prefill-f32.bin`:

- `text.embedding`: `[20,1024]`.
- For each layer: `input`, `norm1`, `qkv`, `v_suffix`, `q_rope`, `k_suffix`,
  `attention_context`, `attention_update`, `attention_residual`, `norm2`,
  `linear1`, `gelu`, `ffn_update`, `output` (14 checkpoints). Packed QKV is
  `[20,3072]`; Q/K/V are `[20,16,64]`; linear1/GELU are `[20,4096]`; other
  layer activations are `[20,1024]`.
- `final.norm` `[20,1024]`, `final.last_hidden` `[1024]`, `final.eos` `[1]`.
- Learned-BOS `kernel.input_linear.input` `[1,32]` and `.output` `[1,1024]`,
  plus `single.last_hidden` `[1024]` and `single.eos` `[1]` for the separate probe.

The fixture SHA256 is
`8f75797b58216ffced227ae1327c2274f08eb5ff445c15482925e715b1c33b0e`.
The initial six K/V prefixes are verified against the existing `.olv` payload
and original voice bytes, rather than duplicated in the fixture. A generated
private C++ header supplies the checker/test specifications. Fixture and
individual checkpoint hashes and bounds are verified before comparison.
On regeneration, the exporter requires metadata and all hashes to equal an
existing baseline; it refuses to overwrite a differing frozen reference.
References were captured before native measurements and were not changed to
match the implementation.

## Numerical gates and observed parity

Structural parity is exact for canonical token IDs, embedding values, original
BF16 widening, shapes/layouts, prefix bytes, tensor selection, integer positions,
causal visibility and offsets. Floating computations are compared numerically.
The development checker records maximum absolute error, RMS error and maximum
relative error over nonzero reference values for **every** checkpoint and
isolated primitive. For zero reference values, relative error is undefined;
their largest absolute error is reported separately. No denominator floor hides
small reference values. Relative error is not a uniform regression gate:
one near-zero RoPE cancellation has relative error 45 despite a small absolute
error, and tanh library differences create tiny values where PyTorch rounded
GELU to zero.

First measurements, before introducing limits, found maximum checkpoint error
2.593e-5, isolated dense error 2.862e-6, final hidden error 1.669e-6 and EOS error
5.723e-6. Isolated RoPE is the largest source of discrepancy (2.587e-5), consistent
with differing FP32 exp/sin/cos library implementations; it is not a position or
prefix-rotation mismatch. End-to-end final norm/hidden errors remain around
2e-6 absolute and below 5e-7 RMS. No claim of bit equality for floating neural
computation or later autoregressive faithfulness is made.

[flowlm_tolerances.hpp](../tests/flowlm_tolerances.hpp) freezes stage-specific
maximum-absolute **and** RMS bounds at approximately twice the initially
observed maxima, rounded upward. Both bounds must pass. Dense QKV, for example,
has isolated limits 6e-6 / 4e-7; isolated LayerNorm 1e-6 / 1.1e-7; isolated
GELU 3.2e-7 / 2.2e-8; RoPE 5.5e-5 / 3e-6. End-to-end final hidden is bounded
at 3e-6 / 9e-7 and EOS at 1.2e-5 / 1.2e-5. There is no single permissive gate
for the whole network and no automatic tolerance adjustment.

Before choosing regression gates, real-model linears were measured using captured upstream
activations, independently of compounded backbone error: input projection,
all six layers' QKV/out projections and FFN linear1/linear2, and EOS. The same
isolation covers LayerNorm, GELU, Q/K RoPE and attention. This produces 62
primitive comparisons per path; backbone execution checks 90 checkpoints
(the two input-linear fixtures are used by primitive tests). Portable/AVX2
agreement is separately checked at every captured output: first maximum
absolute 3.815e-6, nonscalar RMS 3.893e-7. Fixed agreement limits are 8e-6
absolute / 8e-7 RMS, with 8e-6 RMS for scalar EOS.

Normal checker/test runs validate the isolated primitives before executing
the complete backbone, so a kernel regression fails at that boundary first.

The checker writes detailed ignored reports:
`generated/neural/portable-parity.json`, `avx2-parity.json` when available, and
`path-agreement.json`. They contain every measured max-absolute, max-relative,
RMS and zero-reference absolute error, along with informational timings/memory.
The tables below retain an observed run; future reports may differ in timings.

## Memory and informational timings

| Storage | Canonical bytes | Lifetime |
|---|---:|---|
| Complete BF16 `.olp` mapping | 199,449,856 | Resident model; no copy |
| Complete gnome `.olv` mapping | 5,505,280 | Resident voice; shared by states |
| Actually appended 20-position K/V suffix | 983,040 | Per segment |
| Checker capacity for 20 text + one probe position | 1,032,192 | Per state |
| Seven temporary activation arrays | 983,040 | Per 20-position backbone call |
| Attention score buffer (132 positions) | 528 | Per call |
| One widened weight row | 16,384 | Per compute context, reused |
| Normalized result | 81,920 | Owned by caller |
| Selected embedding rows | 81,920 | Prefill call / caller |

Reported backbone scratch is 999,952 bytes, counting activation/score arrays
plus the existing 16 KiB row buffer, and excluding state, result and embedding
ownership. A canonical `prefill()` call has about 1,163,792 requested bytes for
scratch + embeddings + result together. Container/allocator overhead and
Windows paging are not included. Hash validation touches mapping pages;
address-space size is not a working-set guarantee. No complete FP32 model
duplicate exists. The checker additionally maps the 10.99 MB fixture and keeps
development checkpoint copies for comparisons; those are not runtime memory.

Observed Release x64 samples: portable embedding about 0.04 ms, full 20-token
prefill about 152 ms, separate single-token backbone about 21 ms; AVX2/FMA about
0.02 ms / 170 ms / 23 ms. Timed calls exclude model/tokenizer loading, reference
comparisons, observer capture and output. They include scratch allocation and
normal native computation. These are individual informational samples on this
host, not benchmarks or portable performance guarantees. No thread-count,
affinity, vendor-specific or topology tuning was performed; AVX2 showed no
speed advantage in these samples.

## Build, checks and remaining work

Step 2B tokenizer and Step 2C model/voice assets must already exist. From the
repository root, using the existing local environment:

```powershell
.venv\Scripts\python.exe -B tools\neural\export_flowlm_prefill.py
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
.\build\Release\outloud-flowlm-check.exe
ctest --test-dir build -C Release --output-on-failure
git diff --check
git status
```

Neither CMake, native executables nor CTest runs Python or uses the network.
No dependency was added; executable imports are Windows/CRT/MSVC runtime and
BCrypt only. The exporter uses local files and existing packages, and changes
no site-packages files. The checker uses the exported fixture/header and local
asset paths specified by `OUTLOUD_NEURAL_DIRECTORY`, `OUTLOUD_ASSET_DIRECTORY`
and `OUTLOUD_TOKENIZER_ASSET`. These compiled development paths do not enter
the native library API. `--measure` is an explicit diagnostic mode that writes
error reports while bypassing numerical gates; it reports MEASURED, never PASS.
Normal checker/CTest runs enforce all frozen limits.

The separate fallback build was validated with:

```powershell
cmake -S . -B build-portable -G "Visual Studio 17 2022" -A x64 -DOUTLOUD_ENABLE_AVX2=OFF
cmake --build build-portable --config Release --parallel
ctest --test-dir build-portable -C Release --output-on-failure
```

Both Release builds passed **6/6 CTest tests**, including all existing text and
asset checks. New tests cover exact embedding, actual prefix/post-RoPE suffix
layout, all six canonical layers, single supplied deterministic position,
primitive reductions and tails, zero-position/offset RoPE, future-key exclusion,
current/prefix visibility, reset after a failed observer, capacity/input/state
rejection, feature dispatch, portable fallback and optional-path agreement.
Existing model/voice compatibility/integrity tests still pass. Protected
oracle/model/tokenizer/text baselines remain unchanged; new reference files
contain only this step's deterministic fixtures. Nothing is staged or committed.

**UNVERIFIED:** arbitrary prompts/voices, long or repeated backbone calls,
numerical error accumulation in future autoregressive generation, physical
testing across representative AMD/Intel or non-AVX2 CPUs, and performance across
those CPUs. The portable-only build validates omission/fallback on this host;
it does not replace physical testing on a CPU lacking AVX2. Original voice
creation-model provenance remains unverified as described in NATIVE_ASSETS.md.

Remaining work includes the separately scoped sampler/flow network and RNG,
latent feedback loop, EOS/tail stopping behavior, long-running state semantics,
Mimi decoding, PCM and eventual SAPI integration. None is implemented here.

## Observed isolated primitive errors

The following groups take the worst metric across all six layers, separately
for each path. Individual comparisons are present in the generated reports.

| Checkpoint / group | Portable abs | Portable rel | Portable RMS | AVX2 abs | AVX2 rel | AVX2 RMS |
|---|---:|---:|---:|---:|---:|---:|
| Input linear | 9.313e-10 | 9.023e-06 | 8.674e-11 | 9.313e-10 | 9.023e-06 | 8.674e-11 |
| QKV | 2.861e-06 | 8.308e-03 | 1.780e-07 | 2.861e-06 | 9.732e-03 | 1.765e-07 |
| Attention output projection | 2.980e-07 | 2.647e-01 | 2.561e-08 | 2.384e-07 | 3.897e-01 | 2.534e-08 |
| FFN linear1 | 1.907e-06 | 1.540e-01 | 1.979e-07 | 1.907e-06 | 1.638e-01 | 1.958e-07 |
| FFN linear2 | 9.537e-07 | 2.925e-02 | 8.486e-08 | 9.537e-07 | 2.925e-02 | 8.357e-08 |
| EOS projection | 1.907e-06 | 1.777e-07 | 1.907e-06 | 1.907e-06 | 1.777e-07 | 1.907e-06 |
| LayerNorm | 4.768e-07 | 6.667e-02 | 5.028e-08 | 4.768e-07 | 6.667e-02 | 5.028e-08 |
| GELU | 1.534e-07 | 3.953e-05 | 1.013e-08 | 1.534e-07 | 3.953e-05 | 1.013e-08 |
| RoPE Q/K | 2.587e-05 | 4.433e+01 | 1.374e-06 | 2.587e-05 | 4.433e+01 | 1.374e-06 |
| Causal attention | 5.290e-07 | 7.129e-03 | 4.131e-08 | 5.290e-07 | 6.830e-03 | 4.089e-08 |

## Observed errors at every backbone checkpoint

Each row reports maximum absolute error / maximum relative error for nonzero
reference values / RMS error. Embedding and original source-byte checks are
exact. These measurements preceded the frozen limits and are retained without
replacing the upstream fixtures.

| Checkpoint / group | Portable abs | Portable rel | Portable RMS | AVX2 abs | AVX2 rel | AVX2 RMS |
|---|---:|---:|---:|---:|---:|---:|
| `text.embedding` | 0 | 0 | 0 | 0 | 0 | 0 |
| `layer0.input` | 0 | 0 | 0 | 0 | 0 | 0 |
| `layer0.norm1` | 8.941e-08 | 8.606e-05 | 5.949e-09 | 8.941e-08 | 8.606e-05 | 5.949e-09 |
| `layer0.qkv` | 2.861e-06 | 1.158e-03 | 1.154e-07 | 2.861e-06 | 1.701e-03 | 1.142e-07 |
| `layer0.v_suffix` | 1.192e-07 | 7.290e-04 | 1.706e-08 | 1.192e-07 | 1.701e-03 | 1.689e-08 |
| `layer0.q_rope` | 2.593e-05 | 3.890e-03 | 9.599e-07 | 2.593e-05 | 3.864e-03 | 9.597e-07 |
| `layer0.k_suffix` | 1.454e-05 | 8.256e-04 | 6.986e-07 | 1.442e-05 | 8.211e-04 | 6.983e-07 |
| `layer0.attention_context` | 3.353e-07 | 4.080e-03 | 1.458e-08 | 3.353e-07 | 3.202e-03 | 1.439e-08 |
| `layer0.attention_update` | 8.941e-08 | 8.596e-03 | 9.276e-09 | 8.941e-08 | 7.909e-03 | 9.293e-09 |
| `layer0.attention_residual` | 8.941e-08 | 1.119e-03 | 9.310e-09 | 8.941e-08 | 1.116e-03 | 9.329e-09 |
| `layer0.norm2` | 1.267e-07 | 2.753e-03 | 1.872e-08 | 1.453e-07 | 1.333e-01 | 1.865e-08 |
| `layer0.linear1` | 2.980e-07 | 2.421e-01 | 7.212e-09 | 2.682e-07 | 2.323e-01 | 7.188e-09 |
| `layer0.gelu` | 7.451e-08 | 2.421e-01 | 3.106e-09 | 8.196e-08 | 2.323e-01 | 3.094e-09 |
| `layer0.ffn_update` | 8.196e-08 | 5.337e-03 | 7.631e-09 | 8.196e-08 | 4.638e-03 | 7.905e-09 |
| `layer0.output` | 1.043e-07 | 5.539e-03 | 1.144e-08 | 1.118e-07 | 6.369e-03 | 1.153e-08 |
| `layer1.input` | 1.043e-07 | 5.539e-03 | 1.144e-08 | 1.118e-07 | 6.369e-03 | 1.153e-08 |
| `layer1.norm1` | 4.172e-07 | 2.032e-02 | 5.478e-08 | 5.364e-07 | 1.096e-02 | 5.461e-08 |
| `layer1.qkv` | 1.907e-06 | 1.555e-02 | 1.560e-07 | 1.907e-06 | 1.555e-02 | 1.531e-07 |
| `layer1.v_suffix` | 2.943e-07 | 1.555e-02 | 4.282e-08 | 2.757e-07 | 1.555e-02 | 4.268e-08 |
| `layer1.q_rope` | 1.210e-05 | 4.592e-03 | 7.183e-07 | 1.204e-05 | 4.592e-03 | 7.174e-07 |
| `layer1.k_suffix` | 1.395e-05 | 4.500e+01 | 9.515e-07 | 1.371e-05 | 4.400e+01 | 9.504e-07 |
| `layer1.attention_context` | 2.757e-07 | 1.496e-02 | 2.073e-08 | 2.980e-07 | 8.695e-03 | 2.116e-08 |
| `layer1.attention_update` | 6.333e-08 | 1.640e+00 | 1.120e-08 | 6.519e-08 | 1.647e+00 | 1.148e-08 |
| `layer1.attention_residual` | 7.451e-08 | 4.091e-02 | 1.433e-08 | 9.686e-08 | 3.409e-02 | 1.456e-08 |
| `layer1.norm2` | 1.371e-06 | 5.882e-03 | 2.269e-07 | 1.505e-06 | 6.222e-03 | 2.322e-07 |
| `layer1.linear1` | 1.907e-06 | 3.812e-03 | 2.720e-07 | 1.907e-06 | 3.013e-03 | 2.699e-07 |
| `layer1.gelu` | 9.537e-07 | 3.812e-03 | 4.149e-08 | 1.192e-06 | 3.013e-03 | 4.155e-08 |
| `layer1.ffn_update` | 1.900e-07 | 7.151e-03 | 4.217e-08 | 2.086e-07 | 6.228e-03 | 4.238e-08 |
| `layer1.output` | 2.123e-07 | 5.157e-03 | 4.419e-08 | 2.235e-07 | 4.844e-03 | 4.441e-08 |
| `layer2.input` | 2.123e-07 | 5.157e-03 | 4.419e-08 | 2.235e-07 | 4.844e-03 | 4.441e-08 |
| `layer2.norm1` | 1.043e-06 | 4.604e-02 | 1.864e-07 | 1.170e-06 | 4.498e-02 | 1.861e-07 |
| `layer2.qkv` | 2.384e-06 | 9.972e-03 | 2.539e-07 | 2.146e-06 | 1.909e-02 | 2.474e-07 |
| `layer2.v_suffix` | 6.258e-07 | 7.833e-03 | 1.155e-07 | 5.364e-07 | 1.909e-02 | 1.141e-07 |
| `layer2.q_rope` | 1.377e-05 | 2.604e-03 | 8.794e-07 | 1.389e-05 | 2.582e-03 | 8.760e-07 |
| `layer2.k_suffix` | 2.122e-05 | 2.784e-03 | 1.410e-06 | 2.170e-05 | 2.710e-03 | 1.410e-06 |
| `layer2.attention_context` | 6.258e-07 | 4.484e-03 | 6.621e-08 | 6.706e-07 | 5.003e-03 | 6.733e-08 |
| `layer2.attention_update` | 2.384e-07 | 1.753e-01 | 3.951e-08 | 2.384e-07 | 1.483e-01 | 4.032e-08 |
| `layer2.attention_residual` | 2.980e-07 | 1.974e-02 | 5.867e-08 | 2.906e-07 | 1.524e-02 | 5.930e-08 |
| `layer2.norm2` | 2.146e-06 | 1.334e-02 | 4.043e-07 | 1.967e-06 | 1.978e-02 | 4.089e-07 |
| `layer2.linear1` | 2.384e-06 | 1.515e-02 | 3.505e-07 | 2.027e-06 | 1.300e-02 | 3.460e-07 |
| `layer2.gelu` | 1.907e-06 | 1.515e-02 | 7.714e-08 | 1.311e-06 | 1.300e-02 | 7.633e-08 |
| `layer2.ffn_update` | 3.576e-07 | 3.502e-03 | 6.173e-08 | 4.470e-07 | 6.988e-03 | 6.052e-08 |
| `layer2.output` | 3.576e-07 | 3.546e-02 | 7.865e-08 | 4.172e-07 | 4.728e-02 | 7.824e-08 |
| `layer3.input` | 3.576e-07 | 3.546e-02 | 7.865e-08 | 4.172e-07 | 4.728e-02 | 7.824e-08 |
| `layer3.norm1` | 1.609e-06 | 3.927e-02 | 2.746e-07 | 1.431e-06 | 1.009e-02 | 2.732e-07 |
| `layer3.qkv` | 3.822e-06 | 5.683e-03 | 3.160e-07 | 3.576e-06 | 1.099e-02 | 3.124e-07 |
| `layer3.v_suffix` | 7.451e-07 | 5.683e-03 | 1.547e-07 | 8.643e-07 | 1.099e-02 | 1.542e-07 |
| `layer3.q_rope` | 1.293e-05 | 2.683e-02 | 7.612e-07 | 1.305e-05 | 2.032e-02 | 7.599e-07 |
| `layer3.k_suffix` | 1.109e-05 | 6.787e-03 | 7.031e-07 | 1.109e-05 | 1.131e-02 | 7.001e-07 |
| `layer3.attention_context` | 5.066e-07 | 7.016e-03 | 6.356e-08 | 4.768e-07 | 9.010e-03 | 6.418e-08 |
| `layer3.attention_update` | 1.732e-07 | 2.167e-02 | 3.477e-08 | 1.714e-07 | 1.904e-02 | 3.513e-08 |
| `layer3.attention_residual` | 3.576e-07 | 1.937e-02 | 7.327e-08 | 4.768e-07 | 9.201e-02 | 7.330e-08 |
| `layer3.norm2` | 3.040e-06 | 4.976e-02 | 5.806e-07 | 2.801e-06 | 2.864e-02 | 5.797e-07 |
| `layer3.linear1` | 3.338e-06 | 1.383e-02 | 4.140e-07 | 2.861e-06 | 2.097e-02 | 4.141e-07 |
| `layer3.gelu` | 2.027e-06 | 1.383e-02 | 9.010e-08 | 1.907e-06 | 2.097e-02 | 9.101e-08 |
| `layer3.ffn_update` | 4.172e-07 | 3.096e-02 | 8.287e-08 | 4.768e-07 | 4.083e-02 | 8.329e-08 |
| `layer3.output` | 4.619e-07 | 4.281e-02 | 9.689e-08 | 6.407e-07 | 2.752e-02 | 9.712e-08 |
| `layer4.input` | 4.619e-07 | 4.281e-02 | 9.689e-08 | 6.407e-07 | 2.752e-02 | 9.712e-08 |
| `layer4.norm1` | 1.878e-06 | 3.119e-02 | 3.512e-07 | 2.176e-06 | 5.405e-02 | 3.511e-07 |
| `layer4.qkv` | 3.338e-06 | 2.419e-02 | 3.676e-07 | 3.338e-06 | 2.926e-02 | 3.675e-07 |
| `layer4.v_suffix` | 1.013e-06 | 1.437e-02 | 2.077e-07 | 9.052e-07 | 8.353e-03 | 2.076e-07 |
| `layer4.q_rope` | 1.812e-05 | 2.478e-02 | 1.205e-06 | 1.800e-05 | 2.426e-02 | 1.206e-06 |
| `layer4.k_suffix` | 1.669e-05 | 9.387e-03 | 1.066e-06 | 1.669e-05 | 7.422e-03 | 1.064e-06 |
| `layer4.attention_context` | 7.451e-07 | 5.664e-02 | 1.114e-07 | 7.004e-07 | 2.483e-02 | 1.109e-07 |
| `layer4.attention_update` | 4.172e-07 | 8.548e-03 | 6.193e-08 | 3.427e-07 | 8.920e-03 | 6.181e-08 |
| `layer4.attention_residual` | 7.153e-07 | 1.001e-01 | 1.050e-07 | 6.407e-07 | 8.071e-02 | 1.048e-07 |
| `layer4.norm2` | 2.205e-06 | 1.219e-02 | 5.012e-07 | 2.742e-06 | 1.565e-02 | 4.985e-07 |
| `layer4.linear1` | 2.861e-06 | 2.959e-02 | 3.679e-07 | 2.861e-06 | 1.883e-02 | 3.609e-07 |
| `layer4.gelu` | 1.788e-06 | 2.959e-02 | 1.073e-07 | 1.907e-06 | 1.883e-02 | 1.057e-07 |
| `layer4.ffn_update` | 5.588e-07 | 4.888e-02 | 1.099e-07 | 6.184e-07 | 4.117e-02 | 1.067e-07 |
| `layer4.output` | 6.855e-07 | 3.980e-02 | 1.335e-07 | 7.302e-07 | 7.463e-02 | 1.313e-07 |
| `layer5.input` | 6.855e-07 | 3.980e-02 | 1.335e-07 | 7.302e-07 | 7.463e-02 | 1.313e-07 |
| `layer5.norm1` | 1.907e-06 | 1.885e-01 | 4.041e-07 | 2.086e-06 | 1.789e-01 | 3.957e-07 |
| `layer5.qkv` | 4.541e-06 | 4.746e-02 | 4.089e-07 | 4.292e-06 | 5.237e-02 | 3.944e-07 |
| `layer5.v_suffix` | 1.103e-06 | 7.038e-03 | 2.324e-07 | 1.103e-06 | 8.411e-03 | 2.266e-07 |
| `layer5.q_rope` | 6.855e-06 | 1.899e-02 | 6.640e-07 | 6.616e-06 | 1.645e-02 | 6.471e-07 |
| `layer5.k_suffix` | 7.153e-06 | 1.386e-02 | 5.724e-07 | 7.272e-06 | 1.389e-02 | 5.641e-07 |
| `layer5.attention_context` | 9.239e-07 | 1.889e-03 | 6.966e-08 | 1.058e-06 | 7.502e-04 | 6.857e-08 |
| `layer5.attention_update` | 5.066e-07 | 1.721e-02 | 4.633e-08 | 3.576e-07 | 4.136e-02 | 4.588e-08 |
| `layer5.attention_residual` | 9.537e-07 | 2.336e-02 | 1.359e-07 | 8.345e-07 | 8.137e-03 | 1.341e-07 |
| `layer5.norm2` | 2.265e-06 | 2.837e-02 | 4.383e-07 | 2.235e-06 | 2.109e-02 | 4.312e-07 |
| `layer5.linear1` | 2.801e-06 | 8.635e-03 | 3.576e-07 | 2.593e-06 | 1.347e-02 | 3.506e-07 |
| `layer5.gelu` | 2.176e-06 | 8.635e-03 | 1.019e-07 | 2.146e-06 | 1.347e-02 | 1.020e-07 |
| `layer5.ffn_update` | 1.431e-06 | 5.891e-03 | 1.412e-07 | 1.311e-06 | 4.096e-03 | 1.404e-07 |
| `layer5.output` | 1.431e-06 | 2.137e-01 | 1.829e-07 | 1.669e-06 | 1.752e-01 | 1.817e-07 |
| `final.norm` | 1.997e-06 | 2.665e-02 | 4.157e-07 | 1.788e-06 | 2.313e-02 | 4.103e-07 |
| `final.last_hidden` | 1.371e-06 | 3.450e-03 | 4.142e-07 | 1.311e-06 | 8.023e-04 | 4.039e-07 |
| `final.eos` | 5.722e-06 | 5.331e-07 | 5.722e-06 | 1.907e-06 | 1.777e-07 | 1.907e-06 |
| `single.last_hidden` | 1.669e-06 | 5.349e-04 | 4.753e-07 | 1.550e-06 | 8.626e-04 | 4.905e-07 |
| `single.eos` | 0 | 0 | 0 | 0 | 0 | 0 |
