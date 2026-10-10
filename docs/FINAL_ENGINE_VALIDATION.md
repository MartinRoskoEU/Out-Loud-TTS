# Final engine validation and signed development deployment

Date: **2026-10-10**. Source HEAD: `d42c2bfecbe972fc302a886b844cd31b34b9754b` (`Optimization - before performance check`). The tree was clean at entry. All **262 entry source files** still match their saved hashes. The only new tracked-content candidate is this report; audit hosts, scripts, captures and logs are ignored under `generated/final-validation/`. No implementation, inference algorithm, UI, ABI, protocol or format was changed.

**Result:** production build passed; the complete A12 correctness selection passed **31/31**; old/current end-to-end PCM is exact for the measured configurations; the current engine was signed with the existing trusted development certificate and actually installed at the unchanged registered path. Post-deployment signature, hash, short synthesis, registry and asset checks passed. WoW testing remains manual.

## Source and build

Incremental Visual Studio 2022 / MSVC 14.44, Windows SDK 10.0.26100.0, Release x64. Production cache confirms `BUILD_TESTING=OFF`, `OUTLOUD_BUILD_DEVELOPMENT_TOOLS=OFF`, `OUTLOUD_ENABLE_AVX2=ON`. No clean build, dependency installation, downloads or reference regeneration. Existing offline fixtures were available. Production wiring excludes development fixture headers/checkers.

```powershell
cmake -S . -B build-production -A x64 -DBUILD_TESTING=OFF -DOUTLOUD_BUILD_DEVELOPMENT_TOOLS=OFF -DOUTLOUD_ENABLE_AVX2=ON
cmake --build build-production --config Release --parallel --target OutLoudPocketSAPI OutLoudVoiceManagerNative OutLoudVoiceImportHelper outloud-voice-import outloud-voice-library outloud-sapi-voices
```

All six required production images compiled/linked successfully and have PE machine `0x8664` (AMD64). These hashes are **unsigned workspace build outputs**, before signing staged copies:

| Image in `build-production/Release` | Bytes | SHA-256 |
| --- | ---: | --- |
| `outloud-sapi-voices.exe` | 373,760 | `48425a5539ff7e9c5df28a5dbafa0a49082736e69a3724f02e844fc5733f21f0` |
| `outloud-voice-import.exe` | 114,176 | `7c585f434e6590edcc0ddc3a52ac3c1f05d0e4477d8762c2c10cc3cdb3daf8b7` |
| `outloud-voice-library.exe` | 310,784 | `21e67628f3ef54bb88e066143322febff1472d074f18ab02f907585441e45ed0` |
| `OutLoudPocketSAPI.dll` | 280,064 | `50ef5510c6abdfaef33ae9b70bb29dd16ec9b08b2aa13c29c600972c99541c36` |
| `OutLoudVoiceImportHelper.exe` | 425,472 | `feb326b66c1c0f3f5e15dbbaa7067053a69e83ec171fe6ee5a88268d4fc5e400` |
| `OutLoudVoiceManagerNative.dll` | 257,536 | `d9a56b21c2c1354ea5fb6dc113e7eed9de56c201c25a9459fcd6e875ed6b1f01` |

Production, development-checker and fresh frontend builds reported **zero compiler/linker warnings or errors**. The temporary audit host initially failed `/WX` on C4244 while narrowing its segment-limit string for JSON; replacing that conversion with numeric parsing produced a warning-free build. No production source change was needed.

The development tree was configured with `BUILD_TESTING=ON`, AVX2 enabled, retaining its existing development-tool option. Only checkers needed by A12 were built. Fresh Release x64 builds of the existing frontend test and WPF validation projects used `NativeBuildDirectory=build-production` and `--no-restore`; the validation project rebuilt the WPF app dependency. Both reported **0 warnings / 0 errors**. No window or elevated helper was launched.

Production SAPI retains exactly `DllGetClassObject` and `DllCanUnloadNow`; the bridge retains its six C exports and ABI v1. Helper protocol remains v3, OLP version 1 and OLV version 1/schema 1. The fixed CLSID remains `{F38C3398-3552-43B9-8350-7DD301327D70}`. Entry-source equality and existing ABI/protocol/format regressions support these checks.

