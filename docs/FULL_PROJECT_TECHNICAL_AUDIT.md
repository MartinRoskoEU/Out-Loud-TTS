# Full project technical audit

> Historical audit at its recorded baseline. Subsequent cleanup, retirement, stabilization, A06/A08/A09 and A11/A12 address several findings; the original conclusions and measurements below are preserved. Use [README](../README.md), [current architecture](ARCHITECTURE.md) and [A17](DOCUMENTATION_FINALIZATION_A17.md) for current navigation/status.

Audit date: 2026-10-09. Source baseline: `046385a` (`Updated registraction to
the registry`). The working tree was clean at entry. This baseline includes the
exact-name SAPI registration change; the earlier registry audit's `2b1ee6f`
baseline is historical. This review implements no proposed fixes.

Evidence labels: **CONFIRMED** means supported by current source or an executed
check; **LIKELY** means a concrete source-supported failure scenario whose full
outcome was not reproduced; **UNVERIFIED** means no present runtime evidence.
Performance classifications are independent of finding priority/confidence.
Line references below describe this baseline and can move after subsequent edits.
The review covers component boundaries, production control/data paths, parsers,
kernels, test entry points, scripts, tracked inventory and supporting documents.
It is not an exhaustive proof of all execution paths or a penetration test.

## 1. Executive summary

The architecture is consistent and worth preserving: native inference, managed
token coordination, the read-only frontend ABI, a one-shot elevated helper, and
WPF are distinct layers. Production inference has no Python, PyTorch, external
neural runtime or frontend UI framework dependency. The resident two-worker
pipeline, bounded streaming, strict assets and explicit ownership checks are
strong foundations. No P0 issue was established.

There are **18 findings: P0 0, P1 4, P2 12, P3 2**. Seventeen are CONFIRMED
source/build/workflow gaps or opportunities; A02 is LIKELY for the interruption
scenario, with its missing journal established by source. These counts include
release blockers and optimization opportunities, not eighteen reproduced defects.

The most important behavioral issues are the missing upgrade workflow for old
prefixed names, removal recovery across process interruption, and the existing
false conflict from equivalent runtime path spellings. A PowerShell validation
launcher failure was reproduced. Release packaging and distribution attribution
are incomplete for the full current product. No mass refactor is justified.

Release x64 native and WPF builds passed. **8/8 selected native CTests**,
**155 frontend assertions**, **49 fake-COM script assertions**, and in-memory WPF
view validation with an explicit build directory passed. The default
`powershell -File` view-validation invocation failed before validation; both
that failure and the successful explicit-path invocation are retained.

No production library, installed voice, registry entry, installed SAPI DLL,
certificate, game process or speech setting was changed. Build outputs are local
workspace artifacts. No dependency was installed/downloaded, deployment/signing/
registration script executed, or Git staging/commit/push performed.

## 2. Current architecture assessment

The pre-audit tracked inventory contained **224 files**, including **104 C++
source/header/implementation files** and **32 Python/PowerShell scripts**.

| Layer/components | Boundary and dependency assessment |
| --- | --- |
| `outloud_asset_io`, `outloud_assets` | Read-only mappings, BCrypt SHA-256, pinned model/schema and OLV views. Shared immutable ownership. |
| `outloud_text` | Offline tokenizer and upstream-compatible preparation/splitting. No Python runtime calls. |
| `outloud_cpu`, optional `outloud_cpu_avx2` | Validated FP32 operations over BF16 weights; CPUID/OS-state dispatch. AVX2 flags confined to its own target. |
| `outloud_flowlm`, `outloud_flow_sampler`, `outloud_generation`, `outloud_noise` | Backbone/state, acoustic sampling, stop policy and deterministic production noise. |
| `outloud_mimi`, `outloud_workers`, `outloud_orchestration` | Decoder state, two resident workers, bounded latent queue, sequential text segments with continuous request RNG. |
| `OutLoudPocketSAPI.dll` | COM activation, token configuration, caller-thread SAPI controls/PCM/bookmarks; core libraries statically linked. |
| `outloud_voice_import`, `outloud_voice_library` | Prepared safetensors validation/conversion, strict metadata, IDs, storage, locks, staging, replacement/removal. |
| `outloud_sapi_voices` | Owned token schema, scope, runtime checks and transactional Registry64 adapter. Separate from inference DLL and storage. |
| `OutLoudVoiceManagerNative.dll` | Read-only C ABI with request-owned immutable snapshot storage. No mutation export. |
| `OutLoudVoiceImportHelper.exe` | One-shot authenticated pipe, bounded protocol, fixed production roots/scope and coordinated mutation. Compiled asInvoker; explicitly launched via runas. |
| WPF Voice Manager | View -> ViewModel -> service -> bridge; mutation service invokes helper; Voice Tester uses an isolated STA SAPI session. `net10.0-windows`, x64, asInvoker. |
| CLIs | `outloud-voice-import`, `outloud-voice-library`, `outloud-sapi-voices`; each has an explicit narrower administrative/development purpose. |
| Research/tools | tokenizer/assets/neural exporters; oracle/baselines; stage checkers, RNG/synthesis tools, perf/profile/trace executables. |
| Validation | Native CTests, standalone correctness tools, fake-service .NET executable, fake-COM PowerShell regression, supplementary WPF BAML/layout checks. |
| Deployment | Explicit manual prepare/sign/verify/register/unregister scripts for the older development SAPI bundle. |

The CMake link graph has no demonstrated circular target dependency. The apparent
workers/generation/noise relationships are a directed graph, not a circular
runtime pipeline. Pimpl, immutable snapshot copying and separate error/protocol
DTOs have concrete lifetime/ABI purposes. No framework or architecture replacement
is recommended.

Generated exports, fixture headers, assets, build outputs, captures and prior
audit diagnostics live under ignored `generated/`, `build/`, UI `bin/obj`, or
ignored model/voice directories. Tracked JSON baselines and schema records are
intentional reproducibility inputs, not accidentally tracked model weights.

## 3. Confirmed bugs

