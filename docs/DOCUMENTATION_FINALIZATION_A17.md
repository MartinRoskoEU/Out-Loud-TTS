# A17: documentation audit and consolidation

Date: 2026-10-09. Source baseline: `046385a`, with the earlier cleanup,
retirement, stabilization, A06/A08/A09 and A11/A12 work already in the working
tree. This task preserves that implementation and changes Markdown only.
Current source defines supported behavior; prior reports retain historical
evidence. No build, correctness test, benchmark or installed-host check ran here.

## 1. Original documentation problems

The README did not explain the complete production/runtime/preparation boundary,
Voice Manager workflow or release status. Technical information was spread across
milestone records, making historical commands look like current procedures.
Some active management guides still described future legacy retirement or deferred
build work after those phases had completed. Older native/WPF documents lacked
clear pointers to current architecture and build instructions.

The A11/A12 guide was already accurate: its production switches, WPF integration,
fast/broader selections and measured-versus-unexecuted results needed no changes.
Current managed-token naming, safe runtime-path comparisons and removal recovery
were also already documented in their active guides; these were retained, not
implemented again.

## 2. Active documents reviewed

| Current authority | Responsibility |
| --- | --- |
| [README](../README.md) | Project entry, architecture diagram, prerequisites, builds, validation and distribution status. |
| [ARCHITECTURE](ARCHITECTURE.md), new | Current native runtime composition, ownership, caches, dispatch, streaming and cancellation. |
| [VOICE_MANAGER_GUIDE](VOICE_MANAGER_GUIDE.md), new | Current GUI operations, privilege/result boundaries, testing/export, health and recovery. |
| [VOICE_LIBRARY](VOICE_LIBRARY.md), [MACHINE_VOICE_LIBRARY](MACHINE_VOICE_LIBRARY.md) | Storage, identity, security, supported machine scope and maintenance boundaries. |
| [MULTI_VOICE_SAPI](MULTI_VOICE_SAPI.md), [SAPI_ADAPTER](SAPI_ADAPTER.md) | Managed tokens/diagnostics versus shared-engine deployment/registration. |
| [NATIVE_ASSETS](NATIVE_ASSETS.md), [VOICE_IMPORT](VOICE_IMPORT.md) | Current container/import contracts plus explicitly historical execution evidence. |
| [BUILD_AND_VALIDATION_IMPROVEMENTS](BUILD_AND_VALIDATION_IMPROVEMENTS.md) | Authoritative current build/validation procedure, also retaining A11/A12 execution evidence. |
| [RNG_NOTICES](../RNG_NOTICES.md) | Component notices; not a complete product license or redistribution determination. |

Source inspection covered root CMake and its development/validation includes,
PowerShell build/validation/deployment scripts, relevant native headers/loaders,
worker/orchestration/cache implementations, SAPI adapter/token/registry code,
frontend bridge/coordinator and WPF ViewModels/services/projects. Earlier audit,
cleanup, retirement, stabilization and optimization reports supplied context.

## 3. Documents updated

The README now summarizes the two related paths: local model/tokenizer/voice to
native inference to SAPI5 to WoW, and prepared voice import through confirmed
helper mutation to the machine library and owned tokens. Advanced material lives
in the two new current guides instead of expanding the README into a manual.

Active updates: `MACHINE_VOICE_LIBRARY.md`, `VOICE_LIBRARY.md`,
`MULTI_VOICE_SAPI.md`, `SAPI_ADAPTER.md`, `NATIVE_ASSETS.md`, `VOICE_IMPORT.md`.
This report is the third new document.

Brief historical/current-authority notices were added to `POCKET_PIPELINE.md`,
the eight native text/FlowLM/sampler/generation/Mimi/worker/orchestration/RNG
milestone documents, six WPF/import/replace/remove/tester/theme milestone
documents, `SAPI_CANCELLATION.md`, `SAPI_BOOKMARKS.md` and
`FULL_PROJECT_TECHNICAL_AUDIT.md`. The WPF record's duplicate current build block
now links to A11/A12. Historical measurements and conclusions were retained.

## 4. Historical documents intentionally preserved

The following remain historical records, not current instructions:

- `*_HISTORY.md`: original library, machine-library, managed-token and SAPI
  implementation/installation evidence, including retired commands.
- `POCKET_PIPELINE.md` and `NATIVE_*.md` milestone evidence, except the active
  asset/import contracts identified above. Earlier proposals do not establish
  current production dependencies. Their detailed component descriptions can
  still be useful with the current-source notices.
- `VOICE_MANAGER_WPF.md`, `VOICE_MANAGER_THEME.md`, `VOICE_MANAGER_IMPORT.md`,
  `VOICE_MANAGER_REPLACE.md`, `VOICE_MANAGER_REMOVE.md`, `VOICE_MANAGER_TESTER.md`:
  implemented-step designs, validation and earlier protocol/build observations.
