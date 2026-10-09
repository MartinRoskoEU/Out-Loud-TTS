# A11 + A12: production builds and validation

Date: 2026-10-09. Entry HEAD: `046385a`; earlier cleanup, legacy removal,
stabilization and A06/A08/A09 working-tree changes were preserved. This change
concerns build configuration and checker invocation only.

Later [native error logging](NATIVE_ERROR_LOGGING.md) adds two isolated development
targets and directs expected-error SAPI tests to a private test DLL so user logs
remain untouched. The 50-target count and execution results below describe A11/A12;
the 31 registered correctness entries are retained. The focused logger harness is
an explicit separate invocation, not another CTest or benchmark.

## Root cause and build separation

The current pre-change CMake file checked tokenizer/reference assets and five
generated asset/neural headers unconditionally. Checker, listening, benchmark
and profiler targets were also unconditional. `BUILD_TESTING=OFF` suppressed
CTest registrations, but neither those targets nor their build-time fixtures.

The root CMake file now defines production targets. `cmake/Development.cmake`
contains fixture checks and development targets, included only when testing or
research tools are enabled. Production target sources, links, compile definitions,
warnings, floating-point options and helper linker settings were preserved.

| Option | Default | Effect |
| --- | --- | --- |
| `BUILD_TESTING` | ON, standard CTest option | Build correctness checkers and register labeled tests. |
| `OUTLOUD_BUILD_DEVELOPMENT_TOOLS` | OFF | Add research, listening, baseline/allocation, benchmark and profiler tools. |
| `OUTLOUD_ENABLE_AVX2` | ON, unchanged | Compile optional runtime-dispatched AVX2/FMA kernels. |

Only one new option was introduced. Correctness checkers remain available with
research tools OFF. Enabling research tools also retains the reference-checker
targets they historically accompanied, even with CTest registration OFF. Cached
options persist; explicitly enable research tools to reproduce the earlier full
development target inventory. Debug and Release x64 remain supported.

## Build commands

Production, from the repository root:

```powershell
cmake -S . -B build-production -A x64 -DBUILD_TESTING=OFF -DOUTLOUD_BUILD_DEVELOPMENT_TOOLS=OFF
cmake --build build-production --config Release --parallel --target OutLoudPocketSAPI OutLoudVoiceManagerNative OutLoudVoiceImportHelper outloud-voice-import outloud-voice-library outloud-sapi-voices
.\ui\build.ps1
```

`build-production/` is ignored. Production configuration/build needs neither
Python exports nor tokenizer/asset/neural/RNG/utterance reference files. It contains
21 project library/executable targets with AVX2 enabled, and zero CTests. No
research executable is compiled in this configuration.

`ui/build.ps1` defaults to that production directory. A fresh native tree is
configured with both development switches OFF; a supplied existing tree retains
its cached options. The script still builds bridge/helper, builds the solution,
and runs its existing fake-service frontend checks. Direct project builds retain
the previous `build/` default; use the existing property override when needed:

```powershell
$env:DOTNET_GENERATE_ASPNET_CERTIFICATE = 'false'
dotnet build ui/OutLoud.VoiceManager.sln -c Release -p:Platform=x64 `
  "-p:NativeBuildDirectory=$PWD\build-production" --no-restore
