# Stabilization Phase 1

Date: 2026-10-09. This implements A05, A14, A13, A01, A10 and A16 on top of
the unstaged cleanup and architecture retirement. Earlier edits and historical
reports remain intact. Production registrations, installed DLLs and Voice Library
data were not modified.

## Implemented behavior

| Finding | Before | After |
| --- | --- | --- |
| A05: runtime path equivalence | Raw REG_SZ equality could report slash/case differences as conflicts. | One shared comparator handles safe absolute local slash/case/lexical spellings for AssetRoot, ModelPath and TokenizerPath. All other fields/types/subkeys compare exactly. |
| A14: PowerShell launcher defaults | Script-relative parameter expressions could run before PSScriptRoot was available under powershell -File. | Both UI launchers resolve omitted defaults in the body; explicit directories and Debug/Release forwarding remain. |
| A13: exact-name regressions | Current naming was correct but literal name/replacement assertions were incomplete. | Tests check exact Unicode/default/409/Attributes Name values, Vendor, immutable full token ID, rename and replacement identity, with no automatic prefix. |
| A01: retired prefix policy | An old prefixed owned token had a terse stale-name result. | It remains stale_name, with a clear explanation that voice data is intact and the token needs explicit maintenance before replacement. No automatic write or migration. |
| A10: speech collection bounds | Hosts could supply unlimited text/fragments/bookmark metadata. | Aggregate limits are checked before copying, with overflow-safe accumulation and caller-thread cancellation during collection. Oversized requests fail without truncation. |
| A16: asset diagnostics | Open/map/parse failures often collapsed to generic E_FAIL. | Typed asset errors retain Win32 reasons and distinguish invalid data from compatibility failures. The COM boundary returns appropriate HRESULTs and publishes bounded caller-thread IErrorInfo. |

### Path policy and strict validation

`sapi/src/token_comparison.hpp` is the single equality policy used by inspection,
installation idempotence, rename/status comparison and replacement preflight.
It recognizes Windows ordinal case-insensitive comparison, slash equivalence,
`.` components and redundant separators through the existing safe-local-path
validator. Relative, UNC/device, alternate-stream, DOS-device, ambiguous and
parent-traversal paths are not accepted as equivalent. Short-name, hard-link,
symlink or junction aliases are not resolved into equivalent names.

Ownership, immutable IDs, CLSID, REG_SZ decoding and unexpected-field detection
remain strict. VoicePath stays an exact managed-library identity check. Runtime
mutation preflight still pins/checks ancestors and files; no reparse protection
is removed. Read-only comparison is lexical, not proof that assets currently
exist or match model hashes. The snapshot still explains its limited runtime
validation when no expected deployment is supplied.

Equivalent tokens are not rewritten. A deliberate name-only update retains all
three stored runtime spellings. Helper-created future runtime paths use normalized
construction, matching the CLI. Genuinely different runtime fields remain
conflicting and cannot be changed by the name-only update command.

### Native input limits

| Resource | Limit |
| --- | ---: |
| Spoken text, aggregate UTF-16 code units | 262,144 |
| All linked fragments, including metadata | 8,192 |
| All bookmark fragments, including ignored trailing bookmarks | 1,024 |
| Aggregate bookmark names, UTF-16 code units | 65,536 |

These limits allow much more than a long quest/dialogue and substantially more
than the frontend's 12,000-character editing limit. They bound collection memory
and traversal for other SAPI hosts, rather than defining a normal UI workload.
Text storage is at most approximately 512 KiB UTF-16 plus up to approximately
1 MiB UTF-8; metadata and cycle-detection storage are separately bounded. Container
capacity and allocator overhead can exceed content size; this is not a total
process-memory limit or a synthesis-time guarantee.

`collect_add` checks `amount > limit - total` before addition, also guarding an
already-invalid total. `ERROR_BUFFER_OVERFLOW` is returned as an HRESULT for
resource excess. Data is never silently truncated. Counts are checked before
large reads/copies; trailing bookmarks remain ignored for audio/events but count
toward the request budget. Existing cycle, pointer, unsupported-action, embedded
bookmark-NUL and UTF-16 text checks remain. Text converts once after concatenation,
preserving surrogate pairs split across fragments.

GetActions polling stays on the Speak caller through the existing RequestControl.
Collection polls before each fragment and each 4,096-unit copy chunk. Abort
discards the collected prefix, returns S_OK and never starts native generation.
Cancellation ownership, workers, segment scheduling and normal audio math are
unchanged. A currently executing UTF conversion is bounded and not interrupted
inside its Windows API call.

