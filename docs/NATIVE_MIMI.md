# Native Pocket Mimi decoder (Step 2G)

> Current runtime composition: [ARCHITECTURE.md](ARCHITECTURE.md). Build/checker instructions below are historical; use [A11/A12](BUILD_AND_VALIDATION_IMPROVEMENTS.md).

This document records the original Step 2G implementation and measurements.
[A08](PERFORMANCE_OPTIMIZATION_A08.md) adds a decoder-owned **7.5 MiB immutable
FP32 phase-weight cache** for the three dense transposed convolutions. Those
weights are now widened once during construction; the original mapped BF16
weights, arithmetic, mutable scratch and reset semantics remain unchanged.
The memory and timing tables below describe Step 2G, before that addition.

`outloud_mimi` converts one normalized Pocket English latent `[32]` into
**1920 unclamped float32 mono samples at 24000 Hz**, immediately after each
call. The complete canonical Segment 1 is **46 calls / 88320 samples /
3.680 seconds**. This is a sequential, single-threaded decoder with explicit
per-segment state. It uses the existing resident BF16 `.olp`; there is no
FP32 decoder weight copy, new model format, external compute backend, runtime
Python, network, encoder, voice cloning, production RNG, queue, worker or SAPI.

## Verified source and operation order

The unchanged installed Pocket TTS 3.3.0 English configuration and executed
dependencies were inspected: `models/mimi.py`, `models/tts_model.py` at
`_decode_audio_worker`, `modules/dummy_quantizer.py`, `resample.py`, `conv.py`,
`transformer.py`, `attention.py`, `rope.py`, `layer_scale.py`, `seanet.py` and
`stateful_module.py`. The previous pipeline/text/asset/FlowLM/sampler/generation
documents and frozen JSON mappings remain unchanged.

1. TTS's decoder worker computes `normalized_latents * emb_std + emb_mean`,
   in that order, before projection. Mean/std are the **loaded BF16-rounded**
   FlowLM buffers, IDs 2/3, widened exactly to FP32. Native multiplication and
   addition remain separate FP32 operations. There is no latent clamp.
   Captured pre/post checkpoints agree bit-for-bit on both CPU paths.
2. `MimiModel.decode_from_latent` transposes `[1,1,32]` to `[1,32,1]` and
   calls `DummyQuantizer.output_proj`, a bias-free 1x1 Conv1d `[512,32,1]`
   (ID 167). It is just a 32-to-512 projection; **no codebook lookup** occurs.
   Native execution uses the existing BF16 linear boundary with an equivalent
   temporary two-dimensional weight view. Real-input isolated fixtures cover it.
3. Depthwise streaming ConvTranspose1d (ID 168) has 512 groups, kernel 32,
   stride 16, dilation 1, zero padding, no bias. It yields `[512,16]`, expanding
   12.5 Hz latents to 200 Hz transformer positions. Its output overlap persists.
4. ProjectedTransformer's input projection is absent and its output projection
   is Identity. Transpose to token-major `[16,512]`, execute two layers below,
   then transpose back to channel-major `[512,16]`.
5. SEANet decodes with initial causal convolution, three ELU/transpose/residual
   groups at ratios 6/5/4, then ELU and final causal convolution. Output is
   float32 PCM, with **no final tanh, clamp, resampling or overlap flush**.

All 44 decoder tensors (IDs 125..168) occupy **20,610,626 BF16 bytes already
inside `.olp`**. IDs 2/3 add 128 normalization bytes already in that mapping.
Each convolution/linear widens only a weight row or phase row at its compute
boundary. Activations, reductions and accumulation stay FP32; only development
error/correlation statistics use double precision.

## Transformer and context

Mimi has two pre-norm layers, width 512, eight 64-wide heads, FFN width 2048,
LayerNorm epsilon 1e-5, tanh-approximate GELU, bias-free packed QKV/output/FFN
linears and two learned per-channel layer scales per layer. The configured
initial layer scale 0.01 is **not substituted for the stored learned weights**.
Each layer executes norm1 -> QKV -> adjacent-coordinate RoPE on Q/K -> KV
append -> attention -> output projection -> layer scale -> residual, then
norm2 -> linear1 -> GELU -> linear2 -> layer scale -> residual.

