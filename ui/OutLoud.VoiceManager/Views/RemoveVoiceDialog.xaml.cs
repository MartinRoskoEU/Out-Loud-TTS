using System.Windows;
using OutLoud.VoiceManager.Models;

namespace OutLoud.VoiceManager.Views;

public partial class RemoveVoiceDialog : Window
{
    public RemoveVoiceDialog(RemovalDialogModel model)
    {
        InitializeComponent(); DataContext = model;
    }
    private void Confirm(object sender, RoutedEventArgs e) => DialogResult = true;
}
