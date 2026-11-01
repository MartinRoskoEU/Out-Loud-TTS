using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.Windows.Storage.Pickers;
using OutLoud.WinUI.Voices;
using OutLoud.WinUI.VoiceTesting;

namespace OutLoud.WinUI;

public sealed partial class VoiceTesterView : UserControl
{
    private readonly VoiceTesterViewModel _model;

    public VoiceTesterView()
    {
        InitializeComponent();
        _model = new(new SapiVoiceTestBackend(), new WaveExportDialog(this))
        {
            ReadModelState = () => (OwnerWindow as MainWindow)?.GetVoiceTestModelState() ?? VoiceTestModelState.Unknown
        };
        DataContext = _model;
        _model.PropertyChanged += (_, _) => RenderState();
        RenderState();
    }

    internal Window? OwnerWindow { get; set; }
    internal Task StopAsync() => _model.StopAsync();
    internal Task CloseAsync() => _model.CloseAsync();
    internal void RefreshModelState() => _model.RefreshModelState();
    internal void UpdateVoices(VoiceSnapshot snapshot, string? preferredId) => _model.UpdateVoices(snapshot, preferredId);

    private void RenderState()
    {
        TestProgress.Visibility = _model.IsBusy ? Visibility.Visible : Visibility.Collapsed;
        TestNotification.Visibility = _model.HasNotification ? Visibility.Visible : Visibility.Collapsed;
        DiagnosticExpander.Visibility = _model.HasDiagnostic ? Visibility.Visible : Visibility.Collapsed;
        TestReadinessHint.Visibility = _model.PlaybackHint.Length > 0 ? Visibility.Visible : Visibility.Collapsed;
        NotificationTitle.Foreground = (Brush)Application.Current.Resources[_model.Severity switch
        {
            NotificationSeverity.Success => "SystemFillColorSuccessBrush",
            NotificationSeverity.Warning => "SystemFillColorCautionBrush",
            NotificationSeverity.Error => "SystemFillColorCriticalBrush",
            _ => "TextFillColorSecondaryBrush"
        }];
    }

    private sealed class WaveExportDialog(VoiceTesterView owner) : IWaveExportDialog
    {
        public async Task<string?> ChoosePathAsync()
        {
            // Like the original save dialog, confirms overwrite before synthesis.
            // This path-only picker does not create or truncate the destination.
            var picker = new FileSavePicker(owner.OwnerWindow!.AppWindow.Id)
            {
                Title = "Export a new SAPI baseline synthesis", DefaultFileExtension = ".wav",
                SuggestedFileName = "out-loud-test.wav", ShowOverwritePrompt = true
            };
            picker.FileTypeChoices.Add("WAV audio", new List<string> { ".wav" });
            return (await picker.PickSaveFileAsync())?.Path;
        }
    }
}