### Asset failures and COM diagnostics

`AssetError` derives from runtime_error and carries a category and original
Win32 reason. MappedFile captures open/size/map/view errors before handle cleanup;
empty/oversized files and parser failures are invalid_data. Tokenizer keeps its
existing stream-based load/validation and classifies open/read/parse errors.
Voice/model incompatibility is a distinct compatibility category.

The adapter's read-only asset availability check supplies model/tokenizer/voice
context. Lazy resident construction wraps asset failures with the same fixed role
names. I/O failures return HRESULT_FROM_WIN32(original_reason); corruption returns
HRESULT_FROM_WIN32(ERROR_INVALID_DATA); voice/model incompatibility returns
HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH). Allocation failure still returns
E_OUTOFMEMORY. Unknown unrelated exceptions remain E_FAIL; no exception crosses
COM. Existing .olp/.olv/tokenizer validation rules and data layout are unchanged.

Coordinator failures retain an exception_ptr until join and publish diagnostics
on the caller. Error info contains fixed operation/role messages, without speech,
voice names or file paths; it is cleared at request/token-assignment entry. The
adapter links Windows oleaut32 for standard COM error info, not a logging framework.
There is no inference-loop logging or new diagnostic registry/UI protocol.
Some SAPI hosts display only HRESULTs and may discard IErrorInfo.

## Files changed in this phase

- `sapi/src/token_comparison.hpp`, `voice_tokens.cpp` and
  `frontend/mutation/mutation.cpp`: path equality, preserved stored spellings and
  stale-name explanation/normalized future paths.
- `sapi/src/support.hpp`, `adapter.cpp`, `asset_diagnostics.hpp`: bounded collection,
  cancellation wiring and COM diagnostics.
- `engine/include/outloud/asset_error.hpp`, `engine/src/asset_io.hpp`, `asset_io.cpp`,
  `assets.cpp`, `tokenizer.cpp`: typed file/validation/compatibility reasons.
- `sapi/tests/stabilization_checks.hpp`, `host.cpp`, `voice_tokens_test.cpp`,
  `frontend/mutation/replace_tests.hpp`,
  `ui/OutLoud.VoiceManager.Tests/Program.cs`: focused regression coverage.
- `ui/build.ps1`, `ui/validation/validate-with-runtime.ps1`: parameter-default fix.
- `CMakeLists.txt`: standard COM diagnostic linkage and two fast modes registered
  on the existing SAPI checker. No new executable or performance CTest.
- Current managed-SAPI documentation and this report: policy and validation record.

The UI already binds SelectedVoice.SapiDiagnostic in its details pane; the native
explanation flows through the existing bridge/model. No new UI workflow or
Import/Replace/Remove behavior was introduced.

## Build and focused test results

Release x64 incremental builds succeeded for:

```text
OutLoudPocketSAPI
OutLoudVoiceManagerNative
OutLoudVoiceImportHelper
outloud-sapi-check
outloud-sapi-voice-tests
outloud-voice-manager-tests
outloud-asset-tests
outloud-text-tests
outloud-sapi-voices
outloud-voice-library
```

The existing build directory was retained. CMake automatically regenerated after
the source-list/test-registration change. The initial checker compile exposed
one /WX header-order shadowing warning; moving the existing shared scratch include
before the host's global text declaration fixed it without weakening warnings.
The build resumed only the remaining targets; successful targets were not rebuilt
in a clean/full cycle.

WPF solution Release x64 succeeded with **0 warnings / 0 errors**, using the
existing restore state and isolated output under `generated/stabilization-phase-1`
to avoid interference with any running frontend. Bridge/helper output SHA-256
values match the newly built native files. The executable is:

```text
C:\Users\rosko\Desktop\Nendis\application\wow-pocket-tts-sapi\generated\stabilization-phase-1\wpf\Release\OutLoud.VoiceManager.exe
```

| Executed check | Result |
| --- | --- |
| text-pipeline-regressions | PASS, 0.06 s |
| asset-regressions | PASS, 1.14 s |
| sapi-input-regressions | PASS, 0.03 s; synthetic fragments, no inference |
| sapi-asset-diagnostics | PASS, 0.04 s; private assets/in-memory token and registration-free DLL, no inference |
| voice-manager-bridge-regressions | PASS, 9.47 s; private libraries/fake registries |
| sapi-token-regressions | PASS, 7.05 s; private libraries/fake registries |
| Rebuilt frontend test executable | PASS, 156 checks; fake services/elevation |
| Existing managed-voice fake-COM script | PASS, 26 assertions; no registry or playback |
| Launcher smoke | PASS, four separate powershell -File invocations with external build commands stubbed |
| Native helper/bridge copies in WPF output | Exact SHA-256 equality |
| Git whitespace check | PASS |

