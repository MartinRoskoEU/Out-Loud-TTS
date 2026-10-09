# Native one-segment supplied-noise generation (Step 2F)

> Historical Step 2F report. Supplied-noise APIs remain independent references; production RNG, A09 reusable prefill and streaming are described in [ARCHITECTURE.md](ARCHITECTURE.md). Use [A11/A12](BUILD_AND_VALIDATION_IMPROVEMENTS.md) for current build/validation.

`outloud_generation` executes Pocket TTS 3.3.0's complete FlowLM/sampler
lifecycle for one English segment, batch one, single-threaded, using local
resident `.olp`/`.olv` assets and externally supplied noise. The portable CPU
path remains the default; optional AVX2/FMA uses the existing capability
dispatch. No production RNG, Mimi, PCM, decoder queue/thread or SAPI is added.
This establishes canonical latent-stage scheduling and measured arithmetic
parity, not full TTS parity or an audio-quality result.

## Verified upstream scheduling

The unchanged installed `models/tts_model.py` was inspected at
`_estimate_max_gen_len`, `_generate`, `_autoregressive_generation`,
`_run_flow_lm_and_increment_step`, `_expand_kv_cache`, and the text preparation
tail selection. `models/flow_lm.py` still computes `out_eos(hidden) > -4.0`
directly, samples already scaled noise with standard deviation `sqrt(0.3)`,
and applies one-step LSD. `modules/stateful_module.py:increment_steps` advances
all six offsets after the source forward. No source files were edited.

For the canonical text “The watch captain needs your help before the next
caravan arrives.”, native preparation produces exactly the protected 20 token
IDs and tail 3. The upstream maximum is
`ceil((token_count / 3.0 + 2.0) * 12.5) = 109`, an upper bound on candidate
calls, not an expected duration. The voice is `gnome_male`, prefix length 112.

1. Reset mutable state to the shared immutable voice prefix; reserve suffix
   capacity `token_count + max_gen_len` (129 positions, total capacity 241).
2. Prefill all 20 text positions and compute the final hidden/EOS head. Consume
   noise row 0 and execute the sampler. **Discard** this latent and EOS result.
   The logical post-prefill offset is 132.
3. Candidate 0 uses learned BOS. Native code widens the stored BOS directly,
   equivalent to upstream's NaN replacement. Later candidates feed back the
   immediately previous **native normalized** 32-D latent. No denormalization,
   other BOS insertion, Mimi transformation or reference feedback occurs here.
4. Each candidate performs the bias-free 32 -> 1024 input projection, existing
   six-layer backbone, EOS projection, and sampler with the next supplied noise
   row. The candidate consumes one cache position and one noise row regardless
   of whether it will be emitted.
5. Latch the first `eos_logit > -4.0` only at index >=6. There is no sigmoid,
   softmax or EOS token. After computing/sampling the candidate, stop if
   `index >= first_eos + frames_after_eos`, **before emission**. Otherwise save
   the normalized latent for emission and feedback.
6. If the maximum is reached, all computed candidates are emitted unless the
   EOS-tail check stopped one earlier. Return `maximum_length` rather than
   requiring an EOS. A latched EOS whose tail extends past the maximum does
   not extend the safety bound.

| Canonical call | Noise row | Input | Offset before -> after | Emitted |
|---|---:|---|---|---|
| Text prefill | 0 | 20 text tokens | 112 -> 132 | No |
| Candidate 0 | 1 | Learned BOS | 132 -> 133 | Yes |
| Candidates 1..42 | 2..43 | Previous normalized native latent | 133..174 -> 134..175 | Yes |
| Candidate 43 | 44 | Previous normalized native latent; first accepted EOS | 175 -> 176 | Yes |
| Candidates 44..45 | 45..46 | Previous normalized native latent; tail | 176..177 -> 177..178 | Yes |
| Candidate 46 | 47 | Previous normalized native latent; stopping candidate | 178 -> 179 | **No** |

Thus there are **48 draws**, **47 candidates**, **46 emitted frames**, first
EOS **43**, and final offset **179**. Logical suffix length is 20+47=67. These
facts were verified against both the protected pipeline mapping and actual
execution of the installed upstream AR loop, not assumed from the task prompt.

## API, supplied noise and state lifecycle

[generation.hpp](../engine/include/outloud/generation.hpp) exposes
`FlowLMGenerator(backbone, sampler).generate_segment(tokens, noise, options,
observer)`. The backbone already owns the selected resident voice mapping.
The result contains emitted normalized rows, counts, prefill/final offset,
first EOS, termination reason and informational timings. `state()` exposes
the completed suffix for checking. This is one active segment per generator;
the referenced backbone and sampler must not be used concurrently/reentrantly.

