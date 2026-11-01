using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Windowing;
using System.Net.Http;
using OutLoud.WinUI.Services;

namespace OutLoud.WinUI;

public sealed partial class SettingsView
{
    private readonly HttpClient _runtimeUpdateHttp = new(new HttpClientHandler { AllowAutoRedirect = false, UseCookies = false })
    { Timeout = TimeSpan.FromSeconds(10) };

    private bool _checkingRuntimeUpdates;
    private PocketTtsRuntimeUpdateResult? _runtimeUpdateResult;
    private readonly HttpClient _runtimeDownloadHttp = new(new HttpClientHandler { AllowAutoRedirect = false, UseCookies = false })
    { Timeout = Timeout.InfiniteTimeSpan };
    private CancellationTokenSource? _runtimeInstallCancellation;
    private Task<PocketTtsRuntimeInstallResult>? _runtimeInstallTask;
    private PocketTtsRuntimeInstallProgress? _runtimeInstallProgress;
    private string? _runtimeInstallMessage;
    private bool _runtimeInstalling;
    private bool _runtimeConfirming;
    private bool _closingAfterRuntimeUpdate;
    private bool RuntimeUpdateBusy => _runtimeInstalling || _runtimeConfirming;

    private void InitializeRuntimeUpdates() => _window!.AppWindow.Closing += RuntimeUpdateWindow_Closing;

    // Startup and manual checks share the same task and service.
    public Task<PocketTtsRuntimeUpdateResult>? RuntimeUpdateCheck { get; private set; }

    private void StartRuntimeUpdateCheck() => _ = CheckRuntimeUpdatesAsync();

    private async void RuntimeUpdateButton_Click(object sender, RoutedEventArgs args)
    {
        if (_closing || RuntimeUpdateBusy || _checkingRuntimeUpdates) { return; }
        if (_runtimeUpdateResult is { Status: PocketTtsRuntimeUpdateStatus.UpdateAvailable, Asset: not null } update)
        { await ConfirmRuntimeUpdateAsync(update); }
        else { await CheckRuntimeUpdatesAsync(forceRefresh: true); }
    }

    private async Task ConfirmRuntimeUpdateAsync(PocketTtsRuntimeUpdateResult update)
    {
        if (ModelBusy || ModelUpdateBlocksChanges) { return; }
        _runtimeConfirming = true;
        RenderRuntimeUpdateConflicts();
        try
        {
            var installed = await Task.Run(() => PocketTtsRuntimeValidation.ReadVerifiedRuntimeIdentityAsync(_setupCancellation.Token));
            if (_closing) { return; }
            RuntimeVersionText.Text = $"Installed release: {installed.ReleaseVersion}";
            if (installed.ReleaseVersion != update.InstalledReleaseTag)
            {
                _runtimeUpdateResult = null;
                _runtimeInstallMessage = "The installed runtime changed. Check for updates again before installing.";
                return;
            }
            var dialog = new ContentDialog
            {
                XamlRoot = XamlRoot,
                Title = "Update Pocket TTS Runtime?",
                Content = $"Installed release: {installed.ReleaseVersion}\nAvailable release: {update.LatestReleaseTag}\n\nDownload and verify the Windows x64 runtime? The previous runtime will be kept.",
                PrimaryButtonText = "Update", CloseButtonText = "Cancel", DefaultButton = ContentDialogButton.Close,
            };
            if (await dialog.ShowAsync() != ContentDialogResult.Primary || _closing) { return; }
        }
        catch (Exception)
        {
            _runtimeInstallMessage = "Could not verify the installed runtime or open update confirmation. No runtime update was started.";
            return;
        }
        finally
        {
            _runtimeConfirming = false;
            if (!_closing) { RenderRuntimeUpdateConflicts(); }
        }
        if (_closing || ModelBusy || ModelUpdateBlocksChanges) { return; }

        _runtimeInstalling = true;
        _runtimeInstallMessage = null;
        _runtimeInstallProgress = new("Preparing runtime update…");
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(_setupCancellation.Token);
        _runtimeInstallCancellation = cancellation;
        RenderRuntimeUpdateConflicts();
        bool succeeded = false;
        bool cleanupIncomplete = false;
        string? completionMessage = null;
        var progress = new Progress<PocketTtsRuntimeInstallProgress>(value => DispatcherQueue.TryEnqueue(() =>
        {
            if (!_closing && _runtimeInstalling && ReferenceEquals(_runtimeInstallCancellation, cancellation))
            { _runtimeInstallProgress = value; RenderRuntimeUpdateStatus(); }
        }));
        try
        {
            _runtimeInstallTask = new PocketTtsRuntimeInstallService(_runtimeUpdateHttp, _runtimeDownloadHttp).InstallAsync(update, progress, cancellation.Token);
            var result = await _runtimeInstallTask;
            succeeded = result.Succeeded;
            cleanupIncomplete = result.CleanupIncomplete;
            completionMessage = result.Message;
            _runtimeInstallMessage = result.Message;
            if (succeeded) { _runtimeUpdateResult = null; }
            if (!_closing && (!_closingAfterRuntimeUpdate || cleanupIncomplete)) { await RefreshInstalledRuntimeReleaseAsync(); }
        }
        catch (Exception)
        { _runtimeInstallMessage = "The runtime update could not complete. Check local runtime storage and retry."; }
        finally
        {
            _runtimeInstalling = false;
            _runtimeInstallTask = null;
            _runtimeInstallCancellation = null;
            if (!_closing) { RenderRuntimeUpdateConflicts(); }
        }
        if (succeeded && !_closing && !_closingAfterRuntimeUpdate)
        {
            await CheckRuntimeAsync();
            if (!_closing) { await CheckRuntimeUpdatesAsync(forceRefresh: true); }
            if (cleanupIncomplete && !_closing)
            { _runtimeInstallMessage = completionMessage; RenderRuntimeUpdateStatus(); }
        }
    }

