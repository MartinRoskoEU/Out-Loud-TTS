# Out Loud

**Out Loud 0.2.1** fixes native SAPI volume handling. See the
[release notes](docs/releases/0.2.1.md) for the focused patch and the
[0.2.0 notes](docs/releases/0.2.0.md) for the unchanged model identity and
voice-quality guidance. The installer is prepared locally; public publication
is pending. Historical 0.1.0 and 0.2.0 artifacts are preserved.

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
- Your own compatible Out Loud `.olv` or prepared Pocket TTS 6L `.safetensors` voice.
  **No voices or recordings are bundled.** Ordinary audio files cannot be imported.
- The Out Loud addon, installed separately, for World of Warcraft narration.

The English model and tokenizer are included. Speech runs locally on the CPU;
no GPU, Python, cloud service or internet connection is required for synthesis.
Internet access is needed if setup must download missing Microsoft runtimes.

## Install

1. Obtain `OutLoud-Setup-0.2.1.exe` and its checksum from the approved release
   artifacts. Public downloads use
   [GitHub Releases](https://github.com/MartinRoskoEU/Out-Loud-TTS/releases);
   publication of 0.2.1 is pending.
   Compare its SHA-256 with the release checksum if you want to verify the download:

   ```powershell
   Get-FileHash .\OutLoud-Setup-0.2.1.exe -Algorithm SHA256
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

Voice Manager supports prepared states for Out Loud's six-layer English Pocket
TTS model, including older `current_end` states and states without an explicit
zero-padding tensor. Supported legacy states are normalized once during import
and stored as `.olv`; the original source file is unchanged. Shape, dtype,
finite-value, integrity and structural compatibility checks remain strict.
Voice availability depends on supported structure and integrity, independently of
the model revision used for preparation. No per-voice model classifications or
compatibility warnings are shown. Existing OLV v1 fields and payloads are preserved.
Voices prepared with older weights may sound different or exhibit artifacts with
the updated model. For best quality, users may need to prepare new profiles using
the updated weights and their original reference audio. Out Loud does not create
profiles from recordings or automatically regenerate existing voices.

Kyutai TTS 1.6B / DSM `speaker_wavs` profiles use a different architecture and
cannot be imported. Recognized DSM profiles show **Incompatible Voice Profile**
with the detected and supported formats. Other model architectures, raw speaker
embeddings, recordings and malformed profiles remain unsupported. See
[voice import compatibility](docs/VOICE_IMPORT.md) for the exact supported schema.

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
The developer-only [model update workflow](docs/MODEL_UPDATES.md) discovers official
updates, verifies and converts weights, automatically advances the engine version
when weights change, and validates publication with rollback support. It does not
update installed applications or publish releases automatically.

Production packaging uses the existing scripts:

```powershell
.\installer\inno\build-stage.ps1 -StageDirectory "$PWD\generated\installer\stages\release-0.2.1"
.\installer\inno\verify-stage.ps1 -StageDirectory "$PWD\generated\installer\stages\release-0.2.1"
.\installer\inno\build-installer.ps1 -StageDirectory "$PWD\generated\installer\stages\release-0.2.1" -PreserveHistoricalInstallers
```

Use a new stage directory for each build. Build tools require CMake 3.24+,
Visual Studio 2022 C/C++ tools and Windows SDK, .NET 10 SDK, Inno Setup 7.0.2+,
and the pinned offline model/tokenizer inputs. Packaging builds Release x64,
validates the explicit payload and copies the verified installer to
`release/OutLoud-Setup-0.2.1.exe`. The preservation switch leaves older versioned
installers in place. Packaging does not execute setup or publish to GitHub.
`release/`, generated artifacts and user voice files remain excluded from Git.
Successful staging removes verified intermediate build output. Automatic artifact
cleanup retains the current and previous successful publication pairs, referenced
stages and publication records; uncertain or incomplete artifacts remain for
diagnosis. It does not operate on the installed application or voice data.
