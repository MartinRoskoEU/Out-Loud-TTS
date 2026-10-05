# Step 2K: native production noise

**EXACT PYTORCH RNG PARITY ACHIEVED** for the pinned PyTorch 2.14.1+cpu
AVX2 contiguous float32 `[1,32]` Pocket noise path. The native implementation
emulates that path with scalar integer/FP32 operations, including its numerical
approximations, on both native portable and optional AVX2/FMA neural paths.
It generates noise on demand without Python, PyTorch, captured noise files,
new external libraries or network access. Fixture noise remains available.

Step 2J was committed as `77c2420`; the tree was clean before this work.
Worker topology, queue/lifecycle, neural kernels, model math/formats, existing
fixtures and tolerances are unchanged. No orchestration or SAPI is added.

## VERIFIED upstream behavior and source

The actual installed environment is Pocket TTS **3.3.0**, PyTorch
**2.14.1+cpu**, Git source identity
`5c4886908584029761b579af026dcfb627c84070`. Its configuration and
`torch.backends.cpu.get_cpu_capability()` report **AVX2**. Inspection used
these local files; nothing in site-packages was modified or downloaded:

- `.venv/Lib/site-packages/pocket_tts/models/flow_lm.py:FlowLM.forward`
- `.venv/Lib/site-packages/torch/random.py:manual_seed/_manual_seed_impl`
- `.venv/Lib/site-packages/torch/nn/init.py:normal_/_no_grad_normal_`
- `torch/include/ATen/CPUGeneratorImpl.h:CPUGeneratorImpl`
- `torch/include/ATen/core/MT19937RNGEngine.h:mt19937_engine`
- `torch/include/ATen/core/DistributionsHelper.h:uniform_real_distribution/normal_distribution`
- `torch/include/ATen/core/TransformationHelper.h:uniform_real`
- `torch/include/ATen/native/cpu/DistributionTemplates.h:normal_kernel/normal_fill/NormalFill16`
- `torch/include/ATen/native/cpu/avx_mathfun.h:log256_ps/sincos256_ps`

The `torch/include` paths above are under `.venv/Lib/site-packages/`.
Source-file SHA-256 identities and build/version information accompany the
ignored reference metadata. The wheel ships CPU generator declarations and
engine/distribution implementations in headers; its CPUGeneratorImpl.cpp is
not present. Seed/state behavior was therefore also verified against the
installed binary through Generator/manual_seed/get_state calls. No claim is
made that a debugger traced the internal dispatch stub.

Pocket allocates contiguous noise with shape `[1,32]`, float32 on CPU, then
calls `torch.nn.init.normal_(mean=0.0, std=temp**0.5)`, with temperature 0.3
and no clamp. `nn.init.normal_` delegates directly to `Tensor.normal_`.
This is **not** `randn()` followed by a separate scaling operation.

The engine is MT19937: 624 unsigned 32-bit words, period parameters N=624/M=397,
standard 2002 seed recurrence and tempering, with PyTorch's left/next progression.
The full unsigned 64-bit seed is retained as metadata, but initialization of
the MT words uses its low 32 bits. Seed 73 and `73+2^32` therefore have the same
random sequence, while retaining different initial_seed values. This aliasing
matches the installed generator; it is not an independently chosen policy.
Installed negative seed inputs were also checked: -1, -2 and -2^63 map modulo
2^64 to unsigned seeds. The native public seed API takes `uint64_t`; the CLI
accepts unsigned decimal values and implements no signed-seed convenience API.

Reseeding recreates all MT words, resets left=1/next=0 and clears draw/output
state. The installed manual_seed also resets cached scalar-normal state:
this was checked after a one-element normal draw. Cached float/double normal
samples are never needed by the fixed 32-element contiguous path.

For this shape, `normal_kernel` selects `normal_fill`, generating all **32
uniforms first**, then processing two blocks of 16. Each float uniform consumes
one MT 32-bit result: `(word & 0xFFFFFF) * 2^-24`, exactly representable FP32
in `[0,1)`. No random64 calls, rejected draws, spare normal cache or extra tail
block are used. Each block pairs positions j and j+8 for j=0..7:

