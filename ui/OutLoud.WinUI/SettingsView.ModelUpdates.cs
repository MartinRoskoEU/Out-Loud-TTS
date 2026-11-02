using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using OutLoud.WinUI.Services;
using System.Net.Http;

namespace OutLoud.WinUI;

public sealed partial class SettingsView
{
    // Dedicated anonymous client: no OAuth provider, cookies, Windows credentials, or redirects.
    private readonly HttpClient _modelUpdateHttp = new(new HttpClientHandler
    { AllowAutoRedirect = false, UseCookies = false, UseDefaultCredentials = false })
    { Timeout = Timeout.InfiniteTimeSpan };
    private bool _checkingModelUpdates;
    private bool _modelUpdateCheckIsManual;
    private bool _automaticModelUpdateCheckPending;
    private int _automaticModelUpdateSchedule;
    private int _modelUpdateCheckGeneration;
    private bool ModelUpdateBlocksChanges => _checkingUpdates || (_checkingModelUpdates && _modelUpdateCheckIsManual);
    private readonly Dictionary<string, PocketTtsModelUpdateResult> _modelUpdateResults = new(StringComparer.Ordinal);

    private async void UpdateModel_Click(object sender, RoutedEventArgs args)
    {
        if (sender is not Button { DataContext: PocketTtsModelSetupEntry { CanUpdate: true } entry } ||
            ModelBusy || _checkingUpdates || _checkingModelUpdates || RuntimeUpdateBusy || _closing) return;
        _modelInstalling = true;
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(_setupCancellation.Token);
        _modelCancellation = cancellation;
        RenderModelInstallationConflicts();
        _modelTask = UpdateModelAsync(entry, cancellation.Token);
        try { await _modelTask; }
        finally
        {
            _modelTask = null;
            _modelCancellation = null;
            _modelInstalling = false;
            if (!_closing && !_closingAfterModels) await CheckModelsAsync();
            if (!_closing) RenderModelInstallationConflicts();
        }
    }

    private async Task UpdateModelAsync(PocketTtsModelSetupEntry entry, CancellationToken token)
    {
        var dialog = new ContentDialog
        {
            XamlRoot = XamlRoot, Title = "Update model?",
            Content = $"Update {entry.Name}?\n\nInstalled revision: {entry.Revision}\nAvailable revision: {entry.UpdateCommit}\n\nDownload and verify the official model files? Your selected model will stay selected. After successful activation, previous files no longer used by installed models will be removed. Rollback is not available.",
            PrimaryButtonText = "Update", CloseButtonText = "Cancel", DefaultButton = ContentDialogButton.Close,
        };
        try
        {
            bool visible = true;
            ContentDialogResult confirmation;
            try
            {
                using var hide = token.Register(() => DispatcherQueue.TryEnqueue(() => { if (visible) dialog.Hide(); }));
                confirmation = await dialog.ShowAsync();
            }
            finally { visible = false; }
            if (confirmation != ContentDialogResult.Primary || token.IsCancellationRequested) return;
            ShowModelInstallationCard(entry.Name);
            ModelInstallationTitleText.Text = "Updating model";
            CancelModelInstallationButton.Content = "Cancel update";
            var progress = new Progress<PocketTtsModelInstallProgress>(value =>
            { if (!_closing && !_modelInstallationResultShown) RenderModelInstallationProgress(value); });
            var result = await new PocketTtsModelInstallService(_modelHttp).UpdateAsync(
                entry.Name, entry.InventoryIdentity!, entry.UpdateCommit!, progress, token);
            if (result.Succeeded) _automaticModelUpdateCheckPending = true;
            if (!_closing) RenderModelInstallationResult(entry.Name, result, updating: true);
        }
        catch (OperationCanceledException) { }
        catch (Exception)
        {
            if (_closing) return;
            ShowModelInstallationCard(entry.Name);
            RenderModelInstallationResult(entry.Name, new(false, PocketTtsModelInstallFailure.Storage,
                "The model update could not complete. Refresh Models to verify its state. Recorded cleanup will be retried at startup."), updating: true);
        }
    }

    private Task CheckInstalledModelUpdatesAsync() => RunModelUpdateCheckAsync(automatic: false);

    private void QueueAutomaticModelUpdateCheck()
    {
        _automaticModelUpdateCheckPending = true;
        TryStartPendingAutomaticModelUpdateCheck();
    }

    private void TryStartPendingAutomaticModelUpdateCheck()
    {
        if (!_automaticModelUpdateCheckPending || _closing || _checkingUpdates || _checkingModelUpdates || ModelBusy || RuntimeUpdateBusy) { return; }
        _automaticModelUpdateCheckPending = false;
        int schedule = ++_automaticModelUpdateSchedule;
        DispatcherQueue.TryEnqueue(() =>
        {
            if (_closing || schedule != _automaticModelUpdateSchedule) { return; }
            if (_checkingUpdates || _checkingModelUpdates || ModelBusy || RuntimeUpdateBusy)
            { _automaticModelUpdateCheckPending = true; return; }
            _ = RunModelUpdateCheckAsync(automatic: true);
        });
    }