RoPE uses absolute positions, period 10000 and the same float32 frequency
construction as FlowLM. K is stored post-RoPE, V unrotated. Each layer owns
separate linear `[capacity,8,64]` K/V arrays. They retain old positions:
upstream does **not** use a ring or physically truncate the cache.

For query position q, a key k is visible exactly when `0 <= q-k < 250`.
Native attention visits `[max(0,q-249),q]`, including the current key and
excluding future keys in the current 16-position block. The finite softmax
window is mathematically equivalent to upstream's mask of the full linear
cache. Tests poison excluded old positions with NaNs, change future values,
check both sides of the context boundary and compare real upstream attention
inputs/outputs at frames 0/1/2/23/45. Both offsets advance once by 16 after
successful decode: 0 -> 16 -> ... -> **736** after 46 frames. Pad is always zero.

## SEANet and convolution carry

All executed Conv1d operations have stride/dilation/groups 1, constant zero
initial history and biases. PyTorch Conv1d cross-correlates `[Cout,Cin,K]`
weights with saved left history followed by current input; kernels are not
reversed. History is the last `K-1` input values per channel. The source's
`first` flag is unused for constant padding, so native state needs no flag.
Kernel-1 operations have empty carry. Native windows are assembled once into
reusable FP32 scratch; widened rows use the existing dispatched dot product.

ConvTranspose weights are `[Cin,Cout/groups,K]`; all executed kernels are
`2*stride`, dilation 1, padding/output-padding zero. Each call adds the previous
output overlap to the raw output head, saves the tail **minus bias**, and
removes that tail from the emitted output. Native phase rows preserve this
orientation and explicit bias addition/subtraction. Depthwise upsample uses
the same overlap semantics without bias. No final tail is flushed.

| Operation | Cin -> Cout | Kernel / stride | Persistent FP32 values |
|---|---:|---:|---:|
| Depthwise upsample | 512 -> 512 | 32 / 16 | 8192 output overlap |
| Initial decoder conv | 512 -> 512 | 7 / 1 | 3072 input history |
| Group 1 transpose | 512 -> 256 | 12 / 6 | 1536 output overlap |
| Group 1 residual convs | 256 -> 128 -> 256 | 3,1 / 1 | 512,0 input history |
| Group 2 transpose | 256 -> 128 | 10 / 5 | 640 output overlap |
| Group 2 residual convs | 128 -> 64 -> 128 | 3,1 / 1 | 256,0 input history |
| Group 3 transpose | 128 -> 64 | 8 / 4 | 256 output overlap |
| Group 3 residual convs | 64 -> 32 -> 64 | 3,1 / 1 | 128,0 input history |
| Final conv | 64 -> 1 | 3 / 1 | 128 input history |

Each upsampling group is ELU -> ConvTranspose -> residual block. A residual
block preserves its original input, then executes ELU -> kernel-3 compressed
conv -> ELU -> kernel-1 expansion -> original-input addition. Channels/time
progress as `[512,16] -> [256,96] -> [128,480] -> [64,1920] -> [1,1920]`.

**Before full decoder integration**, real upstream input/output and before/
after carry fixtures for all 12 streaming configurations were measured on
both paths. The largest isolated convolution absolute error was 2.09808e-5;
the largest isolated RMS was 1.43748e-6. Isolated upsample output/carry is
bit-exact. Measurements for output and carry of each configuration are in
the ignored parity reports, separately from accumulated decoder error.

| Isolated configuration | Worst absolute | Worst RMS | Meaningful relative |
|---|---:|---:|---:|
| Initial conv | 9.537e-6 | 1.045e-6 | 2.797e-2 |
| Group 1 transpose | 7.629e-6 | 5.193e-7 | 1.244e-2 |
| Group 1 kernel 3 / 1 | 1.526e-5 / 7.629e-6 | 1.438e-6 / 5.737e-7 | 1.412e-2 / 2.828e-2 |
| Group 2 transpose | 4.768e-6 | 2.511e-7 | 4.035e-2 |
| Group 2 kernel 3 / 1 | 3.815e-6 / 1.669e-6 | 2.798e-7 / 1.279e-7 | 1.395e-2 / 1.894e-2 |
| Group 3 transpose | 7.629e-6 | 1.895e-7 | 1.875e-2 |
| Group 3 kernel 3 / 1 | 2.098e-5 / 9.537e-7 | 4.725e-7 / 5.736e-8 | 3.517e-2 / 1.511e-2 |
| Final conv | 1.342e-7 | 1.807e-8 | 2.140e-3 |

