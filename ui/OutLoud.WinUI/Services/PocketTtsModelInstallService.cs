using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Text.Json;

namespace OutLoud.WinUI.Services;

public enum PocketTtsModelInstallFailure
{
    None, Network, Cancelled,
    InsufficientDiskSpace, Verification, Metadata, Storage, Busy, RuntimeChanged, Registration
}

public sealed record PocketTtsModelInstallResult(bool Succeeded, PocketTtsModelInstallFailure Failure, string Message)
{ public bool CleanupPending { get; init; } }
public sealed record PocketTtsModelInstallProgress(string Message, double? Percent = null, bool CanCancel = true);

/// <summary>Verifies private blobs and downloads missing pinned assets directly into private staging.</summary>
public sealed class PocketTtsModelInstallService(HttpClient http)
{
    private static readonly SemaphoreSlim InstallGate = new(1, 1);
    private sealed class InstallException(PocketTtsModelInstallFailure failure, string message) : Exception(message)
    { internal PocketTtsModelInstallFailure Failure { get; } = failure; }

    public Task<PocketTtsModelInstallResult> InstallAsync(string alias,
        IProgress<PocketTtsModelInstallProgress>? progress = null, CancellationToken cancellationToken = default) =>
        MachineDataStorage.IsAdministrator
            ? Task.Run(() => InstallCoreAsync(alias, progress, cancellationToken), cancellationToken)
            : ElevatedMachineDataService.RunAsync<PocketTtsModelInstallProgress, PocketTtsModelInstallResult>(
                new("install-model", alias), progress, cancellationToken);

    public Task<PocketTtsModelInstallResult> UpdateAsync(string alias, string expectedIdentity, string revision,
        IProgress<PocketTtsModelInstallProgress>? progress = null, CancellationToken cancellationToken = default)
    {
        if (string.IsNullOrEmpty(expectedIdentity) || !System.Text.RegularExpressions.Regex.IsMatch(expectedIdentity, @"\A[a-fA-F0-9]{64}\z") ||
            string.IsNullOrEmpty(revision) || !System.Text.RegularExpressions.Regex.IsMatch(revision, @"\A[a-f0-9]{40}\z"))
            throw new ArgumentException("A confirmed model identity and immutable update revision are required.");
        return MachineDataStorage.IsAdministrator
            ? Task.Run(() => InstallCoreAsync(alias, progress, cancellationToken, expectedIdentity, revision), cancellationToken)
            : ElevatedMachineDataService.RunAsync<PocketTtsModelInstallProgress, PocketTtsModelInstallResult>(
                new("update-model", alias, expectedIdentity, ModelRevision: revision), progress, cancellationToken);
    }

