# Current Voice Manager guide

The app is Windows x64 WPF, `net10.0-windows`, `asInvoker`. It manages ProgramData
library entries and HKLM Registry64 voices. [A11/A12](BUILD_AND_VALIDATION_IMPROVEMENTS.md)
is the build guide; [shared engine registration](SAPI_ADAPTER.md) remains a
separate manually initiated development operation.

## Library page and operations

The list shows Name, Library Status and SAPI Status, with full-row selection and
details for identity, metadata and diagnostics. Voice model-version tracking,
classifications and compatibility warnings are not used. Supported structure and
integrity determine availability; see [model updates](MODEL_UPDATES.md).
Refresh preserves selection by
immutable ID. Failure keeps the last good snapshot and shows an error; that list
is not proof of current state. Loading/empty/error states and dismissible
Info/Success/Warning/Error notifications remain distinct. Refresh validates each
row once under a shared library-lock epoch, then reads fresh tokens; no trust is
cached across refreshes or privileged mutations.

| Action | Behavior |
| --- | --- |
| Import Voice | Select a prepared Pocket `.safetensors` or Out Loud `.olv` file and confirm its name. The helper validates/converts prepared state or validates/copies OLV, publishes a new ID and installs its owned machine token. |
| Import-or-Replace | A trimmed, ordinal case-insensitive snapshot name match prompts confirmation. Multiple matches require explicit ID selection. Replacement retains the selected ID, stored name and token; no separate Replace toolbar action. |
| Remove | Confirm selected ID/name/content/health. Helper rechecks them, coordinates journaled file/token removal and refreshes even after failure/conflict. |
| Export Voice | Select a healthy library voice, save a byte-exact `.olv` outside the library and confirm any overwrite. Runs without elevation; selection is retained without refresh. |
| Refresh | Read diagnostics without elevation, cleanup or repair. |

Names are backend-validated/trimmed; duplicate names remain legal. IDs are random
immutable 32-character lowercase hexadecimal strings and select paths/tokens.
Default duplicate-content rejection compares the full OLV SHA-256. Reimport after
removal allocates a new ID; replacement retains it.

Future token descriptions, `409` and `Attributes\Name` use the exact stored name.
No `Out Loud - ` prefix is added; Vendor stays `Out Loud`, Language `409`, token
key `OutLoud_Pocket_<library_id>`. GUI Rename and raw-audio preparation remain
unavailable; rename is a separate library CLI operation and raw-audio preparation
requires upstream tools. **Export Voice** copies portable OLV conditioning;
Tester **Export WAV** synthesizes audio. See [OLV import/export](VOICE_MANAGER_OLV_IMPORT_EXPORT.md)
for workflows, compatibility, file safety and manual checks.

## Privilege and result boundaries

```text
Refresh: WPF -> ViewModel -> service -> native bridge -> read-only snapshot
Export: selected identity -> Save dialog -> non-elevated bridge -> native OLV copy
Mutation: confirmed request -> authenticated pipe -> runas helper -> backend
Tester: ViewModel -> SAPI backend -> request-owned STA -> exact installed token
```

[OutLoudVoiceManagerNative.dll](../frontend/native/voice_manager.h) retains ABI v1,
the original six C exports and immutable opaque result ownership. Four additive
exports provide capability discovery, destination inspection and checked OLV
export. Managed interop copies
borrowed UTF-8 data and frees the result through the native export. There is no
installed-library mutation/root/scope export. The one-shot helper retains protocol
v3 operations 1–3 for prepared Import/Replace and Remove, adding explicit OLV
Import/Replace operations 4/5 with unchanged framing. It authenticates parent/pipe and chooses the fixed protected
machine library/registry. Requests cannot select arbitrary roots/hives/token keys.

Only confirmed mutations launch `OutLoudVoiceImportHelper.exe` with `runas` for
Windows approval. The frontend stays asInvoker. Busy/testing guards prevent
overlapping mutations and exports. Closing waits for an active export to finish
or clean up. Shared runtime resolves from fixed HKLM64 COM registration
in `<deployment>\bin\OutLoudPocketSAPI.dll` layout; the helper does not register
that class. Runtime/registration failure after import reports partial success
with the retained library entry. UAC cancellation before submission is cancellation;
lost transport after submission can report `outcome_unknown` and require refresh.
Warnings/cleanup outcomes are not presented as complete success.

