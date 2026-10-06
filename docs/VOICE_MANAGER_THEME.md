# Step 3D.2: WPF design and Windows themes

The [Step 3D.3 import workflow](VOICE_MANAGER_IMPORT.md) reuses these resources
for its confirmation dialog, including live theme and native caption updates.
The [Step 3D.3.1 replacement dialog and severity notifications](VOICE_MANAGER_REPLACE.md)
reuse the same light/dark/high-contrast palettes and theme service.

This step changes presentation only. The WPF -> ViewModel -> service -> native
bridge -> backend architecture, native ABI, result/status labels, library/SAPI
scope and validation remain unchanged. The app still targets net10.0-windows,
x64, asInvoker. No external UI framework/package is added.

## Appearance and layout

The window uses Segoe UI, a restrained background/surface palette, rounded cards,
clearer spacing and typography, an accent Refresh button, a managed-voice count
and a subtle library-root footer. The table retains Name / Library Status / SAPI
Status, sorting, keyboard selection and virtualization. Native status text stays
verbatim inside colored pills; color is supplementary, with unknown statuses
neutral. Shared styles provide hover, selection and keyboard-focus treatment.

The details card groups identity, health/registration and format/source. Values
remain selectable/copyable; IDs use Consolas and long values wrap. The panels
use proportional widths, minimum sizes and a keyboard/mouse GridSplitter. Table
overflow scrolls horizontally, details scroll vertically. Loading has a small
indeterminate indicator; empty, no-selection, error and partial-SAPI-error states
have intentional layouts. A refresh failure still retains the last good list.

## Theme detection and lifetime

