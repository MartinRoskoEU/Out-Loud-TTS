using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using OutLoud.WinUI.Services;
using OutLoud.WinUI.VoiceTesting;
using System.Net.Http;

namespace OutLoud.WinUI;

public sealed partial class SettingsView
{
    private readonly PocketTtsModelSetupService _modelSetup = new(new PocketTtsCliService());
    private readonly PocketTtsModelSelectionSettings _modelSelectionSettings = new();
    private readonly HttpClient _modelHttp = new(new HttpClientHandler { AllowAutoRedirect = false, UseCookies = false, UseDefaultCredentials = false })
    { Timeout = Timeout.InfiniteTimeSpan };
    private IReadOnlyList<PocketTtsModelSetupEntry> _modelRows = [];
    private bool _checkingModels, _modelInstalling, _modelRemoving, _selectingModel, _renderingSelection;
    private bool _modelInstallationResultShown, _closingAfterModels;
    private CancellationTokenSource? _modelCancellation;
    private Task? _modelTask;
    private bool ModelBusy => _checkingModels || _modelInstalling || _modelRemoving || _selectingModel;
    internal event Action? VoiceTestModelStateChanged;

    // Read the existing checked state for tester messages only; never check, select or modify models here.
    internal VoiceTestModelState GetVoiceTestModelState()
    {
        if (ModelBusy || RuntimeUpdateBusy || _closing) return VoiceTestModelState.Unknown;
        if (_modelSetup.ActiveModel is not null) return VoiceTestModelState.Selected;
        if (_modelRows.Any(model => model.CanActivate)) return VoiceTestModelState.NotSelected;
        if (_modelRows.Any(model => model.CanRemove)) return VoiceTestModelState.Unavailable;
        if (_modelRows.Any(model => model.CanInstall)) return VoiceTestModelState.NotInstalled;
        return VoiceTestModelState.Unknown;
    }

    private void InitializeModelManagement(Window window)
    {
        window.AppWindow.Closing += ModelWindow_Closing;
        window.Closed += (_, _) => { _modelCancellation?.Cancel(); _modelHttp.Dispose(); _modelUpdateHttp.Dispose(); };
    }