- `SAPI_CANCELLATION.md`, `SAPI_BOOKMARKS.md`: original COM/native validation,
  latency and memory observations; later stabilization remains separate evidence.
- `FULL_PROJECT_TECHNICAL_AUDIT.md`, `CODEBASE_CLEANUP_REPORT.md`,
  `LEGACY_ARCHITECTURE_REMOVAL.md`, both `STABILIZATION_PHASE_*.md`,
  `SAPI_VOICE_REGISTRY_AUDIT.md`, `POCKET_PROSODY_AUDIT.md`: dated findings and
  state, including installed registrations that the user says were later removed.
- `PERFORMANCE_BASELINE.md`, `PERFORMANCE_OPTIMIZATION_2I.md` and A06/A08/A09:
  unchanged workload-specific numerical/performance evidence and limits.

The existing A11/A12 report and these cleanup/retirement/stabilization/performance
reports were not rewritten. Reference/oracle material remains preparation and
independent verification material, not a production Python runtime. No historical
report was deleted or mass-reformatted. Legacy installation snapshots are not
presented as mandatory cleanup work for the user's current installation.

## 5. Incorrect or outdated instructions corrected

Production instructions explicitly disable both testing and research tools.
Development instructions distinguish fixture preparation, ordinary fast checks
and explicit broader validation. Native asset/import documents point to the
central guide instead of directing current users through a whole historical
build/test campaign. WPF's duplicate current build procedure was consolidated.

Current library/token CLI examples use `build-production/Release` and the
machine-only interfaces. There are no current scope/root overrides, HKCU voice
workflow or special bundled Gnome token. Generated Gnome assets remain legitimate
numerical fixtures. Read-only `reconcile` does not repair registrations.

Shared-engine staging requires the development checker in addition to the engine.
The manual example supplies explicit roots, an existing signing certificate and
separate verify/register actions. Registering the COM class does not install
voices, update their stored runtime paths or implement a deployment upgrade.
Build completion does not imply deployment.

## 6. Links, paths and source references

The entry scan found no missing repository-file Markdown links. Four historical
links in `MACHINE_VOICE_LIBRARY_HISTORY.md` refer to ignored Step 3C.1 logs under
`generated/perf/3c1-*.txt`. Those files exist in this workspace; the original
evidence links were preserved but are not guaranteed in a fresh checkout.
No artifact was regenerated.

Corrections concern outdated procedure/authority references, not an invented set
of broken source-file links. New current guides link to existing headers and
implementation files. Active examples were compared with the actual CLI branches,
CMake target definitions and PowerShell parameters. Historical commands remain
clearly labeled, including obsolete options in archived reports.

Repository link validity does not imply that the development deployment contains
all linked documents or source files. That separate packaging gap is recorded
below. External URLs and ignored evidence availability are not release guarantees.

## 7. Confirmed build and validation documentation

Static inspection confirms defaults `BUILD_TESTING=ON`,
`OUTLOUD_BUILD_DEVELOPMENT_TOOLS=OFF`, `OUTLOUD_ENABLE_AVX2=ON`. The supported
production command disables the first two and builds these six targets:
`OutLoudPocketSAPI`, `OutLoudVoiceManagerNative`, `OutLoudVoiceImportHelper`,
`outloud-voice-import`, `outloud-voice-library`, `outloud-sapi-voices`.
Production does not require generated neural/tokenizer reference fixtures;
runtime model/tokenizer/voice files are still required for synthesis.

`ui/build.ps1` builds/copies bridge/helper, builds Release x64 WPF and runs its
existing fake-service checks. The frontend/helper remain asInvoker binaries;
confirmed mutation launches the helper with runas. Native C++20/MSVC/Windows SDK,
.NET 10 SDK for WPF compilation and runtime requirements are distinguished from
separate Python/upstream preparation requirements.

`scripts/validate.ps1` defaults to Fast/Release. `-Mode Correctness -List` lists
without executing; `-Mode Correctness` explicitly runs broader checks. It does
not build, export, benchmark or elevate. Missing mandatory fixtures/artifacts
fail; failed fixture dependencies can be Not Run. Listed, unavailable, failed,
passed and unexecuted are not interchangeable outcomes.

Existing **A11/A12 evidence**, not fresh A17 results:

| Check | Recorded result |
| --- | --- |
| Production native Release x64 | Passed. |
| WPF Release x64 and native output copies | Passed. |
| Fast validation | 4/4 passed. |
| Broader correctness | 31 entries listed, not executed in A11/A12. |
| Debug / AVX2-disabled builds | Unverified in A11/A12. |

Full correctness and installed SAPI/WoW checks remain explicit pre-release work.
A17 ran none of these commands.

## 8. Voice Manager and SAPI changes in documentation

The current guide describes snapshot/selection behavior, Import-or-Replace name
matching and explicit target confirmation, immutable random IDs, Remove assertions,
partial/unknown/error results and dismissible notifications. Future registrations
use the validated stored name exactly, with unchanged ID/Vendor/Language fields.
No existing token is renamed by documentation or read-only refresh.

