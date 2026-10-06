using System.Windows;
using System.Windows.Controls;
using OutLoud.VoiceManager.Models;

namespace OutLoud.VoiceManager.Views;

public partial class ImportVoiceDialog : Window
{
    public ImportDialogModel Model { get; }
    public ImportVoiceDialog(string source)
    {
        Model = new ImportDialogModel(source);
        InitializeComponent(); DataContext = Model;
        ConfirmButton.IsEnabled = Model.CanConfirm;
        Loaded += (_, _) => { NameInput.Focus(); NameInput.SelectAll(); };
    }
    private void NameChanged(object sender, TextChangedEventArgs e)
    {
        if (ConfirmButton == null) return;
        Model.DisplayName = NameInput.Text; ConfirmButton.IsEnabled = Model.CanConfirm;
    }
    private void Confirm(object sender, RoutedEventArgs e) { if (Model.CanConfirm) DialogResult = true; }
}
