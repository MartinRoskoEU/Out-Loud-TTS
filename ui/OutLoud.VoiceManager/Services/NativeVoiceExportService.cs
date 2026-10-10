using System.Threading.Tasks;
using OutLoud.VoiceManager.Models;
using OutLoud.VoiceManager.NativeInterop;

namespace OutLoud.VoiceManager.Services;

public sealed class NativeVoiceExportService : IVoiceExportService
{
    public Task<string> InspectDestinationAsync(string path) => Task.Run(() => NativeVoiceManager.InspectExportDestination(path));
    public Task ExportAsync(VoiceRecord voice, VoiceExportRequest request) => Task.Run(() => NativeVoiceManager.Export(voice, request));
}
