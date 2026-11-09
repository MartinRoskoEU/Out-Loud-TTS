using System.Globalization;
using System.Security.Cryptography;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.Windows.Storage.Pickers;
using OutLoud.WinUI.Voices;

namespace OutLoud.WinUI;

public sealed partial class VoiceLibraryView : UserControl
{
    private readonly IVoiceImportService _importer = new ElevatedVoiceImportService();
    private VoiceSnapshot _snapshot = new("", []);
    private BackendError? _refreshError;
    private Window? _window;
    private Task? _operation;
    private ContentDialog? _dialog;
    private bool _busy, _closing, _waitingToClose;
    private string _busyText = "";
    private VoiceRecord? SelectedVoice => VoiceList.SelectedItem as VoiceRecord;
    internal event Action<bool>? BusyChanged;

    public VoiceLibraryView() => InitializeComponent();

    internal void InitializeVoiceLibrary(Window window)
    {
        _window = window;
        window.AppWindow.Closing += Window_Closing;
        window.Closed += (_, _) => _closing = true;
        Loaded += async (_, _) => { if (_operation == null) await RunAsync(RefreshAsync); };
    }

    // Keep one workflow alive through helper completion and reconciliation.
    // Closing never terminates a submitted elevated mutation or an active export.
    private async void Window_Closing(AppWindow sender, AppWindowClosingEventArgs args)
    {
        if (_operation is not { IsCompleted: false } task) return;
        args.Cancel = true;
        if (_waitingToClose) return;
        _waitingToClose = _closing = true;
        try
        {
            _dialog?.Hide();
            await task;
        }
        catch (Exception failure)
        {
            _waitingToClose = _closing = false;
            ShowFailure("closing_failure", "The voice workflow could not finish. Review the error before closing again.", failure, workflow: true);
            RenderState();
            return;
        }
        _window!.Close();
    }

    private Task RunAsync(Func<Task> action)
    {
        if (_busy || _closing) return Task.CompletedTask;
        _busy = true;
        RenderState();
        BusyChanged?.Invoke(true);
        return _operation = CompleteAsync();

        async Task CompleteAsync()
        {
            try { await action(); }
            catch (Exception failure)
            {
                ShowFailure("workflow_failure", "Unable to complete the voice workflow.", failure, workflow: true);
            }
            finally { _busy = false; _busyText = ""; RenderState(); BusyChanged?.Invoke(false); }
        }
    }

    private async Task RefreshAsync() => await ReadSnapshotAsync();

