using System.Windows;
using System.Windows.Controls;
using OutLoud.VoiceManager.Models;

namespace OutLoud.VoiceManager.Views;

public partial class ReplaceVoiceDialog : Window
{
    public ReplacementDialogModel Model { get; }
    public ReplaceVoiceDialog(ReplacementDialogModel model)
    {
        Model = model; InitializeComponent(); DataContext = Model; ReplaceButton.IsEnabled = Model.CanConfirm;
    }
    private void TargetChanged(object sender, SelectionChangedEventArgs e)
    {
        if (ReplaceButton == null) return;
        Model.SelectedTarget = Targets.SelectedItem as VoiceRecord; ReplaceButton.IsEnabled = Model.CanConfirm;
    }
    private void Confirm(object sender, RoutedEventArgs e) { if (Model.CanConfirm) DialogResult = true; }
}
