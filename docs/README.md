# Out Loud 0.4.1 documentation

Out Loud uses WinUI 3, the official Pocket TTS Rust runtime and native
`.safetensors` voice profiles. These guides describe the current implementation.

## Using and building Out Loud

- [Overview, installation and first use](../README.md)
- [Voice Library and Voice Tester](VOICE_MANAGER_GUIDE.md)
- [WinUI, Settings and shared storage](../ui/OutLoud.WinUI/README.md)
- [Model installation, selection and update checks](MODEL_UPDATES.md)
- [Build and targeted validation](BUILD_AND_VALIDATION.md)
- [Production installer, updates and uninstallation](../installer/PRODUCTION.md)
- [Development deployment, signing and SAPI registration](SAPI_ADAPTER.md)

## Native integration

- [Architecture](ARCHITECTURE.md)
- [Native profile validation](VOICE_IMPORT.md)
- [Voice Library storage and operations](VOICE_LIBRARY.md)
- [Machine-wide library and permissions](MACHINE_VOICE_LIBRARY.md)
- [Managed SAPI voice tokens](MULTI_VOICE_SAPI.md)
- [Rust session, streaming and asset selection](RUST_RUNTIME_SESSION.md)
- [Cancellation and SAPI controls](SAPI_CANCELLATION.md)
- [Leading bookmarks](SAPI_BOOKMARKS.md)
- [Bounded error logging](NATIVE_ERROR_LOGGING.md)
- [Upstream sources and bundled runtime identity](../reference/UPSTREAM.md)

## Distribution

- [Out Loud license](../LICENSE)
- [Current third-party notices](../THIRD_PARTY_NOTICES.md)
- [Changelog](../CHANGELOG.md)

The [release records](releases/) describe their respective published versions;
they are not build or deployment instructions for 0.4.1.
