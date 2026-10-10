using System.Threading.Tasks;
using System.Windows;
using Microsoft.Win32;
using OutLoud.VoiceManager.Models;

namespace OutLoud.VoiceManager.Services;

public sealed class VoiceExportDialog : IVoiceExportDialog
{
    private readonly IVoiceExportService exporter;
    public VoiceExportDialog(IVoiceExportService exporter) => this.exporter = exporter;
    public async Task<VoiceExportRequest?> ChooseAsync(VoiceRecord voice)
    {
        var owner = Application.Current.MainWindow;
        var picker = new SaveFileDialog { Title = "Export Out Loud voice", Filter = "Out Loud Voice (*.olv)|*.olv",
            DefaultExt = ".olv", AddExtension = true, CheckPathExists = true,
            // Confirm only AFTER native inspection, binding consent to that fingerprint.
            OverwritePrompt = false, FileName = VoiceFileSelection.ExportFileName(voice.NameText) };
        if (picker.ShowDialog(owner) != true) return null;
        if (!VoiceFileSelection.IsOlv(picker.FileName)) throw new BackendException(new BackendError("frontend", "destination_io", "The exported voice filename must end in .olv."));
        string identity = await exporter.InspectDestinationAsync(picker.FileName);
        if (identity != "absent" && MessageBox.Show(owner, $"{picker.FileName}\n\nThis file already exists. Replace it?",
            "Confirm voice export", MessageBoxButton.YesNo, MessageBoxImage.Warning, MessageBoxResult.No) != MessageBoxResult.Yes) return null;
        return new(picker.FileName, identity);
    }
}
