# Step 2O: leading SAPI bookmarks

> Historical Step 2O design/validation record. Leading-event ordering remains as described below; current aggregate input/diagnostic limits and runtime are in [ARCHITECTURE.md](ARCHITECTURE.md). Use [A11/A12](BUILD_AND_VALIDATION_IMPROVEMENTS.md) for current validation selections.

Previously, `Speak` concatenated `SPVA_Speak` fragments and silently discarded
all `SPVA_Bookmark` fragments. It now collects leading bookmark names separately
from speech. Each name is copied from exactly `pTextStart[0..ulTextLen)`; input
need not be NUL-terminated. Names never enter the tokenizer. See Microsoft's
[SPVTEXTFRAG definition](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ms720504(v=vs.85)).

One or more bookmarks before the first nonempty `SPVA_Speak` fragment are
supported, including `Start` followed by `OUTLOUD:17:3`. Empty speech fragments
and ignored metadata do not end the leading region. Duplicate names and Unicode
are preserved in received order. Plain text produces no bookmark events.
Identifiers such as `OUTLOUD:<session>:<chunk>` are opaque; there is no Out Loud
identifier parser in the engine.

## Event contract

The Speak caller checks `GetEventInterest` and queues each requested bookmark
through `ISpTTSEngineSite::AddEvents` before synthesis/audio delivery:

| SPEVENT field | Value |
| --- | --- |
| `eEventId` | `SPEI_TTS_BOOKMARK` |
| `elParamType` | `SPET_LPARAM_IS_STRING` |
| `ulStreamNum` | Initialized to zero; SAPI supplies the stream number |
| `ullAudioStreamOffset` | 0 bytes, relative to audio produced by this Speak |
| `wParam` | Standard `_wtol` conversion of the name; zero for Out Loud identifiers |
| `lParam` | Pointer to the exact owned, NUL-terminated UTF-16 name |

These fields and event interest follow the
[TTS Engine Vendor Porting Guide](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ms717037(v=vs.85)).
Offset zero is a PCM sample boundary at the utterance start, not a character
position, sample count, wall-clock time or a cumulative offset across requests.
The bookmark bit alone is tested; reserved bits included by `SPFEI` do not imply
bookmark interest. If the site has no bookmark interest, speech still proceeds.
Events precede their associated audio as required by
[ISpTTSEngineSite::Write](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ms719570(v=vs.85)).
A bookmark-only request queues interested events without generating PCM; actual
event playback/notification is managed by SAPI.

## Ownership and stop/error handling

