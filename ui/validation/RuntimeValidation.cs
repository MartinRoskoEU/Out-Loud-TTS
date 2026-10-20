// Supplementary SDK-built BAML/view check with a fake service. It never calls
// App.OnStartup, shows a window or constructs the production native service.
using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using OutLoud.VoiceManager.Models;
using OutLoud.VoiceManager.Services;
using OutLoud.VoiceManager.ViewModels;
using OutLoud.VoiceManager.Views;
using OutLoud.VoiceManager.Themes;

namespace OutLoud.VoiceManager.Validation;

internal static class RuntimeValidation
{
    private sealed class BindingErrors : TraceListener
    {
        public int Errors;
        public override void Write(string? message) { }
        public override void WriteLine(string? message) { }
        public override void TraceEvent(TraceEventCache? cache, string source, TraceEventType type, int id, string? message)
        {
            if (type == TraceEventType.Error || type == TraceEventType.Critical) { ++Errors; Console.Error.WriteLine(message); }
        }
        public override void TraceEvent(TraceEventCache? cache, string source, TraceEventType type, int id, string? format, params object?[]? args)
            => TraceEvent(cache, source, type, id, args == null ? format : string.Format(format ?? "", args));
    }
    private sealed class FakeService : IVoiceManagerService
    {
        public VoiceSnapshot Next = new("fake-root", Array.Empty<VoiceRecord>());
        public BackendError? Error;
        public System.Threading.Tasks.Task<VoiceSnapshot>? Pending;
        public System.Threading.Tasks.Task<VoiceSnapshot> ReadAsync() => Pending ?? (Error == null ?
            System.Threading.Tasks.Task.FromResult(Next) :
            System.Threading.Tasks.Task.FromException<VoiceSnapshot>(new BackendException(Error)));
    }
    private sealed class FakeImport : IVoiceImportService, IImportDialogService
    {
        public System.Threading.Tasks.Task<bool> ConfirmRemovalAsync(VoiceRecord target) => System.Threading.Tasks.Task.FromResult(true);
        public VoiceImportResult Next = new(ImportPhase.Cancelled);
        public System.Threading.Tasks.Task<VoiceImportRequest?> ChooseAsync() => System.Threading.Tasks.Task.FromResult<VoiceImportRequest?>(new("C:\\fake\\new.safetensors", "No collision"));
        public System.Threading.Tasks.Task<VoiceRecord?> ConfirmReplacementAsync(VoiceImportRequest request, System.Collections.Generic.IReadOnlyList<VoiceRecord> candidates)
            => throw new Exception("Unexpected name collision in visual test.");
        public System.Threading.Tasks.Task<VoiceImportResult> ImportAsync(VoiceImportRequest request, IProgress<ImportPhase> progress)
            => System.Threading.Tasks.Task.FromResult(Next);
    }
    private sealed class FakePlayback : IVoiceTestBackend
    {
        public string Name => "Current SAPI Engine / Baseline (fake)";
        public System.Threading.Tasks.TaskCompletionSource<VoiceTestOutcome>? Pending;
        public IProgress<VoiceTestState>? Progress;
        public System.Threading.CancellationToken Stop;
        public System.Threading.Tasks.Task<VoiceTestOutcome> RunAsync(VoiceTestRequest request, IProgress<VoiceTestState> progress, System.Threading.CancellationToken cancellation)
        { Progress = progress; Stop = cancellation; return Pending?.Task ?? System.Threading.Tasks.Task.FromResult(new VoiceTestOutcome(false)); }
    }
    private sealed class FakeExport : IVoiceExportService, IVoiceExportDialog
    {
        public System.Threading.Tasks.Task<string> InspectDestinationAsync(string path) => throw new Exception("No native export inspection in view validation.");
        public System.Threading.Tasks.Task<VoiceExportRequest?> ChooseAsync(VoiceRecord voice) => System.Threading.Tasks.Task.FromResult<VoiceExportRequest?>(new("C:\\fake\\hlas-ž.olv", "absent"));
        public System.Threading.Tasks.Task ExportAsync(VoiceRecord voice, VoiceExportRequest request) => System.Threading.Tasks.Task.CompletedTask;
    }
    private static void Layout(Window window, int width = 1120, int height = 780)
    {
        window.Width = width; window.Height = height;
        // A never-shown Window has no HWND/client layout. Measure the actual
        // content directly so row/control templates and bindings are exercised.
        var root = (FrameworkElement)window.Content;
        root.Measure(new Size(width, height)); root.Arrange(new Rect(0, 0, width, height)); root.UpdateLayout();
        Dispatcher.CurrentDispatcher.Invoke(() => { }, DispatcherPriority.SystemIdle);
    }
    private static void Capture(Window window, string path)
    {
        var root = (FrameworkElement)window.Content;
        var bitmap = new RenderTargetBitmap((int)window.Width, (int)window.Height, 96, 96, PixelFormats.Pbgra32);
        var canvas = new DrawingVisual();
        using (var drawing = canvas.RenderOpen()) drawing.DrawRectangle(window.Background, null, new Rect(0, 0, window.Width, window.Height));
        bitmap.Render(canvas);
        bitmap.Render(root);
        var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(bitmap));
        using var output = File.Create(path); encoder.Save(output);
    }
    private static double Luminance(Color color)
    {
        static double Channel(byte value) { double c = value / 255.0; return c <= 0.04045 ? c / 12.92 : Math.Pow((c + 0.055) / 1.055, 2.4); }
        return 0.2126 * Channel(color.R) + 0.7152 * Channel(color.G) + 0.0722 * Channel(color.B);
    }
    private static void CheckContrast(ResourceDictionary palette)
    {
        foreach (var pair in new[] { ("TextBrush", "CanvasBrush"), ("TextBrush", "SurfaceBrush"), ("MutedTextBrush", "CanvasBrush"),
            ("MutedTextBrush", "SurfaceMutedBrush"), ("TextBrush", "HoverBrush"), ("SelectionTextBrush", "SelectionBrush"), ("AccentTextBrush", "AccentBrush"),
            ("NeutralTextBrush", "NeutralBrush"), ("SuccessTextBrush", "SuccessBrush"), ("AttentionTextBrush", "AttentionBrush"), ("ErrorTextBrush", "ErrorBrush") })
        {
            double a = Luminance(((SolidColorBrush)palette[pair.Item1]).Color), b = Luminance(((SolidColorBrush)palette[pair.Item2]).Color);
            if ((Math.Max(a,b) + 0.05) / (Math.Min(a,b) + 0.05) < 4.5) throw new Exception($"Insufficient palette text contrast: {pair}");
        }
    }
    [STAThread]
    public static int Main(string[] args)
    {
        try
        {
            var errors = new BindingErrors(); PresentationTraceSources.DataBindingSource.Listeners.Add(errors);
            PresentationTraceSources.DataBindingSource.Switch.Level = SourceLevels.Error;
            // Initialize resource dictionaries only. Never Run/OnStartup/native service.
            var app = new App(); app.InitializeComponent();
            var palette = new ThemePalette(app.Resources);
            if (args.Length == 2 && args[0] == "--dsm") {
                // Focused real-view check using a framed diagnosis from the native
                // importer. No App startup, real window, elevation or native reads.
                var result = ImportProtocol.DecodeResult(File.ReadAllBytes(args[1]));
                const string expectedMessage = "Detected voice format: Kyutai TTS 1.6B (DSM)\nSupported voice format: Pocket TTS 6L\n\n" +
                    "This voice profile uses a different architecture and cannot be imported into Out Loud.";
                var importer = new FakeImport { Next = result };
                var dsmModel = new MainViewModel(new FakeService(), importer, importer);
                var view = new MainWindow { DataContext = dsmModel };
                foreach (AppTheme theme in new[] { AppTheme.Light, AppTheme.Dark, AppTheme.HighContrast }) {
                    palette.Apply(theme); dsmModel.ImportAsync().GetAwaiter().GetResult(); Layout(view, 860, 560);
                    var title = (TextBlock)view.FindName("NotificationTitle");
                    var message = (TextBlock)view.FindName("NotificationMessage");
                    if (title.Text != "Incompatible Voice Profile" || message.Text != expectedMessage ||
                        ((Border)view.FindName("OperationNotification")).Visibility != Visibility.Visible ||
                        !dsmModel.ImportDiagnostic.Contains("incompatible_dsm_profile") || errors.Errors != 0)
                        throw new Exception("DSM notification title, message, diagnostics or view binding failed.");
                    dsmModel.DismissNotificationCommand.Execute(null); Layout(view);
                    if (((Border)view.FindName("OperationNotification")).Visibility != Visibility.Collapsed)
                        throw new Exception("DSM notification did not dismiss.");
                }
                if (PresentationSource.FromVisual(view) != null) throw new Exception("DSM validation created a real window.");
                view.Close(); Console.WriteLine("PASS: native DSM diagnosis displays the exact title/message in the compiled WPF view in all three themes.");
                return 0;
            }
            if (ThemePalette.FromWindowsSetting(0, false) != AppTheme.Dark || ThemePalette.FromWindowsSetting(1, false) != AppTheme.Light ||
                ThemePalette.FromWindowsSetting(null, false) != AppTheme.Light || ThemePalette.FromWindowsSetting("0", false) != AppTheme.Light ||
                ThemePalette.FromWindowsSetting(0, true) != AppTheme.HighContrast) throw new Exception("Theme-setting interpretation failed.");
            var tones = new StatusToneConverter();
            if ((StatusTone)tones.Convert("valid", typeof(StatusTone), "library", CultureInfo.InvariantCulture) != StatusTone.Success ||
                (StatusTone)tones.Convert("stale_name", typeof(StatusTone), "sapi", CultureInfo.InvariantCulture) != StatusTone.Attention ||
                (StatusTone)tones.Convert("wrong_path", typeof(StatusTone), "sapi", CultureInfo.InvariantCulture) != StatusTone.Error ||
                (StatusTone)tones.Convert("future_status", typeof(StatusTone), "sapi", CultureInfo.InvariantCulture) != StatusTone.Neutral)
                throw new Exception("Status presentation mapping failed.");
            // The actual production constructor executes the SDK-generated
            // InitializeComponent and loads its compiled MainWindow BAML.
            // App.OnStartup and the production native service are not invoked.
            var window = new MainWindow();
            var voiceGrid = (DataGrid)window.FindName("VoiceList");
            if (voiceGrid.SelectionUnit != DataGridSelectionUnit.FullRow || voiceGrid.SelectionMode != DataGridSelectionMode.Single)
                throw new Exception("Voice grid does not use single full-row selection.");
            var cellStyle = (Style)app.Resources[typeof(DataGridCell)];
            if (cellStyle.Triggers.OfType<Trigger>().Any(trigger => trigger.Property == UIElement.IsKeyboardFocusedProperty))
                throw new Exception("Individual cell focus rectangle remains.");
            var importDialog = new ImportVoiceDialog("C:\\fake\\undead_female.safetensors");
            Layout(importDialog, 530, 480);
            var nameInput = (TextBox)importDialog.FindName("NameInput");
            var confirm = (Button)importDialog.FindName("ConfirmButton");
            nameInput.Text = " "; Layout(importDialog, 530, 480);
            if (confirm.IsEnabled) throw new Exception("Empty import name can be confirmed.");
            nameInput.Text = "Hlas ž 🎵"; Layout(importDialog, 530, 480);
            if (!confirm.IsEnabled || importDialog.Model.Confirm().DisplayName != "Hlas ž 🎵") throw new Exception("Import dialog name editing failed.");
            importDialog.Close();
            var olvDialog = new ImportVoiceDialog("C:\\fake\\hlas-ž.olv");
            foreach (AppTheme theme in new[] { AppTheme.Light, AppTheme.Dark, AppTheme.HighContrast }) {
                palette.Apply(theme); Layout(olvDialog, 530, 480);
                if (olvDialog.Model.SourceFormat != "Out Loud Voice (.olv)" || !((Button)olvDialog.FindName("ConfirmButton")).IsEnabled)
                    throw new Exception("OLV import format/confirmation binding failed.");
            }
            olvDialog.Close();
            var sharedStyles = app.Resources.MergedDictionaries[1];
            string[]? keys = null;
            foreach (AppTheme theme in new[] { AppTheme.Light, AppTheme.Dark, AppTheme.HighContrast, AppTheme.Light })
            {
                palette.Apply(theme); Layout(window);
                var currentPalette = app.Resources.MergedDictionaries[0];
                var currentKeys = currentPalette.Keys.Cast<string>().OrderBy(key => key).ToArray();
                if (keys != null && !keys.SequenceEqual(currentKeys)) throw new Exception("Palette keys differ.");
                keys = currentKeys;
                if (!ReferenceEquals(window.Background, app.Resources["CanvasBrush"]) || !ReferenceEquals(sharedStyles, app.Resources.MergedDictionaries[1]))
                    throw new Exception("Live dynamic resource replacement failed.");
                if (theme != AppTheme.HighContrast) CheckContrast(currentPalette);
            }
            AppTheme fakeTheme = AppTheme.Dark;
            using (var themeService = new WindowsThemeService(palette, window.Dispatcher, () => fakeTheme))
            {
                fakeTheme = AppTheme.Light; themeService.Refresh();
                if (palette.Current != AppTheme.Light) throw new Exception("Injected theme service refresh failed.");
            }
            var service = new FakeService(); var model = new MainViewModel(service); window.DataContext = model;
            model.RefreshAsync().GetAwaiter().GetResult(); Layout(window);
            if (window.Title != "Out Loud" || !model.IsEmpty) throw new Exception("Empty view/title validation failed.");
            var voice = new VoiceRecord("0123456789abcdef0123456789abcdef", "Gnome Male (fake)", "valid", "", "correct", "Library identity/path/name checked; shared runtime configuration was not supplied.",
                "pocket-english-model-compatibility-identifier-with-a-long-value", "olv", 1);
            var second = voice with { LibraryId = "other-fake-id", DisplayName = "Undead Male (fake)", SapiStatus = "stale_name" };
            service.Next = new VoiceSnapshot("fake-root", new[] { voice, second }); model.RefreshAsync().GetAwaiter().GetResult();
            model.SelectedVoice = voice; Layout(window);
            var renamed = voice with { DisplayName = "New name" };
            service.Next = new VoiceSnapshot("fake-root", new[] { renamed, second }); model.RefreshAsync().GetAwaiter().GetResult(); Layout(window);
            if (!ReferenceEquals(model.SelectedVoice, renamed)) throw new Exception("View refresh selection failed.");
            foreach (AppTheme theme in new[] { AppTheme.Light, AppTheme.Dark })
            {
                palette.Apply(theme); Layout(window);
                if (args.Length == 1) { Directory.CreateDirectory(args[0]); Capture(window, Path.Combine(args[0], $"voices-{theme}.png")); }
                Layout(window, 860, 560); Layout(window, 1440, 900);
                var list = (DataGrid)window.FindName("VoiceList");
                if (!ReferenceEquals(list.SelectedItem, renamed)) throw new Exception("Selection changed during theme/resize.");
            }
            service.Error = new BackendError("library", "LibraryAccessDenied", "Fake access denied.", SystemError: 5);
            model.RefreshAsync().GetAwaiter().GetResult(); Layout(window);
            if (!model.HasError || errors.Errors != 0) throw new Exception("Error view/binding validation failed.");
            // Render loading/empty/error controls in both palettes as well.
            foreach (AppTheme theme in new[] { AppTheme.Light, AppTheme.Dark })
            {
                palette.Apply(theme); Layout(window, 860, 560);
                service.Error = null;
                var completion = new System.Threading.Tasks.TaskCompletionSource<VoiceSnapshot>();
                service.Pending = completion.Task;
                var refresh = model.RefreshAsync(); Layout(window);
                if (!model.IsBusy || model.RefreshCommand.CanExecute(null)) throw new Exception("Loading UI state failed.");
                completion.SetResult(new VoiceSnapshot("fake-root", Array.Empty<VoiceRecord>()));
                Dispatcher.CurrentDispatcher.Invoke(() => { }, DispatcherPriority.SystemIdle);
                if (!refresh.IsCompleted) throw new Exception("Loading completion did not reach the UI context.");
                refresh.GetAwaiter().GetResult(); service.Pending = null; Layout(window);
                if (!model.IsEmpty) throw new Exception("Themed empty UI state failed.");
                service.Error = new BackendError("library", "library_access_denied", "Fake read denial.");
                model.RefreshAsync().GetAwaiter().GetResult(); Layout(window);
                if (!model.HasError) throw new Exception("Themed error UI state failed.");
            }
            if (errors.Errors != 0) throw new Exception("Themed control binding error.");
            if (PresentationSource.FromVisual(window) != null) throw new Exception("Validation unexpectedly created a real window.");
            var replacementModel = new ReplacementDialogModel(new("C:\\fake\\new.safetensors", "Same name"), new[] {
                voice with { OlvSha256 = new string('a', 64) }, second with { OlvSha256 = new string('b', 64) } });
            var replaceDialog = new ReplaceVoiceDialog(replacementModel); Layout(replaceDialog, 570, 510);
            var replaceButton = (Button)replaceDialog.FindName("ReplaceButton");
            if (replaceButton.IsEnabled || replacementModel.SelectedTarget != null) throw new Exception("Ambiguous replace auto-selected a target.");
            ((ListBox)replaceDialog.FindName("Targets")).SelectedIndex = 1; Layout(replaceDialog, 570, 510);
            if (!replaceButton.IsEnabled || replacementModel.SelectedTarget?.LibraryId != second.LibraryId) throw new Exception("Explicit replace target selection failed.");
            replaceDialog.Close();
            var notifications = new FakeImport(); var notificationModel = new MainViewModel(new FakeService(), notifications, notifications);
            window.DataContext = notificationModel;
            foreach (AppTheme theme in new[] { AppTheme.Light, AppTheme.Dark, AppTheme.HighContrast }) {
                palette.Apply(theme);
                foreach (var test in new[] {
                    (new VoiceImportResult(ImportPhase.Succeeded, voice.LibraryId), "Success", false),
                    (new VoiceImportResult(ImportPhase.Partial, voice.LibraryId, new("sapi", "registry_io", "Fake partial")), "Attention", true),
                    (new VoiceImportResult(ImportPhase.Failed, Error: new("library", "duplicate_content", "Fake duplicate")), "Error", true),
                    (new VoiceImportResult(ImportPhase.Cancelled), "Neutral", false) }) {
                    notifications.Next = test.Item1; notificationModel.ImportAsync().GetAwaiter().GetResult(); Layout(window, 860, 560);
                    var banner = (Border)window.FindName("OperationNotification"); var message = (TextBlock)window.FindName("NotificationMessage");
                    var diagnostic = (Expander)window.FindName("OperationDiagnostic");
                    if (banner.Visibility != Visibility.Visible || !ReferenceEquals(banner.Background, app.Resources[test.Item2 + "Brush"]) ||
                        !ReferenceEquals(banner.BorderBrush, app.Resources[test.Item2 + "TextBrush"]) || !ReferenceEquals(message.Foreground, banner.BorderBrush) ||
                        (diagnostic.Visibility == Visibility.Visible) != test.Item3) throw new Exception("Notification severity/contrast/diagnostic styling failed.");
                    if (args.Length == 1) Capture(window, Path.Combine(args[0], $"notification-{theme}-{test.Item2}.png"));
                    notificationModel.DismissNotificationCommand.Execute(null); Layout(window);
                    if (banner.Visibility != Visibility.Collapsed || diagnostic.Visibility != Visibility.Collapsed) throw new Exception("Notification dismissal left diagnostic visible.");
                }
            }
            var removeDialog = new RemoveVoiceDialog(new RemovalDialogModel(voice));
            var removeConfirm = (Button)removeDialog.FindName("ConfirmRemoveButton");
            if (!((Button)removeDialog.FindName("CancelRemoveButton")).IsDefault || removeConfirm.IsDefault)
                throw new Exception("Destructive removal must require explicit confirmation, with Cancel as default.");
            var removalService = new FakeService(); var removal = new FakeImport();
            var removalModel = new MainViewModel(removalService, removal, removal); window.DataContext = removalModel;
            var removeAction = (Button)window.FindName("RemoveVoiceButton");
            Layout(window); if (removeAction.IsEnabled) throw new Exception("Remove enabled without a selected voice.");
            var removalPeer = voice with { LibraryId = "fedcba9876543210fedcba9876543210", DisplayName = "Remaining voice" };
            foreach (AppTheme theme in new[] { AppTheme.Light, AppTheme.Dark, AppTheme.HighContrast }) {
                palette.Apply(theme); Layout(removeDialog, 540, 460);
                removalService.Next = new("fake", new[] { voice, removalPeer }); removalModel.RefreshAsync().GetAwaiter().GetResult(); removalModel.SelectedVoice = voice;
                Layout(window, 860, 560);
                if (!removeAction.IsEnabled || !ReferenceEquals(removeConfirm.Background, app.Resources["ErrorBrush"]) ||
                    !ReferenceEquals(removeConfirm.Foreground, app.Resources["ErrorTextBrush"]) ||
                    !ReferenceEquals(removeAction.Foreground, app.Resources["ErrorTextBrush"])) throw new Exception("Themed destructive removal styling or selection enablement failed.");
                removal.Next = new(ImportPhase.Succeeded, voice.LibraryId, Operation: VoiceOperation.Remove);
                removalService.Next = new("fake", new[] { removalPeer }); removalModel.RemoveAsync().GetAwaiter().GetResult(); Layout(window);
                if (removalModel.Voices.Count != 1 || removalModel.SelectedVoice?.LibraryId != removalPeer.LibraryId ||
                    removalModel.ImportStateText != "Voice removed from Out Loud and Windows speech.") throw new Exception("Removal view refresh/selection/result failed.");
                removalModel.DismissNotificationCommand.Execute(null); Layout(window);
                if (((Border)window.FindName("OperationNotification")).Visibility != Visibility.Collapsed) throw new Exception("Removal result dismissal failed.");
                removal.Next = new(ImportPhase.Succeeded, removalPeer.LibraryId, Operation: VoiceOperation.Remove);
                removalService.Next = new("fake", Array.Empty<VoiceRecord>()); removalModel.RemoveAsync().GetAwaiter().GetResult(); Layout(window);
                if (!removalModel.IsEmpty || removeAction.IsEnabled) throw new Exception("Last-row removal did not restore empty/disabled state.");
            }
            removeDialog.Close();
            var exporting = new FakeExport(); var exportService = new FakeService();
            var exportModel = new MainViewModel(exportService, exporter: exporting, exportDialog: exporting); window.DataContext = exportModel;
            var exportButton = (Button)window.FindName("ExportVoiceButton");
            Layout(window); if (exportButton.IsEnabled) throw new Exception("Export enabled without selection.");
            var exportVoice = voice with { OlvSha256 = new string('a',64), DisplayName = "Hlas ž 🎵" };
            exportService.Next = new("fake", new[] { exportVoice }); exportModel.RefreshAsync().GetAwaiter().GetResult(); exportModel.SelectedVoice = exportVoice;
            foreach (AppTheme theme in new[] { AppTheme.Light, AppTheme.Dark, AppTheme.HighContrast }) {
                palette.Apply(theme); Layout(window, 860, 560); Layout(window, 1440, 900);
                if (!exportButton.IsEnabled || !exportButton.Focusable || !ReferenceEquals(exportButton.Command, exportModel.ExportCommand) ||
                    !ReferenceEquals(exportButton.Foreground, app.Resources["AccentTextBrush"])) throw new Exception("Export selection/keyboard/theme binding failed.");
                exportModel.ExportAsync().GetAwaiter().GetResult(); Layout(window, 860, 560);
                if (!ReferenceEquals(exportModel.SelectedVoice, exportVoice) || exportModel.NotificationSeverity != NotificationSeverity.Success ||
                    !ReferenceEquals(((Border)window.FindName("OperationNotification")).Background, app.Resources["SuccessBrush"])) throw new Exception("Export success/selection/theme failed.");
                if (exportButton.TranslatePoint(new Point(), (FrameworkElement)window.Content).X + exportButton.ActualWidth > window.Width)
                    throw new Exception("Export toolbar exceeds the resized window.");
                exportModel.DismissNotificationCommand.Execute(null); Layout(window);
                if (((Border)window.FindName("OperationNotification")).Visibility != Visibility.Collapsed) throw new Exception("Export notification dismissal failed.");
            }
            var playback = new FakePlayback(); var tester = new VoiceTesterViewModel(playback);
            var testerService = new FakeService { Next = new("C:\\fake-library", new[] { voice, voice with { LibraryId = "fedcba9876543210fedcba9876543210" } }) };
            var testerMain = new MainViewModel(testerService, tester: tester); window.DataContext = testerMain;
            testerMain.RefreshAsync().GetAwaiter().GetResult(); testerMain.SelectedVoice = testerMain.Voices[1]; testerMain.ShowTesterCommand.Execute(null); Layout(window);
            var testerView = (VoiceTesterView)window.FindName("TesterPage");
            var selector = (ComboBox)testerView.FindName("TestVoiceSelector"); var editor = (TextBox)testerView.FindName("TestTextEditor");
            var playButton = (Button)testerView.FindName("TestPlayButton"); var stopButton = (Button)testerView.FindName("TestStopButton");
            if (!ReferenceEquals(selector.SelectedItem, testerMain.Voices[1]) || !editor.AcceptsReturn || editor.MaxLength != 0 || !playButton.IsEnabled || stopButton.IsEnabled)
                throw new Exception("Tester selection/editor/idle controls failed.");
            editor.Text = "Wait, traveler! <plain> & return.\nNext line?"; Layout(window);
            if (tester.Text != editor.Text) throw new Exception("Tester editor did not preserve plain text.");
            foreach (AppTheme theme in new[] { AppTheme.Light, AppTheme.Dark, AppTheme.HighContrast }) {
                palette.Apply(theme); Layout(window, 860, 560); Layout(window, 1440, 900);
                if (!ReferenceEquals(editor.Foreground, app.Resources["TextBrush"]) || !ReferenceEquals(editor.Background, app.Resources["CanvasBrush"]) ||
                    !ReferenceEquals(selector.Foreground, app.Resources["TextBrush"]) || !ReferenceEquals(selector.Background, app.Resources["CanvasBrush"]) ||
                    !ReferenceEquals(selector.SelectedItem, testerMain.Voices[1])) throw new Exception("Tester themed controls/resize/identity failed.");
                playback.Pending = new System.Threading.Tasks.TaskCompletionSource<VoiceTestOutcome>();
                var playing = tester.PlayAsync(); Layout(window, 860, 560);
                if (playButton.IsEnabled || !stopButton.IsEnabled || !editor.IsReadOnly) throw new Exception("Tester busy controls failed.");
                playback.Progress!.Report(VoiceTestState.Speaking); Layout(window);
                var stopping = tester.StopAsync(); Layout(window);
                if (!playback.Stop.IsCancellationRequested || !tester.IsBusy || stopButton.IsEnabled) throw new Exception("Tester stopping controls failed.");
                playback.Pending.SetResult(new(true)); Dispatcher.CurrentDispatcher.Invoke(() => { }, DispatcherPriority.SystemIdle);
                playing.GetAwaiter().GetResult(); stopping.GetAwaiter().GetResult(); Layout(window, 860, 560);
                if (playButton.TranslatePoint(new Point(), (FrameworkElement)window.Content).Y + playButton.ActualHeight > window.Height)
                    throw new Exception("Tester playback controls scroll out of the small window.");
                var banner = (Border)testerView.FindName("TestNotification");
                if (banner.Visibility != Visibility.Visible || !ReferenceEquals(banner.Background, app.Resources["NeutralBrush"]) || !playButton.IsEnabled)
                    throw new Exception("Tester stopped banner/controls failed.");
                playback.Pending = new System.Threading.Tasks.TaskCompletionSource<VoiceTestOutcome>();
                playing = tester.PlayAsync(); playback.Pending.SetException(new VoiceTestException(new("fake_sapi_failure", "Fake unavailable voice", unchecked((int)0x80004005), "completion")));
                Dispatcher.CurrentDispatcher.Invoke(() => { }, DispatcherPriority.SystemIdle); playing.GetAwaiter().GetResult(); Layout(window, 860, 560);
                if (!ReferenceEquals(banner.Background, app.Resources["ErrorBrush"]) || !tester.HasDiagnostic) throw new Exception("Tester error severity failed.");
                if (args.Length == 1) Capture(window, Path.Combine(args[0], $"tester-{theme}.png"));
                tester.DismissCommand.Execute(null); Layout(window);
                if (banner.Visibility != Visibility.Collapsed) throw new Exception("Tester notification dismissal failed.");
            }
            string preservedText = tester.Text; testerMain.ShowLibraryCommand.Execute(null); Layout(window);
            if (!testerMain.IsLibraryPage || testerView.Visibility != Visibility.Collapsed || voiceGrid.Visibility != Visibility.Visible)
                throw new Exception("Tester-to-library navigation failed.");
            testerMain.RefreshAsync().GetAwaiter().GetResult(); testerMain.ShowTesterCommand.Execute(null); Layout(window);
            if (tester.Text != preservedText || !ReferenceEquals(selector.SelectedItem, testerMain.SelectedVoice)) throw new Exception("Tester navigation/refresh preservation failed.");
            if (PresentationSource.FromVisual(window) != null) throw new Exception("Tester validation created a real window.");
            if (errors.Errors != 0) throw new Exception("Notification/dialog binding errors.");
            var idleWindow = new MainWindow { DataContext = new MainViewModel(new FakeService(), tester: new VoiceTesterViewModel(new FakePlayback())) };
            bool idleClosed = false; idleWindow.Closed += (_, _) => idleClosed = true; idleWindow.Close();
            if (!idleClosed) throw new Exception("Idle window close was cancelled or reentered.");
            playback.Pending = new System.Threading.Tasks.TaskCompletionSource<VoiceTestOutcome>();
            var closingTest = tester.PlayAsync(); bool busyClosed = false; window.Closed += (_, _) => busyClosed = true;
            window.Close();
            if (busyClosed || !playback.Stop.IsCancellationRequested) throw new Exception("Busy window did not wait for tester shutdown.");
            playback.Pending.SetResult(new(true)); Dispatcher.CurrentDispatcher.Invoke(() => { }, DispatcherPriority.SystemIdle);
            closingTest.GetAwaiter().GetResult();
            if (!busyClosed) throw new Exception("Busy window did not close after cleanup.");
            Console.WriteLine("PASS: SDK-built WPF library and tester views; navigation, exact-ID selection, multiline text, fake Play/Stop/errors, themes/contrast, severity/dismissal, import/replace/remove, refresh and resize; no window shown or production playback.");
            return 0;
        }
        catch (Exception failure) { Console.Error.WriteLine(failure); return 1; }
    }
}