### A05 â€” P2 â€” equivalent runtime path spellings still cause false conflicts

**CONFIRMED; component: SAPI diagnostics/coordination.** Locations:
`frontend/mutation/mutation.cpp:83â€“92`, `tools/sapi-voices/main.cpp:70â€“72`,
`sapi/src/voice_tokens.cpp:111â€“119,162â€“170,222â€“230`.

The helper constructs model/tokenizer paths with `root / L"runtime/filename"`;
the CLI uses `lexically_normal()`. `desired()` serializes their native strings,
and inspection/replacement/update compare registry trees by raw equality.
Equivalent Windows paths can therefore produce `conflict` or refusal. The
previous isolated reproduction in `SAPI_VOICE_REGISTRY_AUDIT.md`, section 11,
matches this unchanged source. This audit did not re-query live registrations;
it does not assert their current status or identify a WoW visibility cause.

Recommendation: centralize normalized runtime construction, and compare the
known path fields using a carefully bounded local-path equivalence policy that
also handles already-stored mixed-slash strings. Keep REG_SZ type/schema,
ownership, allowed-path and reparse protections; never normalize arbitrary token
fields or silently rewrite tokens. Benefit: truthful diagnostics and compatible
cross-caller operations. Complexity medium; regression risk medium. Required
tests: mixed slashes/case, existing stored spelling, genuinely different paths,
wrong types, unknown fields, unsafe paths, inspect/update/replacement. Dependency:
coordinate with A01 before recommending legacy-token updates.

### A14 â€” P2 â€” default PowerShell validation launcher fails under `-File`

**CONFIRMED; component: build/validation tooling.** Location:
`ui/validation/validate-with-runtime.ps1:4â€“5`. Similar default expression exists
at `ui/build.ps1:2â€“3`; that script's failure was not independently executed.

On Windows PowerShell **5.1.26100.9444**, running
`powershell -NoProfile -File ui/validation/validate-with-runtime.ps1 -Configuration Release`
failed with `Join-Path: Cannot bind argument to parameter 'Path' because it is an
empty string`. `$PSScriptRoot` was empty during parameter-default evaluation.
Passing the absolute `-NativeBuildDirectory` let the same validation pass.

Recommendation: leave the parameter default unset and resolve its script-relative
fallback in the script body. Audit other script parameter defaults without
executing deployment actions. Benefit: reliable documented launch routes.
Complexity low; risk low. Required checks: `-File`, direct `&` invocation, explicit
directory, path containing spaces, both configurations. Dependencies none.
PowerShell syntax parsing alone does not catch this binding-time failure.

## 4. Security findings

The inspected production boundaries have substantial protections:

- `.olp` has fixed size/schema/pinned identities and bounded records; OLV has
  exact dimensions/payload/integrity and rejects nonfinite floats
  (`engine/src/assets.cpp:25â€“45,63â€“94,114â€“135`).
- Safetensors import bounds the header to 64 KiB, payload to 1 GiB, requires
  exactly eighteen records, validates shapes/offsets/contiguity/equal prefixes
  and finite cache values (`engine/src/voice_import.cpp:36â€“38,125â€“205`).
- JSON validates UTF-8, duplicate keys, surrogate pairs, depth/node/member
  counts; metadata has an exact schema (`engine/src/voice_json.hpp:20â€“147`,
  `engine/src/voice_library.cpp:123â€“160`). No unchecked input-derived tensor
  offset was established in these reviewed parsers.
- Managed storage rejects traversal/device/UNC/ADS/ambiguous names, pins
  ancestors without delete sharing, and rejects reparse/hard-linked files
  (`engine/src/voice_library_fs.hpp:59â€“137`). Machine anchors/files require
  SYSTEM/Administrators ownership and constrained ACLs
  (`engine/src/voice_library_security.cpp:44â€“89,130â€“162`).
- Helper transport uses generated endpoints, first-instance/local pipe mode,
  a protected caller/Admin/SYSTEM DACL, client/server PID checks, live parent
  handle/session/image-location checks and identification-level SQOS. Source
  paths and names are transmitted in a bounded frame, not command arguments
  (`ElevatedVoiceImportService.cs:49â€“95`, `helper.cpp:47â€“82`,
  `mutation.cpp:48â€“70`). The helper accepts no arbitrary registry/library root.
- Token deletion requires the owner marker, ID, CLSID, strict values and
  children; Registry64 transaction compare-and-exchange propagates failure
  (`sapi/src/voice_tokens.cpp:25â€“39,235â€“241`,
  `sapi/src/voice_registry.cpp:160â€“176`). No fallback best-effort writes.

No exploitable privilege escalation or buffer overflow was demonstrated. The
pipe checks authenticate process/session association; they do not independently
establish publisher trust for user-writable binaries. Public release trust is A03,
not a reason to weaken or remove the current checks. Real elevated transport,
registry transactions and hostile-process races were not executed in this audit.

### A10 â€” P2 â€” native speech input has no aggregate resource bound

**CONFIRMED; component: native SAPI input.** `sapi/src/support.hpp:34â€“69`
collects all fragments into text, bookmark and visited-node containers with no
aggregate character/fragment/bookmark cap. It detects cycles, invalid pointers
where lengths require data, invalid Unicode and bookmark NULs. Cancellation is
polled before and after collection, not during it (`adapter.cpp:269â€“290`).
The frontend's 12,000-character limit (`Models/VoiceTest.cs:9â€“15`) protects only
Voice Tester; other SAPI hosts bypass it. Very large caller-supplied input can
consume memory and prolong the noninterruptible preparation window. This is a
local robustness/denial-of-service surface, not demonstrated code execution.

Recommendation: define aggregate native request limits and checked accumulation,
including metadata-only fragments, and reject before allocating beyond them.
Preserve legitimate normal text and existing splitting semantics. Benefit:
predictable memory and failure latency. Complexity low/medium; risk medium for
long host inputs. Required tests: exact limits, overflow-safe length sums,
fragment cycles, Unicode crossing fragments, many bookmarks, ordinary canonical
text and aborted collection. Dependencies: choose the host-facing policy first;
do not silently truncate input.