`AddEvents` borrows the supplied event/string during the call; SAPI copies the
payload. This is verified by Microsoft's
[samplettsengine bookmark implementation](https://github.com/microsoft/Windows-classic-samples/blob/main/Samples/Win7Samples/winui/speech/engines/samplettsengine/samplettsengine/ttsengobj.cpp),
which terminates a counted name, calls `AddEvents`, then frees its input string.
Pocket uses request-owned `std::wstring` storage, stable throughout each call
and freed by RAII on every exit. No fragment pointer is retained after Speak;
no ownership is transferred and the adapter never frees SAPI's copy.

An application retrieving string events releases its own received payload with
`CoTaskMemFree`/`SpClearEvent`, as documented by
[SPEVENTLPARAMTYPE](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ms717255(v=vs.85)).
SDK 10.0.26100.0 `sphelper.h` also implements separate CoTaskMem-allocated copies
in `CSpEvent::CopyTo` and corresponding cleanup in `SpClearEvent`.

All fragments are validated before event/audio output. Null nonempty names
return `E_POINTER`; embedded NULs return `E_INVALIDARG`, since a NUL-terminated
event cannot preserve them. Bookmarks after nonempty spoken text are ignored as
unsupported metadata: their names are neither spoken nor emitted at an invented
offset, and the speech continues. Correct mid-speech offsets require text/audio
alignment and remain intentionally unsupported.

Site calls remain on the Speak caller thread. Existing action polling runs
before each event and after event forwarding. An observed abort suppresses later
events/audio and returns `S_OK`; already queued events cannot be recalled by the
engine. Failed interest/event calls propagate their HRESULT before native work.
Existing native cancellation and partial Writes are unchanged.

## Validation

The existing registration-free checker tests plain text, the exact
`OUTLOUD_TEST_123` / `Hello traveler.` request, multiple ordered/duplicate/Unicode
names, numeric wParam, copied lifetime after Speak, offset zero before Write,
event interest, bookmark-only requests, ignored non-leading/rejected malformed names,
event/query/Write errors, entry/event/partial-Write aborts and same-engine
recovery. Canonical synthesis and all existing cancellation checks remain.
No new CTest or performance test is added. Results are recorded in
`generated/perf/2o-sapi-check.json` (ignored).

## Zero-PCM compatibility bugfix

Step 2O's whole-request `E_NOTIMPL` for a non-leading bookmark was a regression
from the previous metadata policy. Through real Windows `ISpVoice`,
`<bookmark mark="Start"/>Hello traveler.<bookmark mark="End"/>` returned
`0x80004001` and added zero PCM bytes with the preserved Step 2O DLL. The fix
skips unsupported non-leading bookmarks and retains every spoken fragment and
leading event. No model, format, scheduling, event mask or cancellation change
is involved. This implements an additive bookmark subset; arbitrary mid-speech
event timing is still unsupported.

SAPI XML may contain bookmark fragments anywhere in the chain; their action is
metadata, not a directive to reject speech. See
[SPVACTIONS](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ms717279(v=vs.85)).
The old fake host missed this because its plain/canonical chains had only
speech fragments, and its non-leading case explicitly expected whole-request
failure. That expectation is replaced with stronger byte-for-byte PCM and
leading-event assertions. The checker now additionally uses real Windows SAPI
parsing/site/queued-event delivery, an in-memory token, and an in-memory PCM
stream, without registering Pocket or opening its registered token.

`GetEventInterest` has separate HRESULT and mask outputs. Absence of the bookmark
bit is normal, including zero and reserved-only masks. Plain speech never calls
either event method, even if those methods would fail. `FAILED(hr)` remains the
event error check; success statuses such as `S_FALSE` are not errors under
[COM HRESULT rules](https://learn.microsoft.com/en-us/windows/win32/learnwin32/error-handling-in-com).
`S_FALSE` coverage is defensive, not a claim that Windows SAPI's interest method
was observed returning it. The real site returned `S_OK` with
`0x000000024000ffff`; event interest was not the reproduced cause. Genuine
interest/event failures still propagate, and existing failure tests remain.

Bare plain text and leading-only requests already produced PCM with both the
original workspace DLL and preserved signed Step 2O DLL in isolated real-SAPI
tests (direct factory and normal COM activation via a private manifest). No
automatic trailing bookmark is assumed: the original WoW/PowerShell input chain
was not captured. The reproduced placement regression is fixed, but attributing
the original unwrapped-plain-text silence to it requires live confirmation.
The diagnostic Gnome playback heard during this investigation came from the
preserved Step 2O DLL through Windows SAPI, not from a deployment change.
Bugfix checker evidence is recorded separately in
`generated/perf/2o-bugfix-sapi-check.json` (ignored).

Normal COM activation via private, registration-free manifests also verified
the same XML request against the preserved deployment binaries and the fixed
workspace DLL, using the registered token read-only and unchanged deployed assets:

| DLL | HRESULT | PCM bytes added for Start / Hello traveler. / End |
| --- | --- | ---: |
| Preserved pre-Step-2O | `S_OK` | 72960 |
| Preserved Step 2O | `E_NOTIMPL` | 0 |
| Fixed workspace | `S_OK` | 72960 |

Logs are `generated/perf/2o-bugfix-{pre2o,before,after}-activation.txt`
(ignored). No deployment binary or registered value was replaced.
The final full Release x64 build and checker passed, including real-SAPI
canonical equality, all original abort/recovery cases and retained leading
bookmarks. All 12 existing CTests passed (28.87 seconds) and
`git diff --check` passed. DLL and checker PE import lists are unchanged from
Step 2O; no new runtime dependency is introduced. No production logging remains.

Original Step 2O Release x64 DLL/checker and the full Release build passed. The registration-free
checker passed all bookmark and existing abort/recovery checks, with exact
canonical PCM16: 170 frames, 326400 samples, 652800 bytes and 5270 partial Writes.
All 12 existing CTests passed (27.40 seconds); `git diff --check` passed.
PE inspection confirms x64 and no new redistributable/third-party runtime:
`_wtol` adds only an import of Windows' existing UCRT conversion API set,
`api-ms-win-crt-convert-l1-1-0.dll`. The four VC runtime DLLs remain unchanged.

Manual deployment and live Windows/WoW bookmark verification remain with the
user. This step performs no signing, registration, registry mutation, deployment
copy, WoW process action, staging, commit or push.
