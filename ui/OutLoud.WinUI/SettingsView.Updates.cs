using Microsoft.UI.Xaml;

namespace OutLoud.WinUI;

public sealed partial class SettingsView
{
    private bool _checkingUpdates;

    private async void CheckUpdates_Click(object sender, RoutedEventArgs args) => await CheckUpdatesAsync();

    private async Task CheckUpdatesAsync()
    {
        if (_closing || _checkingUpdates || _checkingApplicationUpdates || _checkingRuntimeUpdates || _checkingModelUpdates || ModelBusy || RuntimeUpdateBusy) return;
        _checkingUpdates = true;
        RenderModelInstallationConflicts();
        try
        {
            await CheckApplicationUpdatesAsync();
            if (_closing) return;
            // Each existing checker reports its own unavailable/error state. A failed runtime
            // comparison therefore still allows the model check. Neither action installs updates.
            await CheckRuntimeUpdatesAsync(forceRefresh: true);
            if (!_closing) await CheckInstalledModelUpdatesAsync();
        }
        finally
        {
            _checkingUpdates = false;
            if (!_closing) RenderModelInstallationConflicts();
        }
    }

    private void RenderUpdateCheckState()
    {
        bool checking = _checkingUpdates || _checkingApplicationUpdates || _checkingRuntimeUpdates || _checkingModelUpdates;
        CheckUpdatesButton.IsEnabled = !checking && !ModelBusy && !RuntimeUpdateBusy && !_closing;
        UpdateChecksProgress.IsActive = checking && !_closing;
        UpdateChecksProgress.Visibility = checking && !_closing ? Visibility.Visible : Visibility.Collapsed;
    }
}