```
u1 = FP32(1 - uniform[j])
radius = sqrt(FP32(-2 * log256_lane(u1)))
theta = FP32(FP32(2*pi<double>) * uniform[j+8])
normal[j]   = FP32(radius * cos256_lane(theta))
normal[j+8] = FP32(radius * sin256_lane(theta))
output = fmadd(normal, FP32(sqrt(double(0.3))), +0)
```

The SIMD log/sincos are the installed avx_mathfun/Cephes polynomial
approximations with explicit FP32 operation order and quadrant/sign handling.
Using platform log/sin/cos or `std::normal_distribution` would not reproduce
this numerical path. Inputs to the logarithm lie in `[2^-24,1]`; angles lie
between zero and the FP32 two-pi constant. Native code implements only this
required domain, not a general transcendental math library.

The final standard deviation is exactly FP32 bits **0x3F0C378C**; the two-pi
constant is **0x40C90FDB**. Because mean is +0 and normal/deviation are finite,
`fmadd(n,std,+0)` is reproduced by a rounded multiplication followed by adding
+0, including normalization of a negative-zero product. All prior polynomial
operations remain separate, not contracted into FMA. No scaling is added to
FlowSampler; its supplied-noise contract still receives already scaled values.

Shape/dtype matter: fewer than 16 elements or noncontiguous tensors use the
scalar cached-double-normal path. Nonmultiples of 16 can redraw the last block;
float64/half/BF16 have other paths. None is implemented by this fixed Pocket
source. Full-utterance draw accounting remains the previously verified 176
draws, but this step submits only independent one-segment requests.

## Implementation decision, ownership and API

[ProductionNoiseSource](../engine/include/outloud/production_noise.hpp) derives
from the existing NoiseSource. Its explicit unsigned seed, `reseed(seed)`,
`next()` and `consumed()` support standalone or existing sequential generation.
`next()` returns a non-owning 32-float row valid until next/reseed. It allocates
nothing, uses no global mutable state, and pre-generates no complete stream.
An idle read-only engine view permits exact progression checks.

The small scalar source adapts the inspected MT and polynomial arithmetic,
compiled in `outloud_noise` with **/fp:strict**, without /arch:AVX2, intrinsics,
vendor logic, std distributions or PyTorch includes. Original notices are
retained in [RNG_NOTICES.md](../RNG_NOTICES.md), with the adaptation marked.
Only the existing neural kernels retain their optional AVX2/FMA dispatch.

`WorkerPipeline::prepare(tokens, uint64_t seed, destination, ...)` selects its
owned production source. The producer reseeds it in the existing untimed reset
job, before compute dispatch. The producer alone mutates the RNG during
synthesis; the consumer has no RNG dependency. Both seed and supplied
`NoiseSource&` overloads enter the same prefill/candidate/sampler path. No
generation loop or queue logic is duplicated or changed. Explicit seed 73
consumes draw 0 for discarded prefill sampling, draws 1..47 for candidates,
including the final computed/discarded candidate.

The caller chooses an explicit seed per prepared segment request. Same seed,
model, voice, tokens and native neural path reproduce output on the verified
build. No automatic seed selection or multi-segment seed advancement policy
is invented here. Existing external-source mode remains unchanged.

## Exact parity and canonical validation

The offline [reference exporter](../tools/neural/export_rng_reference.py)
captures **192 seed-73 rows**, **48 seed-74 rows**, and two rows each for seed
0, `73+2^32` and UINT64_MAX: **246 draws / 7872 FP32 values**. Every full
5056-byte PyTorch state is retained development-only; compact snapshots contain
seed/left/next/all 624 MT words. Capture is repeated exactly. Default manual_seed
and an independent CPU Generator agree, and the first 48 seed-73 rows match
the **existing frozen generation noise stream bit-for-bit**. No frozen fixture
is replaced.

`generated/rng/rng-reference.bin` is **662000 bytes**, SHA-256
`8dd4f21adbbc1f80260eee091eda129435c49fe45997bf31dcdd6994deb9872b`.
Metadata, generated checker constants, raw serialized states and checker
reports remain ignored under `generated/rng/`. The native checker validates
identity, every row, every compact state before/after draws, finite values,
counter progression, all 48 canonical reseeded rows and different-seed output.
It audits zero C++ new/new[] calls throughout draw loops. This is lightweight
deterministic validation, not a statistical battery or general RNG campaign.