## 5. Reliability and resource-lifetime findings

`Resident` destroys the utterance pipeline before tokenizer/voice/model mappings
(`adapter.cpp:94â€“101`). Workers join on destruction, wait for both request
completions before throwing, and reset on their owning workers
(`worker_pipeline.cpp:85â€“126,238â€“285`). Production callers serialize requests;
the native API expressly permits concurrent `request_stop()` only, not concurrent
prepare/run/destruction (`worker_pipeline.hpp:52â€“53`). Unsynchronized guards in
that coordinator-owned API are not evidence of a supported-call data race.

Stop callbacks synchronize with wait mutexes and notify; stopped queues do not
drain future work. Delivery is a single 3,840-byte PCM16 frame and caller-thread
Write retry loop. It waits for genuine coordinator completion and always joins.
No confirmed abandoned worker or exception crossing COM/C ABI was found.

Voice Tester owns session RCWs on one STA, releases SpVoice before tokens and
completes the backend task only after disposal/thread join
(`SapiTestSessionFactory.cs:76â€“104`, `SapiVoiceTestBackend.cs:21â€“32,45â€“62`).
This source path should release the Resident and voice mapping after each test;
fresh real playback/mapping-release verification was not performed. Fake COM
lifetime assertions passed. A live SAPI call or `WaitUntilDone` that never
finishes can still prevent Stop/close completion: both normal and dispose loops
wait until quiescence. This is an **UNVERIFIED external hang scenario**, not a
reproduced deadlock. Add observability/manual hung-output validation before any
change; do not force-kill workers or claim resources are released on a timeout.

### A02 â€” P1 â€” coordinated removal lacks durable interruption recovery

**LIKELY failure outcome; source gap CONFIRMED; component: removal/recovery.**
`engine/src/voice_library.cpp:546â€“560` detaches the entry to a random trash path
before calling the registration callback. Exception rollback at `562â€“576`
works only while this process executes. Unlike replacement's journal
(`voice_library_replace.inc:1â€“6` and `replace_prepared`), removal writes no
original-ID/phase/commit record. `cleanup_pending`, at `590â€“614`, can delete a
recognized trash pair without checking a token or distinguishing a failed
coordinated removal from postcommit cleanup.

An interrupted helper between detach and token deletion can leave an owned token
pointing at the old, absent active directory. A later generic cleanup could
discard its retained voice pair. The same lack of distinction affects trash
retained after a reported restoration failure. Ordinary rollback/partial
warnings are already covered by passing injected tests; process interruption
at this point was not tested and no real orphan is asserted.

Recommendation: persist a bounded owned removal journal before detach, recording
identity, original location and phase; recover/reconcile token outcome before
deleting retained originals. Do not make the storage layer secretly own registry
policy: the coordinator should supply it. Benefit: safe recovery and actionable
unknown outcomes. Complexity medium/high; risk high if implemented carelessly.
Required tests: interruption after journal/detach/token commit, restart/repeated
recovery, missing/conflicting token, restore failure, cleanup retry, in-use files,
and malicious/unexpected journal/files. Dependencies: define durable commit and
recovery ownership, retain existing exception behavior and privilege separation.

## 6. Duplicated code

### A15 â€” P2 â€” frontend error conversion is genuinely duplicated

**CONFIRMED; component: frontend native adapters.**
`frontend/native/voice_manager.cpp:18â€“42` and
`frontend/mutation/mutation.cpp:30â€“46` repeat `library_cause` and both
`convert` implementations, copying the same library/import/SAPI fields. Their
output structs differ and mutation additionally carries duplicate IDs. The
duplicated *field conversion* can drift without being a necessary validation
boundary.

Recommendation: retain shared conversion in a small private frontend helper,
then adapt separately to the read-only ABI view and mutation wire DTO. Keep
ABI/protocol schemas and entry-point catches separate; do not expose mutation
functions through the bridge or relocate this into inference. Benefit: one
error-field mapping policy. Complexity low/medium; risk low/medium. Required
tests: all structured nested causes, path/Win32/tensor fields, duplicate IDs,
OOM/no-exception ABI behavior. Dependencies none.

### A18 â€” P3 â€” test scratch cleanup repeats the same implementation

**CONFIRMED; component: test helpers.** `sapi/tests/scratch.hpp:5â€“26` and
`tests/voice_library_test.cpp:48â€“67` implement the same unique temp-root and
reparse-aware recursive cleanup pattern. `tests/voice_import_test.cpp:44â€“55`
also owns a unique scratch directory with a separate cleanup implementation.

Recommendation: retain the carefully contained/reparse-aware implementation in
a test-only common utility, with suites retaining their local assertion adapters.
No production dependency on test code. Benefit: consistent cleanup safety;
complexity low, risk low. Required tests: unique containment, junction itself
removed without touching its target, locked-file cleanup, constructor failure.
Dependencies none; defer until stabilization defects are resolved.

Similar validation in WPF, protocol, elevated helper, storage and registry
adapter is deliberate defense at different trust boundaries. Likewise C# and
C++ wire serializers are required cross-language peers. Do not consolidate those
just because their field lists resemble each other. Stage reference generator
and resident worker loops are separately exercised implementations used for
parity evidence; they are not dead duplicate production engines.

## 7. Unused or obsolete code

No production source/class was established as safely removable. CMake targets,
script/document references and dynamic entry points were considered: the SAPI
exports/DEF, native P/Invoke exports, helper wWinMain, XAML handlers and views
are runtime roots that a text search of C++ calls alone would miss.
`FlowLMGenerator` remains used by stage/generation and worker parity tools;
`verify_mimi_one_frame.py`, text-case generation and perf analyses have documented
consumers. Conditional RNG/orchestration checkers depend on generated reference
headers and retain useful correctness evidence.