Each metric independently takes its maximum across five captured frames,
output/carry comparisons and both CPU paths. The isolated projection has
9.537e-7 absolute / 1.281e-7 RMS / 3.220e-4 meaningful-relative error.

## State lifecycle and API

The installed TTS worker initializes **fresh Mimi state per text segment**.
It does not carry state across segments of an utterance. Native callers use
a fresh state or `reset()` at that same boundary. Reset zeroes all convolution
carry, restores offset zero and hides old KV values; subsequent cache writes
overwrite retained storage. No copying of weights or complete cache clearing
is required. Capacity is an allocation limit, not an attention window.

```cpp
outloud::MimiDecoder decoder(model); // portable default; shared read-only .olp
outloud::MimiDecoderState state(109); // 1744 positions, canonical source capacity
auto pcm = decoder.decode_frame(normalized_latent_32, state);
// pcm.size() == 1920; state.offset() == 16; PCM valid until next decode call
state.reset(); // begin another segment with clean histories and positions
```

Maximum frames must be 1..4096. Bad latent size/nonfinite values and exhausted
capacity fail before mutation. Started computation/observer failures invalidate
state and require reset. Reentry into the same decoder or active state, including
observer-triggered reset, is rejected. These are single-caller compute contexts.
An optional observer receives ephemeral views for development capture only.

The reusable future streaming boundary is already established:
FlowLM candidate -> normalized `[32]` -> Mimi -> 1920 float samples -> 80 ms.
There is no call coalescing or delay waiting for another latent. Future SAPI
adapters can consume that PCM chunk; queues/threads/COM are outside this step.

## Fixtures, Case A and Case B

[export_mimi_decoder.py](../tools/neural/export_mimi_decoder.py) forces
`HF_HUB_OFFLINE=1`, checks the pinned source model SHA, reads the frozen Step 2F
blob and validates its full/latent hashes. It constructs installed modules on
the meta device, removes unused encoder/encoder-transformer/downsample modules
before assigning CPU weights, and loads only the 44 decoder tensors plus mean/
std. No encoder, FlowLM generation, queues or threads execute in this exporter.
All reference checkpoints repeat bit-for-bit; CPU RNG state remains unchanged.
Regeneration refuses changed frozen metadata/fixture identities.

The ignored **44,923,520-byte** `generated/neural/mimi-decoder-f32.bin` contains
**472 checkpoints**. Selected frames 0/1/2/23/45 have normalized/denormalized
inputs, projection, upsample input/output/carry, every transformer layer norm,
packed QKV, Q/K RoPE, V, attention context/projection/scale/residual, FFN linear/
GELU/projection/scale/residual, used K/V caches, transformer output, every SEANet
conv's real input/output/before/after carry, residual block outputs and PCM.
It also stores all `[46,32]` normalized latents and complete `[88320]` float PCM.
Empty kernel-1 carry checkpoints are retained with exact empty shapes/hashes.

[mimi-decoder-baseline.json](../reference/mimi-decoder-baseline.json) contains
only stable identities, architecture/configuration, names, shapes, dtypes,
offsets and hashes. Native checkers validate whole-file and per-checkpoint
hashes, contiguous bounds and EOF using an ignored generated C++ header.
Canonical latent SHA is
`e8b5f2f49b259976d9ed1c608cd529c39490a0fd878ffbbd098b32f84be5a8c7`;
Segment 1 float32 PCM SHA is
`913b0a8be85de755367766543a3b2f24d0bc9cec8351036e3c63413de08efdef`.

An additional offline **upstream-only** diagnostic, reproducible with
[verify_mimi_one_frame.py](../tools/neural/verify_mimi_one_frame.py), uses the
installed full TTS stream with instance option `max_decoder_frames_per_call=1`.
It reproduces the protected **170 chunks / 326400 samples** whole-utterance
hash `e4e1004ffda5a83b205a6d131d34209a88596012b249657702657c924ec529b9`.
Its first 88320 float32 samples match this decoder fixture **bit-for-bit**.
This validation adds no native multi-segment orchestration and changes no
tracked oracle or site-packages source. Default upstream queue coalescing still
has the reproducibility hazard described in POCKET_PIPELINE.md.

