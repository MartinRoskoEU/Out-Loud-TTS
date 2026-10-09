# Step 3D.4: Out Loud Voice Tester

> Historical Step 3D.4 design/validation evidence. Current user procedures are in [VOICE_MANAGER_GUIDE.md](VOICE_MANAGER_GUIDE.md); build/check selection is in [A11/A12](BUILD_AND_VALIDATION_IMPROVEMENTS.md). Past test results do not certify later installed binaries.

The WPF Voice Manager now has Voice Library and Voice Tester pages. The tester
uses the installed production SAPI engine, with one exact managed machine token
per request. Play, Stop and Export WAV are implemented. Test text survives
navigation. The existing library service, mutation helper, native ABI, registry
scope, deployment and inference code are unchanged. Entry was clean at
`718e6a2` (Add voice removal to WPF voice manager).

## Audit and integration decision

Inspected `MainWindow`, `MainViewModel`, the service/native bridge and immutable
`VoiceSnapshot`/`VoiceRecord` models. Existing library selection already uses
library IDs and replaces snapshots atomically. The window had one library grid;
it is retained in the original namescope, with navigation and a separate tester
UserControl added around it. Import/replace/remove retain their existing helper
and confirmation paths. The app remains net10.0-windows, x64 and asInvoker.

Inspected `sapi/src/adapter.cpp`, its engine/token ownership, request gate,
cooperative cancellation and PCM delivery, `engine/src/asset_io.cpp`, and the
managed-token implementation/manual SAPI scripts. `Resident` owns the model,
voice, tokenizer and pipeline; the pipeline is destroyed first and joins its
workers. `Engine::~Engine()` resets Resident and releases its token. Active Speak
holds an extra engine reference and joins its coordinator before releasing it.
The adapter already streams PCM16 mono 24 kHz; the UI does not buffer or simulate
this stream. Model/voice mappings use `FILE_SHARE_READ` and release their view,
mapping and file handles in `MappedFile::~MappedFile()`.

Chosen integration: Windows SAPI Automation, using `SAPI.SpObjectToken`,
`SAPI.SpVoice` and optional `SAPI.SpFileStream` through built-in .NET dynamic COM
interop. This uses the same installed native Pocket engine as ordinary SAPI
clients. No System.Speech assumption, third-party package, generated interop
assembly, Python or separate inference implementation is involved.

Verified the actual declarations in Windows SDK 10.0.26100.0
`um/sapiaut.idl` and `um/sapi.h`: token SetId defaults, ISpeechVoice operations,
async/purge/plain-text flags, WaitUntilDone, status LastHResult and stream format
enum. Microsoft documents [token identity and SetId](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ms722610(v=vs.85))
and the bounded [WaitUntilDone call](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ms723616(v=vs.85)).
The x64 .NET build and injected automation objects validate the managed call
shape; actual COM activation and installed-engine behavior remain manual.

A narrow native playback bridge would add a new exported ABI and ownership
protocol for functionality already exposed by SAPI. A new managed speech package
would add a dependency and another lifetime layer. Automation is the smaller
integration here, with explicit STA ownership, exact-token checks and observable
HRESULTs. Its tradeoffs are dynamic interface dispatch, no reliable separation
of synthesis and audible playback, and reliance on real SAPI/driver cleanup.

## Architecture and changed files

```
MainWindow navigation -> VoiceTesterView -> VoiceTesterViewModel
    -> IVoiceTestBackend -> SapiVoiceTestBackend
    -> request-owned STA / ISapiTestSession -> installed Windows SAPI token
    -> existing OutLoudPocketSAPI.dll -> existing native engine
```

New files under `ui/OutLoud.VoiceManager/`:

- `Models/VoiceTest.cs`: immutable requests, outcomes, states and diagnostics.
- `Services/IVoiceTestBackend.cs`: backend, session and export-dialog boundaries.
- `Services/SapiVoiceTestBackend.cs`: request serialization, joined STA work,
  cancellation, temporary export and strict WAV validation.
- `Services/SapiTestSessionFactory.cs`: token validation, COM calls and cleanup;
  activation/release delegates allow automation tests without real COM objects.
- `Services/WaveExportDialog.cs`: normal owned Save File dialog.
- `ViewModels/VoiceTesterViewModel.cs`: selection, text, commands, revisions,
  cancellation and severity notifications.
- `Views/VoiceTesterView.xaml` and `.xaml.cs`: tester UI and InitializeComponent.