The worker production checker compares two independent seed-73 requests on
reused resident workers against clean native fixture-backed generation/Mimi.
It checks exact prefill and all candidate input/hidden/noise/velocity/latent/EOS
bits; the complete latch/emission timeline; every PCM frame; and all final
FlowLM/Mimi K/V caches and convolution carry. Canonical counts remain:
**48 draws, 47 candidates, EOS 43, 46 latents/decoder calls, 88320 samples,
FlowLM offset 179, Mimi offset 736**, 3.680 s at 24000 Hz. There is no looser
production tolerance. Native production versus native fixture output is exact;
existing native-versus-upstream neural numerical differences remain unchanged.

`outloud-rng-synth` prepares/tokenizes the fixed canonical text with the existing
native tokenizer, loads only `.olp`/`.olv`/tokenizer assets, and calls the seed
overload. Its runtime path reads no captured noise/neural/RNG fixture. The
development listening WAV is written after synthesis, never inside timing.
The production checker and fixture-free executable produced identical native
AVX2 PCM SHA-256
`558dd80f4507e5ee4294955db6bdf4f49eb10fac5bde5cab7cdc22181ee7e430`.
The listening artifact is `generated/audio/native-production-rng-segment1-avx2.wav`.
This is numerical agreement, not a new human listening evaluation.

## Performance and memory

**VERIFIED, final Release measurements:** the standalone 48-draw RNG diagnostic
took **19.900 microseconds**, **0.414583 microseconds per [32] draw**, in the
AVX2-enabled build. The disabled build measured 15.700 microseconds / 0.327083
microseconds per draw. These are single small diagnostics, not performance
distributions or evidence of a SIMD speed difference; both use the same scalar
RNG. The enabled measurement is about 0.00244% of median worker request time.

The fixture-free production worker benchmark used preloaded/validated assets,
one warm-up and **three timed requests**, with explicit seed/state reset before
each request. Scheduling, samples, finite output and allocation results were
checked outside timing. No observer, sink, profiler, diagnostic trace, WAV or
JSON writing ran inside the timed region. The existing development C++
allocation audit remained enabled. The warm-up contributes no reported sample.

| Metric | Minimum | Median (primary) | Maximum |
| --- | ---: | ---: | ---: |
| Request total, ms | 814.7583 | **815.2997** | 892.6664 |
| First PCM, ms | 84.2239 | **85.2574** | 92.5626 |
| RTF, audio duration / wall duration | 4.1225x | **4.5137x** | 4.5167x |

The three total times were 814.7583, 815.2997 and 892.6664 ms. The third run
was slower, with a total range of 9.55% of the median; the first two agree
within 0.07%. This is adequate for this light median comparison, so no extra
runs were added. No mean or upstream Python benchmark was run.

The stored Step 2J worker median is **1118.040 ms / RTF 3.2915x**. This session's
production median is **302.7403 ms lower (27.0778%)**, with RTF 4.5137x.
This is a historical comparison, not an isolated estimate of RNG's effect:
Step 2J kept a sequential reference context resident alongside its worker
benchmark, whereas this executable retains only the production worker context.
Host/session scheduling also differs. A roughly 20-microsecond RNG cost cannot
explain a 303-millisecond wall-time difference. No neural optimization was
introduced or inferred from this comparison.

Raw reports are ignored under `generated/perf/2k-production-benchmark-avx2.json`
and `generated/perf/2k-summary.json`; RNG reports are under `generated/rng/`.

State size is **2656 bytes** including the NoiseSource vptr,
624 MT words, 64-bit seed/draw count, left/next indices and one 128-byte row.
The worker adds that source plus an 8-byte request seed: **2664 bytes** of
additional inline persistent storage, **94392 -> 97056 bytes**. There is no
heap backing, normal cache or large lookup table. A few scalar temporaries
use the stack. Consumer/queue/weight memory is unchanged.

