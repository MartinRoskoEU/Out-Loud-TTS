# Codebase cleanup report

Date: 2026-10-09. Entry commit: `046385a` (`Updated registraction to the registry`).
This records implemented cleanup, with the existing comprehensive audit as context.
The pre-existing untracked `FULL_PROJECT_TECHNICAL_AUDIT.md` is preserved unchanged.

## Implemented removals and consolidations

| Decision | Component | Concrete change |
| --- | --- | --- |
| CONSOLIDATE | Native frontend error conversion (audit A15) | Removed both local `library_cause` and both pairs of `convert` overloads from `frontend/native/voice_manager.cpp` and `frontend/mutation/mutation.cpp`. One private header, `frontend/error_conversion.hpp`, now maps the fields. |
| CONSOLIDATE | Test scratch ownership (audit A18) | Removed local `Scratch` implementations from library/importer tests and relocated the SAPI utility to `tests/scratch.hpp`. Library, importer, token, multi-voice and bridge tests now use that single utility. |
| REMOVE | Superseded SAPI test utility file | Deleted `sapi/tests/scratch.hpp`; all executable consumers now resolve the shared test header. |
| CONSOLIDATE | Mimi checker's listening WAV writer | Removed its local `little` and `wav` functions, including a duplicate clamp/scale/ties-to-even conversion. Uses the existing `tools/audio/wav.hpp` and `outloud::float_to_pcm16`. |
| REMOVE | Unused native development display constant | Removed `outloud::sapi::voice_name` from `identity.hpp`. Repository-wide code/script searches found only its definition; it is neither an export nor a registration input. |
| REMOVE | Redundant import-operation constant | Removed `mutation::operation = 1`. Its sole test consumer now serializes the existing typed `Operation::import`; the protocol number remains 1. |
| REMOVE | Unused imports/includes | Removed `Path` and `struct` imports from `tools/assets/export_test_fixtures.py`; `Path` from `export_flow_sampler.py` and `export_generation_segment.py`; redundant algorithm/math includes from `tools/audio/wav.hpp`; importer test's obsolete clock include and redundant Windows macro/include block. |

No complete production legacy workflow was removed. The safe deletions are unused
symbols and superseded duplicate implementations; installed-voice compatibility
has explicit migration blockers described below. No standalone test case,
executable, library target, CTest registration, public API or export was deleted.

### Error mapping and boundaries

The shared header supplies field mapping only, instantiated for the two existing
private DTOs. Domain, code, message, path, library/import cause, tensor context,
system error and validation-path precedence retain their existing mappings.
The helper's `duplicate_ids` are copied only when that DTO has the field; the
read-only bridge does not acquire a duplicate-ID vector or allocate its contents.

C ABI view binding, result ownership, bridge allocation/error returns and
exception containment remain in the bridge. Authentication, privilege selection,
mutation coordination and version-3 serialization remain in the helper. Neither
component calls the other's entry point or shares process/registry authority.

### Test scratch ownership

The shared utility uses a random unique child under the validated temporary
parent, checks containment before creation, and removes junctions without
traversing their targets. Ownership is noncopyable. The library test's standalone
cleanup of its deliberately corrupt private fixture calls the same cleanup
function. Importer fixtures now use this root instead of a separate clock/PID
root in the build working directory. Test setup errors still throw; counting
constructor setup checks is no longer part of the library test's assertion total.
Security, corruption, rollback and lifecycle assertions remain.

`tests/asset_test.cpp` retains its specialized two-file corruption-fixture owner;
it does not implement a competing recursive scratch-directory utility.

### WAV conversion

For finite PCM the removed writer used exactly the same clamp, 32767 scale and
ties-to-even arithmetic as the existing shared conversion, with the same RIFF
fields and 24 kHz mono format. The shared function additionally rejects nonfinite
PCM; the checker already validates finite synthesized/reference output. This
change affects its optional listening-file writer, not Mimi decoding or inference.
Optional `--write-audio` listening files were not regenerated during this task.

## Build dependencies and documentation

CMake now supplies `tests/` to the five shared-scratch consumers and removes the
bridge checker's unnecessary `sapi/tests` include directory. The importer checker
links `outloud_voice_library` instead of directly linking `outloud_voice_import`:
the shared scratch utility uses the existing library filesystem validation/error
implementation, and the importer library is already its transitive dependency.
This is a test-only dependency; no production linkage changed. The first build
exposed that missing test dependency; it was corrected before successful builds.
No new external dependency, production library or configuration switch was added.