Modified frontend files: `App.xaml.cs`, `ViewModels/MainViewModel.cs`,
`Views/MainWindow.xaml`, `Views/MainWindow.xaml.cs`, and `Themes/Common.xaml`.
App wires the production backend. MainViewModel wires navigation/snapshots and
guards mutations while testing. Window closing awaits tester cleanup. Common
resources add editor, selector and navigation templates using existing brushes.

Validation adds `ui/OutLoud.VoiceManager.Tests/VoiceTesterTests.cs`, updates its
project source links and Program, and extends `ui/validation/RuntimeValidation.cs`.
This document is new; `VOICE_MANAGER_WPF.md` links to it. No native source,
manifest, solution structure, palette, helper protocol or package was changed.

## Identity, input and UI

The selector shows friendly names, library IDs and original library/SAPI status
strings. Duplicate names are distinguishable and never used as identity.
Only canonical 32-character lowercase hexadecimal managed IDs enter this list.
Playback requires the existing snapshot's `valid` health, `correct` SAPI status
and absence of a row SAPI error. Unsuitable rows remain visible with validation;
these checks do not promise that the installed engine can actually speak.

Each request captures its immutable voice ID, library root and exact text.
The backend opens only:

```
HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Speech\Voices\Tokens\OutLoud_Pocket_<LibraryId>
```

`SpObjectToken.SetId(fullId, "", false)` opens without creating. It checks the
returned ID, `LibraryId`, `OutLoudManagedOwner = OutLoud.ManagedVoice.1`, Pocket
CLSID `{F38C3398-3552-43B9-8350-7DD301327D70}`, and VoicePath against
`<snapshot root>\voices\<LibraryId>\voice.olv`. The snapshot must provide an
absolute root. It assigns this token to SpVoice and checks the assigned ID again.
It never enumerates default voices or falls back to a name/default voice.
Missing/changed tokens, activation and engine failures remain errors with stage,
HRESULT and useful path details. Shared model configuration is still interpreted
by the existing engine; the UI does not alter it.

Entering Tester prefers the selected library ID. The user can choose another
voice in Tester. Removing or invalidating the selected row clears/disables the
selection and cancels a live request. No other voice is silently chosen.
Switching voice during a test requests Stop; another test waits for cleanup.

The multiline editor preserves spaces, punctuation and line breaks exactly.
Whitespace-only input and NUL are rejected; the limit is 12,000 UTF-16 code units.
Oversized pasted input is retained with validation rather than truncated.
The initial sample is the Forsaken quest text from the request. There is no text
rewriting, extra splitting, artificial delay or punctuation processing.
Speak uses flags `1 | 16` (async and IS_NOT_XML); `<`, `>` and `&` stay plain text.
Rate is neutral (0), volume 100; production control semantics remain unchanged.

Navigation, selectors, text, keyboard focus and notifications use existing
Light/Dark/High Contrast dynamic resources. Labels/access keys support keyboard
navigation. State/severity text accompanies color; errors have dismissible
diagnostics. The editor is read-only while busy. At small window sizes the main
tester area scrolls; Play/Stop/export and status remain outside that scroll area.

## States, overlap and Stop

States are Idle, Preparing, Speaking, Exporting, Stopping, Completed, Stopped and
Failed. Speaking is explicitly labelled "SAPI is synthesizing or playing audio".
The Automation wait/status path does not establish the precise first-audible
sample or generation/playback boundary; no fabricated progress or separate
Generating/Playing event is shown. The busy bar is indeterminate.

Play and export are rejected while busy, with a second service-level interlocked
guard. There is never more than one tester SAPI request. An operation revision
ignores progress from earlier/completed requests and all progress after Stop.
A late success after requested cancellation remains Stopped, except when a WAV
has already been successfully published; that outcome truthfully reports saved.

UI Stop sets a request-owned CancellationToken. Only the owning STA invokes COM.
During normal work it uses WaitUntilDone with a 20 ms timeout, checks cancellation
between calls, sends a single async plain-text purge (`1 | 2 | 16`), then keeps
waiting for completion. It suppresses pre-cancelled requests and cancelled work
after initialization. It never queues a new selected voice on the old session.
The UI remains in Stopping until teardown and thread join finish.

The existing native engine observes SAPI abort and cooperatively cancels at safe
points. A current prefill/candidate/decode or site Write is not forcibly
interrupted. An executing COM initialization/driver call cannot be preempted
by this token. Stop is therefore a request followed by quiescence, with latency
dependent on SAPI, the native operation and the audio driver. No forced thread
termination, process kill, detached worker or native kernel change is used.

