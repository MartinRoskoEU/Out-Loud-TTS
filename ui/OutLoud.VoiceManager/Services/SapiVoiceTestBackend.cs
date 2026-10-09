using System;
using System.IO;
using System.Runtime.ExceptionServices;
using System.Threading;
using System.Threading.Tasks;
using OutLoud.VoiceManager.Models;

namespace OutLoud.VoiceManager.Services;

public sealed class SapiVoiceTestBackend : IVoiceTestBackend
{
    private readonly ISapiTestSessionFactory factory;
    private int busy;
    public SapiVoiceTestBackend(ISapiTestSessionFactory? factory = null) => this.factory = factory ?? new SapiTestSessionFactory();
    public string Name => "Current SAPI Engine / Baseline";
    public async Task<VoiceTestOutcome> RunAsync(VoiceTestRequest request, IProgress<VoiceTestState> progress, CancellationToken cancellation)
    {
        if (request.ValidationError is { } validation) throw new VoiceTestException(new("invalid_test", validation));
        if (Interlocked.CompareExchange(ref busy, 1, 0) != 0) throw new VoiceTestException(new("tester_busy", "Wait for the active test to finish stopping."));
        try {
            // The thread-pool task joins the STA; no UI-thread join, COM sharing,
            // detached worker, forced termination, or task completion before teardown.
            return await Task.Run(() => {
                VoiceTestOutcome? outcome = null; Exception? failure = null;
                var thread = new Thread(() => {
                    try { outcome = Run(request, progress, cancellation); } catch (Exception error) { failure = error; }
                }) { Name = "Out Loud SAPI test", IsBackground = false };
                thread.SetApartmentState(ApartmentState.STA); thread.Start(); thread.Join();
                if (failure != null) ExceptionDispatchInfo.Capture(failure).Throw();
                return outcome!;
            }).ConfigureAwait(false);
        } finally { Volatile.Write(ref busy, 0); }
    }
    private VoiceTestOutcome Run(VoiceTestRequest request, IProgress<VoiceTestState> progress, CancellationToken cancellation)
    {
        string? temporary = null; string? destination = null;
        try {
            if (cancellation.IsCancellationRequested) return new(true);
            progress.Report(VoiceTestState.Preparing);
            if (request.ExportPath != null) {
                destination = Path.GetFullPath(request.ExportPath);
                if (!string.Equals(Path.GetExtension(destination), ".wav", StringComparison.OrdinalIgnoreCase)) throw new IOException("Select a WAV destination.");
                temporary = Path.Combine(Path.GetDirectoryName(destination)!, ".outloud-test-" + Guid.NewGuid().ToString("N") + ".wav");
            }
            using (ISapiTestSession session = factory.Create(request, temporary)) {
                if (cancellation.IsCancellationRequested) return new(true);
                session.Start(request.Text); // Exactly one unmodified plain-text request.
                progress.Report(destination == null ? VoiceTestState.Speaking : VoiceTestState.Exporting);
                bool purged = false;
                while (true) {
                    if (cancellation.IsCancellationRequested && !purged) { session.Purge(); purged = true; }
                    if (session.WaitUntilDone(20)) break;
                }
                int result = session.LastResult();
                if (result < 0) throw new VoiceTestException(new("sapi_synthesis_failed", "Windows SAPI reported a synthesis or output failure.", result, "completion"));
            } // Close file stream and release all COM objects BEFORE publication/completion.
            if (cancellation.IsCancellationRequested) return new(true);
            if (destination != null) {
                WaveFile.Validate(temporary!);
                if (cancellation.IsCancellationRequested) return new(true);
                File.Move(temporary!, destination, overwrite: true); temporary = null;
            }
            return new(false, destination);
        }
        catch (VoiceTestException) { throw; }
        catch (Exception error) { throw new VoiceTestException(new("test_io_or_initialization_failed", "The SAPI test or WAV file operation could not finish.", error.HResult, error.GetType().Name, destination ?? "")); }
        finally {
            if (temporary != null && File.Exists(temporary)) {
                try { File.Delete(temporary); }
                catch (Exception error) { throw new VoiceTestException(new("export_cleanup_failed", "The incomplete WAV could not be removed. See the retained temporary path.", error.HResult, "cleanup", temporary)); }
            }
        }
    }
}

public static class WaveFile
{
    // Validate SAPI's actual output instead of assuming its requested format won.
    public static void Validate(string path)
    {
        using var reader = new BinaryReader(File.OpenRead(path)); var stream = reader.BaseStream;
        if (stream.Length < 12 || reader.ReadUInt32() != 0x46464952) throw new InvalidDataException("Missing RIFF WAV header.");
        long end = reader.ReadUInt32() + 8L;
        if (end != stream.Length || reader.ReadUInt32() != 0x45564157) throw new InvalidDataException("Invalid WAV length/type.");
        bool format = false, data = false;
        while (stream.Position + 8 <= end) {
            uint chunk = reader.ReadUInt32(), size = reader.ReadUInt32(); long next = stream.Position + size + (size & 1);
            if (next > end) throw new InvalidDataException("Truncated WAV chunk.");
            if (chunk == 0x20746d66) {
                if (format || size < 16 || reader.ReadUInt16() != 1 || reader.ReadUInt16() != 1 || reader.ReadUInt32() != 24000 ||
                    reader.ReadUInt32() != 48000 || reader.ReadUInt16() != 2 || reader.ReadUInt16() != 16) throw new InvalidDataException("Expected PCM16 mono 24 kHz WAV.");
                format = true;
            }
            if (chunk == 0x61746164) { if (data || size == 0 || size % 2 != 0) throw new InvalidDataException("Invalid PCM data."); data = true; }
            stream.Position = next;
        }
        if (!format || !data || stream.Position != end) throw new InvalidDataException("Incomplete WAV.");
    }
}