    private void RuntimeCancelUpdateButton_Click(object sender, RoutedEventArgs args)
    {
        _runtimeInstallCancellation?.Cancel();
        RuntimeCancelUpdateButton.IsEnabled = false;
    }

    private void RenderRuntimeUpdateConflicts()
    {
        RenderModelInstallationConflicts();
    }

    private async void RuntimeUpdateWindow_Closing(AppWindow sender, AppWindowClosingEventArgs args)
    {
        if (_runtimeInstallTask is not { IsCompleted: false } task) { return; }
        args.Cancel = true;
        if (_closingAfterRuntimeUpdate) { return; }
        _closingAfterRuntimeUpdate = true;
        _runtimeInstallCancellation?.Cancel();
        try
        {
            var result = await task;
            if (result.CleanupIncomplete)
            {
                _closingAfterRuntimeUpdate = false;
                if (!_closing) { RuntimeUpdateStatusText.Text = result.Message; }
                return;
            }
        }
        catch (Exception) { /* The update handler reports failure; cleanup is already awaited. */ }
        _closingAfterRuntimeUpdate = false;
        _window!.Close();
    }

    private async Task CheckRuntimeUpdatesAsync(bool forceRefresh = false)
    {
        if (_closing || _checkingRuntimeUpdates || RuntimeUpdateBusy) { return; }
        _checkingRuntimeUpdates = true;
        _runtimeInstallMessage = null;
        _runtimeUpdateResult = null;
        RenderRuntimeUpdateStatus();
        try
        {
            if (forceRefresh || RuntimeUpdateCheck is null)
            {
                RuntimeUpdateCheck = new PocketTtsRuntimeUpdateService(_runtimeUpdateHttp).CheckAsync(_setupCancellation.Token);
            }
            _runtimeUpdateResult = await RuntimeUpdateCheck;
        }
        catch (Exception)
        {
            _runtimeUpdateResult = new(PocketTtsRuntimeUpdateStatus.Unavailable, "Unable to check for updates");
        }
        finally
        {
            _checkingRuntimeUpdates = false;
            if (!_closing) { RenderRuntimeUpdateStatus(); }
        }
    }