Producer hot-loop auditing still starts after prefill, as previously verified;
the standalone RNG checker separately audits every draw, including draw 0.
Setup/reseed occurs before timing. Existing prefill allocations remain outside
the neural hot-loop audit, but within request compute timing. Internal CRT/OS
allocations are outside this C++ new/new[] audit.

## VERIFIED final validation and reproduction

- Initial Release x64 sanity: **12/12 existing CTests**, 27.34 s, once.
- Final Release x64 build: PASS; **12/12 existing CTests**, 27.12 s, once.
- Final `OUTLOUD_ENABLE_AVX2=OFF` Release build: PASS; **12/12 existing
  CTests**, 20.34 s, once.
- Both native RNG checkers: **246/246 draws bit-exact**, every MT state exact,
  canonical reseed exact, alternate seed differs, zero draw allocations.
- Both production worker checkers: canonical scheduling/PCM/all cache and carry
  bits exact against their corresponding native fixture path; A/B requests on
  reused workers reseed exactly; zero hot-loop allocations.
- Existing fixture-backed AVX2 worker checker: PASS, including both injected
  producer/consumer failures, reuse and idle/completed shutdown.
- Fixture-free AVX2 executable and benchmark: same exact PCM hash as the
  production worker checker. The listening WAV was verified as mono PCM16,
  24000 Hz, **88320 samples / 3.680 s**.
- All ten inspected installed source-file hashes still match the captured
  metadata. Protected tracked neural kernels, baselines, fixtures, tolerances
  and existing CTest definitions are unchanged. No performance CTests were added.
- `git diff --check` and whitespace/newline checks on all new files: PASS.
  No dependencies, runtime Python, network calls or site-packages edits added.
  Nothing staged, committed or pushed; HEAD remains `77c2420`.

Development reproduction (the offline export requires the existing pinned
environment and frozen assets; it is not part of native runtime):

```powershell
.\.venv\Scripts\python.exe -B tools/neural/export_rng_reference.py
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\outloud-rng-check.exe
.\build\Release\outloud-worker-check.exe rng-check avx2
.\build\Release\outloud-rng-synth.exe avx2 --seed 73 --write-audio
.\build\Release\outloud-rng-synth.exe avx2 --seed 73 --benchmark 3
cmake -S . -B build-portable -DOUTLOUD_ENABLE_AVX2=OFF
cmake --build build-portable --config Release
ctest --test-dir build-portable -C Release --output-on-failure
.\build-portable\Release\outloud-rng-check.exe
.\build-portable\Release\outloud-worker-check.exe rng-check portable
```

Without the generated RNG header, CMake omits only `outloud-rng-check`; native
RNG/worker libraries and the fixture-free synthesis executable do not need it.
The existing project's other development fixture requirements are unchanged.

Created files: `RNG_NOTICES.md`, `docs/NATIVE_RNG.md`,
`engine/include/outloud/production_noise.hpp`, `engine/src/production_noise.cpp`,
`tools/audio/wav.hpp`, `tools/neural/export_rng_reference.py`,
`tools/rng-check/main.cpp`, `tools/rng-synth/main.cpp`.
Modified files: `CMakeLists.txt`, `engine/include/outloud/worker_pipeline.hpp`,
`engine/src/worker_pipeline.cpp`, `tools/worker-check/main.cpp`.

## Portability and remaining limits

The RNG has one scalar implementation with fixed integer widths, FP32 constants
and separate operations. Its output does not select arithmetic by neural
KernelPath or AVX2 availability. The disabled build verifies the same RNG
reference. Default IEEE round-to-nearest FP behavior is assumed, as in the
verified reference. Other compilers/rounding environments, physical CPUs and
PyTorch versions/capabilities are unverified. This is compatibility with the
pinned AVX2 reference normal path, not a promise to match PyTorch's own scalar
fallback, other tensor shapes/dtypes, arbitrary means/temperatures or complete
PyTorch state serialization.

Step 2L should implement **native multi-segment orchestration** next. It is
not implemented here; seed advancement across segments remains undecided.
SAPI/COM, registration, WoW integration and broader audio evaluation remain
outside this step.