## Resource lifetime

Each playback/export creates one dedicated STA. A pool task joins it; no UI-thread
Join is used, and the backend task cannot complete before STA work and teardown
finish. COM objects do not cross into the UI or another request. Session-owned
RCWs are tracked by reference so getter aliases are released once. These isolated
objects use explicit FinalReleaseComObject; no shared/external RCW is released.

Cleanup attempts to purge and drain active speech. Even a failed purge still
attempts the wait. SpVoice is released first, then the file stream is closed and
all remaining stream/format/status/token/data-key references are released in
reverse ownership order. Partial initialization, success, Stop, synthesis/output
failure, voice switch, leaving Tester and application closing all reach cleanup.
A failure releasing one object does not skip the others. Cleanup failure is
reported rather than presented as successful playback.

Returning to Library waits for Stop before showing that page; import/replace/remove
are disabled during tester work. Closing an idle window completes directly;
closing during testing cancels the first close, awaits cleanup, then schedules
the final close. Text remains in the ViewModel across page navigation.

Source inspection verifies native Resident destruction when the final Engine
reference is released. It does **not** prove that the installed SAPI host/driver
releases every engine reference after these real COM calls. That production
mapping-release check was deliberately not executed. If COM wait itself fails,
the backend attempts all releases and reports failure, but cannot prove native
quiescence from the failed call. Existing file-sharing protections remain the
authority; a remaining external/SAPI reference can still make a mutation fail.
No global SAPI cache flush or file-sharing relaxation is introduced.

Per-request disposal deliberately sacrifices Resident reuse across tester runs;
the next test may reload assets. Tester timings are not resident-performance
benchmarks and no latency/speedup claims are made. Native inference math and
canonical scheduling are unchanged.

## WAV export

Export WAV is implemented through a fresh request using the same exact-token
SAPI backend. The Save File dialog prompts before overwriting. SpFileStream's
format is set to SDK value 26 (PCM16 mono 24 kHz), and SpVoice is instructed not
to change the output format when assigning that stream. See Microsoft's
[audio format values](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ms720595(v=vs.85))
and [SpFileStream.Open](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee125549(v=vs.85)).

SAPI writes a randomly named private temporary file beside the destination.
After synthesis, all COM objects are released and the stream is closed. The
backend verifies the actual RIFF length/chunk boundaries, PCM format, mono,
24 kHz, 16-bit samples and nonempty even-sized data, then moves the complete file
over the chosen destination. The previous destination is retained on cancellation
or invalid output. Incomplete temporaries are deleted; a cleanup failure reports
the retained path. Destination I/O/format errors retain diagnostics.

Cancellation is checked immediately before publication. Stop racing the final
file move cannot recall an already published valid WAV; the result reports its
saved path. Save-dialog cancellation performs no SAPI request. Export is a new
synthesis, not capture of an earlier Play, microphone or loopback recording.
No sample identity with separately generated playback is claimed. Actual
production SAPI format negotiation/WAV output remains a manual verification;
Automated tests use fake SAPI output and validate real temporary RIFF files.

## Validation completed