User-scope support and the legacy development token are explicit compatibility/
diagnostic paths. Their absence from the machine UI does not make them obsolete.
The ignored previous audit harnesses are local artifacts, not tracked shipping
code. No file deletion is recommended without a specific consumer inventory.

## 8. Performance and memory opportunities

No fresh performance campaign/profiler or inference-quality experiment was run.
Existing Step 2I measurements describe their recorded fixture/session, not the
latest voices or a controlled current regression bound. No speedup is promised.

### A06 â€” P2 â€” refresh repeats full voice validation

**CONFIRMED; SOURCE-SUPPORTED OPPORTUNITY; component: bridge/library.**
`frontend/native/voice_manager.cpp:96â€“99` first calls `library.list_voices()`
then calls `manager.inspect(id)` for every returned row. That inspection calls
`voice(id)`/`library.inspect_voice(id)` again (`voice_tokens.cpp:104â€“108,154`).
Each inspection validates metadata, maps OLV and calls `validate_olv`
(`voice_library.cpp:200â€“220`), which repeats integrity/payload hashes and
finite-value scans. `replacement_pending` also repeatedly scans staging per row.
Work grows with voices, bytes and pending entries; real refresh latency was not
measured here.

Recommendation: inspect token identity against an already-validated row within
one suitably scoped read snapshot, building a bounded pending-journal index once.
Keep fresh privileged mutation checks and file/security identity checks.
Benefit: fewer mappings/hashes/JSON scans and a more coherent list/status view.
Complexity medium; risk medium if snapshot freshness is weakened. Required tests:
instrumented validation counts, retained security/health failures, concurrent
mutation yielding coherent/busy results, selection and row error semantics.
Dependencies none; do not substitute persistent unchecked metadata caching.

### A07 â€” P2 â€” Mimi allocates KV capacity for the full segment

**CONFIRMED; SOURCE-SUPPORTED OPPORTUNITY; component: decoder memory.**
`engine/src/mimi.cpp:18â€“20` allocates four FP32 KV vectors sized
`maximum_frames * 16 * 512`. `cpu/mimi_kernels.cpp:112â€“146` attends to only the
latest 250 positions per query. At the default 128-token capacity, estimated
maximum frames is 559: KV backing alone is **73,269,248 bytes (~69.9 MiB)**.
This is calculated from allocation sizes, not an RSS observation. Retained
capacity is intentional and avoids hot allocations.

Recommendation: consider bounded KV storage of at least a context plus frame's
append requirements, with absolute positions maintained and chronological
accumulation order preserved. Benefit: substantially less per-Resident KV
storage. Complexity high; risk high because state layout/inspection fixtures
change. Required tests: every frame vs current path, window-wrap boundaries,
absolute RoPE positions, long segments, reset/reuse/cancellation, both ISAs,
canonical PCM and listening. Dependencies: define diagnostic-state view
compatibility. Do not replace the cache merely on the strength of this estimate.

### A08 â€” P2 â€” immutable transpose weight preparation repeats per frame

**CONFIRMED; MEASURED BOTTLENECK in historical Step 2I; current benefit UNVERIFIED.**
`engine/src/cpu/mimi_kernels.cpp:78â€“87` gathers/widens immutable transpose
weights into phase rows for every frame. The final historical profile in
`docs/PERFORMANCE_OPTIMIZATION_2I.md:156â€“215` attributes **124.109 ms** across
the three transpose groups to explicit preparation; the model/phase layouts
remain source-visible. The profile also identifies dense linear (~50%) and
SEANet operations as large families, not queue bookkeeping.

Recommendation: prepare the same FP32 phase rows once per model/decoder owner,
subject to an explicit memory budget; retain current dot ordering and carry/bias
arithmetic. Benefit: removes repeated gathers/widening. Complexity medium; risk
medium. Required tests: bitwise converted rows, exact portable/AVX2 per-frame
and final PCM, all configurations, carry reset/recovery; one warm-up plus five
uninstrumented runs per comparison, median primary, no profiler/hooks in timing.
Dependencies none; do not introduce precision/reduction/sampling changes.

### A09 â€” P2 â€” prefill allocations and invariant sampler work can be reused

**CONFIRMED; SOURCE-SUPPORTED OPPORTUNITY; component: FlowLM/sampler.**
`FlowLMBackbone::embed/prefill`, `engine/src/flowlm.cpp:66â€“94`, creates embedding
and normalized-result vectors each prefill even though the Resident is reused.
`FlowSampler::sample`, `flow_sampler.cpp:42â€“66`, recomputes fixed s=0/t=1 time
embeddings from the immutable model for each draw. After combining the condition,
`work.condition_silu = work.y; silu(...)` repeats in every block and the final
projection (`77,86`) although `work.y` is unchanged. Existing hot-loop audit
begins after prefill (`worker_pipeline.cpp:147â€“149`); zero audited hot allocations
does not mean zero allocations per complete request.

Recommendation: reuse bounded per-owner prefill vectors and optionally cache
the exact fixed time vectors; compute unchanged condition SiLU once per sample.
Benefit: less repeated allocation/computation, potentially lower first-audio
overhead. Complexity medium; risk medium. Required tests: embeddings/hidden/EOS,
sampler checkpoints, discarded sample and RNG schedule, full canonical PCM,
reuse/failure/cancellation, observers; measure first PCM before claiming benefit.
Dependencies none; do not expand a borrowed callback view's lifetime or change
numerical operation order.

Fusing kernels, changing reductions, FP16/INT8, additional compute threads, queue
capacity changes and replacing synchronous SAPI delivery are **UNVERIFIED
HYPOTHESES** for this baseline. They have larger correctness/scheduling risk and
no new evidence justifying implementation. Existing documents already warn that
stage medians do not add and instrumented profiles are not benchmarks.

## 9. SAPI and Voice Library assessment

### A01 â€” P1 â€” old prefixed tokens have no Voice Manager reconciliation route

