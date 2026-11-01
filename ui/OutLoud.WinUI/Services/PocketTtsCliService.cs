using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.ExceptionServices;
using System.Runtime.InteropServices;
using System.Runtime.Intrinsics.X86;
using System.Security.Cryptography;
using System.Text;

namespace OutLoud.WinUI.Services;

/// <summary>Runs only model-free diagnostics using the official Windows x64 runtime.</summary>
public sealed class PocketTtsCliService
{
    private static readonly TimeSpan DiagnosticTimeout = TimeSpan.FromSeconds(15);
    private static readonly TimeSpan CleanupTimeout = TimeSpan.FromSeconds(5);
    private readonly string? _executablePath;
    private readonly Action<Process> _terminateProcessTree;

    /// <param name="executablePath">
    /// Optional executable path. Relative paths are based on AppContext.BaseDirectory.
    /// Otherwise use the verified active local runtime, or the pinned app-relative bundled fallback.
    /// </param>
    public PocketTtsCliService(string? executablePath = null)
        : this(executablePath, static process => process.Kill(entireProcessTree: true))
    {
    }

    // Keep termination failures testable without requiring protected OS processes.
    internal PocketTtsCliService(string? executablePath, Action<Process> terminateProcessTree)
    {
        ArgumentNullException.ThrowIfNull(terminateProcessTree);
        if (executablePath is not null)
        {
            ArgumentException.ThrowIfNullOrWhiteSpace(executablePath);
        }

        _executablePath = executablePath;
        _terminateProcessTree = terminateProcessTree;
    }

    public Task<PocketTtsCliResult> GetVersionAsync(CancellationToken cancellationToken = default) =>
        ExecuteAsync(["--version"], cancellationToken);

    public Task<PocketTtsCliResult> GetExportVoiceHelpAsync(CancellationToken cancellationToken = default) =>
        ExecuteAsync(["export-voice", "--help"], cancellationToken);

