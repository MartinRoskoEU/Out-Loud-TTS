using System.Threading.Tasks;
using OutLoud.VoiceManager.Models;
using OutLoud.VoiceManager.NativeInterop;

namespace OutLoud.VoiceManager.Services;

public sealed class NativeVoiceManagerService : IVoiceManagerService
{
    // Synchronous native backend; every call has its own result, copied/freed
    // on this worker. No native pointer or context is retained by the UI.
    public Task<VoiceSnapshot> ReadAsync() => Task.Run(NativeVoiceManager.Read);
}