    private async Task RunModelUpdateCheckAsync(bool automatic)
    {
        if (_closing || _checkingModelUpdates || ModelBusy || RuntimeUpdateBusy) { return; }
        _checkingModelUpdates = true;
        _modelUpdateCheckIsManual = !automatic;
        if (!automatic)
        {
            // This manual run also serves any queued startup request and always bypasses the interval.
            _automaticModelUpdateCheckPending = false;
            ++_automaticModelUpdateSchedule;
            _modelUpdateResults.Clear();
            RenderModelUpdateRows();
            ModelUpdatesStatusText.Text = "Checking for updates...";
            ModelUpdatesStatusText.Visibility = Visibility.Visible;
        }
        int generation = ++_modelUpdateCheckGeneration;
        RenderModelInstallationConflicts();
        var progress = new Progress<PocketTtsModelUpdateResult>(result => DispatcherQueue.TryEnqueue(() =>
        {
            if (_closing || !_checkingModelUpdates || generation != _modelUpdateCheckGeneration ||
                !_modelRows.Any(row => CanDisplayModelUpdate(row, result))) { return; }
            _modelUpdateResults[result.Alias] = result;
            RenderModelUpdateRows();
        }));
        IReadOnlyDictionary<string, string> identities = new Dictionary<string, string>();
        try
        {
            // Startup already verified models. Background checks never repeat initialization or navigation.
            if (!automatic)
            {
                await CheckModelsAsync();
                ModelUpdatesStatusText.Text = "Checking for updates...";
                ModelUpdatesStatusText.Visibility = Visibility.Visible;
            }
            if (_closing) { return; }
            identities = _modelRows.Where(row => row.InventoryIdentity is not null && (!automatic || row.InventoryFilesVerified))
                .ToDictionary(row => row.Name, row => row.InventoryIdentity!, StringComparer.Ordinal);
            var result = await new PocketTtsModelUpdateCoordinator(_modelUpdateHttp).RunAsync(automatic, identities, progress, _setupCancellation.Token);
            if (_closing) { return; }
            foreach (var pair in identities)
            {
                if (_modelUpdateResults.TryGetValue(pair.Key, out var previous) && previous.InstalledIdentity == pair.Value)
                { _modelUpdateResults.Remove(pair.Key); }
            }
            foreach (var model in result.Models.Where(model => _modelRows.Any(row => CanDisplayModelUpdate(row, model))))
            { _modelUpdateResults[model.Alias] = model; }
            RenderModelUpdateRows();
            if (!automatic)
            {
                ModelUpdatesStatusText.Text = result.Message ?? (result.Models.Any(model => model.Status == PocketTtsModelUpdateStatus.Unavailable)
                    ? "Some model updates could not be checked. See the individual model statuses."
                    : "Model update check completed.");
            }
        }
        catch (OperationCanceledException)
        {
            if (!_closing) { MarkModelUpdateCheckUnavailable(identities, automatic); }
        }
        catch (Exception)
        {
            // Background errors are silent; only safe per-model states change. No exception payloads enter the UI.
            if (!_closing) { MarkModelUpdateCheckUnavailable(identities, automatic); }
        }
        finally
        {
            _checkingModelUpdates = false;
            _modelUpdateCheckIsManual = false;
            if (!_closing) { RenderModelInstallationConflicts(); }
        }
    }

    private void MarkModelUpdateCheckUnavailable(IReadOnlyDictionary<string, string> identities, bool automatic)
    {
        foreach (var pair in identities)
        {
            _modelUpdateResults[pair.Key] = new(pair.Key, PocketTtsModelUpdateStatus.Unavailable,
                "The model update check could not complete. Retry later.") { InstalledIdentity = pair.Value };
        }
        RenderModelUpdateRows();
        if (!automatic) { ModelUpdatesStatusText.Text = "Unable to check model updates. Retry later."; }
    }

    private static bool CanDisplayModelUpdate(PocketTtsModelSetupEntry row, PocketTtsModelUpdateResult update) =>
        row.Name == update.Alias && row.InventoryIdentity is not null && row.InventoryIdentity == update.InstalledIdentity &&
        (row.InventoryFilesVerified || update.Status is PocketTtsModelUpdateStatus.Checking or PocketTtsModelUpdateStatus.Unavailable);

    private void SetModelRows(IReadOnlyList<PocketTtsModelSetupEntry> models)
    {
        _modelRows = models;
        foreach (var key in _modelUpdateResults.Where(pair => !models.Any(row => CanDisplayModelUpdate(row, pair.Value))).Select(pair => pair.Key).ToArray())
        { _modelUpdateResults.Remove(key); }
        if (!_checkingModelUpdates) { ModelUpdatesStatusText.Visibility = Visibility.Collapsed; }
        RenderModelUpdateRows();
    }

    private void RenderModelUpdateRows() => ModelList.ItemsSource = _modelRows.Select(entry =>
        _modelUpdateResults.TryGetValue(entry.Name, out var update) && CanDisplayModelUpdate(entry, update)
            ? entry with { UpdateStatusText = update.StatusText, UpdateStatusDetail = update.ComparedCommit is null
                ? update.Message : update.Message + " Main commit: " + update.ComparedCommit,
                UpdateCommit = entry.CanActivate && update.Status == PocketTtsModelUpdateStatus.UpdateAvailable ? update.ComparedCommit : null }
            : entry with { UpdateStatusText = "", UpdateStatusDetail = "" }).ToArray();
}