`NoiseSource::next()` returns a non-owning FP32 row, valid until the next draw.
`FixtureNoiseSource` accepts non-owning flat `[draws,32]` storage and rejects
empty/malformed/nonfinite data. Every draw is revalidated, returned unchanged
and in order. It neither scales nor clamps. Exhaustion/overconsumption throws;
`finish_segment()` also rejects unused fixture rows. A future RNG source can
implement this same interface without fixture EOF enforcement. No native seed,
Gaussian generator or PyTorch RNG compatibility code exists in this step.

`GenerationOptions` supplies the tail and EOS threshold; zero `max_gen_len`
selects the upstream token-count estimate. For other prepared text, callers
should pass Step 2B's `TextSegment.frames_after_eos` rather than infer words
inside this token-only API. The defaults match this canonical segment.
The minimum EOS index is fixed at 6. Nonfinite thresholds and suffix requests
above the existing 4096-position allocation limit fail before generation.

State is reset automatically at the start of each successful new segment.
Matching suffix capacity reuses the existing allocation; a different required
capacity creates a new suffix while sharing the same immutable prefix.
After any started computation/noise/observer/fixture-completion failure, the
generator invalidates the whole suffix and refuses reuse until explicit
`reset()`. Reset hides old suffix contents and restores offset 112 without
copying the prefix. A new noise source is also required for retry. Invalid
options/tokens rejected before generation do not invalidate a completed state.

The existing native backbone commits its offset when its call returns,
followed by sampling in the candidate helper. Upstream commits after sampling.
Both expose the same successful whole-call offsets and noise/emission sequence;
the generator treats any failure within that whole call as reset-required,
so an intermediate committed native position cannot be reused after failure.
Candidate noise is leased before the combined helper; prefill noise is leased
after backbone prefill. This changes no draw order or successful mathematical
result, and noise sources must not rely on intermediate cache state.

Observers receive one completed prefill/candidate event, including input kind,
offsets, hidden, EOS, noise, velocity, latent, latch and emission status. Spans
are valid only during the callback. Observers may inspect but must not mutate
or reenter the generator's compute contexts. Reentrant generate/reset calls
are rejected. No queue, worker, cancellation thread or segment iterator is
introduced.

## Offline reference and reproducibility

[export_generation_segment.py](../tools/neural/export_generation_segment.py)
forces `HF_HUB_OFFLINE=1` and reuses the pinned local FlowLM construction from
Step 2D. It constructs a FlowLM-only TTS wrapper; no Mimi model is instantiated
or executed. It invokes the installed `_run_flow_lm_and_increment_step` for
prefill and calls the actual `_autoregressive_generation` **sequentially**.
An in-memory sink observes upstream emission; the original loop is not
reimplemented. A non-set Event supplies its stop argument, with no worker
thread started. Forward hooks capture the real noise/hidden/velocity/EOS;
an instance wrapper records input and post-call state without editing source.

Seed 73 is reset after assets load, once before the segment. Source generation
is repeated and all scalar scheduling metadata and float bytes must match.
Prefill and BOS hidden/noise/velocity/latent hashes also match frozen Step 2E
checkpoints, whose hidden identities already match Step 2D.

The ignored `generated/neural/generation-segment1-f32.bin` contains **301
checkpoints / 3,526,464 bytes**, SHA-256
`b0b7960f8b9256f1baa028cd3d81a02556d05241616aa4a4b59cc2a65ba2d801`:

- Five prefill records: hidden `[1024]`, EOS `[1]`, noise/velocity/latent `[32]`.
- Six records per each of 47 candidates: input `[32]`, hidden `[1024]`, EOS
  `[1]`, noise/velocity/latent `[32]`. Candidate 0 stores learned BOS as its
  finite input; the runtime source NaN signal is recorded as `input_kind=BOS`.
- Complete noise stream `[48,32]` and emitted latents `[46,32]`.
- Final K and V suffixes, each `[67,16,64]`, for all six layers. K is post-RoPE.

[generation-segment1-baseline.json](../reference/generation-segment1-baseline.json)
tracks only metadata: asset identity, text/tokens, constants, checkpoint
shapes/dtypes/offsets/hashes and each candidate's scalar EOS/latch/emission/
offset timeline. Large arrays remain in the ignored blob. The generated C++
fixture header is also ignored. The reader verifies full-file hash/length,
contiguous bounds, every checkpoint hash and exact EOF. Export refuses to
replace a changed frozen generation baseline. Previous writers/baselines are
never invoked or modified.

## Free-running numerical parity and EOS safety

The checker compares the native free-running input/hidden/EOS/velocity/latent
at every candidate, emitted rows, and final six-layer K/V state. It proves
each feedback input is bitwise equal to the prior **native** latent and every
supplied noise row is bitwise equal to its captured fixture. Counts, offsets,
input kind, first latch and emission decisions must match exactly. Any EOS
boolean disagreement is an unconditional failure, including in `--measure`;
it cannot be accepted by a floating tolerance.

