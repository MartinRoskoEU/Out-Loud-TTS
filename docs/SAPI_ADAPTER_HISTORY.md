# Historical implementation record

Archived when legacy voice management was retired on 2026-10-09.
This preserves implementation-time design and validation evidence.
Commands and compatibility descriptions below are historical, not supported
operating instructions. Use [SAPI_ADAPTER.md](SAPI_ADAPTER.md) for current behavior.
No historical test result below validates the later removal changes.

# Step 2M: Windows SAPI adapter and manual deployment

For managed library voices sharing this DLL/CLSID, see
[Step 3C multi-voice SAPI](MULTI_VOICE_SAPI.md). The HKLM development deployment
procedure below remains separate from managed production tokens and their
ProgramData library. Legacy per-user tokens remain available for diagnostics.

**Current runtime:** Step 2N adds cooperative native/SAPI cancellation; see
[SAPI_CANCELLATION.md](SAPI_CANCELLATION.md). Step 2M timings below remain
historical; current deployment also ships `msvcp140_atomic_wait.dll`.

Step 2L was committed as `fde997e` (Implement native full-text streaming
orchestration), with a clean tree before Step 2M began. This step adds a thin
Windows x64 COM boundary over `UtterancePipeline`; it changes no neural kernels,
tokenization/splitting, RNG arithmetic or worker scheduling. No performance CTest
is added. Development checks and generated artifacts remain local/ignored.

## Evidence and reference

**VERIFIED Windows SDK:** local Windows SDK 10.0.26100.0 `um/sapi.h`,
`um/sapiddk.h` and `um/sperror.h` were inspected for `ISpTTSEngine`,
`ISpObjectWithToken`, `ISpObjectToken`/`ISpDataKey`, `ISpTTSEngineSite`,
`ISpEventSink`, `SPVTEXTFRAG`, `SPVACTIONS`, `SPVESACTIONS` and HRESULTs.
This is an in-process SAPI 5 engine, using the SDK's actual interfaces.

**VERIFIED Chatterbox reference:** the read-only project
`../wow-chatterbox-sapi` supplied these primary references:

- `sapi/src/adapter.cpp`, `identity.hpp`, `OutLoudSAPI.def`: COM factory,
  reference/module accounting, token assignment, format, text and Write handling.
- `sapi/tests/host.cpp`: direct DLL/factory checks, fake site, caller-thread site
  affinity, injected failures and partial output.
- `sapi/CMakeLists.txt`: native DLL/export and system-library linking.
- `sapi/scripts/register.ps1`, `unregister.ps1`, `registry-common.ps1`:
  HKLM Registry64, classic SAPI voice tokens, ownership/snapshots/rollback.
- `sapi/scripts/prepare-deployment.ps1`, `verify-deployment.ps1`:
  versioned copied bundles, PE architecture, hashes, app-local VC runtime,
  existing My-store certificate selection, SignTool and signature verification.
- `sapi/README.md`, `reference_audit.md`, and `native/src/audio_io.cpp`:
  deployment history, scope and conversion behavior. The current working native
  HKLM registration takes precedence over older Kokoro/HKCU history.
- `generated/deployment/OutLoud-cpu-test-v3/deployment.json`: actual existing
  development signer subject/issuer `CN=WoWKokoroSAPI Dev`.

The twelve primary SAPI source/document/script hashes captured before work are
checked again afterwards in `generated/perf/2m-reference-source-hashes.json`.
Neither reference project was modified.

**Pocket decisions:** a distinct DLL/CLSID/token preserves coexistence with
Chatterbox. No Chatterbox ONNX/provider/global-cache layer is copied: all native
Pocket libraries link statically and each engine owns its resident runtime.
Pocket streams immediately rather than holding Chatterbox's completed waveform.
It retains Pocket's existing listening conversion rather than Chatterbox's
32768/round-away conversion. Copying and signing are separate manual scripts.
Ownership checks retain other tokens using the shared CLSID; HKCU overrides
cause an explicit failure instead of Chatterbox's stale-key cleanup.

## COM and native lifetime

Final development binary: `build/Release/OutLoudPocketSAPI.dll`, x64 PE32+
(machine `8664`). The only exports are `DllGetClassObject` and `DllCanUnloadNow`.
No `DllRegisterServer`/`DllUnregisterServer` exists; scripts write explicit keys
when the user later chooses to run them. Do not use regsvr32.

