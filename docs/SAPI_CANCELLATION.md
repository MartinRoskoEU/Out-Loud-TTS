# Step 2N: native cancellation and SAPI StopSpeaking

> Historical Step 2N design/validation record. Current runtime and input/diagnostic boundaries are summarized in [ARCHITECTURE.md](ARCHITECTURE.md); current build/test selections are in [A11/A12](BUILD_AND_VALIDATION_IMPROVEMENTS.md). The original deployment allowlist/memory/timing evidence below is not a current installer contract.

Step 2M was committed as `bda8858` (Implement native Pocket SAPI adapter), and
the tree was clean at entry. This step adds cooperative request cancellation
without changing model math/kernels, tokenizer/splitting, RNG arithmetic,
non-cancelled scheduling or the two-worker architecture. Nothing is registered,
signed or tested in live WoW. No certificate or registry action is performed.

## Verified sources and behavior

**VERIFIED SDK:** Windows SDK 10.0.26100.0 `um/sapiddk.h` defines
ISpTTSEngineSite::GetActions and SPVESACTIONS:

| Flag | Value | Related call |
| --- | ---: | --- |
| SPVES_ABORT | 1 | Abort the current Speak |
| SPVES_SKIP | 2 | GetSkipInfo / CompleteSkip |
| SPVES_RATE | 4 | GetRate |
| SPVES_VOLUME | 8 | GetVolume |

GetActions returns a DWORD action mask; Write returns an HRESULT and consumed
byte count. SDK declarations alone do not specify Pocket's abort return code.

**VERIFIED Chatterbox (read-only primary reference):**

- `../wow-chatterbox-sapi/sapi/src/adapter.cpp`, Engine::Speak, its `actions`
  and `finish` lambdas, async generation wait, Write loop and GenerationCancelled
  catch: entry polling; 10 ms waits; caller-thread controls/output; request-owned
  std::stop_source; request_stop on abort; always join; S_OK for an observed
  SAPI abort; E_ABORT for otherwise cancelled generation. Write errors and
  SP_AUDIO_STOPPED propagate separately.
- Its actions lambda calls GetRate/GetVolume without transforming audio;
  GetSkipInfo and CompleteSkip(0) acknowledge unsupported seeking. It polls
  after generation and on exit too.
- `sapi/tests/host.cpp`, Site::GetActions/Write and main tests: timed generation
  abort, entry abort, delivery abort, partial/error behavior and caller affinity.
- `native/include/chatterbox/t3.hpp`, `audio.hpp`: actual token-aware APIs;
  `native/src/t3.cpp`, T3Runtime::Generate, BeginDecode, T3DecodeState::Step:
  cancellation before/around generation and decode operations.
- `native/src/audio.cpp`, HiFTRuntime::GenerateInput boundary checks, and
  `native/src/s3.cpp`: stop-token propagation through subsequent stages.

No Chatterbox Sleep/try-lock loops are copied. Pocket uses timed CV waits for
the request gate and delivery, and callback notifications for native waits.
Inspected primary source hashes are recorded in ignored
`generated/perf/2n-reference-hashes.json`; the reference project is unchanged.

## Ownership, public API and outcomes

**Pocket choice:** one std::stop_source in RequestControl for each Speak.
It owns the shared atomic stop state. Tokens borrow/share that state through
Delivery, UtterancePipeline and WorkerPipeline. No global cancellation flag,
worker replacement, process helper, detached thread or additional neural worker.
Only the caller invokes site methods and mutates RequestControl::aborted.
Native threads only query stop_requested(). Repeated abort flags invoke
request_stop once; std::stop_source itself is also idempotent.

```
Speak caller: GetActions -> SPVES_ABORT -> request_stop()
    -> Delivery callback: notify delivery CV
    -> active WorkerPipeline callback: notify queue CV
    -> producer/consumer observe stop and finish their jobs
    -> native coordinator observes cancellation / prevents later segments
    -> coordinator marks done, caller joins
    -> Speak returns S_OK, resident runtime remains
```

The backward-compatible default token is empty:

```cpp
WorkerResult WorkerPipeline::run(std::stop_token cancellation = {});
UtteranceResult UtterancePipeline::run(UtterancePCMSink sink, void* context,
    const GenerationObserver* observer, std::stop_token cancellation = {});
```