Updated active documentation:

- `VOICE_MANAGER_WPF.md`: shared private field mapping and test utility locations.
- `MULTI_VOICE_SAPI.md`: current exact-name description/409/Attributes Name policy.
- `MACHINE_VOICE_LIBRARY.md` and `VOICE_MANAGER_IMPORT.md`: current manual name lookup.
- `SAPI_VOICE_REGISTRY_AUDIT.md`: annotated only the relocated source link, retaining
  its audit-time location and historical observations/results.

The exact-name implementation was already present at entry and was not modified.
Historical deployment-token names, inventories, baseline measurements, provenance
and prior test outcomes were not rewritten. This report is the new current
cleanup record.

## Review decisions for retained and deferred components

Reviewed the versioned inventory, CMake target/fixture relationships, native public
and private interfaces, dynamic entry points, test harnesses, scripts, reference
exporters, frontend projects and prior audit dependency evidence. Searches for
single-reference private functions also identified `wWinMain` and `DllMain`;
these are Windows entry points and remain. Single-reference/public interfaces
were not treated as proof of dead code.

| Decision | Candidate / dependency chain | Reason |
| --- | --- | --- |
| DEFER | HKCU/user-scope library root, token operations, CLI scope and explicit-category script/tests | The registry audit documents a still-existing HKCU managed Gnome token and LocalAppData library ID `a4fb27ae6305b2c563711fdb512bcbb2`. Removing that chain would eliminate access/maintenance for actual installed user data. Requires an explicit support-retirement and migration decision. This evidence is from the previous audit; no new live registry inventory was taken. |
| DEFER | Legacy Gnome development token, relative AssetRoot resolution, register/unregister helpers | The audit records the installed `OutLoud_Pocket_GnomeMale_Development` token using relative runtime/voice paths. Current deployment scripts still prepare/register/verify that separate bundle and protect managed tokens. Removing its adapter support could break the installed voice; removing the scripts would remove the current deployment route. Migration/replacement infrastructure is required first. |
| KEEP | Native importer and Python OLV exporter | Native importer is the production validation/mutation boundary. Python exporter generates independent offline compatibility/parity references; it is not an abandoned competing UI importer. |
| KEEP | Voice Library CLI and managed-token CLI | Separate library inspection/recovery and token reconciliation operations remain useful for diagnosing partial outcomes. WPF does not expose orphan-token reconciliation or every recovery operation. CLI separation is explicit, not a prototype architecture to delete. |
| KEEP | Sequential generator, supplied-noise fixtures and worker reference mode | Worker correctness compares against independent sequential scheduling/numerical references. Replacing these with only the production worker path would lose independent regression evidence. |
| KEEP | Separate compiled audit/profile/trace implementations | Macros and separately compiled sources keep hooks/allocation probes out of normal inference while checking concurrency, cancellation and performance. The source inclusion is intentional, not redundant production linkage. |
| KEEP | Backend replacement source in the bridge checker | `OUTLOUD_REPLACEMENT_AUDIT` compiles the private failure-injection path; deleting this inclusion would remove rollback/partial/recovery coverage. |
| KEEP | Three performance analysis scripts and upstream benchmark/profile tools | They consume distinct captured reports/reference workloads; earlier milestone names do not make them equivalent. They also provide overlap analysis and reproducibility. No benchmarks or profilers were run. |
| KEEP | Asset/schema/tokenizer/neural exporters and `reference/pocket_oracle.py`, text-case generator | Needed to rebuild pinned assets and independent numerical/tokenizer/RNG references. Cached models/fixtures are not tracked cleanup candidates and were not deleted. |
| KEEP | Deployment prepare/common/inventory/sign/verify and registry-common helpers | Current x64 packaging, dependency closure, vendor-signature preservation, ownership and registry-view checks depend on them. None was executed for deployment, signing or registry modification. |
| KEEP | WPF services/interfaces, P/Invoke bridge, mutation helper, themes and Voice Tester | Required MVVM/native/privilege boundaries and active Import/Replace/Remove/playback/export functionality. Similar DTOs and separate error boundaries are intentional. |
| KEEP | OLP/OLV versions, explicit scope/ownership/type checks and rollback paths | Current format/data contract and security protections, not obsolete fallback branches. |

