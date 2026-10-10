using System.Threading.Tasks;
using OutLoud.VoiceManager.Models;

namespace OutLoud.VoiceManager.Services;

public interface IVoiceExportService
{
    Task<string> InspectDestinationAsync(string path);
    Task ExportAsync(VoiceRecord voice, VoiceExportRequest request);
}
public interface IVoiceExportDialog
{
    Task<VoiceExportRequest?> ChooseAsync(VoiceRecord voice);
}