WorkerResult and UtteranceResult have SynthesisStatus::completed/cancelled.
Cancellation returns an explicit prefix result, never a fake full completion.
An utterance copies only recorded segment results; no unstarted segment is
reported. Prefix counters describe native work, not a promise of delivered SAPI
bytes; the site byte ledger is authoritative for delivery. A cancelled worker
does not validate fixture EOF or expose a successful completed-state view.
Failures continue to use the existing exception boundary, which waits for both
workers before rethrowing. No C++ exception escapes COM.

**SAPI result:** an observed SPVES_ABORT normally returns S_OK, exactly as the
verified Chatterbox path. Native failure or a Write error remains an error even
if abort is also observed. An unexpected native cancellation without a SAPI
abort would return E_ABORT. SP_AUDIO_STOPPED retains its original HRESULT and
is not silently converted into GetActions cancellation.

## Polling and thread affinity

The Speak caller polls:

1. At request entry, before gathering text or starting native work.
2. Every 10 ms CV wait while another Speak owns the engine request gate.
3. After collecting supported text, before creating the coordinator.
4. On frame-ready/completion notifications or each 10 ms delivery wait timeout.
5. Immediately before every Write, including each partial retry.
6. After a complete frame's Write, BEFORE acknowledging the native sink, so a
   stop signalled by that Write cannot release the next segment first.
7. When native completion is observed, including late abort at natural EOS.

GetActions, GetRate, GetVolume, GetSkipInfo, CompleteSkip and Write stay on the
Speak caller thread; the checker verifies affinity. Lazy asset/runtime creation
and native prepare/run execute on the joined coordinator, so controls can be
observed while those calls execute. Asset validation and text preparation are
not forcibly interrupted internally; stop is checked before/after them.

After abort, the caller's wait predicate remains frame-ready OR done, not the
latched stop bit: it waits for actual quiescence without spinning. A queued
delivery frame is acknowledged/discarded without Write. The coordinator always
joins before request gate release and before the extra engine reference drops.
Token reassignment waits for the active request to finish; it does not destroy
the resident runtime as part of cancellation.

## Native safe points and wake-up graph

Producer checks before text prefill, after prefill before discarded sampling,
at each autoregressive candidate boundary, after candidate compute before
emission, and before/while publishing into the bounded queue. A currently
executing prefill/candidate finishes; the next candidate is not started once
stop is observed. No dense/convolution kernel is interrupted or modified.

Consumer queue predicates include stop; it checks before decoding an acquired
latent, after decode and before publishing PCM. Queued future latents are not
decoded/delivered after stop is observed. A running Mimi decode may finish but
its PCM is discarded. The Delivery sink checks before conversion/publication
and both of its rendezvous waits include stop.

Orchestration checks before each segment and after worker prepare, propagates
the same token to run and breaks on a cancelled worker. Cancellation between
segments prevents the next prepare/dispatch. Continuous RNG state after an
aborted request is discarded; the next prepare/run reseeds once as before.
Neural resets still run on their owning resident workers. No cancellation flag
survives into the next request.

| Wait | Wake/termination |
| --- | --- |
| Producer waiting for queue space | Stop callback locks queue mutex, then notify_all; predicate sees stop |
| Consumer waiting for latent data | Same callback/predicate; queued items are ignored on stop |
| Consumer sink waiting for delivery acknowledgement | Delivery stop callback locks delivery mutex, notify_all; sink returns |
| Caller waiting for PCM | Timed 10 ms wait or delivery notification; polls only on caller thread |
| Native coordinator waiting for worker completion | Both workers acknowledge completion and notify finished; wait never returns early just because stop is set |
| Caller waiting for coordinator shutdown | done notification, then join; coordinator cannot wait forever on a cancelled queue/sink |
| Idle resident workers waiting for jobs | Remain resident/idle; later prepare dispatches a fresh reset epoch |
| Another Speak waiting for engine gate | Timed CV action polling; request completion releases gate and notify_all |

Stop callbacks synchronize with each wait's mutex to close the predicate/sleep
lost-notification window. Worker callback registration occurs BEFORE taking
the queue mutex: an already-stopped token can invoke a callback synchronously.
Callbacks only notify; they do not mutate caches/results or perform site calls.
They detach before the request scope ends. Exceptions still take precedence
and wake the existing failure path.

No native timed polls, Sleep loops, spinning user-code loops or per-frame
cancellation allocation are introduced. Normal queue capacity remains two and
generation/decode operations are unchanged.

## Writes, controls and races

An abort observed before Write suppresses that frame. If only part of a frame
was written, the next action poll stops the remaining bytes immediately; there
is no convenience completion of the 3840-byte frame. Bytes already accepted
cannot be recalled. An executing site Write cannot be preempted by this caller;
the next retry/wait polls once it returns, matching caller-affinity constraints.

