# Out Loud third-party notices

Out Loud 0.4.1 original code is licensed under the [MIT license](LICENSE).
Copyright (c) 2026 Nendis. Third-party software and model assets retain their own
licenses; Out Loud's MIT license does not replace those terms.

## Distributed software

| Component | Distributed version/source | License and notices |
| --- | --- | --- |
| Pocket TTS Rust runtime, Kyutai | Official `v4.0.1`, commit `a922b912dfa366975294afb568d815b305acde5d`; Windows x64 executable reports `3.3.0` | MIT. The original license is installed beside `runtime/pocket-tts/pocket-tts.exe`. Runtime SHA-256: `813cabfc2f8fc7a4e5310deda6025426b3164610dd9f3c3098a6078e5cc1895a`. |
| Rust runtime dependencies | Exact packages in the runtime's [Cargo.lock](licenses/pocket-tts/Cargo.lock) | Original dependency licenses, attribution and source information are in [licenses/pocket-tts](licenses/pocket-tts/). See the scope below. |
| Sonic, Bill Cox | Unmodified sources at `b93885dcb70aae50c6f76b0fe4e0868f029a077e`, statically linked into the SAPI adapter | Apache-2.0; Copyright 2010 Bill Cox, with 2010/2011 credits in the upstream README. [Complete license](licenses/Sonic-Apache-2.0.txt); original source notices remain in `third_party/sonic`. |
| Pocket TTS model configurations, Kyutai | Embedded configurations and pinned repository metadata under `RuntimeMetadata/v4.0.1` | The upstream MIT license is distributed at `RuntimeMetadata/v4.0.1/LICENSE`. Model asset terms are separate. |
| .NET apphost and .NET 10 runtime | Self-contained Windows x64 publish; exact runtime version is recorded in `OutLoud.WinUI.runtimeconfig.json` | Original runtime MIT license and third-party notices are installed as `licenses/DotNet-Runtime-License.txt` and `licenses/DotNet-Runtime-Notices.txt`. The runtime license includes the .NET Foundation and Contributors copyright notice. |
| Windows App SDK / WinUI and resolved Microsoft dependencies | `Microsoft.WindowsAppSDK` 2.5.1 and the packages resolved by the WinUI project | Original package terms/notices are copied to `licenses/nuget/<package>/<version>/`. The SDK's top-level terms and notices are also installed as `licenses/WindowsAppSDK-License.txt` and `licenses/WindowsAppSDK-Notices.txt`. Microsoft binary package terms apply to the redistributed binaries. |
| Microsoft Visual C++ v14 Redistributable x64 | Official 14.51.36247.0 package, embedded in setup | Microsoft's original package terms apply. Setup installs this prerequisite only when required; the package is not relicensed under MIT. Shared Microsoft runtimes are retained on uninstall. |
| Inno Setup, Jordan Russell and Martijn Laan | Unmodified setup/uninstaller engine; current build uses Inno Setup 7.1.0 | [Inno Setup license](licenses/Inno-Setup-license.txt). Original engine copyright notices and website addresses remain intact. |

## Rust runtime dependency notices

The runtime executable contains third-party components in addition to Kyutai's
MIT-licensed code. Its upstream [Rust manifest](https://github.com/kyutai-labs/pocket-tts/blob/a922b912dfa366975294afb568d815b305acde5d/rust/Cargo.toml)
and lockfile identify the source dependencies.

The supplied collection covers all 460 packages in that exact lockfile. It
conservatively includes build-only and non-Windows packages because the upstream
release does not provide an exact dependency/license manifest for the Windows
binary. Inclusion in this collection does not assert that every listed package
is linked into that binary. This is the current runtime's dependency inventory.

- [THIRD-PARTY-NOTICES.txt](licenses/pocket-tts/THIRD-PARTY-NOTICES.txt): original license texts and package attribution.
- [SUPPLEMENTAL-NOTICES.txt](licenses/pocket-tts/SUPPLEMENTAL-NOTICES.txt): original repository notices for packages whose published archives omit them, with immutable source URLs.
- [components.json](licenses/pocket-tts/components.json): exact package versions, source identities and checksums.
- [SOURCE-AVAILABILITY.txt](licenses/pocket-tts/SOURCE-AVAILABILITY.txt): source availability for MPL-2.0 components.

Complete original published source archives for the 18 MPL-2.0 packages are
provided under `licenses/pocket-tts/sources/`, including Symphonia components,
`colored` and `option-ext`. The build verifies them against the pinned lockfile.
Permissive alternatives are selected for multi-licensed dependencies; LGPL
alternatives are not selected. These dependency terms do not change Out Loud's
own MIT license. The collection identifies the bundled runtime; separately
downloaded runtime updates retain their own upstream source and license terms.

## Models and tokenizers

Model weights and tokenizer assets are downloaded through Settings from the
public `kyutai/pocket-tts-without-voice-cloning` repository. They are not bundled
in the installer. Source revisions and content identities are recorded for each
installed model.

The reviewed repository cards declare CC BY 4.0 for these assets. Retain Kyutai
and creator attribution, the license and applicable upstream notices:

- [Model attribution](licenses/Pocket-Model-Attribution.md)
- [Tokenizer attribution](licenses/Pocket-Tokenizer-Attribution.md)
- [CC BY 4.0 legal code](licenses/CC-BY-4.0.txt)
- [Upstream prohibited-use policy](licenses/Pocket-Prohibited-Use-Policy.txt)

## User voice profiles and Windows components

No voice profiles or recordings are bundled. Users supply native `.safetensors`
profiles and are responsible for the rights and consent applicable to those
files and source recordings. File validation does not establish ownership or
speaker consent.

Windows SAPI/COM, ordinary Windows system libraries and installed Windows fonts
remain operating-system components. No font files or Windows SDK signing tools
are included in the installer.