`WindowsThemeService` reads **AppsUseLightTheme** with a read-only OpenSubKey
under `HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize`.
DWORD 0 selects Dark; 1 selects Light. Missing, unexpected or inaccessible
values fall back to Light. It reads the app preference, not SystemUsesLightTheme
or the SAPI token registry. This is the setting documented by Microsoft's
[Windows settings reference](https://learn.microsoft.com/en-us/windows/apps/develop/settings/settings-common).
There are no registry writes and no persisted theme overrides.

When Windows high contrast is active, SystemParameters.HighContrast takes
priority. HighContrast.xaml uses dynamic Windows system colors (including
highlight/highlight-text colors for selection). Exact status labels remain
visible without relying on the green/amber/red treatment.

Live changes are supported through
[SystemEvents.UserPreferenceChanged](https://learn.microsoft.com/en-us/dotnet/api/microsoft.win32.systemevents.userpreferencechanged).
The callback coalesces notifications and schedules a re-read on the WPF
dispatcher; it never touches a view from the notification thread. Only the
palette dictionary is replaced; common styles, view instances, list and
selection remain. Every palette-sensitive brush uses DynamicResource. Event
subscriptions are removed at application exit, queued callbacks check disposal,
and dispatcher shutdown is handled without blocking/spinning/polling.

Native caption/buttons are retained. On Windows 11 the service applies
[DWMWA_USE_IMMERSIVE_DARK_MODE](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute)
when the HWND is ready and on subsequent theme changes. The small UI-only DWM
call loads dwmapi.dll from System32; it does not alter the backend ABI. Older
Windows retains its normal system caption, and an unsupported caption update
leaves the caption under Windows control.

## Changed files

- `ui/OutLoud.VoiceManager/Views/MainWindow.xaml`: layout and themed controls.
- `ui/OutLoud.VoiceManager/App.xaml` / `App.xaml.cs`: common resources, initial
  palette, theme service startup/exit and caption hookup.
- `ui/OutLoud.VoiceManager/Themes/Light.xaml`, `Dark.xaml`, `HighContrast.xaml`:
  matching brush keys for the three palettes.
- `ui/OutLoud.VoiceManager/Themes/Common.xaml`: reusable cards, button, table,
  pills, text/value, tooltip and scrollbar styles/templates.
- `ui/OutLoud.VoiceManager/Themes/ThemePalette.cs`: palette replacement and
  testable app-setting interpretation.
- `ui/OutLoud.VoiceManager/Themes/WindowsThemeService.cs`: read-only preference
  detection, dispatcher-safe live notifications, disposal and caption styling.
- `ui/OutLoud.VoiceManager/Themes/StatusToneConverter.cs`: color categories only;
  no native data transformations.
- `ui/validation/RuntimeValidation.cs`: targeted checks on actual compiled BAML
  with injected themes/fake data; real hidden content is measured directly.
- `docs/VOICE_MANAGER_WPF.md`: links this design/theme step and current resource
  initialization behavior.

No engine/native/bridge, Models, ViewModels, backend-service, manifest or project
configuration changes are needed for this visual step.

## Automated validation

From the repository root:

```powershell
$env:DOTNET_GENERATE_ASPNET_CERTIFICATE = 'false'
dotnet build ui/OutLoud.VoiceManager.sln -c Release -p:Platform=x64
dotnet run --project ui/OutLoud.VoiceManager.Tests/OutLoud.VoiceManager.Tests.csproj -c Release -p:Platform=x64 --no-build
.\ui\validation\validate-with-runtime.ps1
ctest --test-dir build -C Release --output-on-failure
git diff --check
```

Release solution build passed with **zero warnings/errors**; original **27**
fake-service frontend checks and **17/17** native CTests passed (50.64 s).
The existing small view harness now checks setting interpretation, palette-key
parity, light/dark/high-contrast resource replacement, unchanged common styles,
an injected theme-service refresh, status color categories, and ten text/brush
pairs per light/dark palette with contrast ratio at least **4.5:1**. It exercises
real BAML/control templates/bindings with fake empty/selected/refreshed/loading/
error data, preserved selection, and 860x560 / 1120x780 / 1440x900 content layouts.
These targeted checks passed with no binding errors. No new native CTest or
large/slow suite was added.

Validation initializes App.xaml resources without calling App.Run/OnStartup,
uses injected theme values and fake voice data, and never starts production
enumeration or shows a window. Optional ignored PNG captures of the light/dark
fake views were generated and inspected under `generated/ui-design/`.

## Manual validation

Launch in a normal, non-elevated PowerShell from the repository root:

```powershell
.\ui\OutLoud.VoiceManager\bin\Release\net10.0-windows\OutLoud.VoiceManager.exe
```

1. Confirm the title/header, voice count, installed Gnome/Undead entries and
   readable Library/SAPI status text and pills. Select each voice; check details
   and long identifiers, plus text selection/copy.
2. In Windows Settings -> Personalization -> Colors, set the **app** mode to
   Light, then Dark. With a custom mode, the app mode can differ from Windows
   shell mode. Confirm surfaces, text, pills, button and scrollbars update while
   the app stays open; selection and data should remain. Verify the caption on
   Windows 11. The helper never changes your Windows preference itself.
3. Refresh in each mode. Confirm the loading indicator, one active refresh,
   updated count/statuses, and retained selection for a still-present ID.
4. Resize to the minimum window size and a large/maximized size; drag or use
   arrow keys on the panel divider. Verify table columns remain accessible and
   long details wrap/scroll. Check keyboard Tab/arrow navigation and Alt+R.
5. If you use Windows contrast themes, optionally verify their system colors
   and selected-row/focus visibility. Return Windows Settings to your preference.

Do not alter the real library/registry to manufacture empty/error test cases.
OS notification delivery, caption behavior, production P/Invoke and visible
interactive/DPI/resize behavior remain manual/unverified. Automated renders
demonstrate palette/layout behavior with fake data, not the installed voices.

No live app launch, production library/token mutation, registration, signing,
deployment, elevation, SDK installation or certificate operation was performed
in this visual-design step. Nothing was staged, committed or pushed.