The single focused CTest run passed **6/6**, **17.91 seconds** total. Commands:

```powershell
ctest --test-dir build -C Release --output-on-failure -R '^(sapi-token-regressions|voice-manager-bridge-regressions|asset-regressions|text-pipeline-regressions|sapi-input-regressions|sapi-asset-diagnostics)$'
dotnet generated/stabilization-phase-1/wpf/Release/OutLoud.VoiceManager.Tests.dll
powershell -NoProfile -File sapi/tests/managed_voices_script_test.ps1
```

Input checks cover ordinary Unicode, cross-fragment surrogates, exact limits,
aggregate excess, ULONG_MAX bogus lengths rejected before reads, size_t overflow,
cycle detection, bookmark NUL and cancellation before a partial collection can
be exposed. Path tests cover all three fields, slash/case/dot equivalence,
idempotence/no writes, retained spellings during rename, different/unsafe paths,
REG_EXPAND_SZ and broken REG_SZ payloads. Existing schema/ownership/reparse/file
identity tests remain. Exact names are asserted against literal Unicode and
`Undead Female`/`Renamed voice`, alongside ID and replacement preservation.

Asset checks exercise real missing model/tokenizer files, inaccessible directory
and sharing-locked fixtures, oversized/corrupted assets and caller COM error info
on missing/invalid assets with no Write. Mapping-error HRESULT preservation and
compatibility HRESULT mapping use synthetic typed errors; actual OS mapping
resource exhaustion was not induced. Existing asset regressions exercise actual
voice/model mismatch and corruption without synthesis.

Launcher smoke placed harmless cmake/dotnet command shims in ignored workspace
artifacts and launched each real script through powershell -File with default
Release and explicit Debug paths containing spaces. It verified forwarded
arguments without repeating real builds or invoking the WPF runtime validator.
Smoke output saying view checks passed is produced by the launcher after the
stub exits; it is not a claim that supplementary view validation ran.

## Legacy-name maintenance and compatibility

The exact-name convention stays: description, `409` and Attributes Name equal
the entered display name; Vendor is Out Loud; token identity is immutable.
An owned, structurally valid old-prefixed machine token reports stale_name,
not correct/missing/corrupt. Replacement still requires a correct token and
does not silently reconcile it. Generic malformed/foreign tokens keep their
existing strict failure/status behavior.

For a selected managed machine ID, the supported later manual action is to inspect
the library/token and use elevated `outloud-sapi-voices update ID --runtime-root
<deployment>` only after confirming the stale-name-only difference. The command
retains ID, VoicePath, Vendor and stored runtime spellings; it refuses other
differences. This action was not run here. Normal current machine voices can be
removed/reimported through Voice Manager when the user explicitly chooses that
workflow; reimport allocates a new ID. Preserve/export the source beforehand.
Retired HKCU/special-development registrations are outside this product workflow;
their cleanup needs precise manual target review, and their prepared sources can
be imported into the current machine library. No new cross-scope migration or
automatic reconciliation subsystem was added.

Requests above native limits now explicitly fail, including large trailing
metadata that was formerly ignored without a bound. Asset COM failures can now
return specific Win32 HRESULTs instead of E_FAIL, and native callers can catch
AssetError (existing runtime_error catches still work). Standard oleaut32 is an OS
dependency. No format/ABI/helper protocol/CLSID change was made in this phase.

## Limits and deferred work

All six requested findings are addressed within the chosen source policy.
Automatic legacy reconciliation remains deliberately deferred. The machine-only
retirement is now compiled by the affected targets, but this is not its full
post-retirement regression campaign. The configured suite has 19 tests after the
two new fast modes; only the six listed tests ran.

No full neural parity/cancellation/playback campaign, benchmark, profiler,
real elevated mutation, live WoW, installed-engine test or visible UI/manual
inspection ran. These isolated checks do not establish those live integrations.
No forced OS mapping exhaustion or real permission-ACL mutation was attempted.
Other audit findings such as durable crash recovery and product packaging remain
outside this phase. No technical blocker required a speculative replacement fix.

No production registry/library/deployed-binary/certificate change, dependency
download/installation, game launch, automatic process termination, deployment,
signing, staging, commit or push occurred. Existing recordings, models and voices
were preserved. Only workspace build/test artifacts were created or updated.
