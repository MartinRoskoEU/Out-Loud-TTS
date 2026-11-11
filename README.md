# Out Loud

**Offline neural text-to-speech and voice management for Windows.**

Out Loud 0.4.4 is a Windows desktop application that integrates the official [Kyutai Pocket TTS](https://github.com/kyutai-labs/pocket-tts) Rust runtime with Windows SAPI 5.

It provides multilingual speech synthesis, native `.safetensors` voice management, and streaming audio playback. Once the required models are installed, speech generation runs entirely offline.

## Features

### Voice Library

- Import, export, and manage native `.safetensors` voice profiles.
- Register voices with Windows SAPI 5.
- Inspect voice details and registration status.
- Make voices available to SAPI-compatible applications.

### Voice Tester

- Test registered voices using custom text.
- Stream synthesized audio during generation.
- Control playback using the selected voice and model.
- Export speech as a WAV file.

### Model & Runtime Management

- Discover and download supported Kyutai Pocket TTS models.
- Install, verify, select, and remove models.
- Check for model and runtime updates from official Kyutai repositories, and explicitly install verified updates.
- Track model revisions and verify downloaded assets.
- Maintain persistent local model storage without relying on the Hugging Face cache.
- Access public model repositories without authentication.

## Architecture

Out Loud combines a WinUI 3 interface, native C++ components, Windows SAPI 5, and the official Pocket TTS Rust runtime.

Voice profiles are managed independently of model versions. Speech synthesis uses the machine-wide model explicitly selected in Settings, with verified local assets and native `.safetensors` profiles.

| Shared data | Location |
| --- | --- |
| Models, runtime updates, inventories and Settings | `%ProgramData%\OutLoud\ApplicationData` |
| Native voice profiles | `%ProgramData%\OutLoud\VoiceLibrary` |

OutLoud.WinUI requests administrator approval at startup and runs elevated for that session, so model, runtime, Settings, and voice operations do not require additional UAC prompts. The existing voice helper and security checks are retained. External SAPI clients do not require elevation for speech playback.

Audio is generated locally and streamed incrementally through Windows SAPI 5. Model storage, voice registration, and privileged operations use dedicated native components with integrity and access controls.

## Project Background

Out Loud originated during the development of a World of Warcraft addon requiring custom speech synthesis. It evolved into a standalone Windows application for managing AI voice profiles and locally generated speech.

## Installation and first use

### System requirements

- Windows 11 native x64, build 22000 or later.
- An AVX2/FMA-capable x64 processor.
- Administrator approval for installation and when starting Out Loud.
- Internet access to download models and check for updates; installed speech synthesis works offline.

The production Windows installer bundles the WinUI application, self-contained .NET and Windows App SDK dependencies, the native SAPI components and official Pocket TTS Rust runtime. The checked Microsoft Visual C++ x64 redistributable is embedded and installed only when required; setup does not download prerequisites. Models and voice profiles are not bundled.

1. Download `OutLoud-Setup.exe` from [GitHub Releases](https://github.com/MartinRoskoEU/Out-Loud-TTS/releases) and run it.
2. Open Out Loud and approve its administrator request when Windows prompts.
3. In **Settings**, install a supported model and explicitly select it.
4. In **Voice Library**, import a native `.safetensors` voice profile.
5. Select the voice in **Voice Tester**, or use it from a compatible Windows SAPI application.

Models, Settings and managed voices are shared across Windows accounts. Upgrades preserve managed data. Confirmed uninstall removes verified Out Loud-owned resources while preserving unknown user files. See [installer documentation](installer/PRODUCTION.md) for signing, updates, cleanup and limitations.

## Development

### Requirements

- Windows 11 x64
- Visual Studio 2022 C++ Build Tools
- CMake 3.24 or later
- .NET 10 SDK (x64)
- Windows SDK 10.0.26100.0 and the NuGet-restored Windows App SDK dependencies
- .NET 10 x64 runtime and Windows App Runtime 2.5.1 for default framework-dependent development output
- Microsoft Visual C++ v14 Runtime (x64)
- Compatible x64 processor with AVX2/FMA required by the bundled Pocket TTS runtime

The baseline runtime and its upstream license are retained under `development/runtime/pocket-tts/v4.0.1/windows-x64/extracted/`. The project copies and verifies these inputs during build. A build does not download models or install prerequisites. See [WinUI development notes](ui/OutLoud.WinUI/README.md) for runtime provenance and configuration.

### Build

Native components:

```powershell
cmake -S . -B build-winui-native -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=OFF
cmake --build build-winui-native --config Release --target OutLoudPocketSAPI OutLoudVoiceManagerNative OutLoudVoiceImportHelper outloud-sapi-voices
```

WinUI application:

```powershell
dotnet build ui/OutLoud.WinUI/OutLoud.WinUI.csproj -c Release -p:Platform=x64
```

Build operations do not install or register Windows SAPI components. See the [deployment documentation](docs/SAPI_ADAPTER.md) for development installation, signing, and registration.

Build the installer only through [installer/PRODUCTION.md](installer/PRODUCTION.md). The existing script writes `release/OutLoud-Setup.exe` and `release/OutLoud-Setup.exe.sha256`; compiling it does not run setup. To apply source changes to an installer-managed development installation, use the documented `update-installed.ps1` workflow for same-version application updates. Release version changes or changes to the Inno wizard/uninstaller require a newly built installer.

Contributor instructions are in [AGENTS.md](AGENTS.md); current build and targeted checks are in [the validation guide](docs/BUILD_AND_VALIDATION.md). Release records describe their published versions and are not current build instructions.

## Documentation

- [Current documentation index](docs/README.md)
- [Build and Targeted Validation](docs/BUILD_AND_VALIDATION.md)
- [Models and Runtime Updates](docs/MODEL_UPDATES.md)
- [Voice Library](docs/VOICE_LIBRARY.md)
- [Voice Management](docs/VOICE_MANAGER_GUIDE.md)
- [SAPI Integration](docs/MULTI_VOICE_SAPI.md)
- [Rust Runtime](docs/RUST_RUNTIME_SESSION.md)
- [WinUI Development](ui/OutLoud.WinUI/README.md)
- [Deployment](docs/SAPI_ADAPTER.md)
- [Production Installer](installer/PRODUCTION.md)
- [Release History](docs/releases/)

## License

Out Loud is distributed under the repository [LICENSE](LICENSE).

Third-party licenses and attributions are documented in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and `licenses/`.