All floating comparisons record max absolute error, RMS, unrestricted nonzero
relative error, zero-reference absolute error and meaningful relative error.
The meaningful metric includes reference elements with
`abs(reference) >= 1e-6 * max(1, max(abs(reference)))`, matching Step 2E.
Near-zero hidden/K/V elements can still have large relative errors; those
are reported, with absolute and RMS limits enforced independently.

Measurements preceded gates. [generation_tolerances.hpp](../tests/generation_tolerances.hpp)
freezes **301 separate checkpoint limits** from both paths and their agreement:
twice each observed absolute/RMS/meaningful-relative maximum, rounded upward
to two significant digits; observed zeros stay zero. The diagnostic has four
separate field limits. No source fixture was changed to obtain passing output.
These are regression limits for this canonical workload, not universal error
bounds or an audio-quality threshold. Normal runs enforce them and only then
print `RESULT: PASS`. `--measure` bypasses numerical limits, retains structural,
hash and EOS checks, and prints `MEASURED`.

**Observed feedback amplification is significant.** Portable latent max error
starts at 9.54e-7, reaches 3.60e-3 at candidate 38, then falls to 2.00e-4 at
candidate 46. Hidden error peaks at 4.07e-3 at candidate 39. AVX2 follows the
same pattern with slightly smaller peaks. This is not a claim that error
stays at one-frame magnitude, grows monotonically, or is bounded for longer
generation. Final cache drift is measurable and reported below.

A **test-only reference-feedback diagnostic** supplies captured upstream
previous latents to the existing candidate helpers, retaining native backbone
cache history. It is not selectable in the production generator. In this
diagnostic portable/AVX2 latent maxima are 3.93e-6/3.81e-6 and hidden maxima
2.62e-6/2.26e-6. This supports feedback amplification as the explanation for
the much larger free-running errors; it does not establish audible impact.

Every canonical EOS boolean matches on both paths. The minimum upstream
distance from -4 is **2.335375**, at candidate 42, versus maximum EOS errors
6.62e-4 portable and 6.16e-4 AVX2. Portable/AVX2 agreement maxima are hidden
2.88e-4, latent 2.56e-4 and EOS 4.58e-5. Full per-candidate errors, both EOS
logits and threshold margins, K/V errors and timings are written to ignored
`generation-portable-parity.json`, `generation-avx2-parity.json` and
`generation-path-agreement.json`. The checker requires a writable development
fixture directory for these reports; engine generation does not write files.

## Memory and allocation behavior

The existing model mapping is **199,449,856 bytes** (169 BF16 tensors); the
canonical voice mapping is **5,505,280 bytes**. Both remain resident, read-only
and shared by handles. No full FP32 model or sampler copy is created. The
model's sampler records remain the existing 19,518,016 BF16 payload bytes.

| Allocation | Canonical size | Lifetime |
|---|---:|---|
| Six mutable K/V suffixes, capacity 129 | **6,340,608 bytes** | Generator state, reused for matching capacity |
| Logical final K/V suffix, 67 positions | 3,293,184 bytes | Used part of that allocation |
| Emitted rows, 46 x 32 FP32 | **5,888 bytes** logical | Returned result |
| Emitted rows reserved for maximum 109 | **13,952 bytes** allocated | Returned result, one reservation before loop |
| Backbone retained workspace, including its row buffer | **1,000,388 bytes** observed | Backbone context; retains prefill capacities |
| Sampler activation scratch | **23,552 bytes** | Per-call stack |
| Sampler one-row widening buffer | **16,384 bytes** | Sampler context, reused |
| Candidate normalized hidden output | **4,096 bytes** | Generator reusable output buffer |
| Candidate projection arrays | 4,224 bytes | Stack per candidate |
| Candidate velocity/latent and previous latent | 256 + 128 bytes | Reused object / stack |

The retained backbone workspace has 20-token prefill activation capacities
`20*12*1024*4 = 983,040` bytes, full score capacity `241*4 = 964` bytes and its
16 KiB widening buffer. It resizes logical views for single-position calls
without reallocating. The prefill embedding and returned normalized sequence
each temporarily use `20*1024*4 = 81,920` bytes; they are released before AR.
Object headers, cached identity strings, small scalar/reduction arrays and
call-stack overhead are additional. Workspace storage reflects allocator
capacity, so it may vary on other standard libraries or after larger segments.

To avoid straightforward per-candidate allocations, `backbone_into` reuses
activation/output storage, scores reserve the complete state capacity,
candidate helpers reuse the returned hidden row, and asset identity strings
are cached instead of regenerated at every call. Observer label strings are
constructed only when observers are present. Original convenience APIs still
work and retain identical numerical behavior under all prior tests.

The regression executable counts C++ `new`/`new[]` requests during the complete
unobserved canonical candidate loop and observes **zero** on both available
paths. This is a C++ allocation audit, not a profiler of CRT/internal OS
allocations. Setup/prefill allocate before the audit; test observers and
comparison reports intentionally allocate outside the measured path.

## Performance and validation