Read-only ABI v1 snapshot results and protocol-v3 elevated mutations remain distinct.
Removal journals, captured token fingerprints, restoration/cleanup and explicit
recovery are described without promising a cross-filesystem/registry atomic
transaction or proven power-loss durability. `correct` is a limited owned-token
identity/path/name diagnostic when intended runtime is absent; it does not certify
WoW visibility or playback.

Tester uses an exact owned HKLM token on a request-owned STA, plain text, awaited
Stop/cleanup and a separate new synthesis for validated WAV publication. WAV is
not OLV export. GUI raw-audio preparation, Rename and direct OLV import/export
are not invented features. Light/dark/high-contrast behavior and controlled manual
checks are covered; personalization reads are not user-scope voice support.

## 9. Model, voice and engine documentation

The existing OLP/OLV binary field tables were not changed. Current summaries
describe fixed model tensors, mapped immutable ownership, six FP32 post-RoPE K/V
voice prefixes, integrity/declared compatibility and metadata outside OLV.
Integrity does not establish authenticity, source-model provenance or permission.
Raw-audio preparation requires separate weights/tooling; the synthesis OLP omits
that preparation path. Native prepared-state conversion is offline.

A08 caches immutable Mimi transpose rows once per decoder; A09 reuses prefill
storage, caches fixed sampler time embeddings per model/ISA and evaluates combined
condition SiLU once per sample. Mutable condition/state is not globally cached.
Streaming still uses two resident compute workers and a bounded queue. Cancellation
quiesces/joins at safe points and permits fresh reset/reseed recovery.

Separate ISA paths may differ in bits. Old/new exactness applies to documented
deterministic workloads against each ISA's own baseline. No universal numerical
or performance claim was added, and latest optimizations are not claimed deployed
or measured end to end in installed SAPI/WoW.

## 10. Distribution topics deferred

**A03:** complete product packaging/installer, runtime prerequisite handling,
signing policy, installation/uninstall and upgrade behavior. Existing manual
copy/sign/verify/COM scripts are development facilities, not a finished installer.

**A04:** project license choice, complete third-party notices/upstream attribution,
and model/voice redistribution rights. RNG notices do not settle the entire
product. No license was selected, permission inferred or legal investigation run.

## 11. Static validation performed

All **269 local-file links across 46 Markdown documents** resolve in this
workspace, including the four historical ignored logs disclosed above. Active command options,
source links, target names, app/project settings, container constants and script
parameters were compared with source. Git whitespace was checked.

An entry SHA-256 inventory of 254 existing repository-content files allows final
comparison with all earlier implementation work. A17 changed 25 existing Markdown
files and added three. No entry file was deleted and no non-Markdown file was
changed or added by A17. Two tracked paths already
deleted by prior cleanup/retirement remain absent. Ignored static-check JSON is
under `generated/documentation-a17/`; it is not a production artifact.

These checks are documentation validation, not compilation, execution of build or
deployment scripts, binary validation, numerical tests or performance evidence.

## 12. Implementation issues recorded, not fixed

| Finding from source | Consequence / deferred work |
| --- | --- |
| `prepare-deployment.ps1` requires `outloud-sapi-check.exe`; production-only CMake omits it. | That helper needs a development output with prepared references. A03 must decide production package contents. |
| Staging copies selected docs/scripts but not their full relative-link/source closure, WPF app, bridge/helper or management CLIs. | A source checkout is needed for some links; the bundle is not a complete application distribution. No copy/inventory logic was changed. |
| Registration refuses an existing owned class whose DLL path differs; managed tokens retain runtime paths. | No automatic engine/voice upgrade is supplied. A03 must define coordinated upgrades. |
| Deployment script root defaults still use `$PSScriptRoot` in parameter expressions, unlike the body-resolved build/validation defaults. | Static review flags possible Windows PowerShell binding sensitivity. It was not reproduced here; examples supply explicit roots. Any fix requires script work outside A17. |

No new native correctness defect was established by this static documentation
review. Known status/provenance/installed-host limits are described, not silently
changed into stronger guarantees.

## 13. Remaining limits and production boundaries

Source inspection does not establish installed executable freshness, current live
registry contents, user-perceived playback, WoW enumeration/cache behavior, or
universal numerical equivalence. User-reported working integration and removed
legacy registrations were accepted as context; no fresh production inventory was
taken. External references, legal rights and complete distributable link closure
remain outside the static checks.

No C++/CMake/PowerShell/WPF/test/ABI/format behavior changed. No model, voice or
fixture was generated. HKLM/HKCU, ProgramData, installed DLLs/assets, certificates
and speech settings were untouched. No elevated helper, registration, deployment,
signing, installation, game action or user-process termination ran. Nothing was
staged, committed or pushed. A17 is complete within this documentation-only scope.
