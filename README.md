# Out Loud 0.1.0

Offline English text-to-speech for the Out Loud World of Warcraft addon, powered
by Pocket TTS and Windows SAPI5. The included Voice Manager imports, tests,
exports and removes compatible voices.

## Requirements

- **Windows 11, native x64**, build 22000 or later. Windows 10, ARM64,
  32-bit Windows and Windows Server are unsupported.
- Administrator approval for installation and voice imports/removals.
- Microsoft Visual C++ v14 Runtime x64 (14.44.35211.0 or newer) and
  **.NET 10 Desktop Runtime x64**. Setup checks these and offers to download
  and install missing prerequisites from Microsoft with your consent.
- Your own compatible Out Loud `.olv` or prepared Pocket `.safetensors` voice.
  **No voices or recordings are bundled.** Ordinary audio files cannot be imported.
- The Out Loud addon, installed separately, for World of Warcraft narration.

The English model and tokenizer are included. Speech runs locally on the CPU;
no GPU, Python, cloud service or internet connection is required for synthesis.
Internet access is needed if setup must download missing Microsoft runtimes.

## Install

1. Download `OutLoud-Setup-0.1.0.exe` from
   [GitHub Releases](https://github.com/MartinRoskoEU/Out-Loud-TTS/releases).
   Compare its SHA-256 with the release checksum if you want to verify the download:

   ```powershell
   Get-FileHash .\OutLoud-Setup-0.1.0.exe -Algorithm SHA256
   ```

2. Close World of Warcraft, Voice Manager and other applications using Out Loud.
3. Run setup and approve the Windows administrator prompt. Read and accept the
   licenses and acknowledge the separate Kyutai prohibited-use policy. Approve
   missing Microsoft prerequisites if needed; if a restart is requested, restart
   manually and run setup again.
4. Complete setup, then open **Out Loud** from the Start menu. The default
   application directory is `C:\Program Files\OutLoud`.

The installer is publisher-unsigned. Setup separately requests consent for an
installation-specific machine signing certificate used by the SAPI engine.
That local engine certificate does not publisher-sign the downloaded installer.

Updates use the existing installation directory and preserve managed voices,
settings, SAPI registration and installation identity. Setup supports changes to
the managed file list, including the earlier 23-file development installation
updating to this 24-file package. It verifies the recorded files before replacing
or retiring them and retains originals for checked rollback. Unknown or modified
files are never silently overwritten or deleted.

## Use

1. In Voice Manager, choose **Import Voice**, select a compatible voice file and
   confirm its name. Import requests administrator approval.
2. Select the imported voice and open the **Tester** to synthesize speech.
   **Export WAV** saves synthesized audio; **Export Voice** saves a portable
   `.olv` backup outside the managed library.
3. Restart World of Warcraft if it was open during import, then select the
   imported voice in the Out Loud addon's voice settings.

SAPI speech rates from **-10 to +10** are supported using Sonic's pitch-preserving
speed adjustment: approximately **0.333x**, **1x**, and **3x** at -10, 0 and +10.
Rate 0 preserves the original generated PCM and direct streaming path.
Fragment-specific XML rate adjustments are not supported in this release.

Managed voices are shared across Windows users and normally stored in
`C:\ProgramData\OutLoud\VoiceLibrary`. Use Voice Manager to manage them instead
of editing library files or registry entries. The app prepares no voices from
raw recordings; use voices you have the rights and consent to use.

## Uninstall

**Uninstall permanently deletes all managed voices, managed settings and owned
application data for this installation, including the shared voice library.**
There is no keep-voices option. Export any voices you want to retain to a location
outside the managed library before proceeding.

Close Out Loud, World of Warcraft and other SAPI clients. In Windows
**Settings > Apps > Installed apps**, choose **Out Loud > Uninstall**, approve
administrator access and confirm the permanent-deletion warning.

Complete uninstall removes the application/model/tokenizer files, owned SAPI
voice and engine registrations, shortcuts, installation metadata, owned logs,
and the installation-specific certificate and private key. Shared Microsoft
runtimes, unrelated SAPI voices, original import files and backups outside managed
storage remain.

If setup or uninstall reports a lock or recovery problem, follow its message and
rerun the same setup or uninstaller after resolving it. Do not manually delete
installation files or protected recovery records. See
[complete uninstall behavior](installer/inno/UNINSTALL.md) for ownership and
recovery details.

## License

Original Out Loud code is licensed under [MIT](LICENSE).
**Copyright (c) 2026 Nendis.** Third-party components retain their own licenses;
see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [licenses/](licenses/).

The bundled Kyutai model and tokenizer use **CC BY 4.0** with their supplied
attributions. Kyutai's [prohibited-use policy](licenses/Pocket-Prohibited-Use-Policy.txt)
is presented separately during setup. Sonic is licensed under **Apache 2.0**.
These licenses do not grant rights to another person's voice or recordings.

## Development

See [production build instructions](installer/inno/BUILD.md),
[versioning](installer/inno/VERSIONING.md),
[architecture](docs/ARCHITECTURE.md), and the
[Voice Manager guide](docs/VOICE_MANAGER_GUIDE.md).
The authoritative product version is [ProductVersion.props](ProductVersion.props).

Production packaging uses the existing scripts:

```powershell
.\installer\inno\build-stage.ps1 -StageDirectory "$PWD\generated\installer\stages\release-0.1.0"
.\installer\inno\verify-stage.ps1 -StageDirectory "$PWD\generated\installer\stages\release-0.1.0"
.\installer\inno\build-installer.ps1 -StageDirectory "$PWD\generated\installer\stages\release-0.1.0"
```

Use a new stage directory for each build. Build tools require CMake 3.24+,
Visual Studio 2022 C/C++ tools and Windows SDK, .NET 10 SDK, Inno Setup 7.0.2+,
and the pinned offline model/tokenizer inputs. Packaging builds Release x64,
validates the explicit payload and copies the verified installer to
`release/OutLoud-Setup-0.1.0.exe`. It does not execute setup or publish to GitHub.
`release/`, generated artifacts and user voice files remain excluded from Git.