**CONFIRMED source consequence; component: naming/upgrade workflow.**
`desired`, `sapi/src/voice_tokens.cpp:111â€“120`, correctly uses the exact library
display name for the default value, `409` and `Attributes\\Name`, keeps
`Vendor=Out Loud`, and leaves token IDs unchanged. `inspect:168â€“169` classifies
an otherwise identical older prefixed token as `stale_name`.
`VoiceTestRequest.Suitable`, `ui/.../Models/VoiceTest.cs:11`, requires `correct`;
`validate_replacement`, `voice_tokens.cpp:215â€“223`, requires the full desired
tree. Thus a healthy old token can remain playable in a host but is disabled in
Voice Tester and refused for replacement. The WPF mutation operations offer
import/replace/remove, not token-name reconciliation.

This is a compatibility/workflow gap; it does not invalidate the requested
future-registration-only change or authorize migration of installed entries.
No current production token was read or altered to reproduce it. Existing tests
exercise stale-name status, but not this old-convention upgrade workflow.

Recommendation: agree an explicit, user-selected reconciliation workflow or a
carefully scoped compatibility policy; explain stale-name status and retain exact
ID/vendor/ownership. Benefit: users keep healthy existing voices usable without
remove/reimport. Complexity medium; risk medium. Required tests: old/new names,
Unicode, all three name fields, preserved ID/vendor/paths, conflicts, Tester and
replacement behavior. Dependency A05: CLI update can otherwise refuse old
helper-style paths even if only name migration was intended.

Import publishes a new random ID and then registers that ID; partial registration
failure retains a healthy library entry and reports partial, not success
(`mutation.cpp:155â€“166`). Replacement retains ID/name/token and uses a durable
pair journal. Removal confirms ID/name/hash/health and unregisters that same ID.
Passing fake-registry coordination tests support these ordinary paths.

Standalone library remove/rename/import commands deliberately do not coordinate
tokens. Token CLI removal deliberately does not remove files. These are different
administrative APIs, not duplicated WPF implementations. Their narrower effects
must remain clear in release documentation.

The bridge's `correct` status checks identity/name/voice path using actual shared
runtime values when no intended runtime is supplied. It is not a certificate of
asset availability, COM activation or host enumeration. The details already say
so. Keep this contract; clarify presentation alongside A01/A17 rather than
silently changing result semantics. The previous registry audit's snapshots are
historical. This review did not enumerate WoW or infer its cache behavior.

### A16 â€” P2 â€” asset loading errors lose operational detail at SAPI boundary

**CONFIRMED; component: native runtime diagnostics.**
`engine/src/asset_io.cpp:16â€“28` throws generic `std::runtime_error` for file/mapping
failures without path/Win32 error. `sapi/src/adapter.cpp:16â€“22` maps these and
validation runtime errors to generic E_FAIL. `SapiTestSessionFactory.Call`,
`Services/SapiTestSessionFactory.cs:38â€“41`, then reports stage/HRESULT. Missing
model/tokenizer, corrupt bytes and failed mapping can be difficult to distinguish
from another E_FAIL after activation.

Recommendation: retain structured native asset failure context and map known
I/O failures to meaningful HRESULTs; expose bounded diagnostics through an
appropriate opt-in/local mechanism without logging text or secrets. Keep all
exception boundaries. Benefit: actionable broken-install reports. Complexity
medium; risk low/medium. Required tests: missing/inaccessible/corrupt asset,
mapping failure, COM/C ABI errors, no unexpected exception/detail leakage.
Dependencies: package A03's runtime configuration must define expected paths.

## 10. WPF frontend assessment

ViewModels preserve immutable snapshots and selection by ID after refresh;
failed refresh keeps the previous list with an error. Mutation requests capture
confirmed identity and refresh after removal even on failed/partial results
(`MainViewModel.cs:89â€“106,127â€“178`). Busy guards prevent concurrent UI mutation
and testing. Tester captures voice/text before awaits, tracks request revision,
stops on selection changes, and waits for cleanup before releasing its busy state
(`VoiceTesterViewModel.cs:63â€“139`). Tests cover these behaviors.

Theme resources are centralized, brush references dynamic, high contrast supported;
the theme service reads AppsUseLightTheme read-only, marshals live updates to the
dispatcher, and unsubscribes system/window events on disposal. No heavy UI package
was introduced. Fake-layout/light/dark/high-contrast/resize validation passed;
live Windows broadcasts and actual playback still require manual checks.

WAV export uses a fresh SAPI session, validates actual RIFF/PCM16 mono 24 kHz bytes,
closes/releases COM before publishing and removes temporary output on failure
(`SapiVoiceTestBackend.cs:36â€“74,81â€“104`). Its export path handling is a normal
user-context operation, not the elevated library path. Do not conflate its simpler
file handling with a privileged arbitrary-file mutation vulnerability.

`RefreshCommand.Execute` is async void because it implements ICommand. Actual
workflows catch expected backend errors. No reproduced unhandled UI exception
was established; changing command architecture without a failing case is not
recommended. MainWindow waits asynchronously for tester shutdown and prevents
close reentry. It does not force-terminate an active administrative mutation.

## 11. Build and dependency findings

Native targets use C++20, MSVC x64, Windows SDK, `/W4 /WX /utf-8 /permissive-`;
neural operations use `/fp:precise`, production RNG `/fp:strict`, optional AVX2
is separately compiled. WPF/test/validation projects are x64; WPF is net10.0,
asInvoker, and compiler warnings are errors. No NuGet UI/test framework package
reference was found. Python package pins are reference-tool dependencies.

The WPF project copies `OutLoudVoiceImportHelper.exe` and
`OutLoudVoiceManagerNative.dll` from the configured native Release directory
(`OutLoud.VoiceManager.csproj:13â€“27`). Current output copies matched native
SHA-256 hashes. MSBuild verifies their existence, not their freshness relative
to C++ source; `ui/build.ps1` provides ordered native-before-managed compilation.
The explicit build commands in this audit use that order without deployment.

### A11 â€” P2 â€” production configuration is coupled to research fixtures