The tables below record one warm Release x64 run per path, MSVC 19.44, after
asset load/validation and a checking run. Timed generation has no observer,
fixture comparison or console output inside the loop. It includes normal
noise-source retrieval/validation, emission storage and stop checks. Timings
exclude model/voice loading, fixture hashing, initial suffix/output reservation
and the reference-feedback diagnostic. Min/max include the final discarded
candidate. No CPU-specific tuning or benchmark framework was added.

46 emitted latents represent a future **3.680 seconds** of audio, at 80 ms per
latent. The reported partial latent-stage RTF is `3.680 / total_seconds`,
including prefill and all 47 candidate calls. It follows the requested
audio-duration / compute-duration convention: higher is faster.
**THIS IS NOT FULL TTS SPEED:** Mimi and PCM do not exist yet, so no end-to-end
speed or first-PCM latency can be claimed. AVX2 is not assumed to be faster.

Release x64 build and **all 10 CTests pass**, both with AVX2 enabled and with
`OUTLOUD_ENABLE_AVX2=OFF`. The prior eight Step 2B/2C/2D/2E tests remain intact.
New tests cover the exact canonical free-running sequence and path agreement,
noise ordering/finite rows/exhaustion/unused draws, prefill discard, BOS and
normalized feedback, strict EOS threshold, minimum index, one-time latch,
tail zero/three/overflow safety, final discarded call, max-length termination,
malformed inputs, failed-state/reset behavior, reentrancy and allocation reuse.

With the prior local assets/fixtures already exported:

```powershell
$env:HF_HUB_OFFLINE = '1'
$env:PYTHONDONTWRITEBYTECODE = '1'
.\.venv\Scripts\python.exe tools/neural/export_generation_segment.py
cmake -S . -B build -A x64
cmake --build build --config Release
.\build\Release\outloud-generation-check.exe
ctest --test-dir build -C Release --output-on-failure
cmake -S . -B build-portable -A x64 -DOUTLOUD_ENABLE_AVX2=OFF
cmake --build build-portable --config Release
ctest --test-dir build-portable -C Release --output-on-failure
```

No CMake/native command fetches assets or invokes Python. Missing generated
assets fail configuration. Native DLL dependencies remain the Windows/MSVC
runtime and BCrypt. No external compute dependency was added. Physical CPUs
without AVX2, other text/voices/noise streams, much longer feedback sequences,
broader compiler/libm behavior and audible drift remain **UNVERIFIED**. The
disabled build establishes portable build/dispatch on the available host.
Production RNG and its compatibility/consumption behavior remain a separate
step. Mimi, PCM, streaming threads and SAPI remain unimplemented. Step 2F
ends here; previous protected baselines, schemas, assets and source remain
unchanged, with no staging, commit or push.

## Files in this step

| Status | File | Purpose |
|---|---|---|
| New | [generation.hpp](../engine/include/outloud/generation.hpp) | Noise source, stop policy and segment API |
| New | [generation.cpp](../engine/src/generation.cpp) | Sequential one-segment lifecycle |
| New | [export_generation_segment.py](../tools/neural/export_generation_segment.py) | Offline untouched-source capture |
| New | [generation-segment1-baseline.json](../reference/generation-segment1-baseline.json) | Stable reference metadata |
| New | [generation checker](../tools/generation-check/main.cpp) | Canonical errors, EOS safety, diagnostics and timings |
| New | [generation_fixture_checks.hpp](../tests/generation_fixture_checks.hpp) | Fixture validation and free-running comparisons |
| New | [generation_tolerances.hpp](../tests/generation_tolerances.hpp) | Measured checkpoint gates |
| New | [generation_test.cpp](../tests/generation_test.cpp) | Scheduling, failure and allocation regressions |
| New | [NATIVE_GENERATION.md](NATIVE_GENERATION.md) | Architecture and evidence |
| Modified | [CMakeLists.txt](../CMakeLists.txt) | Library, checker and two new CTests |
| Modified | [flowlm.hpp](../engine/include/outloud/flowlm.hpp), [flowlm.cpp](../engine/src/flowlm.cpp) | Reusable workspace/output and cached asset identity |
| Modified | [flow_sampler.hpp](../engine/include/outloud/flow_sampler.hpp), [flow_sampler.cpp](../engine/src/flow_sampler.cpp) | Shared BOS/normal helpers and reusable candidate output |

## Measured candidate progression, EOS and final K/V

Each metric maximum is independent; summary maxima can come from different
candidate indices. EOS classifications and emission/latch metadata are exact
on both paths. The following measurements are retained for review.

| Path | Prefill ms | AR ms | Total ms | Mean candidate ms | Min / max candidate ms | Partial latent-stage RTF |
|---|---:|---:|---:|---:|---:|---:|
| Portable | 147.994 | 1082.692 | 1230.685 | 23.036 | 22.620 / 23.809 | 2.990 |
| AVX2/FMA | 159.610 | 1156.810 | 1316.420 | 24.613 | 24.316 / 26.534 | 2.795 |

