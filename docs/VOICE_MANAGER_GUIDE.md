# Current Voice Manager guide

OutLoud.WinUI Voice Library lists the existing managed machine SAPI voices and
their health/registration diagnostics. Select one row for Detail, Export Voice
or Remove; Refresh preserves selection by library ID and retains the previous
snapshot on failure. Import selects the resulting entry. Removal refreshes the
list; export preserves the current snapshot and selection.

Import Voice and Export Voice accept only native `.safetensors` files. They copy
the original bytes, without conversion or model association. Export keeps the
existing destination inspection and overwrite confirmation. Import/removal use
the existing administrative helper after confirmation. Identical native content
is rejected regardless of its name. A different profile with an existing name
offers Replace voice, Change name or Cancel. Names are unique after trimming
surrounding whitespace and comparing without regard to case. Confirmed replacement
keeps the original name, library ID and Windows speech identity. There is no conversion.

OutLoud.WinUI requests administrator approval when it starts, then uses that
authority throughout the session without another UAC prompt for each operation.
Models, runtime updates and Settings, including the selected model, are shared
under `%ProgramData%\OutLoud\ApplicationData`. Native profiles are shared under
`%ProgramData%\OutLoud\VoiceLibrary` and registered in HKLM Registry64 for all
users. External SAPI applications can use these voices without elevation.

Voice Tester retains the original default text, editing and playback/stop/WAV
controls. It uses the registered native SAPI voice. Choose an installed model
explicitly in Settings before playback; the existing Rust adapter uses that
selection and reports unavailable/invalid models through existing errors. There
is no model selector or model binding on a voice profile.

Refresh never performs recovery. Native import can recover its protected native
replacement transactions; it does not migrate installed models, Settings, legacy
profiles or Windows registrations. Startup can recover inactive staging work
through the existing [verified cleanup mechanism](../ui/OutLoud.WinUI/README.md#operation-cleanup).
Unsupported files/metadata are reported as unsupported. Confirmed complete uninstall follows the
[approved cleanup policy](../installer/PRODUCTION.md#approved-file-cleanup-policy):
remove verified managed application/shared-data resources while preserving
unknown user files. Unlisted files alone do not block other known cleanup.
Ownership/security and active-file checks remain required; source changes need
a new installer before they affect installed binaries.

See [WinUI notes](../ui/OutLoud.WinUI/README.md),
[Voice Library](VOICE_LIBRARY.md) and [SAPI deployment](SAPI_ADAPTER.md).