```

The bridge DLL and helper EXE still copy beside the frontend executable. x64,
asInvoker manifests, explicit helper elevation, ABI and protocol are unchanged.
These commands neither install nor register components. The existing .NET SDK,
Windows SDK, MSVC and normal Microsoft runtime prerequisites remain necessary.
`--no-restore` assumes the projects were previously restored; validation does
not install dependencies.

Development, with explicitly prepared offline fixtures:

```powershell
cmake -S . -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
```

For the full historical development/research inventory, add
`-DOUTLOUD_BUILD_DEVELOPMENT_TOOLS=ON` when configuring. It preserves perf/profile,
worker trace, RNG/utterance listening tools and A08/A09 baseline/allocation tools.
None is scheduled by the correctness entry point. There are 50 configured
project targets with all current fixtures and both switches ON, excluding CMake
dashboard/build utilities. No existing project target was removed.

## Runtime assets versus reference fixtures

The running engine still requires its compatible `.olp` model, tokenizer `.bin`
and `.olv` voice. Model/voice integrity, compatibility and runtime path validation
remain intact. No model is embedded, downloaded or bundled by configuration.
The tracked `pocket_model_schema.hpp` is production source, not an optional test
export; it remains required.

Development-only dependencies are:

| Data | Consumers |
| --- | --- |
| `native_text_fixtures.hpp` and tokenizer asset | Tokenizer/text reference checks; tokenizer asset copied only to checker output. |
| `asset_test_fixtures.hpp` and asset fixture binaries | Asset parity/corruption checks. |
| FlowLM, sampler, generation, Mimi reference headers/binaries | Numerical checkers/regressions, worker reference checks and research tools. |
| `rng_reference.hpp` / `rng-reference.bin` | Standalone RNG exactness checker. |
| `orchestration_reference.hpp` and canonical RNG binary | Full utterance/cancellation checker. |
| Source prepared voices, model/tokenizer/voice assets | Isolated import/library/bridge/token/multi-voice integration checks. |
| Historical old executables/captures | Explicit A08/A09 comparison modes only; never production or ordinary correctness prerequisites. |

Existing cache path overrides remain supported. The existing utterance checker
uses `rng/rng-reference.bin` beside the configured asset directory's parent;
its dependency preflight checks that actual location. This task does not redesign
reference export layouts.

## Fast and broader validation

```powershell
.\scripts\validate.ps1
.\scripts\validate.ps1 -Mode Correctness -List
.\scripts\validate.ps1 -Mode Correctness
```

`Fast` and Release are defaults. Use `-Configuration Debug` and
`-BuildDirectory <path>` for another built development tree. The script invokes
CTest serially, preserves its nonzero failure exit, rejects empty selections,
and never builds, downloads, exports, elevates or benchmarks. `-List` prints
commands/arguments without executing tests; successful listing does not certify
artifact availability or correctness. Standard equivalent selections are:

```powershell
ctest --test-dir build -C Release -L '^fast$' --output-on-failure --no-tests=error
ctest --test-dir build -C Release -L '^correctness$' --output-on-failure --no-tests=error
```

Unfiltered CTest remains a broader run; use the fast entry point for ordinary
edits. All 19 CTests present at task entry remain unchanged in assertion scope
and command arguments. There are now 31 correctness entries, including one
fixture-availability preflight and two prebuilt frontend checks.

| Classification / label | Selection |
| --- | --- |
| Fast / `fast` | Canonical tokenizer parity, text regressions, SAPI input collection, fake-COM managed-voices PowerShell regression. No neural inference. |
| Neural / `neural` | Existing FlowLM/sampler/generation/Mimi numerical gates; RNG exactness; supplied-noise worker scheduling and production-RNG worker checks; full utterance/cancellation/recovery; multi-voice and full registration-free SAPI. |
| Integration / `integration` | Asset validation, isolated import/library/bridge/helper coordination, fake registry tokens, SAPI diagnostics/multi-voice/streaming/bookmarks and fake-service/SDK-built frontend checks. Some also carry `neural`. |
| Broader / `correctness` | All entries above, including fast tests and required fixture preflight. Opt in explicitly. |
| Manual / production-sensitive | Real HKLM operations, real elevated mutations, installed playback, WoW, signing/deployment. Never registered. |
| Performance | Benchmarks, profilers, traces and historical optimization timing campaigns. Never registered. |

Worker and utterance checks run portable and `auto` paths. Auto selects the same
available kernel ISA as the SAPI adapter; explicit old portable/AVX2 commands
remain supported. On a portable-only build/machine, the two selections can
exercise the portable path twice; this keeps the full-SAPI reference setup
explicit without a platform-specific option matrix.

The RNG CTest uses new `--check-only`, retaining noise/state/reseed/allocation
assertions while excluding the existing timing probe. No RNG arithmetic changed.
Checker correctness modes can retain incidental elapsed-time fields in their
existing reports; no benchmark mode, timed repetition campaign or profiler runs.

The full registration-free SAPI test requires a fresh raw FP32 reference from
successful `utterance-cancellation-auto`. CTest fixture ordering prevents it
from consuming an old file after producer failure. That producer includes all
existing cancellation/recovery assertions and writes
`generated/orchestration/native-full-canonical-auto.f32` with the default asset
layout. Existing canonical inputs/independent references were not changed.
The host loads the workspace DLL with an in-memory token and memory audio output;
it does not register Pocket, select an installed voice or play through speakers.

Broader frontend checks execute already-built assemblies only. Before opting in,
prepare the standard output locations explicitly:

```powershell
dotnet build ui/OutLoud.VoiceManager.sln -c Release -p:Platform=x64 --no-restore
dotnet build ui/validation/OutLoud.VoiceManager.Validation.csproj -c Release -p:Platform=x64 --no-restore
```

Both use fake services/peers without App startup or production mutations. Builds
may need the native property override above. The frontend test project uses
`ui/OutLoud.VoiceManager.Tests/bin/<Configuration>/net10.0-windows/`; the
supplementary project uses `ui/validation/bin/x64/<Configuration>/net10.0-windows/`.
These paths were checked with MSBuild property evaluation, without compiling
or running the supplementary project. An isolated output override does not
change that standard test lookup. Missing .NET runtime
or assemblies fail explicitly; the validation entry point never builds them.

## Fixture availability and result policy

Missing mandatory development compile-time headers/tokenizer data fail configure
with export guidance. Production configuration never performs those checks.
Missing optional RNG/utterance headers print their paths at configure time and
omit only the corresponding executable. Their named correctness tests remain
visible and fail as unavailable, even if a header later appears: reconfigure and
build before running them. They cannot become dummy passes.

The utterance fixture preflight verifies its header and canonical RNG binary
before expensive work. Missing runtime fixture binaries fail their existing
load/hash checks. A failed CTest fixture causes dependent checks to be **Not Run**,
not passed. Missing/unbuilt executables and failed/frontend artifacts propagate
nonzero results. No missing-data skip is silently counted as success.

This task deliberately inspected broader selection only. Those entries are
**not executed**, rather than passed or skipped tests. No fixture exporter runs
implicitly. Assertion tolerances, scheduling/count gates and exact comparisons
remain unchanged.

## Verification performed

| Check | Actual result |
| --- | --- |
| Production configure | PASS in separate ignored `build-production`; all six fixture/data cache paths overridden to nonexistent `generated/build-validation/unavailable` paths. No cache removed. |
| Production Release x64 build | PASS: SAPI DLL, bridge DLL, helper EXE and all three management CLIs, including complete static runtime dependencies. |
| Production target inspection | 21 project targets, zero CTests, no research/checker source or generated fixture include paths. Production target command settings match the captured pre-change CMake file. Debug/Release x64 configurations retained. |
| One existing-tree incremental development build | PASS: tokenizer/text/SAPI checkers, RNG checker, worker checker/trace and utterance checker/synth; dependencies reused. No clean build. |
| WPF solution Release x64 | PASS, 0 warnings / 0 errors; `--no-restore`, production native property and isolated output. No app terminated or launched. |
| Native WPF output copies | Bridge/helper bytes match freshly built production outputs. All six native images and WPF apphost verified AMD64. |
| Fast actual execution | **4/4 PASS, 0.81 seconds** through the new script: tokenizer, text, SAPI input and fake-COM PowerShell checks. |
| Broader selection inspection | 31 entries; all previous 19 tests retained, expected standalone arguments/fixture ordering present, no benchmark/profile/privileged command. No broader execution. |
| Missing mandatory header | Expected configure failure, exit 1; clear missing text-fixture path. |
| Missing optional fixtures | Expected CTest preflight failure, exit 8; explicit unavailable path. Separate unavailable-checker branch also fails, exit 1. |
| Missing frontend assembly | Expected clear failure, exit 1, without automatic build. |
| Failure propagation | Synthetic failing fast CTest propagated exit 8 through the script. Empty selection also returned exit 8. |
| Whitespace | `git diff --check` passed; existing line-ending notices are not whitespace errors. |

An initial script inspection exposed Windows PowerShell's empty `$PSScriptRoot`
in a parameter default expression. Default directory resolution was moved into
the script body before the successful selections/execution above. No production
regression was found in the executed scope.

Successful WPF executable:

```text
C:\Users\rosko\Desktop\Nendis\application\wow-pocket-tts-sapi\generated\build-validation\wpf\Release\OutLoud.VoiceManager.exe
```

Logs, selection commands, captured entry CMake, negative-check artifacts and
output-copy hashes are in ignored `generated/build-validation/`. The production
tree retains deliberately nonexistent fixture cache paths from this verification;
they affect only development targets if those are later enabled in that tree.

## Changed files and limits

- `CMakeLists.txt`, `cmake/Development.cmake`: production/development separation;
  no production source/settings changes.
- `cmake/Validation.cmake`, `RequireFixtures.cmake`, `RunFrontend.cmake`:
  labels, existing-checker registration, required-reference ordering and failures.
- `scripts/validate.ps1`: one small serial fast/correctness entry point.
- `tools/rng-check/main.cpp`: correctness-only CLI branch.
- `tools/worker-check/main.cpp`, `tools/utterance-check/main.cpp`: Auto ISA argument
  and fresh SAPI reference output after successful utterance validation.
- `ui/build.ps1`: fixture-independent native default, existing-tree cache policy.
- `README.md`, `docs/VOICE_MANAGER_WPF.md`, this report: current build instructions.

The added complexity is one CMake option, two target/registration includes,
two small CMake execution helpers and one PowerShell entry point; no custom test
framework or new dependency. All production algorithms, formats, ownership,
privilege boundaries, public ABI and helper protocol are unchanged.

The complete neural/system/frontend correctness run, new Auto inference paths
and fresh SAPI reference consumption were **not executed in this task**. Debug
compilation, an AVX2-disabled build and physical non-AVX2 hardware were not tested.
The configured paths and binaries compile, but runtime parity/cancellation under
the new CTest wiring remains for the explicit broader run. Frontend lookup uses
standard prebuilt paths; it does not certify source freshness or follow arbitrary
OutputPath overrides. Base development configuration still requires its original
reference headers even when only fast execution is intended. Runtime assets and
prerequisite distribution remain separate packaging work.

No benchmark, profiler, full neural campaign, live UI/audio/WoW test, signing,
registry mutation, real elevated helper, deployment or production Voice Library
operation ran. No installed files or model/reference caches were removed or
modified. Earlier working-tree changes and historical reports were preserved.
Nothing was staged, committed or pushed.