    private async Task<PocketTtsModelInstallResult> InstallCoreAsync(string alias,
        IProgress<PocketTtsModelInstallProgress>? progress, CancellationToken cancellationToken,
        string? expectedIdentity = null, string? revision = null)
    {
        if (!await InstallGate.WaitAsync(0, cancellationToken).ConfigureAwait(false))
        { return new(false, PocketTtsModelInstallFailure.Busy, "Another model installation is already running."); }
        var privateLeases = new List<FileStream>();
        string? staging = null;
        FileStream? installLock = null;
        PocketTtsDownloadOperation? download = null;
        bool committed = false;
        bool updating = expectedIdentity is not null;
        string? receiptTemporary = null;
        string? receiptPath = null;
        PocketTtsModelInstallResult result;
        try
        {
            try
            {
                PocketTtsRuntimeStore.EnsureDirectory(PocketTtsRuntimeStore.LocalDataPath);
                try { installLock = new(Path.Combine(PocketTtsRuntimeStore.LocalDataPath, "model-install.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None); }
                catch (IOException exception) when ((exception.HResult & 0xFFFF) is 32 or 33)
                { throw new InstallException(PocketTtsModelInstallFailure.Busy, "Another application instance may be installing a model. Close it and retry."); }
                {
                    progress?.Report(new("Checking runtime and trusted installation metadata..."));
                    string runtimePath = PocketTtsRuntimeStore.ResolveActivePath();
                    using FileStream runtimeLease = File.OpenRead(runtimePath);
                    var identity = await PocketTtsModelSetupService.ReadVerifiedRuntimeIdentityAsync(cancellationToken).ConfigureAwait(false);
                    var supported = await PocketTtsModelSetupService.ValidateCliAsync(new PocketTtsCliService(runtimePath),
                        !PocketTtsRuntimeStore.IsWithin(runtimePath, PocketTtsRuntimeStore.RuntimesPath), cancellationToken).ConfigureAwait(false);
                    if (!supported.Contains(alias, StringComparer.Ordinal))
                    { throw new InstallException(PocketTtsModelInstallFailure.Metadata, "The active runtime does not support this model. Check models before installing."); }
                    var inventory = new PocketTtsModelInventoryService();
                    IReadOnlyList<PocketTtsModelInventoryEntry> installed;
                    try { installed = await inventory.ReadAsync(cancellationToken).ConfigureAwait(false); }
                    catch (InvalidDataException)
                    { throw new InstallException(PocketTtsModelInstallFailure.Metadata, "The saved model inventory is incompatible or invalid. Close the app and manually archive the outdated development model-inventory.json before reinstalling. It has not been changed."); }
                    var previous = installed.SingleOrDefault(entry => entry.Alias == alias);
                    if (updating && (previous is null || PocketTtsModelInventoryService.GetIdentity(previous) != expectedIdentity))
                        throw new InstallException(PocketTtsModelInstallFailure.Metadata, "The model changed. Check for updates and confirm again.");
                    if (!updating && previous is not null)
                        throw new InstallException(PocketTtsModelInstallFailure.Metadata, "This model is already installed. Use Update when available, or Remove.");
                    if ((await inventory.ReadPendingAsync(cancellationToken).ConfigureAwait(false)).Any(job => job.Alias == alias))
                        throw new InstallException(PocketTtsModelInstallFailure.Busy, "Pending cleanup must finish before installing or updating this model. Restart Out Loud to retry cleanup.");
                    if (previous is not null)
                    {
                        var oldModel = PocketTtsModelInventoryService.CreateModel(previous);
                        privateLeases.AddRange(await oldModel.OpenVerifiedAssetsAsync(cancellationToken).ConfigureAwait(false));
                        if (previous.UpdateReceiptSha256 is { } oldReceiptHash)
                        {
                            string oldReceipt = PocketTtsModelStore.BlobPath(true, oldReceiptHash);
                            long size = new FileInfo(oldReceipt).Length;
                            if (size > 65536) throw new InvalidDataException("The previous model update receipt changed.");
                            privateLeases.AddRange(await VerifyFileAsync(new("receipt", oldReceipt, size, oldReceiptHash, true, false),
                                cancellationToken).ConfigureAwait(false));
                        }
                    }
                    var plan = updating
                        ? await new PocketTtsModelUpdateService(http).ResolveUpdateAsync(alias, revision!, cancellationToken).ConfigureAwait(false)
                        : await PocketTtsModelInstallPlan.LoadAsync(alias, cancellationToken).ConfigureAwait(false)
                            ?? throw new InstallException(PocketTtsModelInstallFailure.Metadata, "Complete trusted installation metadata is unavailable for this model.");
                    if (previous is not null && plan.InventoryEntry().RequiredFiles.All(file => previous.RequiredFiles.Any(old =>
                        old.RepositoryPath == file.RepositoryPath && old.ExpectedHash == file.ExpectedHash && old.SizeBytes == file.SizeBytes)))
                        throw new InstallException(PocketTtsModelInstallFailure.Metadata, "The confirmed revision has no changed model files. Check for updates again.");

                    var resolved = new Dictionary<PocketTtsModelInstallFile, string>();
                    var missing = new List<PocketTtsModelInstallFile>();
                    foreach (var file in plan.Files)
                    {
                        cancellationToken.ThrowIfCancellationRequested();
                        PocketTtsModelStore.ValidatePath(file.Asset.Path);
                        if (File.Exists(file.Asset.Path))
                        {
                            progress?.Report(new($"Verifying private {file.RepositoryPath}..."));
                            try
                            {
                                privateLeases.AddRange(await VerifyFileAsync(file.Asset, cancellationToken).ConfigureAwait(false));
                                resolved.Add(file, file.Asset.Path);
                            }
                            catch (Exception exception) when (exception is IOException or InvalidDataException or UnauthorizedAccessException or JsonException)
                            { throw new InstallException(PocketTtsModelInstallFailure.Verification, "An existing private model blob failed verification. It was left unchanged. Resolve the private storage conflict before retrying."); }
                        }
                        else { missing.Add(file); }
                    }
                    var groups = missing.GroupBy(file => file.Asset.Path, StringComparer.OrdinalIgnoreCase).ToArray();
                    if (groups.Any(group => group.Any(file => file.Asset.Size != group.First().Asset.Size)))
                    { throw new InvalidDataException("Conflicting content sizes."); }
                    if (groups.Length > 0 || plan.UpdateReceipt is not null)
                    {
                        PocketTtsModelStore.EnsureDirectory(PocketTtsModelStore.Staging);
                        long requiredBytes = checked(groups.Sum(group => group.First().Asset.Size) + (plan.UpdateReceipt?.Length ?? 0));
                        var drive = new DriveInfo(Path.GetPathRoot(PocketTtsModelStore.Root)!);
                        if (drive.AvailableFreeSpace < checked(requiredBytes + 16 * 1024 * 1024))
                        { throw new InstallException(PocketTtsModelInstallFailure.InsufficientDiskSpace, "There is not enough free disk space in private model storage for this installation."); }
                        string[] temporaryNames = Enumerable.Range(0, groups.Length + (plan.UpdateReceipt is null ? 0 : 1))
                            .Select(_ => Guid.NewGuid().ToString("N") + ".partial").ToArray();
                        download = await PocketTtsDownloadOperation.BeginAsync("model", temporaryNames, cancellationToken).ConfigureAwait(false);
                        staging = download.DirectoryPath;
                        if (Directory.Exists(staging) || File.Exists(staging)) { throw new IOException("Temporary directory already exists."); }
                        PocketTtsModelStore.EnsureDirectory(staging);
                        long completed = 0;
                        int temporaryIndex = 0;
                        foreach (var group in groups)
                        {
                            var file = group.First();
                            string temporary = Path.Combine(staging, temporaryNames[temporaryIndex++]);
                            await DownloadAsync(file, temporary, completed, requiredBytes, progress, cancellationToken).ConfigureAwait(false);
                            foreach (var reference in group) { resolved.Add(reference, temporary); }
                            completed += file.Asset.Size;
                        }
                        if (plan.UpdateReceipt is { } receipt)
                        {
                            receiptTemporary = Path.Combine(staging, temporaryNames[^1]);
                            using (var output = PocketTtsModelStore.CreateStagingFile(receiptTemporary))
                            { await output.WriteAsync(receipt, cancellationToken).ConfigureAwait(false); output.Flush(flushToDisk: true); }
                            receiptPath = PocketTtsModelStore.BlobPath(true, plan.InventoryEntry().UpdateReceiptSha256!);
                        }
                    }

                    progress?.Report(new("Verifying complete private model and offline public model configuration..."));
                    await VerifyModelAsync(plan, resolved, cancellationToken).ConfigureAwait(false);
                    if (previous is not null)
                        await VerifyUpdateShapeAsync(previous, plan, resolved, cancellationToken).ConfigureAwait(false);
                    progress?.Report(new("Waiting for active model operations, then publishing verified files..."));
                    using (FileStream publicationLease = await PocketTtsModelStore.AcquireWriteAsync(cancellationToken).ConfigureAwait(false))
                    {
                        foreach (var group in groups)
                        {
                            cancellationToken.ThrowIfCancellationRequested();
                            var file = group.First();
                            string blob = file.Asset.Path;
                            PocketTtsModelStore.EnsureDirectory(Path.GetDirectoryName(blob)!);
                            PocketTtsModelStore.ValidatePath(resolved[file]);
                            PocketTtsModelStore.ValidatePath(blob);
                            try { File.Move(resolved[file], blob); }
                            catch (IOException) when (File.Exists(blob))
                            {
                                // Never replace an existing file, even when an unexpected concurrent writer published it.
                                privateLeases.AddRange(await VerifyFileAsync(file.Asset, cancellationToken).ConfigureAwait(false));
                            }
                            foreach (var reference in group) { resolved[reference] = blob; }
                        }
                        var model = new PocketTtsInstalledModel(alias, plan.Configuration,
                            plan.Files.Select(file => file.Asset with { Path = resolved[file] }).ToArray());
                        _ = model.LocalConfiguration();
                        privateLeases.AddRange(await model.OpenVerifiedAssetsAsync(cancellationToken).ConfigureAwait(false));
                        if (receiptPath is not null)
                        {
                            PocketTtsModelStore.EnsureDirectory(Path.GetDirectoryName(receiptPath)!);
                            try { File.Move(receiptTemporary!, receiptPath); }
                            catch (IOException) when (File.Exists(receiptPath)) { }
                            privateLeases.AddRange(await VerifyFileAsync(new("receipt", receiptPath, plan.UpdateReceipt!.Length,
                                plan.InventoryEntry().UpdateReceiptSha256!, true, false), cancellationToken).ConfigureAwait(false));
                        }
                        if (updating)
                        {
                            if (runtimePath != PocketTtsRuntimeStore.ResolveActivePath() || identity !=
                                await PocketTtsModelSetupService.ReadVerifiedRuntimeIdentityAsync(cancellationToken).ConfigureAwait(false))
                                throw new InstallException(PocketTtsModelInstallFailure.RuntimeChanged, "The runtime changed. No model update was committed.");
                            PocketTtsModelInstallPlan.ValidateInventory(plan.InventoryEntry());
                            progress?.Report(new("Activating the verified model update…", 100, CanCancel: false));
                            await inventory.CommitUpdateAsync(plan.InventoryEntry(), expectedIdentity!, cancellationToken).ConfigureAwait(false);
                            committed = true;
                        }
                    }
                    if (updating)
                    {
                        foreach (var lease in privateLeases) lease.Dispose();
                        privateLeases.Clear();
                        progress?.Report(new("Removing superseded model files…", CanCancel: false));
                        var cleanup = await new PocketTtsModelRemovalService().CleanupAfterUpdateAsync(CancellationToken.None).ConfigureAwait(false);
                        result = new(true, PocketTtsModelInstallFailure.None, $"{alias} updated and verified. The saved model selection is unchanged. " + cleanup.Message)
                            { CleanupPending = cleanup.CleanupPending };
                    }
                    else
                    {
                        if (runtimePath != PocketTtsRuntimeStore.ResolveActivePath() ||
                            identity != await PocketTtsModelSetupService.ReadVerifiedRuntimeIdentityAsync(cancellationToken).ConfigureAwait(false))
                        { throw new InstallException(PocketTtsModelInstallFailure.RuntimeChanged, "The active runtime changed during installation. No model was registered. Check models and retry."); }

                        progress?.Report(new("Registering verified private model...", 100));
                        try { await inventory.RegisterAfterInstallationAsync(plan.InventoryEntry(), cancellationToken, replaceExisting: false).ConfigureAwait(false); }
                        catch (IOException exception) when ((exception.HResult & 0xFFFF) is 39 or 112) { throw; }
                        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or InvalidDataException or InvalidOperationException)
                        { throw new InstallException(PocketTtsModelInstallFailure.Registration, "Verified private files are present, but the model could not be registered. Check local data permissions and the model inventory, then retry."); }
                        committed = true;
                        result = new(true, PocketTtsModelInstallFailure.None, $"{alias} installed and verified. It has not been activated.");
                    }
                }
            }
            catch (InstallException exception) { result = new(false, exception.Failure, exception.Message); }
            catch (OperationCanceledException) { result = new(false, PocketTtsModelInstallFailure.Cancelled, updating
                ? "Model update cancelled. The previous model and saved selection are unchanged."
                : "Model installation cancelled. No incomplete installation was registered."); }
            catch (HttpRequestException) { result = new(false, PocketTtsModelInstallFailure.Network, "The model download failed. Check your connection and retry."); }
            catch (TimeoutException) { result = new(false, PocketTtsModelInstallFailure.Network, "The model download timed out. Check your connection and retry."); }
            catch (IOException exception) when ((exception.HResult & 0xFFFF) is 39 or 112)
            { result = new(false, PocketTtsModelInstallFailure.InsufficientDiskSpace, "The disk is full. Free space on the private model storage drive and retry."); }
            catch (Exception exception) when (exception is InvalidDataException or JsonException)
            { result = new(false, PocketTtsModelInstallFailure.Verification, updating
                ? "Model update verification failed. The previous model and saved selection are unchanged."
                : "Private model integrity or model format verification failed. No model was registered; existing files were preserved."); }
            catch (Exception)
            { result = new(false, PocketTtsModelInstallFailure.Storage, "Model installation could not complete. Check private storage permissions, runtime readiness, and available disk space."); }
            finally
            {
                foreach (var lease in privateLeases) { lease.Dispose(); }
            }
            if (committed && !result.Succeeded)
                result = new(true, PocketTtsModelInstallFailure.None,
                    $"{alias} was updated and verified, but cleanup could not finish. Restart Out Loud to retry the recorded cleanup; the saved selection is unchanged.")
                    { CleanupPending = true };
            bool cleanupFailed = false;
            if (download is not null)
            {
                try { _ = await download.CleanupAsync().ConfigureAwait(false); }
                catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or InvalidDataException) { cleanupFailed = true; }
            }
            if (cleanupFailed)
            { result = result with { CleanupPending = committed || result.CleanupPending,
                Message = result.Message + " Temporary download cleanup remains recorded and will be retried at startup. Unverified files were preserved." }; }
            return committed ? result with { Succeeded = true, Failure = PocketTtsModelInstallFailure.None } : result;
        }
        finally { installLock?.Dispose(); InstallGate.Release(); }
    }

    private static Task<IReadOnlyList<FileStream>> VerifyFileAsync(PocketTtsCachedAsset asset, CancellationToken cancellationToken) =>
        new PocketTtsInstalledModel("verification", "", new[] { asset }).OpenVerifiedAssetsAsync(cancellationToken, requireVoiceCloning: false);

    private static async Task VerifyUpdateShapeAsync(PocketTtsModelInventoryEntry previous, PocketTtsModelInstallPlan plan,
        IReadOnlyDictionary<PocketTtsModelInstallFile, string> resolved, CancellationToken token)
    {
        var oldWeights = previous.RequiredFiles.Single(file => file.IsWeights);
        var newWeights = plan.Files.Single(file => file.Asset.Weights);
        async Task<Dictionary<string, string>> Shape(string path)
        {
            using var file = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
            byte[] length = new byte[8]; await file.ReadExactlyAsync(length, token).ConfigureAwait(false);
            ulong bytes = System.Buffers.Binary.BinaryPrimitives.ReadUInt64LittleEndian(length);
            if (bytes > 16 * 1024 * 1024 || bytes > (ulong)Math.Max(0, file.Length - 8)) throw new InvalidDataException("Invalid tensor header.");
            byte[] header = new byte[(int)bytes]; await file.ReadExactlyAsync(header, token).ConfigureAwait(false);
            using var json = JsonDocument.Parse(header);
            return json.RootElement.EnumerateObject().Where(tensor => tensor.Name != "__metadata__").ToDictionary(
                tensor => tensor.Name, tensor => tensor.Value.GetProperty("dtype").GetString() + ":" +
                    string.Join(',', tensor.Value.GetProperty("shape").EnumerateArray().Select(value => value.GetInt64())), StringComparer.Ordinal);
        }
        var before = await Shape(PocketTtsModelInventoryService.ResolveBlobReference(oldWeights.RelativePath)).ConfigureAwait(false);
        var after = await Shape(resolved[newWeights]).ConfigureAwait(false);
        if (before.Count != after.Count || before.Any(pair => !after.TryGetValue(pair.Key, out var value) || value != pair.Value))
            throw new InstallException(PocketTtsModelInstallFailure.Metadata,
                "The updated weights require a different model configuration. No update was activated; install a compatible Out Loud release instead.");
    }

    private static async Task VerifyModelAsync(PocketTtsModelInstallPlan plan,
        IReadOnlyDictionary<PocketTtsModelInstallFile, string> resolved, CancellationToken cancellationToken)
    {
        var model = new PocketTtsInstalledModel(plan.Alias, plan.Configuration,
            plan.Files.Select(file => file.Asset with { Path = resolved[file] }).ToArray());
        _ = model.LocalConfiguration();
        var leases = await model.OpenVerifiedAssetsAsync(cancellationToken).ConfigureAwait(false);
        foreach (var lease in leases) { lease.Dispose(); }
    }

    private async Task DownloadAsync(PocketTtsModelInstallFile file, string temporary, long completed, long total,
        IProgress<PocketTtsModelInstallProgress>? progress, CancellationToken cancellationToken)
    {
        Uri uri = new($"https://huggingface.co/{PocketTtsModelInstallPlan.Repository}/resolve/{file.Revision}/" +
            string.Join('/', file.RepositoryPath.Split('/').Select(Uri.EscapeDataString)));
        for (int redirects = 0; redirects <= 8; redirects++)
        {
            if (!AllowedDownloadUri(uri)) { throw new InstallException(PocketTtsModelInstallFailure.Network, "Hugging Face returned an unsupported download location. No file was installed."); }
            using var request = new HttpRequestMessage(HttpMethod.Get, uri);
            request.Headers.UserAgent.ParseAdd(ApplicationInfo.UserAgent);
            using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            timeout.CancelAfter(TimeSpan.FromSeconds(30));
            HttpResponseMessage response;
            try { response = await http.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, timeout.Token).ConfigureAwait(false); }
            catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested) { throw new TimeoutException(); }
            using (response)
            {
                int status = (int)response.StatusCode;
                if (status is 301 or 302 or 303 or 307 or 308)
                {
                    if (response.Headers.Location is not { } location) { break; }
                    uri = location.IsAbsoluteUri ? location : new Uri(uri, location);
                    continue;
                }
                if (response.StatusCode is HttpStatusCode.Unauthorized or HttpStatusCode.Forbidden)
                { throw new InstallException(PocketTtsModelInstallFailure.Network, "The public model file is not anonymously accessible. No authentication will be attempted."); }
                if (response.StatusCode == HttpStatusCode.NotFound)
                { throw new InstallException(PocketTtsModelInstallFailure.Metadata, "A pinned official model file is unavailable. Installation metadata must be updated by the application maintainer."); }
                if (response.StatusCode != HttpStatusCode.OK)
                { throw new InstallException(PocketTtsModelInstallFailure.Network, $"Hugging Face could not serve the model file (HTTP {status}). Retry later."); }
                if (response.Content.Headers.ContentLength is { } length && length != file.Asset.Size)
                { throw new InvalidDataException("Unexpected download size."); }
                timeout.CancelAfter(Timeout.InfiniteTimeSpan);
                await using Stream source = await response.Content.ReadAsStreamAsync(cancellationToken).ConfigureAwait(false);
                await using var destination = PocketTtsModelStore.CreateStagingFile(temporary);
                PocketTtsModelStore.ValidateFile(temporary, destination.SafeFileHandle);
                byte[] buffer = new byte[131072];
                long received = 0;
                long lastReport = Environment.TickCount64 - 250;
                while (true)
                {
                    using var idle = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
                    idle.CancelAfter(TimeSpan.FromSeconds(30));
                    int count;
                    try { count = await source.ReadAsync(buffer, idle.Token).ConfigureAwait(false); }
                    catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested) { throw new TimeoutException(); }
                    catch (IOException)
                    { throw new InstallException(PocketTtsModelInstallFailure.Network, "The connection was interrupted before the model download completed. Retry the installation."); }
                    if (count == 0) { break; }
                    received += count;
                    if (received > file.Asset.Size) { throw new InvalidDataException("Download exceeded expected size."); }
                    await destination.WriteAsync(buffer.AsMemory(0, count), cancellationToken).ConfigureAwait(false);
                    if (Environment.TickCount64 - lastReport >= 200)
                    {
                        lastReport = Environment.TickCount64;
                        progress?.Report(new($"Downloading {file.RepositoryPath}: {(completed + received) / 1048576d:F1} / {total / 1048576d:F1} MB",
                            100d * (completed + received) / total));
                    }
                }
                if (received != file.Asset.Size) { throw new InstallException(PocketTtsModelInstallFailure.Network, "The download ended before the complete model file arrived. Retry the installation."); }
                await destination.FlushAsync(cancellationToken).ConfigureAwait(false);
                destination.Flush(flushToDisk: true);
                return;
            }
        }
        throw new InstallException(PocketTtsModelInstallFailure.Network, "The model download could not follow the official download redirects. Retry later.");
    }

    private static bool AllowedDownloadUri(Uri uri) => uri.Scheme == "https" && uri.IsDefaultPort && uri.UserInfo.Length == 0 &&
        (uri.Host.Equals("huggingface.co", StringComparison.OrdinalIgnoreCase) || uri.Host.EndsWith(".huggingface.co", StringComparison.OrdinalIgnoreCase) ||
         uri.Host.Equals("hf.co", StringComparison.OrdinalIgnoreCase) || uri.Host.EndsWith(".hf.co", StringComparison.OrdinalIgnoreCase));
}