**CONFIRMED; component: CMake.** `CMakeLists.txt:14â€“19,69â€“73,105â€“108,130â€“132,
175â€“177` require generated tokenizer/test/asset/neural headers at configure time;
their development executables are created outside `if(BUILD_TESTING)`. Setting
`BUILD_TESTING=OFF` disables registration, not fixture requirements or targets.
Even a user building only the bridge/helper cannot configure without these
reference exports. Assets/headers are deliberately ignored, so a clean checkout
must first reproduce them with the documented reference environment/cache.

Recommendation: gate development targets and their fixture requirements, keeping
production targets independently configurable; retain opt-in full parity builds.
Benefit: smaller build prerequisites and reliable product-source compilation.
Complexity medium; risk low/medium. Required checks: clean production configure/
build with no research fixtures, full reference configuration, x64/ISA options,
UI copy steps. Dependencies A03 should define how actual runtime assets are
obtained; the build must not download them implicitly. This audit used existing
exports/cache and does not claim a new clean-machine build was performed.

### A03 â€” P1 â€” complete product packaging/trust prerequisites are not automated

**CONFIRMED release blocker; component: distribution/dependencies.**
`sapi/deployment-inventory.json:5â€“12`,
`sapi/scripts/deployment-common.ps1:33â€“56,96â€“103`, and
`prepare-deployment.ps1:24â€“48` cover only the development SAPI engine/checker,
four VC DLLs, assets and development voice. The allowlist rejects extra binaries;
it does not package/sign the WPF app, bridge or mutation helper. WPF output
copying does not install/check .NET WindowsDesktop or System32 VC runtime.
Helper uses `/DEPENDENTLOADFLAG:0x800` and System32-only DLL search
(`CMakeLists.txt:339â€“347`, `helper.cpp:40`), so adjacent VC copies alone cannot
satisfy a missing System32 runtime. The previous local build copied three VC
DLLs beside WPF; those copies are not a tracked packaging guarantee.

`docs/VOICE_MANAGER_IMPORT.md:130â€“136` expressly identifies user-writable unsigned
workspace output as development and calls for a signed, administrator-protected
release location. Parent filename/directory checks cannot substitute for that
trust boundary. No exploit or current installed ACL deficiency is asserted.

Recommendation: define a separate complete product inventory and reproducible
staging/verification process, protected installation/update ownership, supported
OS/.NET/VC prerequisites, missing-runtime/first-run diagnostics and explicit
managed registration/recovery policy. Preserve vendor signatures and explicit
UAC authorization. Benefit: a launchable, supportable, trusted public package.
Complexity medium/high; risk medium. Required validation: isolated nonproduction
machine/package checks for clean prerequisites, signature/ACL/dependency closure,
missing assets/library, upgrades and uninstall scope. Dependencies A04 and A11;
no deployment/install action was performed here.

## 12. Testing and coverage gaps

### A12 â€” P2 â€” important correctness tools are outside the standard CTest set

**CONFIRMED; component: test orchestration.** CTest lists **17** tests.
`CMakeLists.txt:225â€“235,245â€“253,284â€“293,298â€“306` creates worker parity, RNG,
utterance/cancellation and fake SAPI host executables without adding their
checks to CTest. Therefore a green `ctest` does not by itself mean these paths
were rerun. The source has meaningful barrier-based cancellation/recovery,
PCM/latent/RNG and host-affinity checks; the problem is reachability from the
normal validation command, not absent test code.

Recommendation: document or add a distinct opt-in correctness validation entry
point for existing tools with bounded fixtures/timeouts. Preserve optional
fixture dependencies and avoid benchmark/profiler CTests. Benefit: important
regressions cannot be missed by assuming CTest is complete. Complexity low/medium;
risk low. Required checks: runner invokes correctness-only modes, fails on errors,
skips unavailable optional fixtures explicitly, runs no real registration or
live host mutation. Dependencies A11; performance campaigns remain separate.

### A13 â€” P2 â€” tests do not assert the new exact-name contract/upgrade interaction

**CONFIRMED; component: SAPI/frontend coverage.**
`sapi/tests/voice_tokens_test.cpp:221â€“225` checks schema, ID, CLSID, Vendor and
Language, then status/idempotence and rename in `240â€“255`. It does not assert
that the default, `409` and `Attributes\\Name` exactly equal the chosen display
name. A consistent prefix in both install and comparison can pass these checks.
Fake script fixtures intentionally still use `Out Loud - Test`; they test token
selection, not registration's naming policy. Passing tests thus do not verify
the recent behavior fully.

Recommendation: targeted exact-name assertions including Unicode and all three
fields, preserved ID/vendor, and old-convention inspect/Tester/replacement cases.
Benefit: locks down the requested contract and makes A01's policy reviewable.
Complexity low; risk low. Dependencies A01 for the intended legacy workflow,
A05 for path variants. Existing tests were not modified in this audit.

### Executed checks and evidence

Raw logs: ignored `generated/full-project-audit/`.

| Check | Result/limits |
| --- | --- |
| `cmake --build build --config Release --parallel` | Passed; all current native targets, including helper, bridge and workspace SAPI DLL. No compiler warning/error reported. Incremental existing x64 MSVC build, not a fresh-machine install. |
| `dotnet build ui/OutLoud.VoiceManager.sln -c Release -p:Platform=x64` | Passed; **0 warnings, 0 errors**. Restore reported up-to-date. ASP.NET certificate generation disabled. |
| Selected seven CTests listed below | **7/7**, 20.83 s; read-only fixtures/private scratch/fake registries. |
| `voice-import-regressions` separately | **1/1**, 0.55 s; its normal two-argument CTest route converts/validates scratch output without synthesis. |
| .NET fake-service test executable | **155 assertions passed**; fake dialogs/helpers/SAPI peers, private WAV fixtures, no production read/mutation. |
| `sapi/tests/managed_voices_script_test.ps1` | **49 assertions passed**, fake COM only. |
| WPF supplementary launcher with default directory | Failed before running view checks; A14. |
| Same WPF launcher with absolute native directory | Passed BAML/bindings/library/tester/selection/theme/contrast/mutations/resize/shutdown with fake services; no visible window or production playback. |
| Static script syntax | **11 PowerShell, 21 Python** tracked scripts parsed; Python used AST only, no imports of reference dependencies or bytecode writes. |
| Native WPF output copies | Helper and bridge SHA-256 equal to `build/Release` artifacts. |