The engine statically links project inference libraries. Its concrete imports are `ole32`, `OLEAUT32`, `SHELL32`, `bcrypt`, `KERNEL32`, `MSVCP140`, `MSVCP140_ATOMIC_WAIT`, `VCRUNTIME140`, `VCRUNTIME140_1`, plus Windows UCRT API-set contracts. Shell32 supports the logger known-folder lookup. Concrete dependencies are present; the actual engine load/synthesis passed. No additional project DLL, Python, PyTorch or neural runtime is loaded by this engine. Dumpbin records for all six production images are retained.

## Complete correctness selection

Executed **once**, serially, through the existing A12 entry point:

```powershell
.\scripts\validate.ps1 -Mode Correctness
```

**Discovered 31; passed 31; failed 0; Not Run 0; unavailable 0. Total wall time 304.22 seconds.** No assertions/tolerances were weakened and no test was rerun. The fixture availability gate passed; the AUTO utterance fixture was produced by its existing correctness checker, not an upstream reference regeneration.

| Existing test | Result | Seconds |
| --- | --- | ---: |
| `canonical-tokenizer-parity` | PASS | 0.04 |
| `text-pipeline-regressions` | PASS | 0.07 |
| `canonical-asset-check` | PASS | 0.95 |
| `asset-regressions` | PASS | 1.38 |
| `canonical-flowlm-prefill` | PASS | 1.09 |
| `flowlm-regressions` | PASS | 2.62 |
| `canonical-flow-sampler` | PASS | 0.86 |
| `flow-sampler-regressions` | PASS | 0.67 |
| `canonical-generation-segment` | PASS | 6.75 |
| `generation-regressions` | PASS | 5.59 |
| `canonical-mimi-decoder` | PASS | 11.09 |
| `mimi-regressions` | PASS | 3.13 |
| `voice-import-regressions` | PASS | 0.72 |
| `voice-library-regressions` | PASS | 1.70 |
| `sapi-input-regressions` | PASS | 0.04 |
| `sapi-asset-diagnostics` | PASS | 0.04 |
| `voice-manager-bridge-regressions` | PASS | 20.08 |
| `sapi-token-regressions` | PASS | 7.78 |
| `sapi-multi-voice-regressions` | PASS | 11.52 |
| `rng-determinism` | PASS | 0.03 |
| `worker-scheduling-portable` | PASS | 6.71 |
| `worker-rng-portable` | PASS | 6.65 |
| `extended-fixtures-available` | PASS | 0.02 |
| `utterance-cancellation-portable` | PASS | 88.20 |
| `worker-scheduling-auto` | PASS | 3.27 |
| `worker-rng-auto` | PASS | 3.12 |
| `utterance-cancellation-auto` | PASS | 41.26 |
| `sapi-streaming-cancellation-bookmarks` | PASS | 75.75 |
| `managed-voices-script-regressions` | PASS | 0.69 |
| `frontend-OutLoud.VoiceManager.Tests` | PASS | 0.36 |
| `frontend-validation` | PASS | 2.01 |

This covers tokenizer/text, model/voice integrity, FlowLM, FlowSampler, generation, Mimi, RNG, both worker ISA configurations, utterance cancellation/recovery, multi-voice SAPI, library/token/bridge coordination, fake-COM PowerShell and frontend correctness. Current frontend checks passed **156 assertions**; managed-voice PowerShell passed **26 assertions**. WPF validation exercised SDK-built views, theme/binding/navigation/refresh/selection/resize and fake playback/mutations without showing a window. Library mutations and registry failure injection used existing test-owned scratch/in-memory registries. No production helper/UAC or real voice management operation ran.

## End-to-end numerical and lifecycle evidence

The measured path is text -> native preparation/tokenizer -> FlowLM -> FlowSampler/seeded RNG -> resident worker Mimi -> FP32 -> PCM16 -> caller-thread SAPI Write -> completed Speak. Registration-free COM uses an in-memory token. Format is PCM16, mono, 24 kHz. No audio device or default speech setting was changed.

