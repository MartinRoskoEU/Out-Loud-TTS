using Microsoft.Win32.SafeHandles;
using System.ComponentModel;
using System.Diagnostics;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;

namespace OutLoud.WinUI.Services;

/// <summary>Durable fail-closed native lifecycle; job closure terminates assigned descendants even after an app crash.</summary>
internal sealed class PocketTtsNativeOperation : IAsyncDisposable
{
    private sealed record Marker(string JobName, bool Assigned);
    private static string Root => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "Temp", "OutLoud-Operations");
    internal static void ValidatePath(string path)
    {
        string full = Path.GetFullPath(path);
        if (!string.Equals(full, Root, StringComparison.OrdinalIgnoreCase) && !PocketTtsRuntimeStore.IsWithin(full, Root))
            throw new InvalidDataException("Native operation metadata must remain in private temporary storage.");
        for (string? current = full; current is not null; current = Path.GetDirectoryName(current))
        {
            try
            {
                if ((File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0)
                    throw new InvalidDataException("Native operation storage cannot use directory links.");
            }
            catch (FileNotFoundException) { }
            catch (DirectoryNotFoundException) { }
        }
    }
    private readonly string _path;
    private readonly string _jobName;
    private readonly SafeFileHandle _job;
    private readonly FileStream _marker;

    internal PocketTtsNativeOperation()
    {
        ValidatePath(Root);
        Directory.CreateDirectory(Root);
        ValidatePath(Root);
        string id = Guid.NewGuid().ToString("N");
        _path = Path.Combine(Root, id + ".json");
        _jobName = "Global\\OutLoud.WinUI-" + id;
        _job = CreateJobObject(IntPtr.Zero, _jobName);
        if (_job.IsInvalid) { throw new IOException("Native process coordination could not start.", new Win32Exception(Marshal.GetLastWin32Error())); }
        try
        {
            var limits = new ExtendedLimits { Basic = new BasicLimits { LimitFlags = 0x2000 } }; // KILL_ON_JOB_CLOSE
            if (!SetInformationJobObject(_job, 9, ref limits, (uint)Marshal.SizeOf<ExtendedLimits>()))
            { throw new IOException("Native process containment is unavailable.", new Win32Exception(Marshal.GetLastWin32Error())); }
            _marker = new FileStream(_path, FileMode.CreateNew, FileAccess.ReadWrite, FileShare.None);
            try { PocketTtsModelStore.ValidateFile(_path, _marker.SafeFileHandle, nativeOperation: true); WriteMarker(); }
            catch { _marker.Dispose(); throw; }
        }
        catch { _job.Dispose(); throw; }
    }

    internal sealed record Running(Process Process, StreamReader Output, StreamReader Error) : IDisposable
    {
        public void Dispose() { Process.Dispose(); Output.Dispose(); Error.Dispose(); }
    }

    // JOB_LIST attaches the process atomically at creation, before any native code or child can run.
    // HANDLE_LIST inherits only the three redirected pipes, never the job or application locks.
    internal Running Start(ProcessStartInfo start)
    {
        var output = new AnonymousPipeServerStream(PipeDirection.In, HandleInheritability.Inheritable);
        var error = new AnonymousPipeServerStream(PipeDirection.In, HandleInheritability.Inheritable);
        using var input = new AnonymousPipeServerStream(PipeDirection.Out, HandleInheritability.Inheritable);
        IntPtr attributes = IntPtr.Zero, jobs = IntPtr.Zero, handles = IntPtr.Zero, environment = IntPtr.Zero;
        bool initialized = false;
        Process? process = null;
        try
        {
            nuint size = 0;
            _ = InitializeProcThreadAttributeList(IntPtr.Zero, 2, 0, ref size);
            attributes = Marshal.AllocHGlobal(checked((int)size));
            if (!InitializeProcThreadAttributeList(attributes, 2, 0, ref size)) { throw new Win32Exception(Marshal.GetLastWin32Error()); }
            initialized = true;
            jobs = Marshal.AllocHGlobal(IntPtr.Size);
            Marshal.WriteIntPtr(jobs, _job.DangerousGetHandle());
            handles = Marshal.AllocHGlobal(3 * IntPtr.Size);
            Marshal.WriteIntPtr(handles, 0, input.ClientSafePipeHandle.DangerousGetHandle());
            Marshal.WriteIntPtr(handles, IntPtr.Size, output.ClientSafePipeHandle.DangerousGetHandle());
            Marshal.WriteIntPtr(handles, 2 * IntPtr.Size, error.ClientSafePipeHandle.DangerousGetHandle());
            if (!UpdateProcThreadAttribute(attributes, 0, 0x2000D, jobs, (nuint)IntPtr.Size, IntPtr.Zero, IntPtr.Zero) ||
                !UpdateProcThreadAttribute(attributes, 0, 0x20002, handles, (nuint)(3 * IntPtr.Size), IntPtr.Zero, IntPtr.Zero))
            { throw new Win32Exception(Marshal.GetLastWin32Error()); }
            var startup = new StartupInfoEx { Attributes = attributes, Startup = new StartupInfo
            {
                Size = (uint)Marshal.SizeOf<StartupInfoEx>(), Flags = 0x100,
                Input = input.ClientSafePipeHandle.DangerousGetHandle(), Output = output.ClientSafePipeHandle.DangerousGetHandle(), Error = error.ClientSafePipeHandle.DangerousGetHandle(),
            } };
            var command = new StringBuilder(string.Join(' ', new[] { start.FileName }.Concat(start.ArgumentList).Select(Quote)));
            environment = Marshal.StringToHGlobalUni(string.Join('\0', start.Environment.Where(pair => pair.Value is not null)
                .OrderBy(pair => pair.Key, StringComparer.OrdinalIgnoreCase).Select(pair => pair.Key + "=" + pair.Value)) + "\0\0");
            if (!CreateProcess(start.FileName, command, IntPtr.Zero, IntPtr.Zero, true,
                0x08080404, environment, start.WorkingDirectory, ref startup, out var information))
            { throw new Win32Exception(Marshal.GetLastWin32Error()); }
            using var processHandle = new SafeProcessHandle(information.Process, true);
            using var threadHandle = new SafeWaitHandle(information.Thread, true);
            process = Process.GetProcessById((int)information.ProcessId);
            _ = process.SafeHandle; // Bind the managed process while its primary thread is suspended.
            input.DisposeLocalCopyOfClientHandle(); output.DisposeLocalCopyOfClientHandle(); error.DisposeLocalCopyOfClientHandle();
            if (ResumeThread(threadHandle) == uint.MaxValue) { throw new Win32Exception(Marshal.GetLastWin32Error()); }
            return new(process, new StreamReader(output, Encoding.UTF8), new StreamReader(error, Encoding.UTF8));
        }
        catch { process?.Dispose(); output.Dispose(); error.Dispose(); throw; }
        finally
        {
            if (initialized) { DeleteProcThreadAttributeList(attributes); }
            if (attributes != IntPtr.Zero) { Marshal.FreeHGlobal(attributes); }
            if (jobs != IntPtr.Zero) { Marshal.FreeHGlobal(jobs); }
            if (handles != IntPtr.Zero) { Marshal.FreeHGlobal(handles); }
            if (environment != IntPtr.Zero) { Marshal.FreeHGlobal(environment); }
        }
    }

    private static string Quote(string value)
    {
        if (value.Contains('\0')) { throw new ArgumentException("Native arguments cannot contain a null character."); }
        var quoted = new StringBuilder("\"");
        int slashes = 0;
        foreach (char character in value)
        {
            if (character == '\\') { slashes++; continue; }
            quoted.Append('\\', character == '"' ? slashes * 2 + 1 : slashes).Append(character);
            slashes = 0;
        }
        return quoted.Append('\\', slashes * 2).Append('"').ToString();
    }

    private void WriteMarker()
    {
        _marker.Position = 0;
        JsonSerializer.Serialize(_marker, new Marker(_jobName, true));
        _marker.SetLength(_marker.Position);
        _marker.Flush(flushToDisk: true);
    }

    internal async Task StopProcessesAsync()
    {
        if (ActiveProcesses(_job) != 0 && !TerminateJobObject(_job, 1))
        { throw new InvalidOperationException("Native process termination is unresolved. Model removal remains blocked."); }
        var deadline = Stopwatch.StartNew();
        while (ActiveProcesses(_job) != 0)
        {
            if (deadline.Elapsed > TimeSpan.FromSeconds(5))
            { throw new InvalidOperationException("Native child processes are still active. Model removal remains blocked."); }
            await Task.Delay(50).ConfigureAwait(false);
        }
    }

    public async ValueTask DisposeAsync()
    {
        bool safe = false;
        try
        {
            await StopProcessesAsync().ConfigureAwait(false);
            safe = true;
        }
        finally
        {
            _job.Dispose();
            _marker.Dispose();
            // Never clear unconfirmed process-tree termination.
            if (safe) { PocketTtsModelStore.DeleteRegularFile(_path, nativeOperation: true); }
        }
    }

    // Caller owns the exclusive model operation lease. A Starting/malformed marker is never guessed safe.
    internal static async Task EnsureQuiescentAsync(CancellationToken cancellationToken)
    {
        ValidatePath(Root);
        if (Directory.Exists(Root))
        {
            foreach (string path in Directory.EnumerateFileSystemEntries(Root))
            {
                cancellationToken.ThrowIfCancellationRequested();
                ValidatePath(path);
                string id = Path.GetFileNameWithoutExtension(path);
                if (Path.GetExtension(path) != ".json" || !Guid.TryParseExact(id, "N", out _))
                { throw new InvalidDataException("Unexpected native operation metadata. Removal is blocked pending reconciliation."); }
                await using (var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read, 4096, FileOptions.Asynchronous))
                {
                    PocketTtsModelStore.ValidateFile(path, stream.SafeFileHandle, nativeOperation: true);
                    if (stream.Length > 16384) { throw new InvalidDataException("Invalid native operation metadata."); }
                    var marker = await JsonSerializer.DeserializeAsync<Marker>(stream, cancellationToken: cancellationToken).ConfigureAwait(false);
                    if (marker is null || !marker.Assigned || marker.JobName != "Global\\OutLoud.WinUI-" + id)
                    { throw new InvalidOperationException("A native process start or child cleanup is unresolved. Model removal is blocked; reconcile the native operation before retrying."); }
                    using var job = OpenJobObject(4, false, marker.JobName);
                    if (job.IsInvalid)
                    {
                        if (Marshal.GetLastWin32Error() != 2)
                        { throw new InvalidOperationException("A native operation's termination cannot be established. Removal is blocked."); }
                    }
                    else if (ActiveProcesses(job) != 0)
                    { throw new InvalidOperationException("A native operation or descendant is still running. Wait for it to stop before removal."); }
                }
                PocketTtsModelStore.DeleteRegularFile(path, nativeOperation: true);
            }
        }
        // Also refuse an independently launched upstream CLI, which does not participate in our locks.
        foreach (var process in Process.GetProcessesByName("pocket-tts"))
        {
            using (process)
            {
                try { if (!process.HasExited && process.MainModule?.FileName is { } executable && (PocketTtsRuntimeStore.IsWithin(executable, PocketTtsRuntimeStore.LocalDataPath) || string.Equals(executable, PocketTtsRuntimeStore.BundledPath, StringComparison.OrdinalIgnoreCase))) { throw new InvalidOperationException("An Out Loud Pocket TTS process is running. Stop it before model removal."); } }
                catch (Win32Exception exception) { throw new InvalidOperationException("Native process state could not be verified. Removal is blocked.", exception); }
            }
        }
    }

    private static uint ActiveProcesses(SafeFileHandle job)
    {
        if (!QueryInformationJobObject(job, 1, out var accounting, (uint)Marshal.SizeOf<Accounting>(), IntPtr.Zero))
        { throw new InvalidOperationException("Native child process state could not be verified."); }
        return accounting.ActiveProcesses;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct BasicLimits
    {
        public long PerProcessUserTime, PerJobUserTime;
        public uint LimitFlags;
        public UIntPtr MinimumWorkingSet, MaximumWorkingSet;
        public uint ActiveProcessLimit;
        public UIntPtr Affinity;
        public uint PriorityClass, SchedulingClass;
    }
    [StructLayout(LayoutKind.Sequential)]
    private struct IoCounters { public ulong ReadOperations, WriteOperations, OtherOperations, ReadBytes, WriteBytes, OtherBytes; }
    [StructLayout(LayoutKind.Sequential)]
    private struct ExtendedLimits
    {
        public BasicLimits Basic;
        public IoCounters Io;
        public UIntPtr ProcessMemory, JobMemory, PeakProcessMemory, PeakJobMemory;
    }
    [StructLayout(LayoutKind.Sequential)]
    private struct Accounting
    {
        public long UserTime, KernelTime, PeriodUserTime, PeriodKernelTime;
        public uint PageFaults, TotalProcesses, ActiveProcesses, TerminatedProcesses;
    }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern SafeFileHandle CreateJobObject(IntPtr attributes, string name);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern SafeFileHandle OpenJobObject(uint access, [MarshalAs(UnmanagedType.Bool)] bool inherit, string name);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool SetInformationJobObject(SafeFileHandle job, int infoClass, ref ExtendedLimits info, uint size);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool QueryInformationJobObject(SafeFileHandle job, int infoClass, out Accounting info, uint size, IntPtr returnedSize);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool TerminateJobObject(SafeFileHandle job, uint exitCode);

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct StartupInfo
    {
        public uint Size;
        public string? Reserved, Desktop, Title;
        public uint X, Y, Width, Height, XChars, YChars, Fill, Flags;
        public ushort Show, ReservedSize;
        public IntPtr ReservedData, Input, Output, Error;
    }
    [StructLayout(LayoutKind.Sequential)]
    private struct StartupInfoEx { public StartupInfo Startup; public IntPtr Attributes; }
    [StructLayout(LayoutKind.Sequential)]
    private struct ProcessInformation { public IntPtr Process, Thread; public uint ProcessId, ThreadId; }
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool InitializeProcThreadAttributeList(IntPtr list, int count, uint flags, ref nuint size);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool UpdateProcThreadAttribute(IntPtr list, uint flags, nuint attribute, IntPtr value, nuint size, IntPtr previous, IntPtr returnedSize);
    [DllImport("kernel32.dll")]
    private static extern void DeleteProcThreadAttributeList(IntPtr list);
    [DllImport("kernel32.dll", EntryPoint = "CreateProcessW", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool CreateProcess(string executable, StringBuilder command, IntPtr processAttributes, IntPtr threadAttributes,
        [MarshalAs(UnmanagedType.Bool)] bool inheritHandles, uint flags, IntPtr environment, string directory, ref StartupInfoEx startup, out ProcessInformation information);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern uint ResumeThread(SafeWaitHandle thread);
}