    private async Task<PocketTtsCliResult> ExecuteAsync(string[] arguments, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        using var timeout = new CancellationTokenSource(DiagnosticTimeout);
        using var operation = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, timeout.Token);
        try
        {
            return await Task.Run(() => ExecuteCoreAsync(arguments, operation.Token), operation.Token)
                .ConfigureAwait(false);
        }
        catch (OperationCanceledException exception) when (cancellationToken.IsCancellationRequested)
        {
            // Caller cancellation takes precedence if both signals are set during cleanup.
            throw new OperationCanceledException("Pocket TTS diagnostics were cancelled by the caller.",
                exception, cancellationToken);
        }
        catch (OperationCanceledException exception) when (timeout.IsCancellationRequested)
        {
            throw new TimeoutException("Pocket TTS diagnostics exceeded the 15-second timeout.", exception);
        }
    }

    private async Task<PocketTtsCliResult> ExecuteCoreAsync(
        string[] arguments, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        EnsureSupportedProcessor();
        using FileStream? nativeReadLease = await PocketTtsModelStore.AcquireReadAsync(cancellationToken).ConfigureAwait(false);
        var selected = _executablePath is null
            ? await PocketTtsRuntimeStore.ReadVerifiedActiveAsync(cancellationToken, initializeIdentity: true).ConfigureAwait(false)
            : null;
        string executablePath = selected?.ExecutablePath ?? ResolveExecutablePath();
        using FileStream executableLease = File.OpenRead(executablePath);
        if (selected is not null && !string.Equals(
            Convert.ToHexString(await SHA256.HashDataAsync(executableLease, cancellationToken).ConfigureAwait(false)),
            selected.Identity.ExecutableSha256, StringComparison.OrdinalIgnoreCase))
        { throw new InvalidDataException("The active runtime changed before execution."); }
        var startInfo = new ProcessStartInfo
        {
            FileName = executablePath,
            WorkingDirectory = Path.GetDirectoryName(executablePath)!,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            StandardOutputEncoding = Encoding.UTF8,
            StandardErrorEncoding = Encoding.UTF8,
        };

        foreach (string argument in arguments)
        {
            startInfo.ArgumentList.Add(argument);
        }

        await using var nativeOperation = new PocketTtsNativeOperation();
        PocketTtsNativeOperation.Running running;
        cancellationToken.ThrowIfCancellationRequested();
        try
        {
            running = nativeOperation.Start(startInfo);
        }
        catch (Win32Exception exception) when (exception.NativeErrorCode is 2 or 3)
        {
            throw new FileNotFoundException(
                "The Pocket TTS executable was not found. Restore the application's runtime/pocket-tts files.",
                executablePath, exception);
        }
        catch (Win32Exception exception)
        {
            throw new InvalidOperationException(
                $"Could not start the Pocket TTS runtime at '{executablePath}'. " +
                "Check that it is a runnable Windows x64 executable and access is allowed.", exception);
        }
        using var runningLease = running;
        Process process = running.Process;
        // Drain both pipes concurrently. Reading one to completion first can deadlock the child.
        using StreamReader standardOutput = running.Output;
        using StreamReader standardError = running.Error;
        Task<string> stdout = standardOutput.ReadToEndAsync(cancellationToken);
        Task<string> stderr = standardError.ReadToEndAsync(cancellationToken);
        Task outputCompleted = Task.WhenAll(stdout, stderr);
        try
        {
            await process.WaitForExitAsync(cancellationToken).ConfigureAwait(false);
            await nativeOperation.StopProcessesAsync().ConfigureAwait(false);
            await outputCompleted.WaitAsync(cancellationToken).ConfigureAwait(false);
            cancellationToken.ThrowIfCancellationRequested();
            return new PocketTtsCliResult(
                executablePath, process.ExitCode,
                await stdout.ConfigureAwait(false), await stderr.ConfigureAwait(false));
        }
        catch
        {
            Exception? cleanupFailure = null;
            try
            {
                await StopOwnedProcessAsync(process).ConfigureAwait(false);
            }
            catch (InvalidOperationException exception)
            {
                cleanupFailure = exception;
            }

            try
            {
                await outputCompleted.WaitAsync(CleanupTimeout).ConfigureAwait(false);
            }
            catch (TimeoutException exception)
            {
                cleanupFailure ??= new InvalidOperationException(
                    "Pocket TTS stopped, but its output streams did not close during cleanup.", exception);
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                // Observe cancelled reads even if process-tree cleanup failed.
            }
            catch (IOException)
            {
                // Preserve the original failure when termination closes a redirected pipe.
            }

            if (cleanupFailure is not null)
            {
                ExceptionDispatchInfo.Capture(cleanupFailure).Throw();
            }

            throw;
        }
    }

    private async Task StopOwnedProcessAsync(Process process)
    {
        AggregateException? partialTerminationFailure = null;
        try
        {
            if (!process.HasExited)
            {
                _terminateProcessTree(process);
            }
        }
        catch (AggregateException exception)
        {
            // A root exit does not mean its descendants were all terminated.
            partialTerminationFailure = exception;
            try
            {
                // Best effort to stop the owned root even when a descendant is inaccessible.
                if (!process.HasExited)
                {
                    process.Kill();
                }
            }
            catch (Exception rootFailure) when (rootFailure is Win32Exception or InvalidOperationException)
            {
                partialTerminationFailure = new AggregateException(exception, rootFailure);
            }
        }
        catch (InvalidOperationException) when (process.HasExited)
        {
            // Natural exit can race cancellation.
        }
        catch (Win32Exception) when (process.HasExited)
        {
            // The process exited between HasExited and Kill.
        }
        catch (Win32Exception exception)
        {
            throw new InvalidOperationException(
                "Could not terminate the Pocket TTS child process after cancellation or failure.", exception);
        }

        try
        {
            await process.WaitForExitAsync().WaitAsync(CleanupTimeout).ConfigureAwait(false);
        }
        catch (TimeoutException exception)
        {
            if (partialTerminationFailure is not null)
            {
                throw IncompleteTreeTermination(new AggregateException(partialTerminationFailure, exception));
            }

            throw new InvalidOperationException(
                "The Pocket TTS child process did not exit during cleanup.", exception);
        }

        if (partialTerminationFailure is not null)
        {
            throw IncompleteTreeTermination(partialTerminationFailure);
        }
    }

    private static InvalidOperationException IncompleteTreeTermination(AggregateException exception) =>
        new("Pocket TTS process-tree termination was incomplete. One or more child processes may still be running; " +
            "cleanup could not be confirmed.", exception);

    internal static void EnsureSupportedProcessor()
    {
        if (!OperatingSystem.IsWindows() || RuntimeInformation.ProcessArchitecture != Architecture.X64 ||
            RuntimeInformation.OSArchitecture != Architecture.X64 || !Avx2.IsSupported || !Fma.IsSupported)
        {
            throw new PlatformNotSupportedException(
                "The verified Pocket TTS runtime requires Windows x64, an x64 application process, " +
                "and a processor with AVX2 and FMA enabled. This system cannot run that runtime safely.");
        }
    }

    private string ResolveExecutablePath()
    {
        string? configuredPath = _executablePath;
        if (!string.IsNullOrWhiteSpace(configuredPath))
        {
            return RequireExecutable(Path.GetFullPath(configuredPath, AppContext.BaseDirectory));
        }

        string appLocalPath = PocketTtsRuntimeStore.ResolveActivePath();
        if (File.Exists(appLocalPath))
        {
            return appLocalPath;
        }

        return RequireExecutable(appLocalPath);
    }

    private static string RequireExecutable(string path)
    {
        if (!File.Exists(path))
        {
            throw new FileNotFoundException(
                "The Pocket TTS executable was not found. Restore the validated runtime/pocket-tts application files and rebuild or reinstall the application.", path);
        }

        return path;
    }
}
