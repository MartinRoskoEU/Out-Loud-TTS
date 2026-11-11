using Microsoft.UI.Xaml;
using OutLoud.WinUI.Services;

namespace OutLoud.WinUI;

public sealed partial class SettingsView
{
    private bool _checkingApplicationUpdates;
    private Uri? _applicationReleaseUri;

    private async Task CheckApplicationUpdatesAsync()
    {
        if (_closing || _checkingApplicationUpdates) return;
        _checkingApplicationUpdates = true;
        ApplicationVersionText.Text = $"Installed version: {ApplicationInfo.Version}";
        ApplicationUpdateStatusText.Text = "Checking for updates…";
        ApplicationReleaseLink.Visibility = Visibility.Collapsed;
        _applicationReleaseUri = null;
        ApplicationUpdateProgress.IsActive = true;
        ApplicationUpdateProgress.Visibility = Visibility.Visible;
        ApplicationUpdateIcon.Visibility = Visibility.Collapsed;
        ApplicationUpdateStatusText.Style = (Style)ModelSetupPanel.Resources["RuntimeNeutralTextStyle"];
        RenderUpdateCheckState();
        try
        {
            var result = await new ApplicationUpdateService(_runtimeUpdateHttp).CheckAsync(_setupCancellation.Token);
            if (_closing) return;
            ApplicationUpdateStatusText.Text = result.Message;
            bool current = !result.Available && result.Message == "Up to date";
            ApplicationUpdateStatusText.Style = (Style)ModelSetupPanel.Resources[result.Available ? "RuntimeAccentTextStyle" : current ? "RuntimeSuccessTextStyle" : "RuntimeNeutralTextStyle"];
            ApplicationUpdateIcon.Glyph = result.Available ? "\uE72C" : current ? "\uE73E" : "\uE7BA";
            _applicationReleaseUri = result.ReleaseUrl;
            ApplicationReleaseLink.Visibility = result.Available ? Visibility.Visible : Visibility.Collapsed;
        }
        finally
        {
            _checkingApplicationUpdates = false;
            if (!_closing)
            {
                ApplicationUpdateProgress.IsActive = false;
                ApplicationUpdateProgress.Visibility = Visibility.Collapsed;
                ApplicationUpdateIcon.Visibility = Visibility.Visible;
                RenderUpdateCheckState();
            }
        }
    }

    private async void ApplicationReleaseLink_Click(object sender, RoutedEventArgs args)
    {
        if (_closing || _checkingApplicationUpdates || _applicationReleaseUri is null) return;
        try
        {
            if (!await Windows.System.Launcher.LaunchUriAsync(_applicationReleaseUri))
                ApplicationUpdateStatusText.Text = "Could not open the release page. Please retry.";
        }
        catch (Exception) { if (!_closing) ApplicationUpdateStatusText.Text = "Could not open the release page. Please retry."; }
    }

    private void ApplicationUpdateRow_SizeChanged(object sender, SizeChangedEventArgs args)
    {
        bool narrow = args.NewSize.Width < 480;
        Microsoft.UI.Xaml.Controls.Grid.SetColumnSpan(ApplicationUpdateStatusPanel, narrow ? 2 : 1);
        Microsoft.UI.Xaml.Controls.Grid.SetRow(ApplicationReleaseLink, narrow ? 1 : 0);
        Microsoft.UI.Xaml.Controls.Grid.SetColumn(ApplicationReleaseLink, narrow ? 0 : 1);
        Microsoft.UI.Xaml.Controls.Grid.SetColumnSpan(ApplicationReleaseLink, narrow ? 2 : 1);
        ApplicationReleaseLink.HorizontalAlignment = narrow ? HorizontalAlignment.Left : HorizontalAlignment.Right;
        ApplicationUpdateRow.RowSpacing = narrow ? 12 : 0;
    }
}