Commands for the selected tests:

```powershell
ctest --test-dir build -C Release --output-on-failure -R '^(canonical-tokenizer-parity|text-pipeline-regressions|canonical-asset-check|asset-regressions|voice-library-regressions|voice-manager-bridge-regressions|sapi-token-regressions)$'
ctest --test-dir build -C Release --output-on-failure -R '^voice-import-regressions$'
dotnet run --project ui/OutLoud.VoiceManager.Tests/OutLoud.VoiceManager.Tests.csproj -c Release -p:Platform=x64 --no-build
powershell -NoProfile -File sapi/tests/managed_voices_script_test.ps1
powershell -NoProfile -File ui/validation/validate-with-runtime.ps1 -Configuration Release -NativeBuildDirectory 'C:\Users\rosko\Desktop\Nendis\application\wow-pocket-tts-sapi\build'
```

Skipped **9 inference CTests**: eight FlowLM/sampler/generation/Mimi stage tests
and SAPI multi-voice synthesis. Standalone RNG, worker, utterance/cancellation,
fake SAPI synthesis host and performance executables were also not executed.
No change to model math warranted an expensive inference campaign here. Their
source/tests and historical results were reviewed, not presented as fresh passes.
The import CTest was added to the safe selection only after inspection established
its default route has no synthesis. No privileged/live-deployment test ran.

Coverage that should be added or explicitly scheduled: A02 interruption/recovery,
A13 naming transition, authentic elevated pipe rejection/peer failures in a
disposable environment, real SAPI release of mapped voices after Play/Stop/export,
blocking/erroring output shutdown and complete package first-run prerequisites.
Historical fake-launcher tests do not establish real UAC/process authentication.
Historical cancellation barrier tests do not preempt an executing kernel or Write.

## 13. Release-readiness blockers

### A04 â€” P1 â€” full distribution attribution/permissions inventory is absent

**CONFIRMED repository gap; component: release materials.** The tracked inventory
contains `RNG_NOTICES.md` but no project LICENSE or complete model/tokenizer/voice/
runtime attribution/distribution manifest. `RNG_NOTICES.md:1â€“164` appropriately
retains original RNG notices and marks scalar adaptations; the development
packager includes that file. `reference/UPSTREAM.md` and
`tools/assets/asset_format.py:13â€“24` pin sources/revisions/hashes, which establish
identity but are not a complete distribution-permissions record.

Recommendation: document the project's chosen licensing policy and exact
upstream/license/notice requirements for each intended shipped component/asset,
including provenance and permission for distributed voice profiles, and include
required materials in A03's inventory. Benefit: a reviewable distribution bill
of materials rather than undocumented assumptions. Complexity medium; code risk
low. Required checks: each shipped item maps to source/revision/notice and a
recorded distribution decision; inspect resulting package materials. Dependencies
A03. **UNVERIFIED:** permissions/licenses held outside this repository and legal
clearance for particular game-derived/custom voices. This audit makes no legal
determination and does not infer license rights from a hash or generic model name.

Fix or resolve A01â€“A04 before declaring a public release. The current manually
registered development deployment is not a complete product installer. Offline
native operation and version-pinned formats are good; compatibility mismatches
are rejected rather than migrated silently. Fresh asset absence, dependency
absence, locked voices, inaccessible ProgramData and no managed voices need
explicit package/first-run validation. Actual WoW enumeration/playback/abort is
a separate user-controlled host acceptance check, not a guessed blocker cause.

## 14. Documentation/repository cleanup

### A17 â€” P3 â€” entry documentation and managed-name table lag current behavior

**CONFIRMED; component: documentation.** `README.md:1â€“5` is a short engine
description with no build/start/architecture guide. Current functional docs are
spread across step reports. `docs/MULTI_VOICE_SAPI.md:69` still promises
`Out Loud - <current library display_name>` for managed token descriptions,
contradicting current `desired()`.

Recommendation: add a concise entry-point/index with native/UI prerequisites,
ordered build commands, administrative scope and diagnostics; update current
managed naming/legacy-transition instructions without rewriting historical audit
snapshots. Benefit: fewer wrong startup/repair assumptions. Complexity low;
risk low. Required check: follow documented commands with explicit prerequisites,
verify names/scope match source. Dependencies A01/A03/A11/A14 determine final
instructions.

`.gitignore` excludes model/voice/build/generated files and expressly restores
tracked WPF `Models` on case-insensitive Windows. No tracked EXE/DLL/OLP/OLV/
safetensors/WAV artifact was found. Baselines, tolerances and research reports
have purpose; retain them. No temporary/generated file was deleted. The previous
prosody/registry reports are now tracked at this baseline and were not changed.

## 15. Prioritized improvement table

Details, exact locations, tests, complexity/risk and dependencies are recorded
in the finding entries above. Priority is stabilization value, not evidence level.