| Workload/configuration | Samples | PCM16 bytes | Writes/frames | Audio seconds | Equality evidence |
| --- | ---: | ---: | ---: | ---: | --- |
| Gnome, `Hello traveler.`, AUTO | 36,480 | 72,960 | 19 | 1.52 | Preserved installed/current/pre-logger exact bytes |
| Gnome, canonical watch-captain medium, AUTO | 326,400 | 652,800 | 170 | 13.60 | Preserved installed/current/native fixture exact bytes |
| Gnome, same short, Portable | 36,480 | 72,960 | 19 | 1.52 | Preserved installed/current exact bytes |
| Actual managed Undead Male, short, AUTO, limit 128 | 40,320 | 80,640 | 21 | 1.68 | Signed staged/deployed exact bytes |

The complete canonical medium text is the existing `sapi/tests/host.cpp` watch-captain utterance, unchanged. Benchmarks use seed **73**, maximum segment tokens **51**, the same Gnome OLV and the same registered model/tokenizer files. Actual installed-voice smoke uses that token's seed 73/AUTO/limit **128**, as recorded separately. Different voice/configuration outputs are not treated as interchangeable fixtures.

Model SHA-256: `30889426a3bed523e725baafe70ec9064044898aa203975980741be1f3d70406`. Tokenizer: `45ffaf8c702dbb6c954bd54280d83ea00f8bf1d47692aca37e078c8deb26935a`. Workspace Gnome: `ff138495b7d3834c2eabb926ff7226a2983e7af17cf5a0c6b4cad05ae6a5bb00`; workspace Undead Male: `8955df736a8b50ca6ff35b81a5fbdf4f3f9e1f38a6230cf39b29ca7df23ce183`. Installed managed Undead Male OLV: `b2aad5d3c6c9627a3d04fc84a5655498d1008c144c31bcb6da70a467ab0e0ebf`. All were validated read-only.

Native canonical full FP32 SHA-256 remains `36788a2c1cfde712fb9ef68151a93b1b78eec40b1e440e99ff1b1b59d1a8bcf0`. Conversion outside benchmark clocks exactly matched the current and old SAPI medium PCM16 (`ab34b5d2d877564298acf2dfda96fb320c6370d1c554a450e3cdcc57086d7f50`). AUTO short SHA-256 is `7582a7c4c1b60052c8c372843c1942d3c7065222f831c319361d17c51321b82a`; Portable short is `1687e1f2aca2db25ee00244b11b051aca6cc2d779e0c522196981e823f1edc3a`. No cross-ISA bitwise claim is made.

Existing multi-voice tests passed distinct Gnome/Undead native-exact PCM, alternating/concurrent engines, token reassignment/failure and cancellation recovery. Full native tests passed eight deterministic cancellation barriers on each ISA, exact same-pipeline recovery, repeated synthesis and sink-failure recovery. The SAPI host passed normal/partial Writes, controls, bookmarks and abort/recovery, including real Windows SAPI parser/site tests using unregistered in-memory tokens. COM lifetime/unload checks passed. The focused private logger adapter additionally injected worker exceptions/allocation failure and recovered the exact normal short PCM on the same engine.

## ERROR-only logger

Current production builds explicitly include `sapi/src/error_logger.cpp`. The engine contains its fixed ERROR record and log-file literals and only the two normal COM exports; private configuration/attempt exports and private worker messages are absent. Source review retained lazy ERROR-only behavior, fixed sanitized event/kind fields, three files of at most 2 MiB each, 10 ms best-effort cross-process mutex acquisition and HRESULT/Win32/IErrorInfo preservation. No text/path/voice-name diagnostic field was introduced.

Ran only the existing **adapter portion** of the focused logger harness, through an ignored wrapper linked against the existing infrastructure. It used the private test DLL and a test-owned scratch root: real asset/read/invalid-data failures, Write/bookmark failure, worker failure/allocation failure, privacy, exactly one attempt per error, unchanged HRESULT/IErrorInfo, cancellation/recovery and module unload all passed. Successful/repeated synthesis, entry/stream abort, SP_AUDIO_STOPPED and recovered requests made **zero logger calls**, and the quiet directory was never created. Zero entry calls imply no logger file open/write path was reached; this is not an OS-wide file-I/O trace.

The prior full rotation/oversize/access/open/disk/Win32 preservation/mutex/4-thread/4-process campaign remains the evidence recorded in [NATIVE_ERROR_LOGGING.md](NATIVE_ERROR_LOGGING.md). Its relevant logger sources match entry hashes; no regression was found, so that entire campaign was **not repeated**. Error injection never targeted the real LocalAppData log root. Rare error-path latency, all possible control errors, real disk exhaustion and cross-integrity sessions remain unmeasured.

