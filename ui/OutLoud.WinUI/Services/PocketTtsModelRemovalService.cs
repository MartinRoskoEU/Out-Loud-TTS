using System.Text.Json;

namespace OutLoud.WinUI.Services;

public sealed record PocketTtsModelRemovalResult(bool Removed, bool Cancelled, long DeletedBytes,
    int SharedFilesRetained, bool CleanupPending, string Message);

/// <summary>Inventory-first removal and durable, reference-checked private cleanup. Never scans external caches.</summary>
public sealed class PocketTtsModelRemovalService
{
    public Task<PocketTtsModelRemovalResult> RemoveAsync(string alias, string expectedIdentity,
        IProgress<PocketTtsModelInstallProgress>? progress = null, CancellationToken cancellationToken = default) =>
        MachineDataStorage.IsAdministrator
            ? Task.Run(() => RunAsync(alias, expectedIdentity, progress, cancellationToken), cancellationToken)
            : ElevatedMachineDataService.RunAsync<PocketTtsModelInstallProgress, PocketTtsModelRemovalResult>(
                new("remove-model", alias, expectedIdentity), progress, cancellationToken);

    internal Task<PocketTtsModelRemovalResult?> RecoverAsync(CancellationToken cancellationToken) =>
        Task.Run<PocketTtsModelRemovalResult?>(async () =>
        {
            try
            {
                bool pendingModels = (await new PocketTtsModelInventoryService().ReadPendingAsync(cancellationToken).ConfigureAwait(false)).Count > 0;
                if (!pendingModels && !PocketTtsDownloadOperation.HasPending && !PocketTtsRuntimeStore.HasInactiveRuntime) return null;
                if (!MachineDataStorage.IsAdministrator)
                    return await ElevatedMachineDataService.RunAsync<PocketTtsModelInstallProgress, PocketTtsModelRemovalResult>(
                        new("recover-models"), null, cancellationToken).ConfigureAwait(false);
                var downloads = await PocketTtsDownloadOperation.RecoverAsync(cancellationToken).ConfigureAwait(false);
                var models = pendingModels ? await RunAsync(null, null, null, cancellationToken).ConfigureAwait(false) : null;
                if (!downloads.HadWork) return models;
                return new(models?.Removed ?? false, false, downloads.DeletedBytes + (models?.DeletedBytes ?? 0), models?.SharedFilesRetained ?? 0,
                    downloads.CleanupPending || models?.CleanupPending == true, downloads.Message + (models is null ? "" : " " + models.Message));
            }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or InvalidDataException or JsonException)
            { return new(false, false, 0, 0, true, "Pending cleanup could not be read. No model files were deleted. " + exception.Message); }
        }, cancellationToken);

    private static async Task<PocketTtsModelRemovalResult> RunAsync(string? alias, string? expectedIdentity,
        IProgress<PocketTtsModelInstallProgress>? progress, CancellationToken cancellationToken)
    {
        bool removed = false;
        long deleted = 0;
        int shared = 0;
        var inventory = new PocketTtsModelInventoryService();
        try
        {
            if (alias is null && (await inventory.ReadPendingAsync(cancellationToken).ConfigureAwait(false)).Count == 0)
            { return new(false, false, 0, 0, false, "No pending model cleanup."); }
            cancellationToken.ThrowIfCancellationRequested();
            // Same order as installation: installation lock, then model writer, then inventory/update writer.
            PocketTtsRuntimeStore.EnsureDirectory(PocketTtsRuntimeStore.LocalDataPath);
            using var installationLock = new FileStream(Path.Combine(PocketTtsRuntimeStore.LocalDataPath, "model-install.lock"),
                FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
            using var operations = PocketTtsModelStore.TryAcquireWrite();
            await PocketTtsNativeOperation.EnsureQuiescentAsync(cancellationToken).ConfigureAwait(false);
            if (alias is not null)
            {
                progress?.Report(new($"Removing {alias}'s registration..."));
                await inventory.CommitRemovalAsync(alias, expectedIdentity!, cancellationToken).ConfigureAwait(false);
                removed = true;
                // Cancellation after the durable commit cannot restore an installed registration.
                progress?.Report(new("Registration removed. Cleaning unreferenced private files...", CanCancel: false));
            }
            bool pending = false;
            var failures = new List<string>();
            foreach (var job in await inventory.ReadPendingAsync(CancellationToken.None).ConfigureAwait(false))
            {
                try
                {
                    cancellationToken.ThrowIfCancellationRequested();
                    var remaining = await inventory.ReadAsync(cancellationToken).ConfigureAwait(false);
                    var references = remaining.SelectMany(PocketTtsModelInventoryService.BlobReferences).ToHashSet(StringComparer.OrdinalIgnoreCase);
                    foreach (string reference in job.BlobPaths.Distinct(StringComparer.OrdinalIgnoreCase))
                    {
                        cancellationToken.ThrowIfCancellationRequested();
                        if (references.Contains(reference)) { shared++; continue; }
                        await PocketTtsNativeOperation.EnsureQuiescentAsync(cancellationToken).ConfigureAwait(false);
                        deleted += PocketTtsModelStore.DeleteRegularFile(PocketTtsModelInventoryService.ResolveBlobReference(reference));
                    }
                    await PocketTtsModelUpdateCoordinator.InvalidateAsync(job.Alias, cancellationToken).ConfigureAwait(false);
                    await inventory.CompleteCleanupAsync(job.Id, CancellationToken.None).ConfigureAwait(false);
                }
                catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or InvalidDataException or
                    InvalidOperationException or JsonException or OperationCanceledException)
                { pending = true; failures.Add(exception is OperationCanceledException ? "Cleanup was interrupted." : exception.Message); }
            }
            string subject = alias is null ? "Pending model cleanup processed." : $"{alias} was removed.";
            string bytes = deleted == 0 ? " No disk space reclaimed." : $" Deleted {deleted:N0} bytes of unreferenced private files.";
            string retained = shared == 0 ? "" : $" Preserved {shared} shared file reference(s) needed by installed models.";
            return new(removed, false, deleted, shared, pending, subject + bytes + retained +
                (pending ? " Cleanup is incomplete. Pending work is saved in the inventory and will be retried at startup or the next removal; busy, unsafe, or inaccessible files remain untouched. " + failures.FirstOrDefault() : ""));
        }
        catch (OperationCanceledException)
        { return new(removed, !removed, deleted, shared, removed, removed ? "Model registration removed; cleanup remains pending." : "Model removal cancelled. No registration or model files were changed."); }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or InvalidDataException or InvalidOperationException or JsonException)
        { return new(removed, false, deleted, shared, removed || alias is null, (removed ? "Registration removed; cleanup remains pending. " : "Model removal could not proceed. ") + exception.Message); }
    }
}
