# Legacy voice-management architecture removal

Date: 2026-10-09. Source baseline: `046385a`, with the preceding unstaged
cleanup preserved. The explicit product decision retires HKCU voice management
and the special development-token workflow. This implements that decision in
source; it does not migrate or delete installed data.

**Implementation completed; build and runtime validation intentionally deferred.**
WoW and Out Loud components are running. Only repository source, fixtures,
configuration and documentation were edited. No project binary or validation
script was executed. Earlier audit/cleanup build results are historical evidence,
not validation of this change.

## 1. Removed functionality

- User-scope managed-token installation, inspection, listing, reconciliation,
  update, removal and effective-HKCR engine verification.
- Product CLI access to alternate libraries, including LocalAppData libraries.
  The existing default root was already ProgramData; there was no separate
  user-root resolver left to delete.
- Special `OutLoud_Pocket_GnomeMale_Development` identity, creation/removal
  workflow and name-specific management exclusion.
- Explicit HKCU category enumeration in the managed-voice diagnostic script.
- Bundling a default Gnome OLV and development-token configuration with new
  shared-engine deployments.

Supported production storage is the ProgramData known-folder path
`OutLoud/VoiceLibrary` (normally `C:\ProgramData\OutLoud\VoiceLibrary`). Tokens
are exclusively HKLM Registry64 under `SOFTWARE\Microsoft\Speech\Voices\Tokens`.

## 2. Deleted source files

Deleted `sapi/src/voice_registry_location.hpp`: its hive/view/scope selector is
unnecessary when the production registry location is fixed. The preceding
cleanup's deletion of `sapi/tests/scratch.hpp` is separate from this task.

## 3. Deleted functions/classes and constants

Removed `SapiVoiceScope`, `SapiVoiceErrorCode::unsupported_scope`,
`detail::voice_registry_location`, both scope-validation wrappers, the
development `sapi::token_name` constant, and script helpers `Get-PocketTokenPath`
and `Assert-OwnedToken`. Remaining error-code numeric values are preserved.

Removed scope arguments from `managed_sapi_token_id`, `SapiVoiceRegistry` and
`SapiVoiceManager`, including replacement preflight. Production and fake
implementations, CLI callers and frontend coordination now use the simpler
signatures. No replacement scope abstraction was introduced.

## 4. Removed CLI options

`outloud-sapi-voices` no longer accepts `--scope`, `--root` or
`--allow-development-library-root`. `outloud-voice-library` no longer accepts
`--root`. Both construct the default ProgramData library. Token operations retain
`--runtime-root`, which identifies the shared deployment rather than a voice
library or registry scope. Existing operation commands remain available.

## 5. Removed script responsibilities

No complete script file was deleted: all affected files also serve current
machine management or shared-engine deployment.

| Script/configuration | Removed portion / resulting responsibility |
| --- | --- |
| `register.ps1` | Removed default-token creation and token/name/voice/generation parameters. Registers or verifies only the owned HKLM64 shared COM engine. |
| `unregister.ps1` | Removed default-token and user-voice handling. Deletes only the owned COM class, refusing while any machine voice references its CLSID. |
| `registry-common.ps1` | Removed special-token path/ownership helpers; retains engine ownership, rollback snapshots and security checks. |
| `test-managed-voices.ps1` | Removed `Scope`, `ExplicitUserCategory`, category COM construction and HKCU enumeration. Retains default enumeration and exact managed HKLM-ID selection. |
| `prepare-deployment.ps1` | Removed `VoicePath` and Gnome voice copy. New bundles contain shared model/tokenizer and binaries, with no default voice. |
| `deployment-inventory.json`, `deployment-common.ps1` | Inventory schema 2 removes the default token and voice asset; validates only the two shared runtime assets. The copy manifest format stays schema 1. |

General copy/sign/verify infrastructure, six-image dependency allowlist,
Microsoft signature preservation and administrator/manual-operation guards remain.
Current multi-voice documentation is included in new bundles.

## 6. Removed test cases and fixtures