Pre-logger versus current AUTO short timing appears in the complete table below. First Write median increased **2.90%** and total decreased **3.03%**, with overlapping ranges in both. These do not isolate or bound logger branch/code-layout overhead; no exactly-zero overhead claim is made.

## Historical baseline and measurement method

The primary baseline is the exact signed, working installed engine preserved before replacement, SHA-256 `a131176aa831d9f3ac38665a7b31fdd3141c142b109c9541b277ca4d2eb0f9f4`, 260,912 bytes. Its existing manifest was created **2026-10-05T15:08:46Z**, and its engine signed hash exactly matched the actual file. That date predates the documented October 9 A08/A09 changes, suggesting those changes are absent. However, the old receipt has no exact source revision/full build option record: it is labeled **previous deployed working engine**, not a certified fully unoptimized build. It already has the historical two-worker, RNG, orchestration, SAPI cancellation/bookmark implementation; not every prior optimization can be reconstructed from its receipt.

The separate pre-logger baseline is `generated/error-logging/pre-logger.dll`, SHA-256 `56ae12bfbba39a7a0edaf345238615700ea734c46b3bcd8eed02851c2c5cf7e2`. It preserves the already optimized A08/A09 workspace engine before logger changes and serves only the small logger comparison. Frozen A08/A09 executables measure isolated/three-frame workloads and were inspected as historical evidence but not pooled into these total-synthesis results. No old source revision was reconstructed or working files reset.

Current baseline identity before signing: `50ef5510c6abdfaef33ae9b70bb29dd16ec9b08b2aa13c29c600972c99541c36`, the Release x64 production DLL from this HEAD. Signing changes container bytes/signature only; the signed staged engine passed synthesis before installation.

The ignored `perf_host.cpp` reuses the existing fake token/COM setup and production PCM format. Each baseline/current configuration uses the **same complete workload, assets, seed, segment settings, resident two-worker architecture and clock boundaries**. Assets and tokenizer are validated before generation timing. Each short/medium mode receives **one warm-up generation, then three timed repetitions** on a resident engine. Native prepare resets/reseeds per Speak as before. Scheduling/sample counts/HRESULT checks run after the end timestamp; timed Site::Write only records first arrival and accepts/counts bytes, with no PCM capture or profiler/hook. Untimed warm-up captures and fixture/old-new comparisons, raw JSON and PCM writes are outside timed regions. No WAV was written.

No correctness tests, compilers or other synthesis benchmarks ran concurrently with timing. Existing user processes remained running; no affinity/clock control or process termination was used. A brief read-only installed-file inventory occurred during the portable baseline run; portable results are treated as overlapping/inconclusive. Measurement order is old AUTO, current AUTO, old/current portable short, then pre-logger AUTO short; it was not randomized. Three measurements do not provide a statistical release bound; no extra timing repetitions were added.

CPU: AMD Ryzen 9 5900X. The compiled runtime dispatcher reports **AUTO -> AVX2/FMA**; explicit Portable was also measured. The old DLL has no ISA diagnostic export, but its AUTO short output differs from its explicit Portable output and exactly matches current AVX2/FMA PCM, corroborating equivalent dispatch. Portable and AVX2 results are compared only with their matching baseline.

Total time is synchronous `ISpTTSEngine::Speak` entry to return, including native request preparation, synthesis, conversion, caller delivery and coordinator join. First Write is the first PCM16 delivery proxy, including adapter overhead; it is not an internal Mimi timestamp. **RTF = wall seconds / generated audio seconds; lower is better.** No inverse speed factor is mixed into the table. Warm preparation/prefill and first acoustic latent are not separately observable through these unchanged DLL interfaces and were not fabricated or instrumented.

One cold-resident short warm-up observation is retained separately: previous AUTO first/total **301.1649 / 593.3937 ms**, current AUTO **304.7139 / 587.1518 ms**; previous Portable **365.8102 / 915.5654 ms**, current Portable **359.1482 / 882.5810 ms**. These include lazy runtime/asset initialization and synthesis, not isolated constructor cost, and have **one observation each**, no min/median/max distribution or speedup inference. Medium warm-ups use that already resident engine. Cold and warm results are not pooled.

## All warm performance results