    private void RenderRuntimeUpdateStatus()
    {
        bool indeterminate = _checkingRuntimeUpdates || (_runtimeInstalling && _runtimeInstallProgress?.Percent is null);
        RuntimeUpdateProgress.IsActive = indeterminate;
        RuntimeUpdateProgress.Visibility = indeterminate ? Visibility.Visible : Visibility.Collapsed;
        RuntimeInstallProgressBar.Visibility = _runtimeInstalling ? Visibility.Visible : Visibility.Collapsed;
        RuntimeInstallProgressBar.IsIndeterminate = _runtimeInstallProgress?.Percent is null;
        RuntimeInstallProgressBar.Value = _runtimeInstallProgress?.Percent ?? 0;
        RuntimeCancelUpdateButton.Visibility = _runtimeInstalling ? Visibility.Visible : Visibility.Collapsed;
        RuntimeCancelUpdateButton.IsEnabled = _runtimeInstalling && _runtimeInstallProgress?.CanCancel == true && _runtimeInstallCancellation?.IsCancellationRequested == false;
        RuntimeUpdateStatusText.Text = _runtimeInstalling ? _runtimeInstallProgress?.Message ?? "Preparing runtime update…"
            : _runtimeInstallMessage ?? (_checkingRuntimeUpdates ? "Checking for updates..." : _runtimeUpdateResult switch
        {
            { Status: PocketTtsRuntimeUpdateStatus.UpToDate } => "Up to date",
            { Status: PocketTtsRuntimeUpdateStatus.UpdateAvailable, LatestReleaseTag: { } tag } => $"Update available: {tag}",
            { Status: PocketTtsRuntimeUpdateStatus.NoCompatibleWindowsRuntimeAsset } => "No compatible Windows runtime available",
            _ => "Unable to check for updates",
        });
        string textStyle = "RuntimeNeutralTextStyle";
        string glyph = "\uE7BA";
        if (!_checkingRuntimeUpdates && !_runtimeInstalling && _runtimeInstallMessage is null)
        {
            switch (_runtimeUpdateResult?.Status)
            {
                case PocketTtsRuntimeUpdateStatus.UpToDate:
                    textStyle = "RuntimeSuccessTextStyle";
                    glyph = "\uE73E";
                    break;
                case PocketTtsRuntimeUpdateStatus.UpdateAvailable:
                    textStyle = "RuntimeAccentTextStyle";
                    glyph = "\uE72C";
                    break;
                case PocketTtsRuntimeUpdateStatus.NoCompatibleWindowsRuntimeAsset:
                    textStyle = "RuntimeWarningTextStyle";
                    break;
            }
        }
        RuntimeUpdateStatusText.Style = (Style)ModelSetupPanel.Resources[textStyle];
        RuntimeUpdateIcon.Glyph = glyph;
        RuntimeUpdateIcon.Visibility = _checkingRuntimeUpdates || _runtimeInstalling ? Visibility.Collapsed : Visibility.Visible;
        bool installableUpdate = _runtimeUpdateResult is { Status: PocketTtsRuntimeUpdateStatus.UpdateAvailable, Asset: not null };
        RuntimeUpdateButton.Content = _runtimeInstalling ? "Updating…" : installableUpdate ? "Update" : "Check for updates";
        RuntimeUpdateButton.IsEnabled = !_checkingRuntimeUpdates && !RuntimeUpdateBusy && !_closing &&
            (RuntimeUpdateCheck is not null || _runtimeUpdateResult is not null || _runtimeInstallMessage is not null) &&
            (!installableUpdate || (!ModelBusy && !ModelUpdateBlocksChanges));
        ToolTipService.SetToolTip(RuntimeUpdateButton, installableUpdate
            ? "Confirm, download, and verify this runtime update."
            : "Check for a newer Pocket TTS Runtime release.");
    }

    private void RenderRuntimeReadiness(bool? ready)
    {
        string state = ready switch { true => "Success", false => "Error", null => "Neutral" };
        RuntimeReadinessIndicator.Style = (Style)ModelSetupPanel.Resources[$"Runtime{state}IndicatorStyle"];
        string description = ready switch
        {
            true => "Runtime ready",
            false => string.IsNullOrWhiteSpace(ModelSetupStatusText.Text) ? "Runtime verification failed" : ModelSetupStatusText.Text,
            null => "Runtime status unknown (checking)",
        };
        ToolTipService.SetToolTip(RuntimeReadinessIndicator, description);
        AutomationProperties.SetName(RuntimeReadinessIndicator, description);
    }

    private async Task RefreshInstalledRuntimeReleaseAsync()
    {
        try
        {
            var identity = await Task.Run(() => PocketTtsRuntimeValidation.ReadVerifiedRuntimeIdentityAsync(_setupCancellation.Token));
            if (!_closing) { RuntimeVersionText.Text = $"Installed release: {identity.ReleaseVersion}"; }
        }
        catch (Exception)
        {
            if (!_closing) { RuntimeVersionText.Text = "Installed release unavailable"; }
        }
    }
}
