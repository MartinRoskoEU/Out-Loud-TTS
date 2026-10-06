using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Win32.SafeHandles;
using OutLoud.VoiceManager.Models;

namespace OutLoud.VoiceManager.Services;

public interface IImportHelperProcess : IDisposable { int Id { get; } Task WaitForExitAsync(); }
public interface IElevationLauncher { Task<IImportHelperProcess> LaunchAsync(string executable, string endpoint, int parentId); }
public sealed class ElevationLauncher : IElevationLauncher
{
    private sealed class HelperProcess : IImportHelperProcess
    {
        private readonly Process process;
        public HelperProcess(Process process) => this.process = process;
        public int Id => process.Id;
        public Task WaitForExitAsync() => process.WaitForExitAsync();
        public void Dispose() => process.Dispose();
    }
    public Task<IImportHelperProcess> LaunchAsync(string executable, string endpoint, int parentId) => Task.Run<IImportHelperProcess>(() =>
        new HelperProcess(Process.Start(new ProcessStartInfo
        {
            FileName = executable, UseShellExecute = true, Verb = "runas", WorkingDirectory = Environment.SystemDirectory,
            // Only generated ASCII endpoint and numeric PID. Source/name never enter the command line.
            Arguments = endpoint + " " + parentId.ToString(CultureInfo.InvariantCulture), WindowStyle = ProcessWindowStyle.Hidden
        }) ?? throw new IOException("Windows did not return the helper process.")));
}
public sealed class ElevatedVoiceImportService : IVoiceImportService
{
    private readonly IElevationLauncher launcher;
    public ElevatedVoiceImportService(IElevationLauncher? launcher = null) => this.launcher = launcher ?? new ElevationLauncher();
    public async Task<VoiceImportResult> ImportAsync(VoiceImportRequest request, IProgress<ImportPhase> progress)
    {
        bool submitted = false;
        try
        {
            byte[] payload = ImportProtocol.EncodeRequest(request);
            string helper = Path.Combine(AppContext.BaseDirectory, "OutLoudVoiceImportHelper.exe");
            if (!File.Exists(helper)) throw new FileNotFoundException("The voice import helper is missing.", helper);
            string endpoint = "OutLoud.Import." + Guid.NewGuid().ToString("N");
            using var pipe = CreatePipe(endpoint);
            progress.Report(ImportPhase.AwaitingElevation);
            using IImportHelperProcess process = await launcher.LaunchAsync(helper, endpoint, Environment.ProcessId);
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(20));
            await pipe.WaitForConnectionAsync(timeout.Token);
            if (!GetNamedPipeClientProcessId(pipe.SafePipeHandle, out uint client) || client != process.Id)
                throw new IOException("Unexpected import helper connection.");
            submitted = true; // Conservatively reconcile even if request delivery becomes uncertain.
            await pipe.WriteAsync(payload, timeout.Token);
            byte[] frame = await ImportProtocol.ReadFrameAsync(pipe, timeout.Token);
            if (ImportProtocol.ValidateHeader(frame[..16]) == 4)
            {
                if (frame.Length != 16) throw new InvalidDataException("Invalid helper start notification.");
                progress.Report(ImportPhase.Importing);
                // Native work has no forced timeout/termination: never kill an active mutation.
                frame = await ImportProtocol.ReadFrameAsync(pipe, CancellationToken.None);
            }
            VoiceImportResult result = ImportProtocol.DecodeResult(frame);
            if (result.Operation != request.Operation && result.Error?.Code != "invalid_request") throw new InvalidDataException("Helper returned the wrong operation outcome.");
            if (result.Error?.Code == "invalid_request") result = result with { Operation = request.Operation, LibraryId = request.TargetId };
            await process.WaitForExitAsync();
            return result;
        }
        catch (Win32Exception error) when (error.NativeErrorCode == 1223 && !submitted) { return new(ImportPhase.Cancelled, Operation: request.Operation); }
        catch (Exception error)
        {
            return new(ImportPhase.Failed, Error: new BackendError("helper", submitted ? "outcome_unknown" : "launch_or_transport_failed",
                submitted ? "Communication with the helper failed. Refresh to check whether voice data changed; do not immediately retry." :
                "Could not start or communicate with the voice import helper.", LibraryCode: error.GetType().Name,
                SystemError: error is Win32Exception win32 ? (uint)win32.NativeErrorCode : 0), Operation: request.Operation);
        }
    }
    [StructLayout(LayoutKind.Sequential)] private struct SecurityAttributes { public int Length; public IntPtr Descriptor; public int Inherit; }
    private static NamedPipeServerStream CreatePipe(string endpoint)
    {
        string sid = WindowsIdentity.GetCurrent().User?.Value ?? throw new IOException("Unable to identify the caller.");
        // Exact caller + Administrators (including over-the-shoulder UAC) + SYSTEM.
        if (!ConvertStringSecurityDescriptorToSecurityDescriptor("D:P(A;;GA;;;" + sid + ")(A;;GA;;;BA)(A;;GA;;;SY)", 1, out IntPtr descriptor, out _))
            throw new Win32Exception(Marshal.GetLastWin32Error());
        try
        {
            var attributes = new SecurityAttributes { Length = Marshal.SizeOf<SecurityAttributes>(), Descriptor = descriptor };
            SafePipeHandle handle = CreateNamedPipe("\\\\.\\pipe\\" + endpoint, 3 | 0x40000000 | 0x80000, 8, 1,
                ImportProtocol.Limit, ImportProtocol.Limit, 0, ref attributes); // byte mode, reject remote, first instance
            if (handle.IsInvalid) { int error = Marshal.GetLastWin32Error(); handle.Dispose(); throw new Win32Exception(error); }
            try { return new NamedPipeServerStream(PipeDirection.InOut, true, false, handle); }
            catch { handle.Dispose(); throw; }
        }
        finally { LocalFree(descriptor); }
    }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    private static extern SafePipeHandle CreateNamedPipe(string name, uint openMode, uint pipeMode, uint instances, int output, int input, uint timeout, ref SecurityAttributes attributes);
    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    private static extern bool ConvertStringSecurityDescriptorToSecurityDescriptor(string text, uint revision, out IntPtr descriptor, out uint size);
    [DllImport("kernel32.dll")][DefaultDllImportSearchPaths(DllImportSearchPath.System32)] private static extern IntPtr LocalFree(IntPtr pointer);
    [DllImport("kernel32.dll", SetLastError = true)][DefaultDllImportSearchPaths(DllImportSearchPath.System32)] private static extern bool GetNamedPipeClientProcessId(SafePipeHandle pipe, out uint process);
}