`sapi/tests/voice_tokens_test.cpp` removes scope-selector assertions, the fake
user-token map, user-token lifecycle cases and special development-token
fixture. Generic unrelated/unmanaged-token preservation remains. Existing
machine cases retain ownership/type/schema checks, engine file identity,
transaction failure/races, rename, orphan, corruption and path-security coverage.
The exact-name snapshot assertion checks description, `409` and Attributes Name.

`sapi/tests/managed_voices_script_test.ps1` removes fake category objects,
HKCU collections and alternate-category branch cases. It retains fake default
enumeration, exact-ID/duplicate-name/prefix-decoy selection, no-playback mode,
missing/duplicate/ownership/description errors and explicit Speak requirements.
Frontend fake registries use the fixed-location interface; obsolete scope
assertions are removed while coordinator/bridge assertions remain.

No test executable or CTest registration was removed. These source edits have
not been executed, so no new assertion count or passing result is claimed.

## 7. Removed CMake targets/dependencies

None: the retired behavior occupied branches in targets still required by the
machine system. The deleted header was not an explicit CMake source entry.
Its include was removed from production and test source. No external library,
link dependency or inference/checker target became exclusive dead code.
The preceding cleanup's CMake changes remain intact.

## 8. Simplified existing components

- The native registry walks fixed HKLM ancestors with `KEY_WOW64_64KEY` and
  verifies the fixed machine COM registration directly.
- Token management no longer dispatches by scope or recognizes a special voice
  name; generic ownership/identity validation determines managed candidates.
- Frontend read-only snapshots and elevated Import/Replace/Remove call the same
  machine-only API without scope arguments.
- CLI library construction and script parameter handling have a single product
  path. No arbitrary root is exposed by the product CLIs.

Active `VOICE_LIBRARY.md`, `MACHINE_VOICE_LIBRARY.md`, `MULTI_VOICE_SAPI.md`,
`SAPI_ADAPTER.md` and README links describe this architecture. Their four prior
design/validation records are preserved in corresponding `*_HISTORY.md` files,
marked as historical with unsupported old commands. The registry audit retains
its findings and annotates the removed header reference.

## 9. Shared components deliberately preserved

The shared engine CLSID remains `{F38C3398-3552-43B9-8350-7DD301327D70}`.
Token keys remain `OutLoud_Pocket_<library_id>`. Description/`409`/Attributes Name
remain exactly the entered library name; Vendor remains `Out Loud`. Immutable
IDs, existing runtime fields/defaults and strict REG_SZ/schema checks remain.

ProgramData security, reparse/link/path protections, privileges, authentication,
transactional registry compare-and-exchange, reversible library detach,
replacement validation, rollback and truthful partial/error outcomes remain.
The WPF ViewModel/service/native bridge boundaries, asInvoker manifest, helper
protocol and C ABI structures/exports are unchanged. FlowLM, RNG, Mimi, splitting,
OLP/OLV, streaming, cancellation, bookmarks and Voice Tester were not changed.

Generic AssetRoot-relative runtime loading is shared engine behavior and remains.
The explicit `VoiceLibrary(path)` constructor and
`MachineVoiceLibraryPolicy::development_override` remain for isolated test
fixtures. Production callers use the default policy; the override is no longer
exposed by either CLI and does not select another registry hive. Removing these
seams would force valuable security/rollback tests toward production storage.

## 10. Remaining legacy references

Historical audits, milestones and the new `*_HISTORY.md` records still mention
retired IDs/scopes/commands. They describe previous observations rather than
supported workflows. No executable source/script retains the special token
string, user-token scope selector or explicit HKCU voice-category branch.

The LocalAppData known-folder reference in the token checker is a negative
machine-path-security test, not a library-root selection. Read-only HKCU COM
override detection remains in shared-engine maintenance as a security guard.
Windows app-theme reading and user certificate-store selection are unrelated
to voice-library scope. `OutLoudPocketSAPI-2M` remains the existing shared COM
ownership marker; changing it would break ownership of the current engine.

## 11. Known compatibility consequences