| Path / candidate field | Max absolute | Max candidate RMS | Max meaningful relative |
|---|---:|---:|---:|
| Portable / hidden | 4.066229e-03 | 3.176981e-04 | 5.042518e+00 |
| Portable / velocity | 3.596693e-03 | 8.089463e-04 | 2.229869e-02 |
| Portable / latent | 3.596663e-03 | 8.089400e-04 | 3.068372e-02 |
| Portable / eos | 6.618500e-04 | 6.618500e-04 | 8.483811e-05 |
| AVX2/FMA / hidden | 3.778398e-03 | 2.952551e-04 | 4.680473e+00 |
| AVX2/FMA / velocity | 3.340572e-03 | 7.514856e-04 | 2.072859e-02 |
| AVX2/FMA / latent | 3.340542e-03 | 7.514753e-04 | 2.861680e-02 |
| AVX2/FMA / eos | 6.160736e-04 | 6.160736e-04 | 7.897034e-05 |

### All free-running candidates

P = portable; A = AVX2/FMA. Each latent has 32 components, each hidden has 1024.

| Index | P hidden abs | A hidden abs | P latent abs | P latent RMS | P latent meaningful rel | A latent abs | A latent RMS | A latent meaningful rel |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 | 1.520e-06 | 1.431e-06 | 9.537e-07 | 3.611e-07 | 1.625e-04 | 7.153e-07 | 2.875e-07 | 1.375e-05 |
| 1 | 1.758e-06 | 1.855e-06 | 1.222e-06 | 3.962e-07 | 1.699e-05 | 1.013e-06 | 3.619e-07 | 1.723e-05 |
| 2 | 2.444e-06 | 2.682e-06 | 2.027e-06 | 6.721e-07 | 4.521e-03 | 2.265e-06 | 7.162e-07 | 3.617e-03 |
| 3 | 5.096e-06 | 5.692e-06 | 1.287e-05 | 4.542e-06 | 8.085e-05 | 1.621e-05 | 5.700e-06 | 1.042e-04 |
| 4 | 7.927e-06 | 9.716e-06 | 9.954e-06 | 3.463e-06 | 1.893e-04 | 1.103e-05 | 3.662e-06 | 2.097e-04 |
| 5 | 8.106e-06 | 9.134e-06 | 1.037e-05 | 4.090e-06 | 5.766e-05 | 1.121e-05 | 4.672e-06 | 4.650e-05 |
| 6 | 9.030e-06 | 1.085e-05 | 1.049e-05 | 4.345e-06 | 1.388e-03 | 1.383e-05 | 5.532e-06 | 1.711e-03 |
| 7 | 7.033e-06 | 8.404e-06 | 8.583e-06 | 3.647e-06 | 5.848e-05 | 9.537e-06 | 4.328e-06 | 6.657e-05 |
| 8 | 7.391e-06 | 9.656e-06 | 5.126e-06 | 2.628e-06 | 1.143e-04 | 6.199e-06 | 3.264e-06 | 1.108e-04 |
| 9 | 5.576e-06 | 7.056e-06 | 4.888e-06 | 2.095e-06 | 1.960e-05 | 5.245e-06 | 2.376e-06 | 2.104e-05 |
| 10 | 1.052e-05 | 1.170e-05 | 4.411e-06 | 1.381e-06 | 1.048e-05 | 4.590e-06 | 1.550e-06 | 3.156e-05 |
| 11 | 3.904e-06 | 4.828e-06 | 3.815e-06 | 1.617e-06 | 1.859e-05 | 3.934e-06 | 1.656e-06 | 1.195e-05 |
| 12 | 5.499e-06 | 6.258e-06 | 7.570e-06 | 1.758e-06 | 1.860e-05 | 8.285e-06 | 1.902e-06 | 2.036e-05 |
| 13 | 4.888e-06 | 4.038e-06 | 3.894e-05 | 7.411e-06 | 3.222e-04 | 2.964e-05 | 5.669e-06 | 2.453e-04 |
| 14 | 8.395e-05 | 6.489e-05 | 4.536e-05 | 8.553e-06 | 1.843e-04 | 3.523e-05 | 6.669e-06 | 1.431e-04 |
| 15 | 5.636e-05 | 4.172e-05 | 3.666e-05 | 7.084e-06 | 4.990e-04 | 2.775e-05 | 5.413e-06 | 4.060e-04 |
| 16 | 9.060e-06 | 7.510e-06 | 1.782e-05 | 5.038e-06 | 8.803e-03 | 1.639e-05 | 4.567e-06 | 8.284e-03 |
| 17 | 1.008e-04 | 9.298e-05 | 8.702e-05 | 2.125e-05 | 2.680e-04 | 7.963e-05 | 1.927e-05 | 2.196e-04 |
| 18 | 3.403e-05 | 2.953e-05 | 2.468e-05 | 9.232e-06 | 7.152e-04 | 1.848e-05 | 7.123e-06 | 5.444e-04 |
| 19 | 2.721e-05 | 2.018e-05 | 3.666e-05 | 1.401e-05 | 4.173e-04 | 3.064e-05 | 1.119e-05 | 3.231e-04 |
| 20 | 2.344e-04 | 1.945e-04 | 2.605e-05 | 1.289e-05 | 2.734e-04 | 2.038e-05 | 9.962e-06 | 2.155e-04 |
| 21 | 2.384e-05 | 1.907e-05 | 4.256e-05 | 2.046e-05 | 1.852e-04 | 3.159e-05 | 1.525e-05 | 1.359e-04 |
| 22 | 2.738e-04 | 2.028e-04 | 4.101e-05 | 1.652e-05 | 8.583e-04 | 3.099e-05 | 1.217e-05 | 6.106e-04 |
| 23 | 3.210e-05 | 2.396e-05 | 3.588e-05 | 1.341e-05 | 3.989e-04 | 2.789e-05 | 1.007e-05 | 2.749e-04 |
| 24 | 7.832e-05 | 5.567e-05 | 7.343e-05 | 2.287e-05 | 1.714e-03 | 5.339e-05 | 1.671e-05 | 1.264e-03 |
| 25 | 4.220e-05 | 3.070e-05 | 4.607e-05 | 1.928e-05 | 5.572e-03 | 2.956e-05 | 1.340e-05 | 3.537e-03 |
| 26 | 6.926e-05 | 3.827e-05 | 5.704e-05 | 1.999e-05 | 5.586e-04 | 3.922e-05 | 1.376e-05 | 3.882e-04 |
| 27 | 4.092e-05 | 2.762e-05 | 3.231e-05 | 1.547e-05 | 2.422e-04 | 2.372e-05 | 1.086e-05 | 1.514e-04 |
| 28 | 2.915e-05 | 2.021e-05 | 1.609e-05 | 6.552e-06 | 5.400e-04 | 1.097e-05 | 4.557e-06 | 4.247e-04 |
| 29 | 1.827e-05 | 1.267e-05 | 2.283e-05 | 7.097e-06 | 1.162e-04 | 1.478e-05 | 4.698e-06 | 9.367e-05 |
| 30 | 2.098e-05 | 1.693e-05 | 4.673e-05 | 1.625e-05 | 1.346e-04 | 3.850e-05 | 1.317e-05 | 1.133e-04 |
| 31 | 1.869e-04 | 1.566e-04 | 3.070e-04 | 5.632e-05 | 1.333e-03 | 2.623e-04 | 4.808e-05 | 1.139e-03 |
| 32 | 5.724e-05 | 5.080e-05 | 4.452e-05 | 1.189e-05 | 6.053e-04 | 4.406e-05 | 1.155e-05 | 8.071e-04 |
| 33 | 2.902e-04 | 2.749e-04 | 5.245e-05 | 1.938e-05 | 2.431e-04 | 4.911e-05 | 1.789e-05 | 2.360e-04 |
| 34 | 2.575e-04 | 2.487e-04 | 2.532e-04 | 8.662e-05 | 3.561e-04 | 2.360e-04 | 8.127e-05 | 3.374e-04 |
| 35 | 8.768e-04 | 8.134e-04 | 8.997e-04 | 1.893e-04 | 3.068e-02 | 8.391e-04 | 1.764e-04 | 2.862e-02 |
| 36 | 7.371e-04 | 6.874e-04 | 1.475e-03 | 3.604e-04 | 2.396e-02 | 1.376e-03 | 3.359e-04 | 2.234e-02 |
| 37 | 2.694e-03 | 2.508e-03 | 4.965e-04 | 1.699e-04 | 6.248e-04 | 4.621e-04 | 1.581e-04 | 5.823e-04 |
| 38 | 2.350e-03 | 2.187e-03 | 3.597e-03 | 8.089e-04 | 2.893e-02 | 3.341e-03 | 7.515e-04 | 2.690e-02 |
| 39 | 4.066e-03 | 3.778e-03 | 6.368e-04 | 2.124e-04 | 8.065e-03 | 5.929e-04 | 1.973e-04 | 7.515e-03 |
| 40 | 4.238e-04 | 3.946e-04 | 2.757e-04 | 1.116e-04 | 9.589e-03 | 2.564e-04 | 1.036e-04 | 8.914e-03 |
| 41 | 1.883e-04 | 1.746e-04 | 4.822e-04 | 1.923e-04 | 7.220e-03 | 4.472e-04 | 1.782e-04 | 6.743e-03 |
| 42 | 2.663e-04 | 2.470e-04 | 5.302e-04 | 1.569e-04 | 4.550e-03 | 4.916e-04 | 1.456e-04 | 4.226e-03 |
| 43 | 2.442e-04 | 2.273e-04 | 4.023e-04 | 9.533e-05 | 1.243e-02 | 3.732e-04 | 8.865e-05 | 1.164e-02 |
| 44 | 1.392e-04 | 1.293e-04 | 2.979e-04 | 7.643e-05 | 8.099e-04 | 2.764e-04 | 7.080e-05 | 7.515e-04 |
| 45 | 1.022e-04 | 9.501e-05 | 2.549e-04 | 4.989e-05 | 9.385e-04 | 2.396e-04 | 4.651e-05 | 8.404e-04 |
| 46 | 7.206e-05 | 6.652e-05 | 1.997e-04 | 3.819e-05 | 5.289e-04 | 1.879e-04 | 3.627e-05 | 4.976e-04 |

