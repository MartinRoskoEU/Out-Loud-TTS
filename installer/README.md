# Out Loud 0.4.0 installer

The current installer packages OutLoud.WinUI, its self-contained application
dependencies, the native COM/SAPI adapter and helpers, and the official Pocket
TTS Rust executable. Native `.safetensors` voices and model weights are installed
separately through Out Loud.

- [Build, signing, updates, shared data and uninstall](PRODUCTION.md)
- [Inno entry point](inno/README.md)
- [Development deployment](../docs/SAPI_ADAPTER.md)
- [Current distribution licenses](../THIRD_PARTY_NOTICES.md)

Build through `inno/build-installer.ps1`. Output is `release/OutLoud-Setup.exe`
with its SHA-256 file. Building does not install or register the application.
Installer acceptance testing is performed manually by the user in Windows Sandbox.