| ID | Priority | Confidence | Finding | Complexity / risk | Dependencies |
| --- | --- | --- | --- | --- | --- |
| A01 | P1 | CONFIRMED | Old-name token reconciliation gap blocks Tester/replacement | Medium / medium | A05; explicit legacy policy |
| A02 | P1 | LIKELY | Detached removals lack durable coordination recovery | Medium/high / high | Recovery/commit policy |
| A03 | P1 | CONFIRMED | Full signed/protected product package and prerequisites absent | Medium/high / medium | A04, A11 |
| A04 | P1 | CONFIRMED | Complete distribution licensing/attribution inventory absent | Medium / low code risk | A03 |
| A05 | P2 | CONFIRMED | Equivalent runtime paths produce false conflicts | Medium / medium | Coordinate A01 |
| A06 | P2 | CONFIRMED | Refresh validates/hashes voices repeatedly | Medium / medium | Snapshot contract |
| A07 | P2 | CONFIRMED | Full-segment Mimi KV backing exceeds live attention needs | High / high | Diagnostic-state compatibility |
| A08 | P2 | CONFIRMED | Repeated immutable convolution phase preparation | Medium / medium | Correctness/perf validation |
| A09 | P2 | CONFIRMED | Prefill allocation and invariant sampler work | Medium / medium | Correctness/perf validation |
| A10 | P2 | CONFIRMED | Native speech collection lacks aggregate input limits | Low/medium / medium | Host limit policy |
| A11 | P2 | CONFIRMED | Production configure requires research fixtures | Medium / low/medium | Product build definition |
| A12 | P2 | CONFIRMED | Standalone critical correctness tools absent from standard CTest | Low/medium / low | A11 |
| A13 | P2 | CONFIRMED | Exact-name and old-convention interaction assertions missing | Low / low | A01, A05 |
| A14 | P2 | CONFIRMED | Default `-File` WPF validation parameter fails | Low / low | None |
| A15 | P2 | CONFIRMED | Frontend native structured-error mapping duplicated | Low/medium / low/medium | None |
| A16 | P2 | CONFIRMED | Native asset/SAPI failures collapse into generic diagnostics | Medium / low/medium | A03's runtime layout |
| A17 | P3 | CONFIRMED | README/current naming docs need an entry guide/update | Low / low | Stabilization decisions |
| A18 | P3 | CONFIRMED | Duplicate test scratch cleanup | Low / low | None |

**Counts: P0 0; P1 4; P2 12; P3 2.** Not all improvements must precede a release;
performance changes, especially A07, should not delay necessary safety/workflow
work or be bundled into it.

## 16. Recommended implementation phases

1. **First stabilization phase:** A05 path-equivalence regression/correction,
   A13 exact-name/legacy assertions, explicit A01 reconciliation behavior, and
   A14 launcher fix. This is bounded control/tooling work; no kernel/format change.
   Installed tokens remain untouched unless a user explicitly invokes a reviewed
   reconciliation operation.
2. **Recovery and bounded input:** implement A02 journal/reconciliation with
   interruption tests in private scratch/fake registries; define A10's request
   bounds and A16's failure diagnostics. Keep normal result/rollback semantics.
3. **Release foundation:** decide A03/A04 packaging/trust/attribution, separate
   fixture-dependent builds (A11), make correctness coverage discoverable (A12),
   and update current docs (A17). Validate real privilege/host paths only in an
   explicitly controlled environment.
4. **Measured optional efficiency:** A06 refresh reuse first; A08 immutable
   preparation, then A09 buffers/constants; evaluate A07 memory only with exact
   state/PCM evidence. Compare min/median/max of five timed repetitions after
   asset validation and one warm-up, reset seed/state per run, validate outputs
   and serialize outside timing; one separate normal profile if needed. Preserve
   ISA-specific correctness and perform listening before shipping math-adjacent
   changes. A15/A18 can be small independent maintenance patches later.

No proposed change is implemented by this report. Refactoring awaits review and
approval; do not treat the recommendations as authorization to mutate production.

## 17. Areas already well implemented and best left unchanged

- Layering and x64/asInvoker frontend/read-only bridge/explicit helper privilege
  separation. Avoid a combined elevated UI or mutation-capable read ABI.
- Immutable random library IDs, fixed owner/CLSID schema and explicit HKLM64/HKCU
  separation. Do not identify/delete voices by display name or merge scopes.
- Pinned model identity, lossless BF16 widening and finite FP32 voice conditioning.
  Preserve formats and rejection checks; there is no reason to loosen them.
- RNG lane arithmetic, discarded prefill draw, EOS tail and continuous segment
  scheduling. Preserve separate `/fp:strict` and current ISA checks.
- Two resident workers, bounded two-latent queue, single-frame SAPI delivery,
  caller-affine controls and joined cooperative cancellation. No measured reason
  supports more threads, a complete output buffer or polling native waits.
- Replacement backups/journal/identity checks; truthful import partial and removal
  warning/unknown outcomes. Build durable removal recovery around these principles.
- Per-request native snapshot ownership, SafeHandle copying and no exceptions
  crossing ABI/COM. Avoid long-lived borrowed unmanaged UI pointers.
- STA session-owned COM teardown, captured tester identity, immutable UI refresh,
  busy guards and stale-progress suppression.
- WPF-native dynamic resources, readable high contrast and safe theme unsubscription.
- Reference-only Python, retained numerical fixtures/research, private fake-registry
  tests and signature preservation for vendor runtimes.

The fixed text splitter's default 50-token soft target is deliberate upstream
behavior. `MaximumSegmentTokens=128` is a resident *capacity*, as documented in
`SAPI_ADAPTER.md:186`; automatically passing it into the splitter would change
validated audio scheduling. Long unsplittable sentences can still exceed capacity
and be rejected; do not silently invent a new splitting algorithm as cleanup.

### FIX BEFORE RELEASE

- Resolve old-name token workflows and equivalent runtime paths (A01/A05/A13).
- Add durable coordinated-removal recovery and interruption evidence (A02).
- Define complete trusted packaging/prerequisites and attribution (A03/A04).
- Set a bounded native-input policy and reliable startup/error diagnostics
  (A10/A16), then perform controlled real SAPI/UAC/host acceptance checks.

### RECOMMENDED IMPROVEMENTS

- Fix the PowerShell launcher and separate product builds from research fixtures
  (A14/A11); make existing correctness checks explicit (A12).
- Eliminate repeated validation within a read snapshot (A06).
- Measure immutable convolution preparation and prefill/sampler reuse (A08/A09).
- Consolidate shared frontend error-field conversion without merging boundaries
  (A15).

### OPTIONAL CLEANUP

- Update the entry guide/current naming instructions, retaining historical reports
  (A17).
- Consolidate only test scratch helpers (A18).
- Investigate bounded Mimi KV memory after correctness/state-view design (A07);
  no unverified performance redesign or file deletion.