Skip policy: call GetSkipInfo, then CompleteSkip(0); no word/sentence seeking.
Rate/volume policy: call GetRate/GetVolume; no resampling, gain, pitch or neural
prosody transformation. Control-query HRESULT handling matches Chatterbox's
observational policy. These flags do not alter canonical bytes.

Race coverage distinguishes deterministic safe-point tests from operations
that are deliberately non-preemptible:

| Race | Evidence/handling |
| --- | --- |
| Before native prepare | Entry abort suppresses coordinator; pre-stopped native run has zero draws/frames |
| First-segment prefill | Timed first-PCM abort; native barriers before and after prefill prevent subsequent work |
| Candidate computing | Candidate-complete barrier requests stop before publication/next candidate |
| Producer/full queue | Deterministic full-queue barrier with consumer sink held; cancellation wakes and joins |
| Consumer/empty queue | Deterministic empty-queue barrier; cancellation wakes and joins |
| Mimi decoding | Barriers before decode and after decoded PCM; current operation finishes without unsafe interruption |
| Native frame awaiting delivery | Streaming abort wakes Delivery; no next Write after observed abort |
| Immediately before Write | Caller action checks suppress delivery |
| Mid-partial Write | Exactly 126 bytes accepted, then no remaining retry |
| Segment boundary | Native barrier proves segment 2 never starts; SAPI abort after frame 46 also checks recovery |
| Near EOS | SAPI abort after frame 169 stops final delivery |
| Natural completion/late abort | Abort after frame 170 returns S_OK with all canonical bytes delivered |

The frame-46 SAPI trigger is polled before sink acknowledgement. The separate
native boundary barrier additionally proves that a stopped token prevents the
next segment's prepare/dispatch. Kernel mid-instruction interruption
is intentionally unimplemented; timed abort checks cooperative latency, not an
exact instruction location. Repeated stop observations do not double-join or
change COM counters.

## Focused validation and residency

Existing native utterance checker adds eight deterministic, test-only barriers
plus a pre-cancelled request. `engine/src/cancellation_audit.hpp` is compiled
only under OUTLOUD_UTTERANCE_CHECK; production library/DLL and normal observation
contain no probes. Barriers use CVs and callback/queue state, not sleeps.
Each cancellation is followed by exact full canonical FP32 recovery on the same
pipeline. Existing normal RNG rows, EOS/lifecycle checks and zero hot-allocation
audit remain intact. No new CTest, stress/soak suite or profiler.

The registration-free fake SAPI host tests entry, 20 ms first-PCM-wait trigger,
frames 1/10/45/46/70/169/170, and a partial Write. Every cancellation is followed
by full canonical PCM16 recovery on the SAME engine. It checks module lifetime,
partial/error handling, repeated abort flags and controls without transformation.

Read-only VirtualQuery/K32GetMappedFileName checks retain the exact model/voice
allocation bases. In this isolated checker, the two compute workers are
identified after canonical synthesis by dominant non-caller accumulated thread
CPU time; their IDs must remain live after cancellation and recovery. Other
Windows loader/service threads can appear or expire and are not treated as
native worker recreation. Tokenizer residency follows the unchanged Resident
owner: cancellation never resets/reconstructs it. No production diagnostic
interface, resident identifier, hook or registry access is added for this check.

Native API validation passed with the original canonical FP32 SHA256:
`36788a2c1cfde712fb9ef68151a93b1b78eec40b1e440e99ff1b1b59d1a8bcf0`.
Non-aborted SAPI remains **170 frames / 326400 samples / 652800 bytes /
13.600 seconds**, 170 normal Writes, with exact native PCM16 equality.
Initial Release build and 12/12 CTests passed once (27.36 s). Final Release x64
build and 12/12 CTests passed once (27.31 s). The native full-utterance checker
passed once with all eight new barriers, pre-cancelled request and exact recovery;
the final registration-free host passed all ten abort cases, normal/partial PCM,
Write errors, controls, residency checks and DllCanUnloadNow=S_OK. No blocked
worker/coordinator remained. All seven scripts parsed successfully; read-only
inventory/PE/Microsoft-signature helpers passed for all six shipped images.
No new CTest/performance gate and no profiler. Raw JSON is under
`generated/perf/2n-*.json`.

## Light latency and normal-path observations