No abandoned alternative SAPI engine, earlier library implementation or genuinely
superseded executable was established in the versioned tree. Similar names alone
were insufficient evidence for deletion. No entire obsolete test dependency chain
was found after tracing these actual consumers and blockers.

### Test and checker decisions

| Decision | Targets / projects / scripts | Distinct coverage retained |
| --- | --- | --- |
| KEEP | `outloud-tokenizer-check`, `outloud-text-tests` | Canonical tokenizer parity and text preparation/splitting regressions. |
| KEEP | `outloud-asset-check`, `outloud-asset-tests` | Original/widened tensor parity and malformed/integrity/compatibility inputs. |
| KEEP | FlowLM, flow-sampler, generation and Mimi `-check` / `-tests` targets | Independent numerical checkpoints versus invalid shapes, reset, offsets, stop policies and lifecycle errors. The paired executables are not duplicate assertions. |
| KEEP | `outloud-voice-import-tests`, `outloud-voice-library-tests` | Parser/format conversion versus metadata, ownership, storage security and library lifecycle. Only shared setup bodies were consolidated. |
| KEEP | `outloud-voice-manager-tests`, `outloud-sapi-voice-tests`, `outloud-sapi-multi-check` | ABI/error fields and production coordinator failure injection; token transaction/scope/ownership checks; actual DLL multi-token synthesis and engine residency. |
| KEEP | `outloud-rng-check`, worker check/trace, utterance checker, registration-free `outloud-sapi-check` | RNG state/normal draws, worker scheduling/allocation/failure trace, full-utterance/cancellation barriers, COM/write/control/abort/bookmark behavior. Lack of CTest registration is not evidence of obsolescence. |
| KEEP | WPF console test project and supplementary validation project | Fake service/elevation/transport/ViewModel tests versus SDK-built XAML/BAML/navigation/theme/binding validation. |
| KEEP | `managed_voices_script_test.ps1` | Fake COM proves exact default/explicit category branching without real registrations or playback; catches a previously observed helper regression. |

## Before and after

Counts exclude the pre-existing untracked comprehensive audit and ignored artifacts.
Because nothing was staged, Git's index still contains 224 paths. The after column
counts surviving baseline paths plus this task's proposed new files, not a claim
that new files have been committed.

| Measure | Before | After |
| --- | ---: | ---: |
| Versioned-content candidates, including docs/config | 224 | 226 |
| Code/source files (`cpp/hpp/h/inc/cs/xaml/ps1/py`) | 176 | 177 |
| C/C++ source/header/include files | 104 | 105 |
| Configured native executable/library targets | 47 | 47 |
| Native correctness/check/trace executable targets | 22 | 22 |
| Registered CTest cases | 17 | 17 |
| Managed test/validation projects | 2 | 2 |
| Legacy support families considered for retirement: user scope, development token | 2 | 2 deferred |
| Implementations in the three consolidated groups: error mapper, directory scratch, WAV writer | 7 (2 + 3 + 2) | 3 (1 + 1 + 1) |
| Modified existing files | 0 | 21 |
| Deleted existing files | 0 | 1 |
| New task files (two headers, this report) | 0 | 3 |
| Total task changed/deleted/added paths | 0 | 25 |

The native count excludes CMake/CTest dashboard and build utility targets and
includes the available optional AVX2, RNG and orchestration checkers in this tree.
CTest uses 17 of the 22 correctness executable targets.
Code/config changes remove 170 lines and add 100, including the two new shared
headers: **70 fewer non-document lines**. File count increases by one source
header because shared frontend and test utilities occupy separate boundaries.
Simplification is fewer competing implementation bodies, not fewer runtime
features or a smaller target inventory. No runtime speed/memory claim is made.

## Executed validation