With `DOTNET_GENERATE_ASPNET_CERTIFICATE=false`, ran:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
dotnet build ui/OutLoud.VoiceManager.sln -c Release -p:Platform=x64
dotnet run --project ui/OutLoud.VoiceManager.Tests/OutLoud.VoiceManager.Tests.csproj -c Release -p:Platform=x64 --no-build
ctest --test-dir build -C Release --output-on-failure
.\ui\validation\validate-with-runtime.ps1
git diff --check
git status --short
```

Native configure/build passed. Release x64 WPF build passed with zero warnings
and errors. **155 frontend checks passed**, retaining the existing import,
replace, remove and helper/transport cases. **17/17 existing CTests passed
(58.76 seconds)**, including canonical native fixtures and library/bridge/token
regressions. No native test, inference code or performance gate was added.

New fake tests cover exact ID/token mapping with duplicate names, text validation
and punctuation, overlap, Play/Stop/state, old callbacks, repeated generation,
selection changes, missing/changed tokens with no fallback, engine activation
errors, async HRESULTs, STA affinity/join/disposal, failed purge/release,
navigation, busy shutdown and WAV success/cancellation/invalid output/I/O errors.
Automation activation/release are injected: no real ProgID activation occurs in
these tests. Temporary files are in a uniquely owned test folder, never Library.

The SDK-built BAML validation passed with fake services and no shown window:
Light/Dark/High Contrast resources, contrast checks, exact selection, multiline
binding, busy/Stop/error controls, severity/dismissal, navigation, refresh,
resizing, fixed playback controls and idle/busy Window closing. Generated fake
view PNGs were inspected under ignored `generated/ui/3d4/`.

Whitespace validation covers tracked and new files. Nothing was staged,
committed or pushed. No production playback/export, registry/library mutation,
UAC, deployment/signing, certificate action or WoW action was performed.

## Manual real-machine procedure (not executed)

From a normal, non-administrator PowerShell:

```powershell
Set-Location 'C:\Users\rosko\Desktop\Nendis\application\wow-pocket-tts-sapi'
$env:DOTNET_GENERATE_ASPNET_CERTIFICATE = 'false'
Start-Process -FilePath '.\ui\OutLoud.VoiceManager\bin\Release\net10.0-windows\OutLoud.VoiceManager.exe'
```

1. Launch the normal application without Admin, using the command above.
2. Open Voice Tester.
3. Select Gnome Male; verify its library ID and suitable status.
4. Enter `Hello traveler. This is the Out Loud voice test.`
5. Press Play and check that the UI remains responsive while SAPI works.
6. Verify the correct audible Gnome voice and full text. Observe completion.
7. Play longer text, press Stop during playback and confirm silence/Stopped after
   cleanup. Also try Stop during the initial preparation/synthesis wait.
8. Repeat with Undead Male; verify the voice really changes. Switch voices while
   busy and confirm stopping precedes another Play.
9. Test the Forsaken sample and punctuation-heavy multiline quest text including
   `<`, `>` and `&`; confirm no markup interpretation or inserted pause feature.
10. Try rapid Play/Stop. Play stays unavailable while busy/stopping; no old test
    should restart or overwrite a newer status.
11. Return to Voice Library during a test; it should wait for cleanup.
12. Refresh and verify existing selection/actions. Open import/replace/remove
    confirmations and Cancel; do not approve mutations or change production
    registration/assets as part of this validation.
13. Close/reopen the application, including closing during a test. It should
    finish stopping/cleanup without a crash or an orphan tester thread.
14. Where possible, verify the tested `.olv` is no longer held by this app after
    completion/Stop/leaving Tester, and after app exit. Use a read-only handle
    viewer or the optional sharing probe below; other SAPI clients may retain it.
15. Export WAV to a separate user-owned test directory. Verify RIFF PCM16 mono
    24 kHz and listen to the selected voice. Try cancelled export and an
    inaccessible destination; check errors and preservation of an existing WAV.

Also change Windows Light/Dark/High Contrast settings while the app is running,
test keyboard labels/focus, and resize from 860x560 through 1440x900. Stop and
status should remain visible, with readable selector/text/notifications.

Optional read-only sharing probe in a separate PowerShell (replace the path with
the exact tested voice under the displayed library root):

```powershell
$testedVoicePath = 'C:\ProgramData\OutLoud\VoiceLibrary\voices\<LibraryId>\voice.olv'
$probe = $null
try {
    $probe = [IO.File]::Open($testedVoicePath, [IO.FileMode]::Open,
        [IO.FileAccess]::Read, [IO.FileShare]::None)
    'Exclusive read succeeded; no conflicting file handle observed at this instant.'
} catch { $_.Exception.Message }
finally { if ($null -ne $probe) { $probe.Dispose() } }
```

This neither writes nor changes the asset. An access-denied/path error is not
proof of a remaining mapping; check the path and normal read permissions first.
A sharing failure can belong to another process. A successful instantaneous
probe is not a general guarantee about all other clients or later activity.
Actual replace/remove after playback should be validated separately with an
explicitly disposable voice and authorization to mutate it.

## Follow-up boundaries

Real installed-token activation, audible playback/device errors, cancellation
latency, actual COM/Resident release and SAPI WAV rendering are unverified until
the manual procedure. The tests establish orchestration/call behavior using
fakes; they do not establish audible correctness. No separate benchmark or
sample-equality claim was made.

Future punctuation/A-B work implements `IVoiceTestBackend` (or a narrow native
test service behind it), preserving the view and state/lifetime contract. It
must be explicitly selected and labelled when implemented. This step exposes
only Current SAPI Engine / Baseline: no experimental controls, comma splitting,
silence durations or production punctuation changes.