Old CLI/script arguments and old C++ scope-parameter call sites are unsupported;
external C++ consumers must update and rebuild. C ABI/helper transport remain
unchanged. New deployment validation requires inventory schema 2; prepare a fresh
bundle later instead of combining new scripts with an old voice-bearing layout.
Existing installed bundles retain their original files and behavior until an
explicit later upgrade.

Existing HKCU library entries/tokens and the old development token are untouched.
This change provides no migration/cleanup command, and installing updated source
does not make those registrations disappear. Generic adapter loading is retained,
so source retirement is not a claim that old tokens immediately stop synthesizing.
An old machine token can still block shared-engine unregistration through the
generic CLSID dependency check. Old prefixed managed tokens can still report a
stale name under the already-present exact-name policy; no automatic rewrite was
added. Existing path-string diagnostic/recovery audit findings are not fixed here.

The retirement decision is complete. Any later deletion/migration of actual
installed legacy data is a separate, explicitly targeted maintenance action.
No remaining shared coupling blocks removal of the retired management branches.

## 12. Files requiring later build validation

Rebuild the changed API and implementations:
`engine/include/outloud/sapi_voices.hpp`, `sapi/src/voice_registry.cpp`,
`sapi/src/voice_tokens.cpp`, `sapi/src/identity.hpp`, both management CLI sources,
`frontend/native/voice_manager.cpp`, `frontend/mutation/mutation.cpp`,
`frontend/native/tests.cpp` and `sapi/tests/voice_tokens_test.cpp`.
Compile all dependent mutation/replacement/removal fixtures too. Build the helper,
bridge, SAPI DLL and checker targets, then WPF and its native-copy steps after
running components are closed by the user. No build was attempted in this pass.

Later validation, not commands executed for this task:

```powershell
cmake --build build --config Release --parallel
dotnet build ui/OutLoud.VoiceManager.sln -c Release -p:Platform=x64
ctest --test-dir build -C Release --output-on-failure
dotnet run --project ui/OutLoud.VoiceManager.Tests/OutLoud.VoiceManager.Tests.csproj -c Release -p:Platform=x64 --no-build
powershell -NoProfile -File sapi/tests/managed_voices_script_test.ps1
```

Also run the existing SDK-built WPF validation project and registration-free
SAPI/multi-voice/cancellation checkers using the documented isolated fixtures.
Statically review/validate fresh bundle inventory, signatures and shared-engine
script behavior before any separately authorized registration/deployment action.
Retain all current inference/RNG/asset regressions; no new performance suite.
The configured CTest suite still contains 17 cases; frontend assertion totals
must be reported from that later execution rather than copied from prior reports.

### Before / after and static checks

Counts compare the worktree after the preceding cleanup with this removal,
excluding the pre-existing untracked comprehensive audit and ignored artifacts.
They are source counts, not regenerated or compiled target inventory.

| Measure | Before | After |
| --- | ---: | ---: |
| Supported product library/registration scopes | 2 | 1: ProgramData / HKLM64 |
| Special development-token management workflow | 1 | 0 |
| Product CLI library-root override | Yes | No |
| Default voice included in new shared deployment | Yes | No |
| C/C++ source/header/include candidates | 105 | 104 |
| All code/source candidates | 177 | 176 |
| Native targets / registered CTests | 47 / 17 | 47 / 17 |
| Content candidates including documents/configuration | 226 | 230 |

One obsolete source header is deleted; four historical document archives and
this report are added. This task touches 32 paths: 26 existing files modified,
one deleted and five added. Git's index is unchanged. Earlier cleanup edits and
the comprehensive audit are preserved.

Static reference searches found no obsolete scope/type/header/token callers in
active source/scripts. Quoted native includes and literal CMake source paths
resolve. Changed PowerShell files parse without syntax errors using only the
PowerShell AST parser; none was executed or dot-sourced. Inventory JSON parses
with schema 2, the original CLSID, six binaries and two runtime assets. Source
review and Git whitespace checks found no obvious leftover-reference errors.
These checks do not establish compilation, linking, script runtime correctness
or live integration.

No build, configuration, automated test, application launch, process stop,
registry/library mutation, installed-binary change, signing, deployment, staging,
commit or push occurred. Generated assets and existing voice files are untouched.