One final correctness observation per trigger, measuring from the flag becoming
observable (frame/byte counter or timer deadline) to Speak return with the
coordinator quiesced/joined:

| Trigger | Delivered bytes | Abort-to-return, ms |
| --- | ---: | ---: |
| Entry | 0 | 0.0019 |
| First-PCM wait, timer at 20 ms | 0 | 39.6132 |
| After frame 1 | 3840 | 3.8751 |
| After frame 10, segment 1 | 38400 | 0.0853 |
| After frame 45 | 172800 | 0.0829 |
| After frame 46, segment boundary | 176640 | 0.0884 |
| After frame 70, segment 2 | 268800 | 0.0861 |
| After frame 169, near EOS | 648960 | 0.0874 |
| Late natural-end abort | 652800 | 0.0856 |
| Partial Write | 126 | 2.9450 |

No additional latency repetitions were requested. These individual cases are
not a repeated-run distribution, so no mean/min/median/max is inferred. First
PCM cancellation can wait for the currently executing prefill; streaming abort
often finds workers backpressured on the delivery/queue and quiesces quickly.
The post-full-frame poll runs before acknowledgement to prevent releasing the
next segment first. No kernel optimization follows from these observations.

One untimed warm-up plus ONE normal canonical Speak observation (no profiler,
no additional benchmark repetitions) measured **84.7770 ms first Write** and
**3014.3947 ms total Speak**, with 170 Writes and 652800 bytes. Step 2M's
separate-session medians were 91.2425 / 3818.1258 ms. First output remains in
the same tens-of-milliseconds range and total synthesis in the same few-seconds
range; this single session does not establish a speedup or controlled regression
bound. Conversion, controls and stop checks are included. JSON/capture validation
remain outside the Speak clock; the timed site owns no complete PCM buffer.

## Memory and deployment

This MSVC x64 build measures std::stop_source and std::stop_token as **8 bytes
each**, with **32-byte shared stop state** (implementation-specific, excluding
allocator overhead). One shared state allocation per Speak; token copies and
stack callback registration add no per-frame allocation. RequestControl also
contains the site pointer and one caller-owned bool (24 bytes with x64 padding);
Delivery adds a token and native status. Each pointer-capturing stop callback is
**40 bytes** on the stack. One wakes Delivery; one active worker-run callback
wakes the native queue. No exception allocation is needed for normal
cancellation. Existing error exceptions are unchanged.

Measured persistent native storage changes: orchestration object **2864 ->
2872 bytes**, worker inline object **97056 -> 97064 bytes**: one additional
8-byte token handle in each. The status field fits WorkerResult's prior padding.
The engine adds a **72-byte request-gate CV** and active bool/padding (80 bytes
including the next pointer's alignment), so waiting concurrent Speaks can poll
abort. This is control state, not PCM storage. Existing one-frame
3840-byte PCM16 rendezvous remains; no complete output buffer or new neural
scratch/cache allocation. OS/allocator backing overhead was not profiled.

**Verified rebuilt dependency closure:** std::stop_token callback synchronization
adds `msvcp140_atomic_wait.dll`. Its own imports are the already shipped VC
images, Windows ADVAPI32.dll/synch API set/KERNEL32 and UCRT. The System32 copy
is x64 with a valid Microsoft signature. Inventory/copy/verification allowlist
now covers six PE images: project DLL/checker plus FOUR Microsoft VC runtime
DLLs. No external neural runtime. Preserve all four vendor signatures; sign
only project binaries. The new cancellation document is included in bundles.
No registry/CLSID/token/certificate selection or signing architecture changes.
Source/bundle hashes are calculated afresh by manual copy/sign scripts.

## Manual work still remaining

After review/commit by the user: prepare a fresh deployment, sign project copies
using an existing certificate, verify all signatures, manually register the
Pocket engine/voice, test Windows SAPI playback/abort, then WoW
`C_VoiceChat.SpeakText` and StopSpeaking. Fake-host tests do not prove live WoW
integration; current blocking site Write behavior and actual game callbacks
remain to be verified manually.

SIGNING WAS NOT EXECUTED. REGISTRATION WAS NOT EXECUTED.
UNREGISTRATION WAS NOT EXECUTED. REGISTRY WAS NOT MODIFIED.
NO CERTIFICATE WAS CREATED, IMPORTED OR INSTALLED. REGSVR32 WAS NOT RUN.
WOW WAS NOT LAUNCHED OR STOPPED. No registered voices were changed.
Nothing was staged, committed or pushed. Stop after Step 2N.