Source: [MainViewModel](../ui/OutLoud.VoiceManager/ViewModels/MainViewModel.cs),
[dialogs](../ui/OutLoud.VoiceManager/Services/ImportDialogService.cs),
[elevation service](../ui/OutLoud.VoiceManager/Services/ElevatedVoiceImportService.cs),
[native coordinator](../frontend/mutation/mutation.cpp).

## Health, ownership and recovery

The default root is resolved through ProgramData, normally
`C:\ProgramData\OutLoud\VoiceLibrary`. Ordinary users read; mutations require
enabled administrator/SYSTEM authority. Protected ACL/ownership, pinned local
paths/file identity, reparse/hard-link, unknown-file and in-use checks remain
enforced. See [storage](VOICE_LIBRARY.md) and [security](MACHINE_VOICE_LIBRARY.md).

Library `valid` and SAPI `correct` are different diagnostics. Without intended
runtime configuration, `correct` checks the exact owned HKLM64 token's library
identity/path/name/schema/defaults, not runtime availability, playback, default
enumeration or WoW visibility. Its detail states that limit. `stale_name` can
describe an older prefixed owned token; reading does not rewrite it. Exact-ID
maintenance is documented in [managed SAPI voices](MULTI_VOICE_SAPI.md).

Replacement stages/validates content and uses its journal/rollback/recovery.
Removal publishes a journal before reversible detach and fingerprint-guards
token removal. Recovery restores unchanged-token data or finishes verified
absent-token removal; ambiguous/unsafe data stays pending. Refresh can show
`removal_pending` but never repairs/elevates. Authorized Remove retry recovers
that ID first; administrator `recover-removals` is explicit separate maintenance.
Partial restoration/cleanup failures are warnings. This is process-interruption
recovery, not an atomic filesystem/registry transaction or proven power-loss
guarantee. See the unchanged [A02 state machine](STABILIZATION_PHASE_2.md).

## Voice Tester and WAV

Enter Tester from the library page. Selection prefers the current library ID;
friendly names/IDs remain distinguishable. Play/export requires valid health,
`correct` SAPI status and no row SAPI error. Plain text preserves punctuation and
line breaks. Whitespace-only/NUL input is rejected; maximum length is 12,000
UTF-16 code units. Oversized pasted text remains visible with validation instead
of truncation; the editor is read-only while busy.

Play uses installed Windows SAPI Automation on a request-owned STA. The exact
HKLM token is opened without creation, ownership/ID/CLSID/voice path and assigned
identity are checked, and another voice is never substituted. Rate is neutral,
volume 100. Preparing/Speaking/Exporting/Stopping/Completed/Stopped/Failed states
are explicit; synthesis and audible playback times cannot be separated reliably.
Stop purges and awaits COM cleanup. Switching voice, leaving Tester and closing
the window also stop/await active work before a new request.

Export WAV runs a **new synthesis** into a private temporary file beside the
chosen destination, closes the stream, validates PCM16 mono 24 kHz RIFF data and
publishes the finished file. Cancellation/error removes incomplete output;
cleanup failure reports the retained path. It does not record previous speaker
playback or export OLV metadata.

Source: [ViewModel](../ui/OutLoud.VoiceManager/ViewModels/VoiceTesterViewModel.cs),
[backend/WAV validation](../ui/OutLoud.VoiceManager/Services/SapiVoiceTestBackend.cs),
[COM session](../ui/OutLoud.VoiceManager/Services/SapiTestSessionFactory.cs).
[Step 3D.4](VOICE_MANAGER_TESTER.md) retains detailed historical validation.

## Themes and manual checks

Pure WPF resources support light/dark/high contrast. The theme service reads
Windows `AppsUseLightTheme`, observes preferences/high contrast and updates on
the dispatcher. This personalization read is not HKCU voice management.
[Theme source](../ui/OutLoud.VoiceManager/Themes/WindowsThemeService.cs) and
[historical design evidence](VOICE_MANAGER_THEME.md) describe the implementation.

After deliberate installation/update, manually check list/details, Refresh/ID
selection preservation, light/dark/high contrast and resizing. In a controlled
session, verify exact-voice Play/Stop/export and confirmed mutations/error/pending
notifications. These can speak, export or change installed voices; they are not
ordinary builds/fast checks. Workspace results do not prove installed binaries
or WoW are using the latest source.