### Every EOS comparison

Both native classifications equal the source classification in every row.
Margin is the upstream absolute distance from -4.0. EOS errors are absolute
(also RMS for a scalar). Latched index is absent until the first accepted EOS.

| Index | Source logit | P logit | A logit | Source margin | P error | A error | EOS | Latched index | Emitted |
|---|---:|---:|---:|---:|---:|---:|---|---:|---|
| 0 | -11.40162277 | -11.40162086 | -11.40162277 | 7.401623 | 1.907e-06 | 0.000e+00 | False | - | True |
| 1 | -11.93600273 | -11.93599606 | -11.93599892 | 7.936003 | 6.676e-06 | 3.815e-06 | False | - | True |
| 2 | -12.21126938 | -12.21126747 | -12.21126747 | 8.211269 | 1.907e-06 | 1.907e-06 | False | - | True |
| 3 | -11.34947968 | -11.34947777 | -11.34948063 | 7.349480 | 1.907e-06 | 9.537e-07 | False | - | True |
| 4 | -11.08350754 | -11.08350563 | -11.08350754 | 7.083508 | 1.907e-06 | 0.000e+00 | False | - | True |
| 5 | -11.73186684 | -11.73186684 | -11.73186874 | 7.731867 | 0.000e+00 | 1.907e-06 | False | - | True |
| 6 | -12.62917042 | -12.62917328 | -12.62917328 | 8.629170 | 2.861e-06 | 2.861e-06 | False | - | True |
| 7 | -11.79260254 | -11.79259300 | -11.79259396 | 7.792603 | 9.537e-06 | 8.583e-06 | False | - | True |
| 8 | -12.67670059 | -12.67670345 | -12.67670441 | 8.676701 | 2.861e-06 | 3.815e-06 | False | - | True |
| 9 | -13.08222008 | -13.08222389 | -13.08222675 | 9.082220 | 3.815e-06 | 6.676e-06 | False | - | True |
| 10 | -12.92146683 | -12.92146683 | -12.92146587 | 8.921467 | 0.000e+00 | 9.537e-07 | False | - | True |
| 11 | -11.16657829 | -11.16658020 | -11.16658020 | 7.166578 | 1.907e-06 | 1.907e-06 | False | - | True |
| 12 | -12.94622612 | -12.94622612 | -12.94622421 | 8.946226 | 0.000e+00 | 1.907e-06 | False | - | True |
| 13 | -13.08726883 | -13.08726311 | -13.08726597 | 9.087269 | 5.722e-06 | 2.861e-06 | False | - | True |
| 14 | -13.36580658 | -13.36581039 | -13.36581039 | 9.365807 | 3.815e-06 | 3.815e-06 | False | - | True |
| 15 | -12.01845360 | -12.01845932 | -12.01846123 | 8.018454 | 5.722e-06 | 7.629e-06 | False | - | True |
| 16 | -11.80949879 | -11.80949783 | -11.80949783 | 7.809499 | 9.537e-07 | 9.537e-07 | False | - | True |
| 17 | -12.00794315 | -12.00794697 | -12.00794601 | 8.007943 | 3.815e-06 | 2.861e-06 | False | - | True |
| 18 | -13.02916145 | -13.02915668 | -13.02915764 | 9.029161 | 4.768e-06 | 3.815e-06 | False | - | True |
| 19 | -12.46101189 | -12.46101761 | -12.46101475 | 8.461012 | 5.722e-06 | 2.861e-06 | False | - | True |
| 20 | -12.44974709 | -12.44973564 | -12.44973564 | 8.449747 | 1.144e-05 | 1.144e-05 | False | - | True |
| 21 | -10.74811554 | -10.74809456 | -10.74810028 | 6.748116 | 2.098e-05 | 1.526e-05 | False | - | True |
| 22 | -13.31728745 | -13.31723595 | -13.31724834 | 9.317287 | 5.150e-05 | 3.910e-05 | False | - | True |
| 23 | -11.83891296 | -11.83892250 | -11.83891773 | 7.838913 | 9.537e-06 | 4.768e-06 | False | - | True |
| 24 | -12.71098900 | -12.71101189 | -12.71100044 | 8.710989 | 2.289e-05 | 1.144e-05 | False | - | True |
| 25 | -12.12364674 | -12.12367821 | -12.12366962 | 8.123647 | 3.147e-05 | 2.289e-05 | False | - | True |
| 26 | -11.96603584 | -11.96603394 | -11.96603584 | 7.966036 | 1.907e-06 | 0.000e+00 | False | - | True |
| 27 | -11.50292778 | -11.50294495 | -11.50293922 | 7.502928 | 1.717e-05 | 1.144e-05 | False | - | True |
| 28 | -10.92797661 | -10.92798901 | -10.92798615 | 6.927977 | 1.240e-05 | 9.537e-06 | False | - | True |
| 29 | -10.66474724 | -10.66475582 | -10.66475296 | 6.664747 | 8.583e-06 | 5.722e-06 | False | - | True |
| 30 | -12.09419441 | -12.09420586 | -12.09420013 | 8.094194 | 1.144e-05 | 5.722e-06 | False | - | True |
| 31 | -11.21972752 | -11.21972752 | -11.21972466 | 7.219728 | 0.000e+00 | 2.861e-06 | False | - | True |
| 32 | -11.44387627 | -11.44386101 | -11.44385910 | 7.443876 | 1.526e-05 | 1.717e-05 | False | - | True |
| 33 | -12.44323826 | -12.44326401 | -12.44326401 | 8.443238 | 2.575e-05 | 2.575e-05 | False | - | True |
| 34 | -12.84171009 | -12.84170437 | -12.84170151 | 8.841710 | 5.722e-06 | 8.583e-06 | False | - | True |
| 35 | -11.46550369 | -11.46553993 | -11.46553516 | 7.465504 | 3.624e-05 | 3.147e-05 | False | - | True |
| 36 | -10.04477310 | -10.04486465 | -10.04485703 | 6.044773 | 9.155e-05 | 8.392e-05 | False | - | True |
| 37 | -10.37933254 | -10.37907791 | -10.37909412 | 6.379333 | 2.546e-04 | 2.384e-04 | False | - | True |
| 38 | -10.10062599 | -10.10086060 | -10.10084152 | 6.100626 | 2.346e-04 | 2.155e-04 | False | - | True |
| 39 | -10.11218834 | -10.11250305 | -10.11247826 | 6.112188 | 3.147e-04 | 2.899e-04 | False | - | True |
| 40 | -8.96940994 | -8.96984386 | -8.96981239 | 4.969410 | 4.339e-04 | 4.025e-04 | False | - | True |
| 41 | -7.95083427 | -7.95091295 | -7.95090675 | 3.950834 | 7.868e-05 | 7.248e-05 | False | - | True |
| 42 | -6.33537483 | -6.33573580 | -6.33571053 | 2.335375 | 3.610e-04 | 3.357e-04 | False | - | True |
| 43 | 7.80132866 | 7.80066681 | 7.80071259 | 11.801329 | 6.618e-04 | 6.161e-04 | True | 43 | True |
| 44 | 11.70649719 | 11.70668411 | 11.70666981 | 15.706497 | 1.869e-04 | 1.726e-04 | True | 43 | True |
| 45 | 9.89647007 | 9.89680862 | 9.89678383 | 13.896470 | 3.386e-04 | 3.138e-04 | True | 43 | True |
| 46 | 8.26616764 | 8.26635361 | 8.26633644 | 12.266168 | 1.860e-04 | 1.688e-04 | True | 43 | False |

