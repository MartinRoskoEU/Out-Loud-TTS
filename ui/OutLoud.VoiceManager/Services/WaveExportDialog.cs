using System.Threading.Tasks;
using System.Windows;
using Microsoft.Win32;

namespace OutLoud.VoiceManager.Services;

public sealed class WaveExportDialog : IWaveExportDialog
{
    public Task<string?> ChoosePathAsync()
    {
        var dialog = new SaveFileDialog { Title = "Export a new SAPI baseline synthesis", Filter = "WAV audio (*.wav)|*.wav", DefaultExt = ".wav", AddExtension = true, OverwritePrompt = true, FileName = "out-loud-test.wav" };
        return Task.FromResult(dialog.ShowDialog(Application.Current.MainWindow) == true ? dialog.FileName : null);
    }
}
