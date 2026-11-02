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