    private async Task CheckModelsAsync(bool isStartup = false)
    {
        if (ModelBusy || _closing || RuntimeUpdateBusy) { return; }
        _checkingModels = true;
        RenderModelInstallationConflicts();
        RenderRuntimeReadiness(null);
        RuntimeVersionText.Text = "Checking installed release…";
        ModelSetupStatusText.Text = "Checking runtime and verified private model assets…";
        ModelSetupStatusText.Visibility = Visibility.Visible;
        ModelListEmptyText.Text = "Refreshing models…";
        try
        {
            if (isStartup)
            {
                var cleanup = await new PocketTtsModelRemovalService().RecoverAsync(_setupCancellation.Token);
                if (cleanup is not null && !_closing)
                {
                    ShowModelInstallationCard("Pending cleanup");
                    RenderModelRemovalResult("Pending model cleanup", cleanup, recovery: true);
                }
            }
            var result = await _modelSetup.CheckAsync(_setupCancellation.Token);
            if (_closing) { return; }
            ModelSetupStatusText.Text = result.InventoryError is null ? result.Message : result.Message + "\n" + result.InventoryError;
            ModelSetupStatusText.Visibility = result.RuntimeReady && result.InventoryError is null ? Visibility.Collapsed : Visibility.Visible;
            RenderRuntimeReadiness(result.RuntimeReady);
            await RefreshInstalledRuntimeReleaseAsync();
            SetModelRows(result.Models);
            ModelList.Visibility = _modelRows.Count > 0 ? Visibility.Visible : Visibility.Collapsed;
            ModelListHost.Visibility = ModelList.Visibility;
            ModelListEmptyText.Visibility = _modelRows.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
            ModelListEmptyText.Text = result.InventoryError ?? (result.RuntimeReady
                ? "No supported or identifiable installed models were found."
                : "Runtime support is unavailable. No identifiable installed models were found.");

            var usable = _modelSetup.GetSelectableModels(_modelRows);
            _modelSetup.ClearActiveModel();
            PocketTtsModelSetupEntry? selected = null;
            string? selectionWarning = null;
            try
            {
                // A missing/blocked runtime does not invalidate the user's saved model choice.
                if (result.RuntimeReady)
                    selected = await _modelSelectionSettings.GetStartupModelAsync(usable, _setupCancellation.Token);
                if (selected is not null) { _modelSetup.Activate(selected); }
            }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or InvalidDataException or System.Text.Json.JsonException)
            { selectionWarning = "Saved model selection could not be verified or cleared. No replacement was selected. Check local application data permissions."; }
            SetModelSelection(usable, selected);

            // Read-only discovery is anonymous. Offline failure never invalidates
            // locally verified installations or substitutes a different model.
            try
            {
                var repository = await new PocketTtsModelDiscoveryService(_modelHttp).DiscoverAsync(cancellationToken: _setupCancellation.Token);
                if (!_closing) { ShowModelMessage((selectionWarning is null ? "" : selectionWarning + "\n") + $"Public repository metadata: {repository.Revision[..12]}. Model files use their pinned revisions."); }
            }
            catch (OperationCanceledException) { }
            catch (Exception)
            { if (!_closing) { ShowModelMessage((selectionWarning is null ? "" : selectionWarning + "\n") + "Public repository metadata could not be refreshed. Verified private installations remain available; no files were changed."); } }
        }
        catch (OperationCanceledException) { }
        catch (Exception)
        {
            if (!_closing)
            {
                _modelSetup.ClearActiveModel();
                SetModelRows([]);
                ModelList.ItemsSource = null;
                ModelListHost.Visibility = Visibility.Collapsed;
                ModelListEmptyText.Visibility = Visibility.Visible;
                ModelListEmptyText.Text = "Models could not be checked. Refresh after resolving the reported error.";
                SetModelSelection([], null);
                await RefreshInstalledRuntimeReleaseAsync();
                RenderRuntimeReadiness(false);
            }
        }
        finally
        {
            _checkingModels = false;
            if (!_closing)
            {
                RenderModelInstallationConflicts();
                if (isStartup)
                {
                    StartRuntimeUpdateCheck();
                    QueueAutomaticModelUpdateCheck();
                }
            }
        }
    }

    private void SetModelSelection(IReadOnlyList<PocketTtsModelSetupEntry> usable, PocketTtsModelSetupEntry? selected)
    {
        _renderingSelection = true;
        try { SelectedModelComboBox.ItemsSource = usable; SelectedModelComboBox.SelectedItem = selected; }
        finally { _renderingSelection = false; }
    }

    private async void SelectedModel_SelectionChanged(object sender, SelectionChangedEventArgs args)
    {
        if (_renderingSelection || ModelBusy || ModelUpdateBlocksChanges || RuntimeUpdateBusy || _closing || SelectedModelComboBox.SelectedItem is not PocketTtsModelSetupEntry entry) { return; }
        _selectingModel = true;
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(_setupCancellation.Token);
        _modelCancellation = cancellation;
        RenderModelInstallationConflicts();
        _modelTask = SelectModelAsync(entry, cancellation.Token);
        try { await _modelTask; }
        finally
        {
            _modelTask = null;
            _modelCancellation = null;
            _selectingModel = false;
            if (!_closing) { RenderModelInstallationConflicts(); }
        }
    }

    private async Task SelectModelAsync(PocketTtsModelSetupEntry entry, CancellationToken token)
    {
        try
        {
            await _modelSetup.ActivateAsync(entry, token);
            await _modelSelectionSettings.SaveAsync(entry.Name, token);
            if (!_closing) { ShowModelMessage($"Selected model: {entry.Name}. Revision: {entry.Revision}"); }
        }
        catch (Exception)
        {
            _modelSetup.ClearActiveModel();
            if (!_closing)
            {
                SetModelSelection(_modelSetup.GetSelectableModels(_modelRows), null);
                ShowModelMessage("The model could not be selected and saved. Refresh models after checking file integrity and local storage permissions.");
            }
        }
    }

    private async void ManageModel_Click(object sender, RoutedEventArgs args)
    {
        if (sender is not Button { DataContext: PocketTtsModelSetupEntry entry } || ModelBusy || ModelUpdateBlocksChanges || RuntimeUpdateBusy || _closing || !entry.CanManage) { return; }
        _modelRemoving = entry.CanRemove;
        _modelInstalling = !entry.CanRemove;
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(_setupCancellation.Token);
        _modelCancellation = cancellation;
        RenderModelInstallationConflicts();
        _modelTask = ManageModelAsync(entry, cancellation.Token);
        try { await _modelTask; }
        finally
        {
            _modelTask = null;
            _modelCancellation = null;
            _modelInstalling = _modelRemoving = false;
            if (!_closing && !_closingAfterModels) { await CheckModelsAsync(); }
            if (!_closing) { RenderModelInstallationConflicts(); }
        }
    }

    private async Task ManageModelAsync(PocketTtsModelSetupEntry entry, CancellationToken token)
    {
        bool remove = entry.CanRemove;
        try
        {
            var dialog = new ContentDialog
            {
                XamlRoot = XamlRoot,
                Title = remove ? "Remove model?" : "Install model?",
                Content = remove
                    ? $"Remove {entry.Name}?\n\nIts registration and private files no longer used by other installed models will be removed permanently. Shared private blobs will be kept. A selected model will be cleared."
                    : $"Install {entry.Name}?\n\nDownload and verify its public model files at revision {entry.Revision} into Out Loud's private model storage? No sign-in is required. The model will not be selected automatically.",
                PrimaryButtonText = remove ? "Remove" : "Install", CloseButtonText = "Cancel", DefaultButton = ContentDialogButton.Close,
            };
            ContentDialogResult confirmation;
            bool dialogVisible = true;
            try
            {
                // A managed callback avoids marshaling the native Hide method as a WinRT delegate.
                using var cancelDialog = token.Register(() => DispatcherQueue.TryEnqueue(() =>
                {
                    if (dialogVisible) { dialog.Hide(); }
                }));
                confirmation = await dialog.ShowAsync();
            }
            finally { dialogVisible = false; }
            if (confirmation != ContentDialogResult.Primary || token.IsCancellationRequested) { return; }
            ShowModelInstallationCard(entry.Name);
            if (remove) { ModelInstallationTitleText.Text = "Removing model"; CancelModelInstallationButton.Content = "Cancel removal"; }
            var progress = new Progress<PocketTtsModelInstallProgress>(value =>
            { if (!_closing && !_modelInstallationResultShown) { RenderModelInstallationProgress(value); } });
            RenderModelInstallationProgress(new(remove ? "Checking model operations before removal…" : "Checking runtime and trusted installation metadata…"));
            if (remove)
            {
                var result = await new PocketTtsModelRemovalService().RemoveAsync(entry.Name, entry.InventoryIdentity!, progress, token);
                if (result.Removed)
                {
                    string? previous = _modelSetup.ActiveModel?.Name;
                    _modelSetup.Deactivate(entry.Name);
                    try { await _modelSelectionSettings.ForgetRemovedModelAsync(entry.Name, previous, CancellationToken.None); }
                    catch (Exception) { result = result with { Message = result.Message + " The saved selection could not be cleared; check application data permissions." }; }
                }
                if (!_closing) { RenderModelRemovalResult(entry.Name, result); }
            }
            else
            {
                var result = await new PocketTtsModelInstallService(_modelHttp).InstallAsync(entry.Name, progress, token);
                if (result.Succeeded) { _automaticModelUpdateCheckPending = true; }
                if (!_closing) { RenderModelInstallationResult(entry.Name, result); }
            }
        }
        catch (OperationCanceledException) { }
        catch (Exception)
        {
            if (!_closing)
            {
                ShowModelInstallationCard(entry.Name);
                RenderModelInstallationResult(entry.Name, new(false, PocketTtsModelInstallFailure.Storage,
                    "The model operation could not complete. Refresh models to verify its state; pending private cleanup remains saved for recovery."));
            }
        }
    }

    private void RenderModelRemovalResult(string alias, PocketTtsModelRemovalResult result, bool recovery = false)
    {
        bool success = (result.Removed || recovery) && !result.CleanupPending;
        RenderModelInstallationResult(alias, new(success, result.Cancelled ? PocketTtsModelInstallFailure.Cancelled : PocketTtsModelInstallFailure.Storage, result.Message));
        ModelInstallationTitleText.Text = result.CleanupPending ? (recovery ? "Cleanup incomplete" : "Model cleanup incomplete") : result.Cancelled ? "Removal cancelled" :
            success ? (recovery ? "Cleanup completed" : "Model removed") : "Removal failed";
        ModelInstallationStatusText.Text = result.Message;
        if (result.CleanupPending)
        {
            ModelInstallationTitleText.Style = (Style)ModelInstallationPanel.Resources["InstallationNeutralTitleStyle"];
            ModelInstallationResultIcon.Glyph = "\uE7BA";
        }
    }

    private void CancelModelInstallation_Click(object sender, RoutedEventArgs args)
    {
        _modelCancellation?.Cancel();
        CancelModelInstallationButton.IsEnabled = false;
    }

    private void ShowModelMessage(string message)
    { ModelUpdatesStatusText.Text = message; ModelUpdatesStatusText.Visibility = Visibility.Visible; }

    private void RenderModelInstallationConflicts()
    {
        ModelListHost.IsEnabled = !ModelBusy && !ModelUpdateBlocksChanges && !RuntimeUpdateBusy && !_closing;
        SelectedModelComboBox.IsEnabled = ModelListHost.IsEnabled && _modelSetup.GetSelectableModels(_modelRows).Count > 0;
        RenderRuntimeUpdateStatus();
        TryStartPendingAutomaticModelUpdateCheck();
        VoiceTestModelStateChanged?.Invoke();
    }

    private async void ModelWindow_Closing(AppWindow sender, AppWindowClosingEventArgs args)
    {
        if (_modelTask is not { IsCompleted: false } task) { return; }
        args.Cancel = true;
        if (_closingAfterModels) { return; }
        _closingAfterModels = true;
        _modelCancellation?.Cancel();
        try { await task; }
        catch (Exception) { /* Durable cleanup remains recoverable in schema-2 inventory. */ }
        _window?.Close();
    }
}