### Final K/V suffix drift

All 67 suffix positions are compared, including text and the final discarded
candidate. Each cell is max absolute / RMS error. Imported prefix is shared
and remains untouched.

| Layer | P keys abs / RMS | P values abs / RMS | A keys abs / RMS | A values abs / RMS |
|---|---:|---:|---:|---:|
| 0 | 1.480341e-03 / 3.409809e-05 | 3.305227e-04 / 9.478614e-06 | 1.375675e-03 / 3.165502e-05 | 3.069267e-04 / 8.789801e-06 |
| 1 | 7.026196e-04 / 1.740097e-05 | 4.121736e-04 / 1.055450e-05 | 6.533861e-04 / 1.613445e-05 | 3.828853e-04 / 9.796457e-06 |
| 2 | 2.363682e-03 / 4.191796e-05 | 8.970350e-04 / 2.239469e-05 | 2.196312e-03 / 3.890680e-05 | 8.330941e-04 / 2.079133e-05 |
| 3 | 2.142012e-03 / 5.015196e-05 | 8.611679e-04 / 3.298493e-05 | 1.991630e-03 / 4.654509e-05 | 8.000135e-04 / 3.063336e-05 |
| 4 | 1.343116e-03 / 4.773352e-05 | 1.124859e-03 / 3.086083e-05 | 1.249567e-03 / 4.426934e-05 | 1.045704e-03 / 2.864380e-05 |
| 5 | 3.116012e-03 / 5.737924e-05 | 1.534685e-03 / 4.488061e-05 | 2.894759e-03 / 5.324342e-05 | 1.426294e-03 / 4.168057e-05 |
