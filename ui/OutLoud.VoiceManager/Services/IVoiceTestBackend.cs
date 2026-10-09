using System;
using System.Threading;
using System.Threading.Tasks;
using OutLoud.VoiceManager.Models;

namespace OutLoud.VoiceManager.Services;

// Future experiments implement this boundary explicitly; only SAPI baseline exists now.
public interface IVoiceTestBackend
{
    string Name { get; }
    // Completion, including cancellation/failure, must follow playback shutdown
    // and resource cleanup so library mutations can safely resume.
    Task<VoiceTestOutcome> RunAsync(VoiceTestRequest request, IProgress<VoiceTestState> progress, CancellationToken cancellation);
}
public interface IWaveExportDialog { Task<string?> ChoosePathAsync(); }
// Private-to-app COM seam: objects never leave their owning STA thread.
public interface ISapiTestSession : IDisposable
{
    void Start(string text);
    void Purge();
    bool WaitUntilDone(int milliseconds);
    int LastResult();
}
public interface ISapiTestSessionFactory
{
    ISapiTestSession Create(VoiceTestRequest request, string? temporaryWavePath);
}
