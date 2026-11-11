# Build and targeted validation

Use Windows 11 x64, Visual Studio 2022 C++ Build Tools, CMake 3.24 or later,
.NET 10 SDK and Windows SDK 10.0.26100.0. Restore the WinUI project's declared
NuGet dependencies. Product versions come from `../ProductVersion.props`.

## Application build

From the repository root:

```powershell
cmake -S . -B build-winui-native -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=OFF
cmake --build build-winui-native --config Release --target OutLoudPocketSAPI OutLoudVoiceManagerNative OutLoudVoiceImportHelper outloud-sapi-voices
dotnet build ui/OutLoud.WinUI/OutLoud.WinUI.csproj -c Release -p:Platform=x64
```

The WinUI project copies the native bridge/helper and pinned baseline runtime
through its existing build targets. Default development output is framework
dependent; see [WinUI prerequisites](../ui/OutLoud.WinUI/README.md#prerequisites-and-configuration).
Builds do not install certificates, register COM/SAPI or install models/voices.
No Python environment or converted model assets are required.

## Existing targeted native checks

Configure a separate checker build to keep production settings unchanged:

```powershell
cmake -S . -B build-native-checks -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
cmake --build build-native-checks --config Release --target outloud-voice-library-tests outloud-rust-session-tests outloud-sapi-volume-tests
ctest --test-dir build-native-checks -C Release -R '^(voice-library|rust-session-lifecycle|sapi-volume)$' --output-on-failure
```

Select only the checks relevant to the change. The library checker uses isolated
fixtures; the basic session lifecycle check uses controlled child processes.
The volume checker uses synthetic PCM through the SAPI/Sonic path. These checks
do not require machine-wide registration or installed user models.

`scripts/validate.ps1` executes configured CTests without building. Pass an explicit
`-BuildDirectory`; its default `build` directory is not the application build
above. `-List` lists the selected tests. Its `Fast`/`Correctness` modes filter
CTest labels, so use explicit CTest names for checks that have no matching label.
The current `rust-session-lifecycle` check has the `correctness` label.

`OUTLOUD_BUILD_DEVELOPMENT_TOOLS=ON` builds the existing development checkers
without requiring CTest registration. `OUTLOUD_ENABLE_RUST_SAPI=ON` additionally
enables the private SAPI checker DLL; it is not a production-backend switch.
Real-runtime checks require explicitly supplied verified assets; inspect the
checker's usage before running them. Private overrides do not exist in shipping
COM exports. Do not treat a build as a live playback or UI test.

The existing `outloud-rust-selection-tests --model-update-contract` mode checks
updated-manifest schema, pinned revisions, trusted paths and fixed configuration
without models, runtime execution or registration. Its private
`--model-update-manifest <alias> <receipt-json> <expected-yaml>` mode compares an
updated configuration against the managed install plan. These are checker-only
interfaces, not production model-import or configuration overrides.

## Packaging and deployment

Use the [production installer workflow](../installer/PRODUCTION.md) to build
`release/OutLoud-Setup.exe` and its SHA-256 file. Use the
[development deployment workflow](SAPI_ADAPTER.md) for explicit deployment.
Building either workflow does not authorize live installation or registration.
The user performs Windows Sandbox installer acceptance testing.

Documentation-only changes need content/link checks and `git diff --check`,
not application tests. Build the installer when distribution notices or its
payload change, and only when requested.

## Recorded validation: 0.4.4

- Release x64 WinUI builds and the four production native targets completed.
- 46 isolated application-update/runtime-repair checks passed, covering release
  comparisons, malformed/draft/prerelease metadata, network failures, regular-file
  and hardlink checks, directory rename protection, no-overwrite atomic
  publication, exact-version repair metadata and supported/unsafe ZIP layouts. Tests used synthetic files and
  HTTP responses, without installed storage, registrations or synthesis.
- 30 isolated Settings assertions passed, including the combined update flow,
  application-check busy state, duplicate-request protection and startup wiring.
- 470 focused native inventory upgrade checks passed using private fixture files
  and simulated signing/registration. They include generic identical restored
  payloads, read-lease write/rename protection, strict post-snapshot identity
  checks, journal binding and rejection of changed content, size, unknown paths
  and invalid partial operations. The latest checks also cover verified copy
  results for empty files and copy-buffer boundaries, refusal of incorrect
  source hashes/overwrites, retained original read leases and duplicate paths
  in Restart Manager checks. The installer now reuses locked baseline checks
  and verified copy results instead of rereading unchanged bytes.
  Cancellation checks cover the real sidecar boundary and simulated fresh,
  release and repair operations: cancellation before begin creates no transaction,
  and cancellation after staging rolls back without signing or publication.
  The user tested installation cancellation; the latest installer was not
  launched by the agent.
- Bulk-import and responsive-uninstall changes passed Release x64 WinUI/native
  builds and installer compilation. The 207 private uninstall checks passed.
  On 2026-11-11 the user confirmed bulk import and uninstall work. No live
  uninstall or import into the installed library was performed by the agent.
  Individual conflict/report/layout scenarios were not separately confirmed.
- 768 isolated maintenance lifecycle checks passed, including rollback/recovery.
  The first authorized live attempt stopped before publication because Restart
  Manager detected the helper's own new snapshot leases. This was corrected and
  regression-tested, including the DetectedSelf flag; external-client checks
  remain active. Subsequent user-provided installation logs confirmed successful
  completion, including final verification and archive cleanup.
- Self-contained installer packaging completed. All 592 payload files and the
  versions of eight project components were verified; the installer checksum
  matches its SHA-256 file. The original bundled Kyutai runtime matched its pin.
- Actual runtime download/repair, application startup update UI, a separate
  older-version upgrade and playback were not individually live-verified for
  this release. The user's acceptance does not establish Windows Sandbox
  coverage; no agent-run installation or uninstallation is claimed for the
  final artifact.

On 2026-11-11, installation log `Setup Log 2026-11-11 #005.txt` showed a
66.23-second finalization interval. An isolated 592-file fixture identified
repeated directory indexing across full recovery checkpoints in archive audit.
Indexing each validated path once reduced archive audit from 95.82 seconds to
6.37 seconds, with the same 1,797 inventoried archive files. Both runs still
decoded and validated every checkpoint and retained file/hash/identity checks.
These fixture timings do not establish end-to-end live installer duration.
The 207 focused uninstall/archive checks also passed with private files and
simulated registration/certificates, including interrupted cleanup, modified and
unknown files, busy resources and large retained histories. The updated Settings
card passed a Release x64 build; its live rendering remains to be checked.

## Recorded validation: 0.4.3

- 148 isolated native checks passed, covering content/name collisions, confirmed
  replacement, retained library/SAPI identity, active-file protection, stale
  confirmations, interrupted recovery, unknown/changed files and staging-handle
  security access. No installed voice files or registrations were changed by these checks.
- Release x64 native components and self-contained WinUI installer packaging
  completed. The versioned installer still requires Windows Sandbox acceptance testing.

## Recorded validation: 0.4.2

- 24 isolated UI regression checks passed using the updated production method
  bodies. They exercised three minimize/restore cycles, maximize/restore, actual
  negative WinUI insets, synchronous and asynchronous workflow errors,
  duplicate-operation blocking, and a failed pending task during window closing.
- The isolated harness used a temporary WinUI window and simulated workflow
  failures. It did not initialize the installed application, models or SAPI.
- Release x64 native components and self-contained WinUI installer packaging
  completed. All 592 packaged file hashes and the versions of eight project
  components were verified; the installer checksum matches its SHA-256 file.
- Live installation, upgrade, playback and Windows Sandbox acceptance testing
  were not performed.

On 2026-11-04, the user confirmed that minimizing the application no longer
causes a crash. This confirmation covers minimization; it does not establish
other live acceptance-test results.