CLSID: `{F38C3398-3552-43B9-8350-7DD301327D70}`.

The factory implements `IUnknown`/`IClassFactory`. The engine implements
`IUnknown`/`ISpTTSEngine`/`ISpObjectWithToken` with one canonical IUnknown.
Aggregation returns `CLASS_E_NOAGGREGATION`; an unrelated CLSID returns
`CLASS_E_CLASSNOTAVAILABLE`. Atomic reference counts delete objects at zero.
Live factories/engines and balanced `LockServer` locks prevent unloading.
An unmatched unlock returns `E_UNEXPECTED`. Speak holds an additional self
reference until all work completes. `DllMain` only records the module handle
and disables thread notifications; it performs no synthesis or COM activation.

SetObjectToken validates/resolves configuration and retains the token;
GetObjectToken returns an AddRef'd pointer. Model/voice mappings, immutable
tokenizer and pipeline load lazily on the first nonempty Speak and remain
resident on that engine. Speak and token reassignment serialize. Reassigning
different runtime paths/capacities/kernel selection discards the old runtime;
a seed-only change can reuse it. Destruction releases pipeline/joins workers
first, then tokenizer/voice/model owners, then token. No detached threads.

## Speak and text

```
ordered bounded SPVTEXTFRAG UTF-16 spans
  -> concatenate supported text once
  -> strict UTF-8
  -> UtterancePipeline::prepare(full_text, seed)
  -> UtterancePipeline::run(frame_sink, context)
  -> one 1920-float frame
  -> one 1920-int16 frame
  -> immediate ISpTTSEngineSite::Write
```

**SDK contract / Chatterbox policy:** SPVA_Speak contributes exactly
`pTextStart[0..ulTextLen)` in linked-list order; no inserted spaces and no
per-fragment synthesis/reseed. Null data with nonzero length returns E_POINTER;
a cycle returns E_INVALIDARG. Section and ParseUnknownTag metadata are ignored
without speaking their contents. Step 2O forwards leading Bookmark fragments
as ordered SPEI_TTS_BOOKMARK string events at byte offset zero when requested
by the site; names never enter synthesis. The Step 2O compatibility fix ignores
unsupported non-leading bookmarks as metadata so they cannot suppress speech;
no approximate mid-speech event offsets are emitted. Zero-length silence is ignored.
Pronounce, SpellOut, nonzero silence and other unsupported actions return
E_NOTIMPL before audio begins. Other events/SSML/prosody transforms are not
implemented. Fragment pitch/emphasis/rate/volume state does not alter synthesis;
the supported product subset is ordinary English text with optional leading
bookmarks. Ownership and limitations are detailed in `docs/SAPI_BOOKMARKS.md`.

WideCharToMultiByte with CP_UTF8/WC_ERR_INVALID_CHARS uses explicit lengths and
does not depend on locale. Conversion happens after concatenation, allowing a
surrogate pair to span fragments. Malformed UTF-16 returns a Win32-derived
HRESULT (ERROR_NO_UNICODE_TRANSLATION in the checker). Empty supported text
returns S_OK without synthesis. Embedded NUL is preserved by conversion and
left to the existing native text API's validation.

**Verified reference constraint / Pocket bridge:** Chatterbox's fake host
explicitly checks that site calls stay on the Speak caller thread. Pocket's
unchanged sink runs on its Mimi consumer. Therefore one joined coordinator
thread calls the existing pipeline while the synchronous Speak caller delivers
the site writes. A condition-variable rendezvous exposes exactly one 3840-byte
PCM16 frame. The native sink waits for that frame's complete delivery before
returning; no second frame is awaited before Write. This is coordination of the
existing API, not an additional neural worker or audio renderer. The two native
resident workers and their scheduling remain unchanged. No COM site proxy,
marshalling, presumed site agility or detached work is introduced.

Production owns neither full segment nor full utterance PCM. There is no WAV,
helper process, Python/PyTorch/ONNX/oneDNN runtime or network in synthesis.
Only the correctness checker captures full PCM for comparison.

## Format, conversion and Write

GetOutputFormat allocates WAVEFORMATEX using CoTaskMemAlloc; the caller frees it
with CoTaskMemFree. The returned format GUID is SPDFID_WaveFormatEx. Requested
input formats do not change this initial supported output:

| Field | Value |
| --- | ---: |
| wFormatTag | WAVE_FORMAT_PCM = 1 |
| nChannels | 1 |
| nSamplesPerSec | 24000 |
| nAvgBytesPerSec | 48000 |
| nBlockAlign | 2 |
| wBitsPerSample | 16 |
| cbSize | 0 |

Speak requires those exact fields or returns SPERR_UNSUPPORTED_FORMAT.
Each native 1920-sample frame is 80 ms and becomes 3840 audio bytes.
`engine/include/outloud/pcm16.hpp` provides one implementation shared with
`tools/audio/wav.hpp` and the checker: reject nonfinite values, clamp to [-1,1],
multiply by 32767, round ties to even. Thus +/-1 maps to +/-32767, preserving
the existing Pocket listening output. No gain, resampling or dithering.

**Verified SDK / Chatterbox Write handling:** `pcbWritten` can be less than the
requested count. The adapter retries the remainder immediately, advancing the
pointer by consumed bytes. Zero progress, odd byte counts or counts larger than
the request return E_UNEXPECTED. Failed Write HRESULTs propagate exactly;
SP_AUDIO_STOPPED is also returned exactly although it is a success-category
status. The frame sink throws internally on delivery failure so the existing
native error boundary quiesces the current job. Coordinator joining completes
before COM returns. The checker verifies the next request recovers.

No C++ exception escapes COM. Allocation failure maps to E_OUTOFMEMORY,
argument/capacity/invalid native preparation to E_INVALIDARG, missing paths to
HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND), filesystem errors to Win32-derived
HRESULTs, token and output-site failures preserve their HRESULT, other native
failures return E_FAIL. Unassigned token returns SPERR_UNINITIALIZED. Previously
streamed audio cannot be recalled.

## Token configuration and separate voices

Development token: `OutLoud_Pocket_GnomeMale_Development`.
Display: **Out Loud - Gnome Male (Development)**.
All voice tokens share the same DLL and CLSID.

| String value | Resolution/default |
| --- | --- |
| AssetRoot | Absolute installation root; absent uses DLL directory's parent (`bin/..`) |
| ModelPath | Absolute or root-relative; default `runtime/pocket-english.olp` |
| TokenizerPath | Absolute or root-relative; default `runtime/pocket-english-tokenizer.bin` |
| VoicePath | Required absolute or root-relative `.olv` selection |
| Seed | Decimal uint64, default **73**, including when absent |
| MaximumSegmentTokens | Decimal positive per-segment capacity, default **128** |
| KernelPath | Auto (default), Portable or AVX2 |

All three asset files must exist before token assignment succeeds. Model,
voice and tokenizer content validation occurs during resident construction.
There are no personal developer paths in production C++. The capacity is a
storage limit, not a change to Pocket's 50-token soft splitter. Unsplittable
segments beyond the configured capacity fail in native preparation before
streaming. The checker uses capacity 51 to match Step 2L's reserved edge case;
the manual registration script accepts 1..128. The C++ boundary additionally
checks the native 4096-position limit.

Every Speak prepares with its configured development seed. The native run
reseeds once per utterance; it owns the continuous stream across Pocket text
segments. No per-fragment or per-segment reseeding is added. Final randomized
production seeding is a later product decision.

A future voice pack adds `.olv` files plus metadata/registration for another
`OutLoud_Pocket_*` token with its own VoicePath, display and attributes. A
user-generated voice outside the shared bundle can use an absolute path; the
register script verifies it exists. The main model/tokenizer/DLL remain shared.
No engine rebuild or one-DLL-per-voice scheme is required. Voice-pack management
and custom voice-content validation tools are not implemented here.

## Deployment and dependency inventory

Adapted versioned Chatterbox layout:

```
OutLoud-Pocket-<version>/
  bin/OutLoudPocketSAPI.dll
  bin/outloud-sapi-check.exe
  bin/msvcp140.dll
  bin/msvcp140_atomic_wait.dll
  bin/vcruntime140.dll
  bin/vcruntime140_1.dll
  runtime/pocket-english.olp
  runtime/pocket-english-tokenizer.bin
  voices/gnome_male.olv
  scripts/*.ps1
  docs/SAPI_ADAPTER.md
  docs/SAPI_CANCELLATION.md
  RNG_NOTICES.md
  deployment-inventory.json
  deployment.json
```

**VERIFIED Release dependency closure:** dumpbin /exports /dependents /headers
was run on the adapter and /dependents on the checker and all four shipped
Microsoft runtime images. `sapi.lib`/`uuid.lib` supply SDK symbols; there is no
sapi.dll import in the adapter. Its direct imports are:

- ole32.dll, bcrypt.dll, KERNEL32.dll (Windows, not shipped).
- MSVCP140.dll, MSVCP140_ATOMIC_WAIT.dll, VCRUNTIME140.dll, VCRUNTIME140_1.dll
  (VC runtime, shipped app-local). The atomic-wait image is added in Step 2N by
  MSVC's stop-token callback synchronization, verified from actual Release imports.
- api-ms-win-crt-runtime/heap/math/string/stdio/filesystem/locale-l1-1-0.dll
  (Windows UCRT API sets, not shipped).

The VC runtime closure adds only Windows UCRT time/environment/convert/utility
API sets and the already listed VC images/KERNEL32. Windows 10/11 supplies UCRT
and OS transitive dependencies; no OS/driver DLLs or VC installer is copied.
The atomic-wait image additionally imports ADVAPI32.dll and
api-ms-win-core-synch-l1-2-0.dll (Windows, not shipped).
Local System32 VC images were inspected as x64 with valid Microsoft signatures;
copy-only staging extends Chatterbox's three VC sources with this required image, and checks them
again. Redistribution rights/licensing remain those of Microsoft's runtime.
No third-party neural DLL ships. All outloud_* model, text, RNG, CPU,
generation, Mimi, worker, orchestration and asset libraries are static/internal.

`sapi/deployment-inventory.json` explicitly lists ALL six shipped PE images.
The checker is included as a development diagnostic EXE; final consumer pack
selection is later work. All other packaged assets/scripts/docs are non-PE
and checked by SHA256, not Authenticode. The generated deployment manifest
records every copied file, source hash and current deployed hash.

| Shipped image | Signing policy |
| --- | --- |
| OutLoudPocketSAPI.dll | Project Authenticode signature required before registration |
| outloud-sapi-check.exe | Project Authenticode signature required before registration |
| msvcp140.dll | Preserve/verify existing valid Microsoft signature |
| msvcp140_atomic_wait.dll | Preserve/verify existing valid Microsoft signature |
| vcruntime140.dll | Preserve/verify existing valid Microsoft signature |
| vcruntime140_1.dll | Preserve/verify existing valid Microsoft signature |

Both built project images are currently **NotSigned**. No deployed signed bundle
has been created. Additional PE files cause bundle verification to fail until
inventory/policy/discovery are explicitly updated. Staging refuses an existing
destination; no installed deployment is overwritten.

## Manual signing and registration workflow

**Verified Chatterbox workflow:** copy a versioned bundle; select an existing
certificate by thumbprint in CurrentUser/My, or LocalMachine/My with `/sm`;
SignTool signs unsigned project copies with `/sha1 <thumbprint> /s My /fd SHA256`;
optional explicit RFC3161 `/tr <url> /td SHA256`; preserve valid vendor signatures;
verify Get-AuthenticodeSignature status Valid and expected project signer.
Pocket additionally runs SignTool `verify /pa /all /v` on each inventory image.

The inspected reference's actual certificate subject is **CN=WoWKokoroSAPI Dev**,
not a newly assumed Out Loud certificate. A future neutral subject such as
**CN=Out Loud Development Code Signing** is compatible with thumbprint-based
selection, if the user separately obtains/configures it. No thumbprint/password,
PFX path or new certificate is supplied by this project. No certificate is
created/imported/installed by any script. The selected existing certificate
must have accessible private key and Code Signing EKU; valid trusted signatures
are required. Timestamping, when requested, uses only the user-provided URL.

The following are **instructions for later manual use**, not commands executed
during Step 2M. Use x64 PowerShell; elevate only registration/unregistration as
needed for HKLM. Run from the project root; choose a fresh versioned destination:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
$bundle = 'C:\OutLoud\OutLoud-Pocket-development-01'  # editable install destination
.\sapi\scripts\prepare-deployment.ps1 -DeploymentRoot $bundle
# COPY ONLY. Does not sign or register.
& "$bundle\scripts\sign-deployment.ps1" -DeploymentRoot $bundle `
    -CertificateThumbprint '<YOUR EXISTING CERTIFICATE THUMBPRINT>'
# Optional: -CertificateStoreLocation LocalMachine -SignToolPath <SDK tool> -TimestampUrl <RFC3161 URL>
& "$bundle\scripts\verify-deployment.ps1" -DeploymentRoot $bundle -RequireSignatures
# After reviewing/committing Step 2N, use an elevated x64 shell with WoW closed:
& "$bundle\scripts\register.ps1" -DeploymentRoot $bundle
# Later manual removal, with WoW closed:
& "$bundle\scripts\unregister.ps1" -ReportDirectory 'C:\OutLoud\reports\unregister-01'
```