    private async Task ReadSnapshotAsync(string? selectId = null, bool selectRemaining = false)
    {
        _refreshError = null;
        try
        {
            bool allowInitialEmptyState = _snapshot.Voices.Count == 0;
            VoiceSnapshot next = await Task.Run(() =>
            {
                try { return NativeVoiceManager.Read(); }
                catch (BackendException failure) when (allowInitialEmptyState)
                {
                    string root = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData), "OutLoud", "VoiceLibrary");
                    if (!VoiceLibraryPresentation.IsUninitializedEmpty(failure.Error, root)) throw;
                    return new VoiceSnapshot(root, []);
                }
            });
            string? previousId = selectId ?? SelectedVoice?.LibraryId;
            _snapshot = next;
            VoiceList.ItemsSource = next.Voices;
            VoiceList.SelectedItem = next.Voices.FirstOrDefault(voice => voice.LibraryId == previousId)
                ?? (selectRemaining ? next.Voices.FirstOrDefault() : null);
            LibraryRootText.Text = next.LibraryRoot;
            SapiWarningText.Visibility = next.Voices.Any(voice => voice.SapiError != null) ? Visibility.Visible : Visibility.Collapsed;
        }
        catch (BackendException failure) { _refreshError = failure.Error; }
        catch (Exception failure)
        {
            _refreshError = new("frontend", "unexpected_failure", "Unable to refresh the voice list.", LibraryCode: failure.GetType().Name);
        }
        RefreshErrorPanel.Visibility = _refreshError != null ? Visibility.Visible : Visibility.Collapsed;
        RefreshErrorText.Text = _refreshError is null ? "" : VoiceLibraryPresentation.RefreshFailureText(_refreshError);
        RefreshDiagnosticText.Text = _refreshError?.Diagnostic ?? "";
        RenderState();
    }

    private void RenderState()
    {
        RefreshButton.IsEnabled = ImportButton.IsEnabled = !_busy && !_closing;
        DetailButton.IsEnabled = !_busy && !_closing && SelectedVoice != null;
        RemoveButton.IsEnabled = !_busy && !_closing && SelectedVoice?.CanRemove == true;
        ExportButton.IsEnabled = !_busy && !_closing && SelectedVoice?.CanExport == true;
        VoiceList.IsEnabled = !_busy && !_closing;
        BusyProgress.Visibility = _busy ? Visibility.Visible : Visibility.Collapsed;
        EmptyState.Visibility = !_busy && _refreshError == null && _snapshot.Voices.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
        StateText.Text = _busy ? (_busyText.Length == 0 ? "Reading machine voice state…" : _busyText) : _refreshError != null
            ? "Refresh failed. Showing the last successful snapshot, if available." : $"{_snapshot.Voices.Count} managed voice(s)";
    }

    private void SetBusyText(string text) { _busyText = text; RenderState(); }
    private void VoiceList_SelectionChanged(object sender, SelectionChangedEventArgs args) => RenderState();
    private async void Refresh_Click(object sender, RoutedEventArgs args) => await RunAsync(RefreshAsync);
    private async void Import_Click(object sender, RoutedEventArgs args) => await RunAsync(ImportAsync);
    private async void Export_Click(object sender, RoutedEventArgs args) => await RunAsync(ExportAsync);
    private async void Remove_Click(object sender, RoutedEventArgs args) => await RunAsync(RemoveAsync);
    private async void Detail_Click(object sender, RoutedEventArgs args) => await RunAsync(ShowDetailAsync);
    private void Dismiss_Click(object sender, RoutedEventArgs args) => OperationPanel.Visibility = Visibility.Collapsed;

    private async Task<ContentDialogResult> ShowDialogAsync(ContentDialog dialog)
    {
        if (_closing) return ContentDialogResult.None;
        _dialog = dialog;
        dialog.XamlRoot = XamlRoot;
        dialog.RequestedTheme = ActualTheme;
        try { return await dialog.ShowAsync(); }
        finally { _dialog = null; }
    }

    private static TextBlock Text(string text) => new() { Text = text, TextWrapping = TextWrapping.Wrap, IsTextSelectionEnabled = true };
    private static void AddDetail(StackPanel panel, string label, string? value)
    {
        panel.Children.Add(new TextBlock { Text = label, FontSize = 12, Margin = new Thickness(0, 12, 0, 0),
            Foreground = (Brush)Application.Current.Resources["TextFillColorSecondaryBrush"] });
        panel.Children.Add(Text(value ?? "Unavailable"));
    }

    private async Task ShowDetailAsync()
    {
        if (SelectedVoice is not { } voice) return;
        var content = new StackPanel { Spacing = 14, Padding = new Thickness(0, 0, 20, 4) };
        var secondary = (Brush)Application.Current.Resources["TextFillColorSecondaryBrush"];
        var statusStyles = (VoiceLibraryStatusStyleConverter)Resources["VoiceStatusStyle"];
        var technicalFont = new FontFamily("Consolas");

        StackPanel AddSection(string heading)
        {
            if (content.Children.Count > 0)
                content.Children.Add(new Border { Height = 1, Background = (Brush)Application.Current.Resources["DividerStrokeColorDefaultBrush"] });
            var section = new StackPanel { Spacing = 10 };
            section.Children.Add(new TextBlock
            {
                Text = heading, FontSize = 14, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
                Foreground = secondary, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 0, 0, 4),
            });
            content.Children.Add(section);
            return section;
        }

        void AddField(StackPanel section, string label, string? value, bool technical = false, string? statusKind = null, bool stacked = false)
        {
            var field = new Grid { ColumnSpacing = 24, RowSpacing = 6 };
            if (stacked)
            {
                field.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
                field.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            }
            else
            {
                field.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(176) });
                field.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            }
            field.Children.Add(new TextBlock
            {
                Text = label, FontSize = 13, FontWeight = Microsoft.UI.Text.FontWeights.Medium, Foreground = secondary,
                TextWrapping = TextWrapping.Wrap, VerticalAlignment = VerticalAlignment.Center,
            });
            var valueText = Text(value ?? "Unavailable");
            valueText.FontSize = technical ? 13 : 14;
            valueText.VerticalAlignment = VerticalAlignment.Center;
            if (technical) valueText.FontFamily = technicalFont;
            if (statusKind is not null)
                valueText.Style = (Style)statusStyles.Convert(valueText.Text, typeof(Style), statusKind, "");
            if (stacked) Grid.SetRow(valueText, 1);
            else Grid.SetColumn(valueText, 1);
            field.Children.Add(valueText);
            section.Children.Add(field);
        }

        void AddDiagnostic(StackPanel section, string message)
        {
            if (message.Length == 0) return;
            var diagnostic = Text(message);
            diagnostic.FontSize = 13;
            diagnostic.Foreground = secondary;
            section.Children.Add(diagnostic);
        }

        var status = AddSection("Status");
        AddField(status, "Library health", voice.Health, statusKind: "Library");
        AddField(status, "SAPI status · machine", voice.SapiStatusText, statusKind: "Sapi");
        var diagnostics = new StackPanel { Spacing = 6, Margin = new Thickness(0, 4, 0, 0) };
        AddDiagnostic(diagnostics, voice.LibraryDetail);
        AddDiagnostic(diagnostics, voice.SapiDiagnostic);
        if (diagnostics.Children.Count > 0) status.Children.Add(diagnostics);

        var information = AddSection("Voice information");
        AddField(information, "Import kind", voice.ImportKindText);
        if (voice.ImportKind == "pocket_native")
            AddField(information, "Format", "Native Pocket voice (.safetensors)");
        AddField(information, "Library ID", voice.LibraryId, technical: true);
        AddField(information, "Source filename", voice.SourceFilename);
        AddField(information, "Current content imported (UTC)", voice.ImportedUtc);

        var technicalDetails = AddSection("Technical details");
        if (voice.ImportKind == "pocket_native")
        {
            AddField(technicalDetails, "Profile path", voice.ProfilePath, technical: true, stacked: true);
            AddField(technicalDetails, "Profile SHA256", voice.ProfileSha256, technical: true, stacked: true);
        }
        AddField(technicalDetails, "Source SHA256", voice.SourceSha256, technical: true, stacked: true);

        var body = new ScrollViewer
        {
            Content = content,
            HorizontalContentAlignment = HorizontalAlignment.Stretch,
            HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled,
            HorizontalScrollMode = ScrollMode.Disabled,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
        };
        var dialog = new ContentDialog
        {
            Title = voice.NameText,
            CloseButtonText = "Close",
            HorizontalContentAlignment = HorizontalAlignment.Stretch,
            Content = body,
        };
        dialog.Resources["ContentDialogMaxWidth"] = 640d;
        dialog.Resources["ContentDialogMaxHeight"] = 832d;
        var root = XamlRoot;
        FrameworkElement? titlePart = null, footerPart = null;

        static FrameworkElement? FindPart(DependencyObject parent, string name)
        {
            if (parent is FrameworkElement element && element.Name == name) return element;
            for (int index = 0; index < VisualTreeHelper.GetChildrenCount(parent); index++)
                if (FindPart(VisualTreeHelper.GetChild(parent, index), name) is { } found) return found;
            return null;
        }

        void UpdateBodySize()
        {
            // Reserve native title/footer chrome and edge clearance independently of body height.
            // Only the property list scrolls; scaled text can grow without a fixed row height.
            double titleHeight = Math.Max(28, titlePart?.ActualHeight ?? 0);
            double footerHeight = Math.Max(84, footerPart?.ActualHeight ?? 0);
            body.MaxHeight = Math.Max(0, Math.Min(594, (root.Size.Height - titleHeight - footerHeight - 112) * 1.1));
        }

        void RootChanged(XamlRoot sender, XamlRootChangedEventArgs args) => UpdateBodySize();
        void ChromeSizeChanged(object sender, SizeChangedEventArgs args) => UpdateBodySize();
        dialog.Opened += (_, _) =>
        {
            titlePart = FindPart(dialog, "Title");
            footerPart = FindPart(dialog, "CommandSpace");
            if (titlePart is not null) titlePart.SizeChanged += ChromeSizeChanged;
            if (footerPart is not null) footerPart.SizeChanged += ChromeSizeChanged;
            if (FindPart(dialog, "CloseButton") is Button closeButton)
            {
                // Retain the native close action and focus handling; adjust only its footprint.
                closeButton.Width = double.NaN;
                closeButton.Height = double.NaN;
                closeButton.MinWidth = 96;
                closeButton.MinHeight = 36;
                closeButton.Padding = new Thickness(16, 6, 16, 6);
                closeButton.FontSize = 14;
                closeButton.HorizontalAlignment = HorizontalAlignment.Right;
            }
            UpdateBodySize();
        };
        UpdateBodySize();
        root.Changed += RootChanged;
        try { await ShowDialogAsync(dialog); }
        finally
        {
            root.Changed -= RootChanged;
            if (titlePart is not null) titlePart.SizeChanged -= ChromeSizeChanged;
            if (footerPart is not null) footerPart.SizeChanged -= ChromeSizeChanged;
        }
    }

    private async Task ImportAsync()
    {
        OperationPanel.Visibility = Visibility.Collapsed;
        SetBusyText("Choose a voice file and confirm its name.");
        try
        {
            var picker = new FileOpenPicker(_window!.AppWindow.Id) { Title = "Select a voice file" };
            picker.FileTypeFilter.Add(".safetensors");
            var file = await picker.PickSingleFileAsync();
            if (_closing) return;
            if (file == null) { ShowResult(new(ImportPhase.Cancelled)); return; }
            if (!IsNativeFile(file.Path)) throw new BackendException(new("frontend", "source_io", "Select a .safetensors voice file."));
            var name = new TextBox { Header = "Voice name", Text = CultureInfo.CurrentCulture.TextInfo.ToTitleCase(
                Path.GetFileNameWithoutExtension(file.Path).Replace('_', ' ').Replace('-', ' ')) };
            var content = new StackPanel { Spacing = 16 };
            content.Children.Add(Text(file.Path));
            content.Children.Add(Text("Native Pocket voice (.safetensors)"));
            content.Children.Add(name);
            content.Children.Add(Text("The file will be validated and installed in the managed Voice Library."));
            content.Children.Add(Text("Out Loud uses administrator permission to manage this voice in the library and Windows speech system for all users."));
            var dialog = new ContentDialog { Title = "Import a voice", Content = content, PrimaryButtonText = "Import", CloseButtonText = "Cancel", DefaultButton = ContentDialogButton.Close };
            void ValidateName() => dialog.IsPrimaryButtonEnabled = !string.IsNullOrWhiteSpace(name.Text) && !name.Text.Any(char.IsControl);
            name.TextChanged += (_, _) => ValidateName();
            dialog.Opened += (_, _) => { name.Focus(FocusState.Programmatic); name.SelectAll(); };
            ValidateName();
            VoiceImportResult result;
            while (true)
            {
                if (await ShowDialogAsync(dialog) != ContentDialogResult.Primary || _closing) { ShowResult(new(ImportPhase.Cancelled)); return; }
                result = await _importer.ImportAsync(new(file.Path, name.Text.Trim()), new ImportProgress(this, false));
                if (result.Error?.Code != "duplicate_name" || _closing) break;
                await ReadSnapshotAsync();
                VoiceRecord? target = _refreshError == null && result.DuplicateIds is { Length: 1 } ids
                    ? _snapshot.Voices.FirstOrDefault(voice => voice.LibraryId == ids[0] && voice.CanExport) : null;
                string? sourceHash = null;
                if (target != null)
                {
                    using var source = File.OpenRead(file.Path);
                    sourceHash = Convert.ToHexString(await SHA256.HashDataAsync(source)).ToLowerInvariant();
                }
                var collision = new ContentDialog {
                    Title = "Voice name already exists",
                    Content = Text(target != null
                        ? $"A voice named \"{target.DisplayName}\" already exists, but this is a different voice profile. Replace its profile? Its name and Windows speech identity will be kept."
                        : "This name is already used. The existing voice could not be uniquely verified for replacement. Choose another name or cancel."),
                    PrimaryButtonText = "Replace voice", IsPrimaryButtonEnabled = target != null,
                    SecondaryButtonText = "Change name", CloseButtonText = "Cancel", DefaultButton = ContentDialogButton.Close,
                };
                var choice = await ShowDialogAsync(collision);
                if (choice == ContentDialogResult.Secondary && !_closing) continue;
                if (choice != ContentDialogResult.Primary || _closing || target == null) { ShowResult(new(ImportPhase.Cancelled)); return; }
                result = await _importer.ImportAsync(new(file.Path, target.DisplayName!, target.LibraryId, target.ContentSha256,
                    VoiceOperation.ReplaceNative, ExpectedSourceHash: sourceHash), new ImportProgress(this, false));
                break;
            }
            if (result.Phase is ImportPhase.Succeeded or ImportPhase.Partial || result.Operation == VoiceOperation.ReplaceNative ||
                result.Error?.Code is "duplicate_content" or "replacement_conflict" or "replacement_pending" or "outcome_unknown")
                await ReadSnapshotAsync(result.LibraryId);
            ShowResult(result);
        }
        catch (Exception failure) { ShowFailure("import_failure", "Unable to complete the import workflow.", failure); }
    }

    private async Task RemoveAsync()
    {
        if (SelectedVoice is not { CanRemove: true } target) return;
        OperationPanel.Visibility = Visibility.Collapsed;
        SetBusyText("Confirm removal of the selected voice.");
        try
        {
            var content = new StackPanel { Spacing = 16 };
            content.Children.Add(Text("This will remove the voice from Out Loud and Windows speech. This action cannot be undone."));
            AddDetail(content, "Library ID", target.LibraryId);
            content.Children.Add(Text("This removes the voice for all users. Close WoW or other active speech consumers before removing a loaded voice."));
            var dialog = new ContentDialog { Title = $"Remove \"{target.NameText}\"?", Content = content,
                PrimaryButtonText = "Remove", CloseButtonText = "Cancel", DefaultButton = ContentDialogButton.Close };
            if (await ShowDialogAsync(dialog) != ContentDialogResult.Primary || _closing)
            { ShowResult(new(ImportPhase.Cancelled, Operation: VoiceOperation.Remove)); return; }
            var request = new VoiceImportRequest("", target.DisplayName ?? "", target.LibraryId, target.ContentSha256, VoiceOperation.Remove, target.Health);
            var result = await _importer.ImportAsync(request, new ImportProgress(this, true));
            ShowResult(result);
            // Reconcile conflicts and failures too; the displayed snapshot is never authority.
            await ReadSnapshotAsync(target.LibraryId, selectRemaining: true);
        }
        catch (Exception failure) { ShowFailure("removal_failure", "Unable to complete the removal workflow.", failure, VoiceOperation.Remove); }
    }

    private async Task ExportAsync()
    {
        if (SelectedVoice is not { CanExport: true } target) return;
        OperationPanel.Visibility = Visibility.Collapsed;
        SetBusyText("Choose where to save the native voice.");
        try
        {
            var picker = new FileSavePicker(_window!.AppWindow.Id) { Title = "Export native voice", SuggestedFileName = ExportFileName(target.NameText),
                DefaultFileExtension = ".safetensors", ShowOverwritePrompt = false };
            picker.FileTypeChoices.Add("Native Pocket voice", new List<string> { ".safetensors" });
            var file = await picker.PickSaveFileAsync();
            if (_closing) return;
            if (file == null) { ShowExportCancelled(); return; }
            if (!IsNativeFile(file.Path)) throw new BackendException(new("frontend", "destination_io", "The exported voice filename must end in .safetensors."));
            string identity = await Task.Run(() => NativeVoiceManager.InspectExportDestination(file.Path));
            if (_closing) return;
            if (identity != "absent" && await ShowDialogAsync(new ContentDialog { Title = "Confirm voice export",
                Content = Text($"{file.Path}\n\nThis file already exists. Replace it?"), PrimaryButtonText = "Replace", CloseButtonText = "Cancel",
                DefaultButton = ContentDialogButton.Close }) != ContentDialogResult.Primary) { ShowExportCancelled(); return; }
            if (_closing) return;
            SetBusyText("Exporting the selected voice…");
            await Task.Run(() => NativeVoiceManager.Export(target, new(file.Path, identity)));
            ShowNotification("Success", "Voice exported successfully.", "", "SystemFillColorSuccessBrush");
        }
        catch (Exception failure) { ShowFailure("export_failure", "Unable to export the voice file.", failure, export: true); }
        // Export retains the source snapshot and selection.
    }

    private void ShowExportCancelled() => ShowNotification("Information", "Export cancelled. No file was written.", "", "TextFillColorSecondaryBrush");
    private static bool IsNativeFile(string path) => string.Equals(Path.GetExtension(path), ".safetensors", StringComparison.OrdinalIgnoreCase);

    // Original filename sanitization, with only the native extension changed.
    internal static string ExportFileName(string name)
    {
        var invalid = Path.GetInvalidFileNameChars();
        string stem = new(name.Select(c => invalid.Contains(c) ? '_' : c).ToArray());
        stem = stem.Trim().TrimEnd('.', ' ');
        if (stem.Length > 120) stem = stem[..(char.IsHighSurrogate(stem[119]) ? 119 : 120)];
        if (stem.Length == 0) stem = "voice";
        string device = stem.Split('.')[0].ToUpperInvariant();
        if (device is "CON" or "PRN" or "AUX" or "NUL" || device.Length == 4 &&
            (device.StartsWith("COM", StringComparison.Ordinal) || device.StartsWith("LPT", StringComparison.Ordinal)) && device[3] is >= '1' and <= '9') stem = "voice-" + stem;
        return stem + ".safetensors";
    }

    private sealed class ImportProgress(VoiceLibraryView owner, bool remove) : IProgress<ImportPhase>
    {
        public void Report(ImportPhase phase)
        {
            void Update() => owner.SetBusyText(phase == ImportPhase.AwaitingElevation ? "Starting the voice helper…" : remove
                ? "Removing the selected voice and its Windows speech registration…" : "Validating and saving the voice file…");
            if (owner.DispatcherQueue.HasThreadAccess) Update();
            else owner.DispatcherQueue.TryEnqueue(Update);
        }
    }

    private void ShowFailure(string code, string message, Exception failure, VoiceOperation operation = VoiceOperation.ImportNative, bool export = false, bool workflow = false)
    {
        BackendError error = failure is BackendException backend ? backend.Error : new("frontend", code, message, LibraryCode: failure.GetType().Name);
        if (workflow) ShowNotification("Error", message, error.Diagnostic, "SystemFillColorCriticalBrush");
        else if (!export) ShowResult(new(ImportPhase.Failed, Error: error, Operation: operation));
        else
        {
            bool conflict = error.Code is "export_conflict" or "voice_not_found";
            ShowNotification(conflict ? "Warning" : "Error", conflict ? "The selected voice or destination changed. Refresh the list or choose the destination again." : "Export failed: " + error.Message,
                error.Diagnostic, conflict ? "SystemFillColorCautionBrush" : "SystemFillColorCriticalBrush");
        }
    }

    private void ShowResult(VoiceImportResult result)
    {
        bool remove = result.Operation == VoiceOperation.Remove;
        bool replace = result.Operation == VoiceOperation.ReplaceNative;
        bool warning = result.Phase == ImportPhase.Partial || result.Error?.Code is "voice_in_use" or "removal_conflict" or "removal_partial" or
            "removal_pending" or "replacement_pending" or "replacement_conflict" or "duplicate_content" or "duplicate_name" or "voice_not_found" or "reconciliation_required" or "outcome_unknown";
        string title = result.Phase == ImportPhase.Cancelled ? "Information" : result.Phase == ImportPhase.Succeeded ? "Success" : warning ? "Warning" : "Error";
        string message = result.Phase switch
        {
            ImportPhase.Succeeded when remove => "Voice removed from Out Loud and Windows speech.",
            ImportPhase.Succeeded when replace => "Voice profile replaced. Its name and Windows speech identity were kept.",
            ImportPhase.Succeeded => "Voice imported and added to Windows speech.",
            ImportPhase.Partial when remove && result.Error?.Code is "removal_partial" or "removal_pending" => "Removal is incomplete. The library entry is detached, but its Windows speech registration still needs review. See the diagnostic details.",
            ImportPhase.Partial when remove => "Voice removed from Out Loud and Windows speech, but file cleanup is pending. See the diagnostic details.",
            ImportPhase.Partial when replace => "Voice profile replaced, but Windows speech registration needs review. The new profile has been kept.",
            ImportPhase.Partial => "Voice imported but SAPI registration failed. The imported voice has been kept.",
            ImportPhase.Cancelled when remove => "Removal cancelled. No voice was removed.",
            ImportPhase.Cancelled when replace => "Replacement cancelled. No voice profile was replaced.",
            ImportPhase.Cancelled => "Import cancelled. No voice was added.",
            _ when result.Error?.Code == "voice_in_use" && remove => "Voice could not be removed because it is in use. Close WoW and other active speech consumers, then retry.",
            _ when result.Error?.Code == "voice_in_use" && replace => "Voice could not be replaced because it is in use. Close WoW and other active speech consumers, then retry.",
            _ when result.Error?.Code == "replacement_conflict" => "The voice or source profile changed since confirmation. Refresh and confirm the replacement again.",
            _ when result.Error?.Code == "replacement_pending" => "Replacement recovery or backup cleanup is pending. Import again to retry recovery safely. See the diagnostic details.",
            _ when remove && result.Error?.Code is "removal_conflict" or "voice_not_found" => "The removal target changed or disappeared. Check the refreshed list before confirming again.",
            _ when result.Error?.Code == "outcome_unknown" => "Operation outcome could not be confirmed. Check the refreshed list before retrying.",
            _ when result.Error?.Code == "duplicate_content" => "This exact voice profile is already in the library" + ExistingVoiceNames(result) + ". No duplicate was added and no voice was replaced.",
            _ when result.Error?.Code == "duplicate_name" => "A different voice profile already uses this name. Choose another name or confirm replacement.",
            _ => (remove ? "Removal failed: " : replace ? "Replacement failed: " : "Import failed: ") + (result.Error?.Message ?? "See the diagnostic details.")
        };
        string diagnostic = (result.Error?.Diagnostic ?? "") + (result.DuplicateIds is { Length: > 0 } ids ? "\nExisting library IDs: " + string.Join(", ", ids) : "");
        if (diagnostic.Length > 0 && result.LibraryId != null) diagnostic += "\nLibrary ID: " + result.LibraryId;
        ShowNotification(title, message, diagnostic, title switch { "Success" => "SystemFillColorSuccessBrush", "Warning" => "SystemFillColorCautionBrush",
            "Error" => "SystemFillColorCriticalBrush", _ => "TextFillColorSecondaryBrush" });
    }

    private string ExistingVoiceNames(VoiceImportResult result)
    {
        if (_refreshError != null || result.DuplicateIds is not { Length: > 0 } ids) return "";
        var names = _snapshot.Voices.Where(voice => ids.Contains(voice.LibraryId) && voice.DisplayName != null)
            .Select(voice => "\"" + voice.DisplayName + "\"");
        string text = string.Join(", ", names);
        return text.Length == 0 ? "" : " as " + text;
    }

    private void ShowNotification(string title, string message, string diagnostic, string brush)
    {
        OperationTitleText.Text = title;
        OperationTitleText.Foreground = (Brush)Application.Current.Resources[brush];
        OperationMessageText.Text = message;
        OperationDiagnosticText.Text = diagnostic;
        OperationDiagnostic.IsExpanded = false;
        OperationDiagnostic.Visibility = diagnostic.Length > 0 ? Visibility.Visible : Visibility.Collapsed;
        OperationPanel.Visibility = Visibility.Visible;
    }
}