All time rows are ms; RTF rows are dimensionless. Difference is **current median minus previous median**; positive means slower. Min/median/max show all three observations. "Overlap" explicitly marks timing-range overlap.

| Baseline / case | Metric | Previous min / median / max | Current min / median / max | Median difference | Difference % | Overlap |
| --- | --- | --- | --- | ---: | ---: | --- |
| Deployed AUTO / short | First Write | 47.7687 / 48.6353 / 53.5499 | 46.0236 / 48.2978 / 48.8291 | -0.3375 | -0.69% | Yes |
| Deployed AUTO / short | Total Speak | 336.3470 / 338.3082 / 348.6304 | 306.6886 / 309.7880 / 320.3448 | -28.5202 | -8.43% | No |
| Deployed AUTO / short | RTF | 0.221281 / 0.222571 / 0.229362 | 0.201769 / 0.203808 / 0.210753 | -0.018763 | -8.43% | No |
| Deployed AUTO / medium | First Write | 96.9043 / 100.1642 / 101.9756 | 103.7711 / 106.6487 / 113.6659 | +6.4845 | +6.47% | No |
| Deployed AUTO / medium | Total Speak | 3594.7232 / 4004.4667 / 4127.2356 | 2997.7619 / 3025.4392 / 3501.4020 | -979.0275 | -24.45% | No |
| Deployed AUTO / medium | RTF | 0.264318 / 0.294446 / 0.303473 | 0.220424 / 0.222459 / 0.257456 | -0.071987 | -24.45% | No |
| Deployed Portable / short | First Write | 102.8952 / 103.2849 / 103.9603 | 101.3146 / 102.3612 / 103.6418 | -0.9237 | -0.89% | Yes |
| Deployed Portable / short | Total Speak | 646.3148 / 654.8538 / 794.2403 | 625.2993 / 626.1122 / 693.8783 | -28.7416 | -4.39% | Yes |
| Deployed Portable / short | RTF | 0.425207 / 0.430825 / 0.522527 | 0.411381 / 0.411916 / 0.456499 | -0.018909 | -4.39% | Yes |
| Pre-logger AUTO / short | First Write | 46.0570 / 46.9348 / 47.6243 | 46.0236 / 48.2978 / 48.8291 | +1.3630 | +2.90% | Yes |
| Pre-logger AUTO / short | Total Speak | 319.1386 / 319.4788 / 321.0592 | 306.6886 / 309.7880 / 320.3448 | -9.6908 | -3.03% | Yes |
| Pre-logger AUTO / short | RTF | 0.209960 / 0.210183 / 0.211223 | 0.201769 / 0.203808 / 0.210753 | -0.006376 | -3.03% | Yes |

**Improvements:** AUTO short total median **338.3082 -> 309.7880 ms (-8.43%)** and medium **4004.4667 -> 3025.4392 ms (-24.45%)**, with disjoint observed total-time ranges. This measures full synthesis, not combined isolated optimization percentages.

**Regression:** AUTO medium first Write **100.1642 -> 106.6487 ms (+6.4845 ms, +6.47%)**, with disjoint observed ranges. This unfavorable first-output result is retained and is not explained away by the faster total. It is an observed latency regression in this session, not a statistically established universal bound or a demonstrated neural correctness defect.

**Inconclusive:** AUTO short first Write (-0.69%), Portable first Write (-0.89%) and Portable total (-4.39%), plus both pre-logger comparisons, have overlapping ranges. Portable total spread and AUTO medium total variation are visible in the table. These smaller differences cannot be reliably attributed to implementation changes.

**Unchanged:** sample/frame counts, audio duration, seed/settings and exact same-ISA captured PCM are unchanged. No exactly-equal timing metric was observed. Formal timing-equivalence/regression bounds remain inconclusive with three runs and ordinary scheduling variation. Overall, no meaningful total-synthesis slowdown was observed in these workloads; AUTO complete-time medians improved, while first-output latency needs manual assessment.

No process RAM/CPU profiling was performed. Existing design retention costs remain distinct from timing/numerical regressions: A08 retains 7,864,320 bytes of prepared immutable Mimi phase payload and A09 adds 4,096 sampler inline bytes plus reusable prefill output capacity dependent on the largest input. These are previous design calculations, not fresh process-memory measurements. This task introduced no retained neural storage.

## Signing and dependency coverage

