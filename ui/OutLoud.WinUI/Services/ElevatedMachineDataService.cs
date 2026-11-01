using Microsoft.Win32.SafeHandles;
using OutLoud.WinUI.Voices;
using System.Buffers.Binary;
using System.Diagnostics;
using System.Globalization;
using System.IO.Pipes;
using System.Net.Http;
using System.Runtime.InteropServices;
using System.Text.Json;

namespace OutLoud.WinUI.Services;

/// <summary>Allowlisted shared-data requests over the existing authenticated, elevated helper.</summary>
internal static class ElevatedMachineDataService
{
    private const int Limit = 1024 * 1024;
    internal sealed record Request(string Operation, string? Alias = null, string? Identity = null,
        PocketTtsRuntimeUpdateResult? Release = null, string? ExpectedSelection = null,
        bool Automatic = false, IReadOnlyDictionary<string, string>? ModelIdentities = null);
    private sealed record Message(string Kind, JsonElement Value);

    internal static async Task<TResult> RunAsync<TProgress, TResult>(Request request, IProgress<TProgress>? progress,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        string endpoint = "OutLoud.Storage." + Guid.NewGuid().ToString("N");
        using var pipe = ElevatedVoiceImportService.CreatePipe(endpoint);
        using var process = await new ElevationLauncher().LaunchAsync(
            Path.Combine(AppContext.BaseDirectory, "OutLoudVoiceImportHelper.exe"), endpoint, Environment.ProcessId).ConfigureAwait(false);
        using var connection = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        await pipe.WaitForConnectionAsync(connection.Token).ConfigureAwait(false);
        if (!GetNamedPipeClientProcessId(pipe.SafePipeHandle, out uint client) || client != process.Id)
            throw new IOException("Unexpected shared-data helper connection.");
        cancellationToken.ThrowIfCancellationRequested();
        var writer = new SemaphoreSlim(1, 1);
        await WriteAsync(pipe, new Message("request", JsonSerializer.SerializeToElement(request)), writer).ConfigureAwait(false);
        // Cancellation is cooperative. Keep reading until the helper has finished its existing cleanup.
        using var stopCancellation = new CancellationTokenSource();
        Task cancel = SendCancellationAsync();
        try
        {
            while (true)
            {
                Message message = await ReadAsync(pipe).ConfigureAwait(false);
                if (message.Kind == "progress") { progress?.Report(message.Value.Deserialize<TProgress>()!); continue; }
                if (message.Kind == "error") throw new IOException(message.Value.GetString());
                if (message.Kind != "result") throw new InvalidDataException("Invalid shared-data helper response.");
                var result = message.Value.Deserialize<TResult>() ?? throw new InvalidDataException("Missing shared-data result.");
                await process.WaitForExitAsync().ConfigureAwait(false);
                return result;
            }
        }
        finally { stopCancellation.Cancel(); await cancel.ConfigureAwait(false); writer.Dispose(); }

        async Task SendCancellationAsync()
        {
            using var stopped = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, stopCancellation.Token);
            try { await Task.Delay(Timeout.Infinite, stopped.Token).ConfigureAwait(false); }
            catch (OperationCanceledException) { }
            if (cancellationToken.IsCancellationRequested && !stopCancellation.IsCancellationRequested)
            {
                try { await WriteAsync(pipe, new Message("cancel", JsonSerializer.SerializeToElement(true)), writer).ConfigureAwait(false); }
                catch (Exception e) when (e is IOException or ObjectDisposedException) { }
            }
        }
    }

    internal static bool IsWorker => Environment.GetCommandLineArgs().Skip(1).FirstOrDefault() == "--machine-data-worker";

    internal static async Task RunWorkerAsync()
    {
        string[] args = Environment.GetCommandLineArgs();
        if (!MachineDataStorage.IsAdministrator || args.Length != 4 ||
            !long.TryParse(args[2], NumberStyles.None, CultureInfo.InvariantCulture, out long handle) || handle <= 0 ||
            !int.TryParse(args[3], NumberStyles.None, CultureInfo.InvariantCulture, out int parentId))
            throw new InvalidDataException("Invalid elevated shared-data worker invocation.");
        using var pipe = new NamedPipeClientStream(PipeDirection.InOut, true, true, new SafePipeHandle(new IntPtr(handle), true));
        using var parent = Process.GetProcessById(parentId);
        if (!GetNamedPipeServerProcessId(pipe.SafePipeHandle, out uint server) || server != parentId ||
            parent.SessionId != Process.GetCurrentProcess().SessionId ||
            !string.Equals(parent.MainModule?.FileName, Path.Combine(AppContext.BaseDirectory, "OutLoud.WinUI.exe"), StringComparison.OrdinalIgnoreCase))
            throw new IOException("The shared-data caller could not be authenticated.");
        using var writer = new SemaphoreSlim(1, 1);
        using var operation = new CancellationTokenSource();
        using var stopObserver = new CancellationTokenSource();
        Message message = await ReadAsync(pipe).ConfigureAwait(false);
        if (message.Kind != "request") throw new InvalidDataException("Missing shared-data request.");
        var request = message.Value.Deserialize<Request>() ?? throw new InvalidDataException("Invalid shared-data request.");
        Task cancellation = ObserveCancellationAsync();
        try
        {
            if (request.Operation is not ("install-model" or "remove-model" or "recover-models" or "select-model" or "install-runtime" or "check-model-updates"))
                throw new InvalidDataException("Unsupported shared-data operation.");
            MachineDataStorage.Prepare();
            using var http = NewHttp();
            using var download = NewHttp();
            object result;
            switch (request.Operation)
            {
                case "install-model":
                    result = await new PocketTtsModelInstallService(http).InstallAsync(request.Alias!,
                        Progress<PocketTtsModelInstallProgress>(), operation.Token).ConfigureAwait(false);
                    break;
                case "remove-model":
                    var removal = await new PocketTtsModelRemovalService().RemoveAsync(request.Alias!, request.Identity!,
                        Progress<PocketTtsModelInstallProgress>(), operation.Token).ConfigureAwait(false);
                    if (removal.Removed)
                    {
                        try { await new PocketTtsModelSelectionSettings().ForgetRemovedModelAsync(request.Alias!, null, CancellationToken.None).ConfigureAwait(false); }
                        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or InvalidDataException or JsonException)
                        { removal = removal with { Message = removal.Message + " The saved selection could not be cleared. Refresh Settings before selecting another model." }; }
                    }
                    result = removal;
                    break;
                case "recover-models":
                    result = (object?)await new PocketTtsModelRemovalService().RecoverAsync(operation.Token).ConfigureAwait(false)
                        ?? new PocketTtsModelRemovalResult(false, false, 0, 0, false, "No pending model cleanup.");
                    break;
                case "select-model":
                    using (var selectionLease = await PocketTtsModelStore.AcquireReadAsync(operation.Token).ConfigureAwait(false))
                    {
                        if (request.Alias is not null)
                        {
                            var setup = new PocketTtsModelSetupService(new PocketTtsCliService());
                            var checkedModels = await setup.CheckAsync(operation.Token).ConfigureAwait(false);
                            var selected = setup.GetSelectableModels(checkedModels.Models).SingleOrDefault(m => m.Name == request.Alias)
                                ?? throw new InvalidDataException("The selected model is unavailable or unverified.");
                            await setup.ActivateAsync(selected, operation.Token).ConfigureAwait(false);
                        }
                        await new PocketTtsModelSelectionSettings().SaveAsync(request.Alias, operation.Token, request.ExpectedSelection).ConfigureAwait(false);
                    }
                    result = true;
                    break;
                case "check-model-updates":
                    result = await new PocketTtsModelUpdateCoordinator(http).RunAsync(request.Automatic,
                        request.ModelIdentities ?? throw new InvalidDataException("Missing model update identities."),
                        Progress<PocketTtsModelUpdateResult>(), operation.Token).ConfigureAwait(false);
                    break;
                default:
                    result = await new PocketTtsRuntimeInstallService(http, download).InstallAsync(
                        request.Release ?? throw new InvalidDataException("Missing confirmed runtime release."),
                        Progress<PocketTtsRuntimeInstallProgress>(), operation.Token).ConfigureAwait(false);
                    break;
            }
            await WriteAsync(pipe, new Message("result", JsonSerializer.SerializeToElement(result)), writer).ConfigureAwait(false);
        }
        catch (Exception exception)
        {
            await WriteAsync(pipe, new Message("error", JsonSerializer.SerializeToElement(exception.Message)), writer).ConfigureAwait(false);
        }
        // Stop observing only after the existing service has completed mutation/cleanup.
        finally { stopObserver.Cancel(); await cancellation.ConfigureAwait(false); }

        IProgress<T> Progress<T>() => new ForwardProgress<T>(value =>
            WriteAsync(pipe, new Message("progress", JsonSerializer.SerializeToElement(value)), writer).GetAwaiter().GetResult());

        async Task ObserveCancellationAsync()
        {
            try
            {
                Message next = await ReadAsync(pipe, stopObserver.Token).ConfigureAwait(false);
                if (next.Kind != "cancel") throw new InvalidDataException("Invalid shared-data cancellation.");
            }
            catch (Exception exception) when (exception is IOException or ObjectDisposedException or OperationCanceledException) { }
            finally { operation.Cancel(); }
        }
    }

    private sealed class ForwardProgress<T>(Action<T> report) : IProgress<T> { public void Report(T value) => report(value); }
    private static HttpClient NewHttp() => new(new HttpClientHandler
        { AllowAutoRedirect = false, UseCookies = false, UseDefaultCredentials = false }) { Timeout = Timeout.InfiniteTimeSpan };

    private static async Task<Message> ReadAsync(Stream pipe, CancellationToken cancellationToken = default)
    {
        byte[] size = new byte[4];
        await pipe.ReadExactlyAsync(size, cancellationToken).ConfigureAwait(false);
        int length = BinaryPrimitives.ReadInt32LittleEndian(size);
        if (length <= 0 || length > Limit) throw new InvalidDataException("Invalid shared-data message size.");
        byte[] bytes = new byte[length];
        await pipe.ReadExactlyAsync(bytes, cancellationToken).ConfigureAwait(false);
        return JsonSerializer.Deserialize<Message>(bytes) ?? throw new InvalidDataException("Invalid shared-data message.");
    }

    private static async Task WriteAsync(Stream pipe, Message value, SemaphoreSlim writer)
    {
        byte[] bytes = JsonSerializer.SerializeToUtf8Bytes(value);
        if (bytes.Length > Limit) throw new InvalidDataException("Shared-data message is too large.");
        byte[] size = new byte[4];
        BinaryPrimitives.WriteInt32LittleEndian(size, bytes.Length);
        await writer.WaitAsync().ConfigureAwait(false);
        try { await pipe.WriteAsync(size).ConfigureAwait(false); await pipe.WriteAsync(bytes).ConfigureAwait(false); await pipe.FlushAsync().ConfigureAwait(false); }
        finally { writer.Release(); }
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    private static extern bool GetNamedPipeClientProcessId(SafePipeHandle pipe, out uint process);
    [DllImport("kernel32.dll", SetLastError = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    private static extern bool GetNamedPipeServerProcessId(SafePipeHandle pipe, out uint process);
}
