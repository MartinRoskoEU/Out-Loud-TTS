# Current native architecture

This describes current source. Milestone reports retain their original evidence;
current build procedures are centralized in [A11/A12](BUILD_AND_VALIDATION_IMPROVEMENTS.md).

## Runtime and preparation

The runtime is native Windows x64 C++20 for the pinned Pocket TTS 3.3.0 English,
CPU, batch-1 synthesis contract. `OutLoudPocketSAPI.dll` statically links internal
project libraries. Local compatible model/tokenizer/voice files, Windows SAPI/COM,
UCRT and Microsoft VC runtimes are required. Native inference has no Python,
PyTorch, external neural runtime or network dependency.

Offline model/tokenizer export and raw-audio voice preparation are separate.
Python exporters and the upstream oracle produce pinned assets or independent
references. The native importer converts prepared Pocket states; Voice Manager
does not compute conditioning from microphone/WAV input.

## Formats and lifetime

| File | Contract |
| --- | --- |
| `.olp`, version 1 | 169 retained BF16 model tensors; strict IDs/shapes/layout, pinned model/compatibility identity, payload/container SHA-256. |
| tokenizer `.bin` | Exported pinned Unigram vocabulary/scores and text-preparation tables for native English text processing. |
| `.olv`, version 1 / schema 1 | Six layers of finite FP32 FlowLM K/V voice prefixes, K already post-RoPE; validated prefix/layout, integrity and declared model compatibility. |
| library `metadata.json`, schema 1 | Immutable ID, display name, import/source/hash/compatibility metadata, outside the OLV. |

The unchanged binary layout is in [NATIVE_ASSETS.md](NATIVE_ASSETS.md);
[assets.hpp](../engine/include/outloud/assets.hpp) and
[assets.cpp](../engine/src/assets.cpp) define/validate the current contract.
Integrity hashes do not authenticate a publisher. Valid shapes and declared
compatibility do not establish the original model provenance of a prepared voice.
See [native import](VOICE_IMPORT.md) for conversion APIs and that limit.

Model/voice objects share ownership of immutable read-only mappings; tensor/cache
spans borrow them. Mutable neural state belongs to compute owners, not OLV files.
Mappings retain read handles. A resident SAPI voice can remain in use after Speak
finishes or is stopped, so removal may require the host to release it first.

## Generation and streaming

1. Native English text preparation/tokenization splits segments using the existing
   soft 50-token rule. Configured maximum segment capacity is a separate hard bound.
2. Each segment resets neural state/restores its voice prefix. FlowLM prefills text,
   consumes the discarded prefill sample, then generates candidates from learned
   BOS and normalized latent feedback with existing EOS/emission rules.
3. FlowSampler combines current hidden state and production noise. Noise draw
   order, integration and stopping-candidate semantics remain unchanged.
4. The producer publishes normalized 32-float latents through a bounded queue of
   capacity two. The second resident worker runs Mimi streaming decode: 1,920
   FP32 samples per emitted latent, or 80 ms at 24 kHz mono.
5. Utterance orchestration streams frames, resetting neural state between segments
   while retaining one continuous request RNG. SAPI converts to PCM16 and performs
   caller-thread site writes, including partial-write retries/action polls.

`WorkerPipeline` owns exactly two resident compute threads. Native/SAPI coordinators
prepare/dispatch/wait; they are not additional neural workers. The SAPI request
coordinator is joined. Neither native orchestration nor SAPI requires a complete
utterance PCM buffer; the adapter uses a one-frame PCM16 delivery rendezvous.

Contracts: [worker_pipeline.hpp](../engine/include/outloud/worker_pipeline.hpp),
[orchestration.hpp](../engine/include/outloud/orchestration.hpp). The tokenizer
must outlive orchestration; sink/context must survive `run`. Observer spans/sample
references borrow data only during callbacks. Pipeline methods are not concurrent
or reentrant; concurrent `request_stop()` is permitted.

## Reuse and CPU dispatch

- **A08:** three dense Mimi transpose-convolution groups prepare immutable FP32
  phase rows once per decoder. Reset retains those weights while clearing mutable
  state/carries. Audio-dependent windows/dots/carry arithmetic remain per frame.
- **A09 FlowLM:** `prefill_into` reuses normalization scratch for embeddings and
  caller-owned result storage. Generator/producer reuse their candidate result.
  Capacity growth can allocate; a previously sized warm path avoids the two former
  prefill allocations. Owning APIs remain available.
- **A09 sampler:** normalized fixed `s=0`/`t=1` embeddings are cached at construction
  for that model/ISA. Combined-condition SiLU is computed once per sample and reused
  across blocks/final modulation, without caching the changing condition across
  frames. Detailed observers retain their original checkpoints.

Caches are owner-local, never global across models. [A08](PERFORMANCE_OPTIMIZATION_A08.md)
and [A09](PERFORMANCE_OPTIMIZATION_A09.md) retain memory costs and exact evidence.
Portable kernels remain available; optional AVX2/FMA uses runtime CPU checks.
Public native constructors default to portable; the SAPI token's `KernelPath=Auto`
selects an available path. ISA paths may produce different bits. Demonstrated
old/new equality applies to each tested deterministic workload against its own
baseline, not every text/voice/compiler or portable-versus-AVX2 equality. Full
installed post-optimization WoW performance has not been measured.

## SAPI controls and cancellation

The fixed engine CLSID is `{F38C3398-3552-43B9-8350-7DD301327D70}`. Managed tokens
select exact library identities and runtime paths; they do not import sources.
[Shared deployment](SAPI_ADAPTER.md) and [voice registration](MULTI_VOICE_SAPI.md)
are separate.

Each Speak owns one stop source. Caller-thread GetActions polls observe abort;
tokens propagate through delivery/orchestration/workers, waking queue and sink
waits. A running kernel completes at a safe point; future work/publication stops,
both workers quiesce and the coordinator joins. Observed SAPI abort normally
returns S_OK; native/write failures still propagate. Fresh prepare/reset/reseed
permits reuse without recreating workers or a global stop flag.

Skip acknowledges zero skipped items. SAPI site rate uses Sonic; baseline and
fragment volume percentages combine at the final PCM16 write boundary after
rate processing. Volume changes refresh on the caller thread, including partial
writes; unity preserves bytes and zero writes silence. Equal-volume fragments
remain concatenated; actual XML volume transitions drain separate rendering
runs. Volume does not change neural inference. Supported leading bookmarks stay out of text and
are queued on the Speak caller before synthesis/writes at stream-relative byte
offset zero when requested. Bookmark-only requests can queue events without PCM;
actual notification/playback timing belongs to SAPI. They do not synthesize
pauses. Current collection limits are 262,144 spoken UTF-16 code units, 8,192
fragments, 1,024 bookmarks and 65,536 bookmark-name code units; excess fails without
truncation. Asset errors retain role/Win32 context through bounded COM diagnostics.

Source: [adapter.cpp](../sapi/src/adapter.cpp), [support.hpp](../sapi/src/support.hpp).
Historical evidence: [cancellation](SAPI_CANCELLATION.md), [bookmarks](SAPI_BOOKMARKS.md),
[later input/diagnostic stabilization](STABILIZATION_PHASE_1.md).

The adapter records operational failures through a private, always-enabled
[error-only logger](NATIVE_ERROR_LOGGING.md). Successful synthesis and normal
cancellation never enter file logging; neural components have no logging dependency.