Signing targets are preflighted from the explicit inventory/allowlist; missing
targets, wrong architecture/hash/signature or unexpected signer fail loudly.
Only copied project binaries are signed; source hashes remain recorded. The
manifest's deployed hashes update after successful signing. If signing fails
partway, do not register; stage a new fresh bundle and retry after resolving the
failure. Verification without -RequireSignatures is read-only pre-sign inspection
(project NotSigned allowed, Microsoft images still must be Valid); registration
always requires signatures.

**WOULD modify only HKLM Registry64:**

```
SOFTWARE\Classes\CLSID\{F38C3398-3552-43B9-8350-7DD301327D70}
  InprocServer32: (Default)=<bundle>\bin\OutLoudPocketSAPI.dll, ThreadingModel=Both
SOFTWARE\Microsoft\Speech\Voices\Tokens\OutLoud_Pocket_GnomeMale_Development
  (Default)/409: development display name
  CLSID: same Pocket engine GUID
  AssetRoot/ModelPath/TokenizerPath/VoicePath/Seed/MaximumSegmentTokens/KernelPath
  Attributes: Name, Gender=Male, Age=Adult, Language=409, Vendor=Out Loud, Version=0.1
```

Class/token ownership marker: `OutLoudPocketOwner=OutLoudPocketSAPI-2M`.
Register refuses unrelated existing keys, replacement of an existing token,
and a shared class pointing at another bundle. Choose a new token for another
voice or explicitly unregister before replacement. Before mutation it verifies
the signed bundle/files and writes a fresh snapshot for rollback. Errors restore
only these explicit keys, reporting rollback failure instead of claiming success.
Unregister removes exactly the named owned token and retains the shared CLSID
while any other token references it. No wildcard deletion, certificate action,
automatic installation, process termination or voice enumeration. HKCU is only
read for a conflicting override of this new CLSID; nothing is removed there.

Additional voice example after later manual approval/workflow:
`register.ps1 -DeploymentRoot <same bundle> -TokenName OutLoud_Pocket_MyVoice
-DisplayName 'Out Loud - My Voice' -VoicePath C:\MyVoices\my_voice.olv -Seed 73`.
Separate packs may supply their own metadata/helpers; this is not a pack manager.

Order is **BUILD -> INSPECT/COPY -> SIGN -> VERIFY SIGNATURES -> REGISTER ->
Windows SAPI test -> WoW test**. Registration-free checking of exact copied
binaries is also possible before registry changes. No live Windows SAPI voice
enumeration or WoW test has been performed here.

## Registration-free validation and performance

`outloud-sapi-check` loads the built DLL directly, obtains its exported factory,
and supplies an entirely in-memory ISpObjectToken plus fake ISpTTSEngineSite.
It never CoCreates registry-backed SAPI tokens or registers anything:

```powershell
.\build\Release\outloud-sapi-check.exe `
  .\build\Release\OutLoudPocketSAPI.dll `
  .\generated\assets\pocket-english.olp `
  .\generated\tokenizer\pocket-english-tokenizer.bin `
  .\generated\assets\gnome_male.olv `
  .\generated\orchestration\native-full-canonical-avx2.f32