| Check | Result |
| --- | --- |
| All native components, Release x64 (`cmake --build build --config Release --parallel`) | PASS, including helper, bridge, SAPI DLL, libraries and all checker targets. Final build exit 0. |
| Existing complete CTest suite | **17/17 PASS**, 60.77 seconds; isolated scratch libraries and injected registries. |
| Final rebuilt Mimi checker after WAV consolidation | **1/1 PASS**, 9.52 seconds. Only this affected checker was repeated after the final tool edit. |
| WPF solution, Release x64, isolated output | PASS, **0 warnings / 0 errors**. |
| Frontend test executable from isolated Release output | **155 checks PASS**, fake services/elevation; no production native read. |
| Supplementary SDK-built WPF view validation | PASS: library/tester views, exact-ID selection, navigation, multiline text, fake Play/Stop/errors, themes/contrast, notifications, import/replace/remove, refresh and resize. No window shown. |
| Managed-voice PowerShell fake-COM regression | **49 assertions PASS**, no registry access or playback. |
| Python syntax | All 21 tracked Python scripts parsed; no asset export or upstream downloads executed. |
| Includes/build references | Remaining quoted native includes resolve; CMake regeneration and all-target compilation/linking pass. No active code/project reference to the removed SAPI scratch header, old local converters or removed constants remains. |
| Exports/dependencies | Dumpbin confirms x64 native/WPF images; bridge's six C ABI exports and SAPI's two COM exports remain. Existing tests load/check production DLL exports. Concrete imported DLLs exist in System32. |
| Helper loader policy | Dumpbin load-config retains dependent-load flag **0x0800**; asInvoker build/manifest declarations unchanged. |
| WPF copies | Isolated output bridge/helper SHA-256 values match the newly built native files. |
| Git whitespace | `git diff --check` passes. |

The first WPF build to the normal output directory failed at the bridge-copy
stage because the user's already-running Voice Manager, PID 25144, holds that
DLL open. No process was stopped. A successful solution build used a global
`OutputPath` override under ignored workspace artifacts:

```powershell
$env:DOTNET_GENERATE_ASPNET_CERTIFICATE = 'false'
dotnet build ui/OutLoud.VoiceManager.sln -c Release -p:Platform=x64 `
  '-p:OutputPath=C:\Users\rosko\Desktop\Nendis\application\wow-pocket-tts-sapi\generated\codebase-cleanup\wpf\Release\'
```

Successfully built executable:

```text
C:\Users\rosko\Desktop\Nendis\application\wow-pocket-tts-sapi\generated\codebase-cleanup\wpf\Release\OutLoud.VoiceManager.exe
```

This directory contains the exact updated `OutLoudVoiceManagerNative.dll` and
`OutLoudVoiceImportHelper.exe`. The bridge/helper import the three VC runtime
DLLs (`msvcp140`, `vcruntime140`, `vcruntime140_1`) plus Windows/UCRT contracts;
the SAPI DLL additionally imports `msvcp140_atomic_wait`. System32 availability
was checked; no vendor DLLs were copied or system files changed. Native project
libraries remain statically linked. Full deployment/prerequisite packaging remains
separate from a developer build.

Bridge SHA-256: `dc9b53f517ad3e328ddca1f57e5f0c1fbd030fc735ce88a7f870ce9866b1dcd2`.
Helper SHA-256: `b57d95d06beea5c363083c61946a0df0b7cacf19d76822377ff205d94f5451d0`.
Logs, dependency/PE/loader dumps and count data are under ignored
`generated/codebase-cleanup/`.

## Manual limits and remaining opportunities

Live WoW, real audio playback/listening exports, real elevated Import/Replace/Remove,
registered old-voice compatibility and the running application's visual state
were not exercised. Automated fake/in-memory checks support unchanged frontend
functionality; they do not certify those live integrations. No required isolated
check remained blocked; standalone RNG/worker/utterance/cancellation/profile
executables were built and retained but not rerun because their code paths were
unchanged and this cleanup's full CTests cover the affected components.

Separate decisions/work remain for retiring actual user-scope/development voices
and their deployment chain; that requires migration, not automatic deletion.
Fixture requirements for production-only configuration and product packaging
remain build/design work. Earlier audit findings concerning crash recovery,
path-string diagnostics, old-prefixed installed-token status and default
PowerShell parameter binding remain outside these cleanup edits. There is no
claim that consolidating utilities repairs those findings.

No HKLM/HKCU registrations, ProgramData/current Voice Library files, installed
DLLs, certificates or speech settings were modified. No real elevated helper,
registration/unregistration, deployment, signing or game action was executed.
No pre-existing ignored model/reference/cache/artifact files were deleted.
Nothing was staged, committed or pushed.
