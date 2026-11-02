using Microsoft.UI.Xaml;
using OutLoud.WinUI.Services;

namespace OutLoud.WinUI;

public sealed partial class SettingsView
{
    private void RenderModelInstallationProgress(PocketTtsModelInstallProgress progress)
    {
        if (_modelInstallationResultShown) { return; }
        ModelInstallationStatusText.Text = progress.Message;
        ModelInstallationProgress.Visibility = Visibility.Visible;
        ModelInstallationProgress.IsIndeterminate = progress.Percent is null;
        ModelInstallationProgress.Value = progress.Percent ?? 0;
        CancelModelInstallationButton.Visibility = Visibility.Visible;
        CancelModelInstallationButton.IsEnabled = progress.CanCancel && _modelCancellation?.IsCancellationRequested == false;
    }

    private void ShowModelInstallationCard(string alias)
    {
        _modelInstallationResultShown = false;
        CancelModelInstallationButton.Content = "Cancel installation";
        ModelInstallationTitleText.Text = "Installing model";
        ModelInstallationTitleText.Style = (Style)ModelInstallationPanel.Resources["InstallationTitleStyle"];
        ModelInstallationModelText.Text = alias;
        ModelInstallationModelText.Visibility = Visibility.Visible;
        ModelInstallationResultIcon.Visibility = Visibility.Collapsed;
        ModelInstallationHeading.ColumnSpacing = 0;
        ModelInstallationHeading.MinHeight = 0;
        DismissModelInstallationButton.Visibility = Visibility.Collapsed;
        ModelInstallationPanel.Visibility = Visibility.Visible;
        RevealModelInstallationCard();
    }

    private void RenderModelInstallationResult(string alias, PocketTtsModelInstallResult result, bool updating = false)
    {
        _modelInstallationResultShown = true;
        bool cancelled = !result.Succeeded && result.Failure == PocketTtsModelInstallFailure.Cancelled;
        ModelInstallationTitleText.Text = result.Succeeded ? "Installation completed" : cancelled ? "Installation cancelled" : "Installation failed";
        if (updating) ModelInstallationTitleText.Text = result.Succeeded ? "Update completed" : cancelled ? "Update cancelled" : "Update failed";
        string titleStyle = result.Succeeded ? "InstallationSuccessTitleStyle" : cancelled ? "InstallationNeutralTitleStyle" : "InstallationErrorTitleStyle";
        ModelInstallationTitleText.Style = (Style)ModelInstallationPanel.Resources[titleStyle];
        ModelInstallationResultIcon.Glyph = result.Succeeded ? "\uE73E" : cancelled ? "\uE711" : "\uEA39";
        ModelInstallationResultIcon.Visibility = Visibility.Visible;
        ModelInstallationHeading.ColumnSpacing = 10;
        ModelInstallationHeading.MinHeight = 32;
        ModelInstallationModelText.Visibility = result.Succeeded ? Visibility.Collapsed : Visibility.Visible;
        ModelInstallationStatusText.Text = updating || !result.Succeeded ? result.Message : $"{alias} was installed and verified.";
        if (result.CleanupPending)
        {
            ModelInstallationTitleText.Text = updating ? "Update completed; cleanup pending" : "Installation completed; cleanup pending";
            ModelInstallationTitleText.Style = (Style)ModelInstallationPanel.Resources["InstallationNeutralTitleStyle"];
            ModelInstallationResultIcon.Glyph = "\uE7BA";
        }
        ModelInstallationProgress.Visibility = Visibility.Collapsed;
        CancelModelInstallationButton.Visibility = Visibility.Collapsed;
        DismissModelInstallationButton.Visibility = Visibility.Visible;
        RevealModelInstallationCard();
    }

    private void DismissModelInstallation_Click(object sender, RoutedEventArgs args)
    {
        if (_modelInstallationResultShown) { ModelInstallationPanel.Visibility = Visibility.Collapsed; }
    }

    private void RevealModelInstallationCard() => DispatcherQueue.TryEnqueue(() =>
    {
        if (_closing || !IsLoaded || Visibility != Visibility.Visible || ModelInstallationPanel.Visibility != Visibility.Visible) { return; }
        ModelInstallationPanel.UpdateLayout();
        ModelInstallationPanel.StartBringIntoView(new BringIntoViewOptions { AnimationDesired = false, VerticalAlignmentRatio = 0 });
    });

}