# Optional LIGHT observation: append 'bench'; one warm-up and three timed Speaks.
```

Canonical full text uses three ordered SAPI pieces, one native preparation/run,
seed 73, and the Step 2L canonical schedule: segments 46/84/40 frames, **170
frames, 326400 PCM16 samples, 652800 bytes, 13.600 seconds**. Normal delivery is
170 Write calls. A fake consuming at most 126 bytes requires **5270** Write
calls (31 per frame), producing exactly the same captured bytes.

Checks cover all WAVEFORMATEX fields, same-object A/B resident reuse, canonical
native FP32-to-PCM16 byte equality, first Write before completion, caller-thread
site affinity, partial delivery, zero/odd/oversized writes, E_ACCESSDENIED and
SP_AUDIO_STOPPED recovery, malformed UTF16 through COM, unsupported/cyclic
fragments, canonical IUnknown identity, token ownership/refcounts, aggregation
rejection, lock balance and final DllCanUnloadNow=S_OK. Strict accented/emoji and
fragment-spanning-surrogate conversion are also checked by the shared helper.
First delivery blocks the native sink's return, so pipeline completion cannot
precede that Write; this is enforced by the production rendezvous itself.

Initial existing 12/12 CTests passed once (27.20 s). Final Release build passed;
existing 12/12 CTests passed once (27.23 s), the final adapter check passed, and
the native full-utterance checker passed once, preserving canonical FP32 SHA256
`36788a2c1cfde712fb9ef68151a93b1b78eec40b1e440e99ff1b1b59d1a8bcf0`.
No new CTest, profiler or optimization
is added. Script validation is AST parsing/read-only review; only non-mutating
deployment helper functions are exercised for real PE architecture, Microsoft
signature policy, inventory and path escape rejection. Register/unregister/sign
scripts are never executed or dot-sourced. Registry mutation/rollback paths
remain untested against a live registry by instruction.

**Observed AVX2-capable host:** one untimed canonical warm-up loads the resident
runtime, then exactly three timed canonical Speaks on that same object. Each
Speak reseeds through native prepare/run. Timed fake-site delivery counts bytes
without allocating a full capture; output/count validation and JSON are outside
the Speak clock. No profiler/observer hooks. No additional timed repetitions.

| Metric (ms) | Minimum | Median (primary) | Maximum |
| --- | ---: | ---: | ---: |
| First Write | 91.1023 | **91.2425** | 91.6058 |
| Total Speak | 3816.7784 | **3818.1258** | 3841.9254 |

Every measured request: 170 frames / 170 Writes / 652800 bytes. PCM conversion
cost was not separately measured; it is included in Speak, with one 3840-byte
buffer. Report: `generated/perf/2m-sapi-benchmark.json`. Step 2L's separate host
observation was first PCM median 92.2589 ms and total median 3860.1418 ms;
these different sessions do not establish a speedup/regression. No mean is used.

## Current cancellation behavior (Step 2N)

GetActions is polled on the Speak caller thread at entry, while waiting for the
request gate and for PCM (10 ms timed condition-variable waits), immediately
before every Write/partial retry, after each full frame before sink acknowledgement,
and on completion. SPVES_ABORT requests a
request-scoped native stop source once. Both workers and orchestration cooperate,
queue/delivery waits wake, and the coordinator joins before Speak returns S_OK,
matching Chatterbox's requested-abort result. Real Write errors remain distinct;
SP_AUDIO_STOPPED is still returned exactly rather than assumed to be an abort.

Skip is acknowledged with CompleteSkip(0); rate/volume are queried without
waveform transformation. Lazy resident construction and native text preparation
now run on the joined coordinator, permitting caller-thread action polling while
they execute. The consumer still converts one PCM16 frame; the caller alone
writes it. No new neural worker or full PCM buffer is introduced.

The current operation/Write cannot be forcibly interrupted; cancellation takes
effect at safe boundaries. See [SAPI_CANCELLATION.md](SAPI_CANCELLATION.md) for
ownership, wake-up graph, deterministic checks, latency and memory. Windows SAPI
playback and WoW SpeakText/StopSpeaking remain MANUAL, after signing/verification/
registration. No live WoW integration claim is made from the fake host alone.

## Actions and repository state

SIGNING WAS NOT EXECUTED. NO CERTIFICATE WAS CREATED OR IMPORTED.
REGISTRATION WAS NOT EXECUTED. UNREGISTRATION WAS NOT EXECUTED.
REGISTRY WAS NOT MODIFIED. REGSVR32 WAS NOT RUN.
No certificate was installed; no currently registered voice was changed;
WoW was not launched, stopped or restarted. No scripts performed live actions.
Nothing was staged, committed or pushed during Step 2M.

Changed existing files: `CMakeLists.txt`, `tools/audio/wav.hpp`.
Created: `engine/include/outloud/pcm16.hpp`, this document,
`sapi/deployment-inventory.json`, `sapi/src/adapter.cpp`, `support.hpp`,
`identity.hpp`, `OutLoudPocketSAPI.def`, `sapi/tests/host.cpp`, `token.hpp`, and
seven `sapi/scripts` files: deployment-common, prepare-deployment,
sign-deployment, verify-deployment, registry-common, register and unregister.
Generated build binaries/reports/captures and reference hashes are ignored.