The working installed signer was verified before signing. Existing certificate: **CN=Out Loud Development Code Signing**, thumbprint `C0D1C865319C9047D1043E2478A0BDF23CA76634`, accessible private key in `Cert:\CurrentUser\My`, code-signing EKU, expiry **2031-10-05 UTC**. Read-only inspection found that same certificate in the existing `CurrentUser\Root` trust store. No certificate/store/trust change or key export occurred.

Used inspected existing `prepare-deployment.ps1`, `sign-deployment.ps1`, `verify-deployment.ps1` with **explicit roots/paths**, avoiding historical parameter-default ambiguity. Production contains no checker, so a workspace stage-input directory combines the production engine and newly rebuilt development checker solely to satisfy the existing six-image staging policy. Model/tokenizer are read-only copied into the workspace stage. No registration/unregistration script was executed.

```powershell
.\sapi\scripts\sign-deployment.ps1 -DeploymentRoot "$PWD\generated\final-validation\signed-stage" `
    -CertificateThumbprint C0D1C865319C9047D1043E2478A0BDF23CA76634 `
    -CertificateStoreLocation CurrentUser `
    -SignToolPath 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe'
.\sapi\scripts\verify-deployment.ps1 -DeploymentRoot "$PWD\generated\final-validation\signed-stage" -RequireSignatures
```

SignTool SHA-256 Authenticode signs **staged copies** of the engine and checker. `/pa /all /v` verification and Get-AuthenticodeSignature **Valid** passed for all six staged AMD64 images; SignTool reported zero warnings/errors. The four Microsoft VC images retained their vendor signatures and hashes. No new timestamp was added, consistent with the existing untimestamped development engine; this is development signing, not a release publisher/timestamp policy.

WoW loads one project DLL, `OutLoudPocketSAPI.dll`, plus the four Microsoft VC runtimes and Windows/UCRT dependencies. Those vendor DLLs were already valid, AMD64 and identical to verified staged copies, so no replacement/signing of them was necessary. Frontend bridge/helper/CLIs are not dependencies of this DLL and were built but not installed or signed in this engine-only deployment. The newly signed checker stays in the workspace stage; the installed checker was left untouched.

| Engine identity | Bytes | SHA-256 | Signature |
| --- | ---: | --- | --- |
| Original installed / exact backup | 260,912 | `a131176aa831d9f3ac38665a7b31fdd3141c142b109c9541b277ca4d2eb0f9f4` | Valid, existing development signer |
| New unsigned production | 280,064 | `50ef5510c6abdfaef33ae9b70bb29dd16ec9b08b2aa13c29c600972c99541c36` | Not deployed unsigned |
| New signed staged / actual installed | 281,904 | `e1c7115d42200f3f804bbcb5cd6e583cc15a51f895ce4d70e027d04e2956fd86` | Valid, same signer and trust |

Staged signed checker: 228,144 bytes, SHA-256 `5e4dcfe57bda89e20eaa3980ab82514a5ea707528afdd8bde0d34548bbf68fea`. Original installed checker remains hash `f08e5b9cf330dd34e557f38160595ea74f3df13f7bb112244d710c87087590bf`. Full staged image/runtime signature records are retained in signing/verification logs.

## Actual deployment and post-deployment checks

**Deployed successfully at 2026-10-10T01:43:36Z.** Read-only HKLM64 COM inspection established this destination before any installed mutation:

```text
C:\OutLoud\OutLoud-Pocket-development-01\bin\OutLoudPocketSAPI.dll
```

Source was the verified signed staged DLL:

```text
C:\Users\rosko\Desktop\Nendis\application\wow-pocket-tts-sapi\generated\final-validation\signed-stage\bin\OutLoudPocketSAPI.dll
```

Exact original backup, created fresh before replacement and verified again afterward:

```text
C:\Users\rosko\Desktop\Nendis\application\wow-pocket-tts-sapi\generated\final-validation\backup-20261010T012708Z-2f557b33f9874f13b73070d7fc0913a2\OutLoudPocketSAPI.dll
```

No WoW process was running. Immediately before replacement, all enumerated process module lists were inspected: **zero target holders and zero uninspected processes**; a read/write exclusive-open probe passed without writing. The target and ancestors were checked against the exact registered path and for reparse points. Existing registry/file snapshots, original and backup hashes, staged signature and installed vendor dependencies were rechecked. No elevation or security bypass was used.

A verified temporary sibling copy was atomically exchanged with the DLL using Windows File.Replace, preserving destination security. Before the successful replacement, audit automation encountered a strict-mode heterogeneous-JSON field check and PowerShell's null-string binding to File.Replace. Both stopped with the old installed DLL intact; corrected guards and `[NullString]::Value` were validated in workspace artifacts. Failed-attempt logs are retained. No production implementation was modified.

After replacement: installed SHA-256 exactly equals signed staged hash; AMD64 and signer/trust are Valid; installed SignTool `/pa /all /v` passes with zero warnings/errors. Registration remains at the same path with the same CLSID and `ThreadingModel=Both`. Full typed registry snapshots of speech tokens and engine classes across HKLM/HKCU/HKCR 64/32 views compare equal. All four current managed token paths still exist; model/tokenizer/voice hashes match before.

Both installation and ProgramData library inventories cover **30 files**. Exactly one changed file is authorized/expected: the engine DLL. All **29 other file hashes/sizes** and the file-name inventory are unchanged, including installed checker, vendor runtimes, old deployment scripts/docs/manifests, model/tokenizer and all current voice/metadata files. No real registry/library mutation was performed.

Post-deployment smoke directly loaded the installed DLL registration-free with the actual managed Undead Male file and unchanged seed/AUTO/limit-128 values. It returned S_OK with **21 Writes, 40,320 samples, 80,640 bytes, 1.68 seconds** of mono 24 kHz PCM16, exactly matching staged PCM, SHA-256 `7c6fb7df702cd90c32ac02b293d0edc19f14d4283518f99ca295451b78a20134`. The host released COM objects, reached DllCanUnloadNow=S_OK and unloaded. This verifies local synthesis/dependencies, not audible playback or a new WoW session.

**Rollback was not needed.** The original signed backup remains hash-identical and valid. The controlled replacement script included guarded restoration on a critical post-check failure; that restoration branch was not exercised against the real installation.

**Installed manifest limitation:** this existing installation still has the historical schema-1 inventory/manifest describing its original engine. Only binary replacement was authorized, so those metadata files were deliberately left unchanged; their old engine hash is now historical/stale. The modern schema-2 staged inventory passed its own verifier, but no claim is made that the old full installed bundle now passes modern hash verification. Actual installed identity is recorded in `deployment-receipt.json`, `post-integrity.json` and this report. Updating installed packaging metadata belongs to separately authorized maintenance/release work.

## Assessment, manual work and evidence

1. **Numerical correctness:** verified within the complete existing selection and captured same-ISA workloads; no unexpected PCM/numerical differences. Not a proof for every voice/text/platform.
2. **Total performance:** no meaningful slowdown detected in tested complete workloads; AUTO medians improved. A statistical regression bound remains inconclusive. The medium first-Write latency increase is explicitly retained.
3. **Installation:** actually updated, signed hash verified at the unchanged development path.
4. **Signing:** the required engine and staged checker are properly signed/trusted; all actual engine vendor DLLs remain validly signed.
5. **Manual WoW readiness:** ready for the user's fresh-client short/medium playback, voice switching and StopSpeaking test. No game was started/stopped or inspected internally, and no user application was terminated.

Before a general release, unresolved work includes real WoW/audio-device playback with the new DLL, a product-wide signing/deployment/manifest/timestamp policy, installer/licensing work (A03/A04, intentionally not begun), and broader hardware/long-workload/performance-memory evidence as appropriate. These do not block the completed controlled development deployment. A07 remains deferred.

Raw evidence includes production/development/frontend build logs, six PE dumps, entry source hashes, 31-test selection and complete run/LastTest logs, correctness-results JSON, private logger adapter log, audit CMake/C++ sources, every three-run timing JSON and captured PCM, comparison JSON, registry/file pre/post inventories, certificate/trust public metadata, staged signing/verification logs, failed pre-replacement automation logs, process scan, deployment receipt and installed smoke/signature logs. All are ignored under `generated/final-validation/`; prior measurements and documents were not overwritten.

Nothing was staged, committed or pushed. No certificates/trust, registration, voices, model/tokenizer files, speech settings, neural algorithms or UI were changed. The only installed-system change was the authorized signed engine DLL replacement.