**Case A** always decodes the exact frozen 46 upstream normalized latents.
It alone controls decoder parity. **Case B** decodes the Step 2F native
free-running normalized latents with the same supplied 48-row noise stream.
Its waveform includes FlowLM/sampler feedback drift and is informational.
No Case B error contributes to a decoder tolerance.

| PCM comparison | Portable max abs / RMS | AVX2/FMA max abs / RMS |
|---|---:|---:|
| Case A: decoder isolation | 1.400709e-6 / 6.931622e-8 | 1.475215e-6 / 6.827520e-8 |
| Case B: free-running vs upstream | 4.296124e-3 / 1.618039e-4 | 4.007578e-3 / 1.506594e-4 |

Case A mean-centered correlation is effectively 1 (difference below 1e-12),
peak-amplitude difference 8.940697e-8 on both paths. Case B correlations are
0.999995837 / 0.999996391 and peak differences 7.748604e-7 / 4.470348e-7.
These are numerical observations, not perceptual audio-quality conclusions.

After measurement, [mimi_tolerances.hpp](../tests/mimi_tolerances.hpp) freezes
**114 stage-specific absolute/RMS/meaningful-relative gates**, twice the
observed Case A, isolated primitive and path-agreement maxima rounded upward
to two significant digits. Measured zeros remain zero. All three gates must
pass; correlation and peak difference are informational. Complete PCM limits
are **3.0e-6 absolute / 1.4e-7 RMS / 7.3e-2 meaningful relative**. Individual
frame PCM RMS limit is 4.6e-7, reflecting the largest per-frame RMS rather than
the whole-waveform average. Higher absolute RoPE discrepancies at late positions
(about 1.97e-4 internally) reflect differing FP32 transcendental routines;
final transformer absolute error remains 4.53e-6. Denormalization stays exact.
Meaningful relative includes `abs(reference) >= 1e-6*max(1,max(abs(reference)))`;
unrestricted nonzero-relative and zero-reference absolute errors are also reported.

Normal `outloud-mimi-check` checks primitives before integration and emits PASS
only after numerical gates. `--measure` bypasses numerical gates, retains all
structural/hash checks and prints MEASURED. Ignored `mimi-portable-parity.json`,
`mimi-avx2-parity.json` and `mimi-path-agreement.json` preserve every metric.
Each path has 607 comparisons, including every frame's PCM/count/offset,
selected-frame checkpoints, isolated projection/convolutions/attention and
complete waveform. Path agreement separately checks captured native outputs.

## Listening artifacts and PCM boundary

The checker creates ignored 24 kHz mono PCM16 listening copies under
`generated/audio/`: `upstream-segment1.wav`, `native-reference-latents.wav`
and `native-free-running.wav`. The two native copies use the portable path.
A tiny development-only writer clamps solely at integer conversion, scales
by 32767 and explicitly rounds nearest with ties to even. No audio library
was added. The float32 fixture remains the regression oracle; PCM16 bytes
do not define neural parity. Listening/perceptual evaluation is **UNVERIFIED**.

## Memory, allocation and performance

| Native storage | Bytes | Lifetime |
|---|---:|---|
| Decoder weights | 20,610,626 BF16 | Already in shared resident .olp |
| Transformer K/V, capacity 1744 | 14,286,848 | Reused state allocation |
| Logical K/V after 46 frames (736 positions) | 6,029,312 | Used part of that capacity |
| Upsample output overlap | 32,768 | Persistent state |
| Remaining SEANet carry | 26,112 | Persistent state |
| Total convolution carry | 58,880 | Persistent state |
| Decoder scratch/preparation | 3,459,200 | Reused decoder context |
| PCM output frame | 7,680 | Reused decoder context |

Scratch includes three SEANet activation/residual arrays (1,474,560 bytes),
transformer arrays (458,752), convolution windows (1,474,560), three bounded
row buffers (49,152), and latent/projection arrays (2176). Vectors allocate
at construction. Object headers, call stacks and allocator overhead are extra.
The state can instead reserve 46 frames if that bound is known; canonical
tests use upstream capacity 109 frames. The C++ new/new[] allocation audit
observes **zero requests across all 46 unobserved decoder calls**, both paths.
This does not measure internal CRT/OS allocations. Development observers,
fixture mappings/checkpoint copies and complete waveform vectors are additional.

One warm Release x64 measurement, MSVC 19.44, resident model/voice, no observer,
comparison, output or PCM copies in the timed decoder region:

| Path | First one-frame decode ms | 46-frame decoder ms | Mean ms/frame | FlowLM/sampler ms | Combined compute RTF | First PCM estimate ms |
|---|---:|---:|---:|---:|---:|---:|
| Portable | 32.780 | 1442.385 | 31.356 | 1271.390 | 1.356 | 207.537 |
| AVX2/FMA | 34.445 | 1492.728 | 32.451 | 1533.342 | 1.216 | 241.139 |

Combined compute RTF follows the requested convention:
`3.680 seconds / (prefill + all 47 FlowLM/sampler candidates + Mimi seconds)`.
Generation time is measured with existing resident Step 2F code and supplied
noise. First PCM estimate adds measured prefill (including discarded sampling),
one separately measured BOS candidate and first Mimi decode. Portable pieces
are 151.056 + 23.701 + 32.780 ms; AVX2/FMA pieces are
170.384 + 36.310 + 34.445 ms. These sequential
pieces estimate native computation, **not final application wall latency**.
Loading/hashing, state/context initialization, text setup, WAV writing, SAPI,
audio playback and future production RNG are excluded. No worker overlap,
CPU tuning, affinity or threading is introduced. Timings vary between runs;
AVX2/FMA is not assumed to be faster.

Portable remains the default baseline x64 path. Optional dot products use the
existing isolated AVX2/FMA library and CPUID/OSXSAVE/XGETBV capability checks.
No vendor, topology, cache, core-count or BF16 instruction assumptions are added.
Both paths compute single-threaded with FP32 activations and BF16-backed rows.

## Build, tests and remaining work

Prior locally exported assets/fixtures are prerequisites. CMake never invokes
Python or downloads anything; missing fixtures fail configuration. From the
repository root:

```powershell
.venv/Scripts/python.exe -B tools/neural/export_mimi_decoder.py
# Optional upstream-only protected waveform verification:
.venv/Scripts/python.exe -B tools/neural/verify_mimi_one_frame.py
cmake -S . -B build -A x64
cmake --build build --config Release
.\build\Release\outloud-mimi-check.exe
ctest --test-dir build -C Release --output-on-failure
cmake -S . -B build-portable -A x64 -DOUTLOUD_ENABLE_AVX2=OFF
cmake --build build-portable --config Release
ctest --test-dir build-portable -C Release --output-on-failure
git diff --check
git status
```

CTest adds `canonical-mimi-decoder` and `mimi-regressions`. The previous ten
tests remain intact. Coverage includes source configuration identity, exact
denormalization, projection, all convolution configurations and nonzero carry,
attention/context/future masking, transformer/SEANet checkpoints, output length,
all 46 reference frames, numerical rejection, path agreement, consecutive state,
reset bit equality, failed observer recovery, reentrancy, capacity, malformed/
nonfinite inputs and heap reuse. Release and AVX2-disabled builds pass all
**12/12 tests**. Native imports remain Windows/MSVC/CRT/BCrypt only. Installed
Pocket source/configuration integrity is checked against wheel RECORD hashes.

**UNVERIFIED:** other texts/voices/latents, substantially longer decoder streams,
broader compilers/libm or physical AMD/Intel/non-AVX2 CPUs, perceptual impact of
native latent drift, end-to-end application latency and production RNG parity.
The disabled build verifies build/fallback on this host, not physical testing
on a CPU without AVX2. Production RNG, native multi-segment orchestration,
worker queues/threads, SAPI/COM, registration, registry and WoW integration
remain outside Step 2G. No encoder/cloning path was ported.

## Files

New: `engine/include/outloud/mimi.hpp`, `engine/src/mimi.cpp`,
`engine/include/outloud/cpu/mimi_kernels.hpp`, `engine/src/cpu/mimi_kernels.cpp`,
`tools/neural/export_mimi_decoder.py`, `tools/neural/verify_mimi_one_frame.py`,
`tools/mimi-check/main.cpp`, `tests/mimi_fixture_checks.hpp`,
`tests/mimi_tolerances.hpp`, `tests/mimi_test.cpp`,
`reference/mimi-decoder-baseline.json`, this document.
Modified: `CMakeLists.txt` for the library/checker/regression targets and tests.
Generated blobs, headers, reports and WAVs remain ignored. Prior baselines,
schema and site-packages remain unchanged; nothing is staged, committed or pushed.
