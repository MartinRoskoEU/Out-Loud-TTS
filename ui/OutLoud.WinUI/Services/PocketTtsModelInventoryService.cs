using System.Text.Json;
using System.Security.Cryptography;
using System.Text.RegularExpressions;

namespace OutLoud.WinUI.Services;

/// <summary>Persistent installation records only. Never downloads, removes, or changes model files.</summary>
public sealed class PocketTtsModelInventoryService
{
    private const int SchemaVersion = 2;
    private const int MaximumModels = 4096;
    private const int MaximumDocumentBytes = 16 * 1024 * 1024;
    private static readonly SemaphoreSlim WriteGate = new(1, 1);
    private sealed record InventoryDocument(int SchemaVersion, IReadOnlyList<PocketTtsModelInventoryEntry> Models,
        IReadOnlyList<PocketTtsModelCleanup>? PendingCleanups = null);

    public static string InventoryPath => Path.Combine(PocketTtsRuntimeStore.LocalDataPath, "model-inventory.json");

    // Includes commits, every required file/hash/location, and configuration. Identity alone is not installation proof.
    internal static string GetIdentity(PocketTtsModelInventoryEntry entry) =>
        Convert.ToHexString(SHA256.HashData(JsonSerializer.SerializeToUtf8Bytes(entry)));

    public async Task<IReadOnlyList<PocketTtsModelInventoryEntry>> ReadAsync(CancellationToken cancellationToken = default) =>
        (await ReadDocumentAsync(cancellationToken).ConfigureAwait(false)).Models;

    internal async Task<IReadOnlyList<PocketTtsModelCleanup>> ReadPendingAsync(CancellationToken cancellationToken = default) =>
        (await ReadDocumentAsync(cancellationToken).ConfigureAwait(false)).PendingCleanups ?? [];

    private async Task<InventoryDocument> ReadDocumentAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        try
        {
            await using FileStream stream = new(InventoryPath, FileMode.Open, FileAccess.Read,
                FileShare.Read | FileShare.Delete, 65536, FileOptions.Asynchronous);
            if (stream.Length > MaximumDocumentBytes) { throw new InvalidDataException("The local model inventory is too large."); }
            var document = await JsonSerializer.DeserializeAsync<InventoryDocument>(stream, cancellationToken: cancellationToken).ConfigureAwait(false);
            if (document is null || document.SchemaVersion != SchemaVersion || document.Models is null || document.Models.Count > MaximumModels)
            { throw new InvalidDataException("The model inventory format is incompatible or invalid. Close the app and manually archive the outdated development model-inventory.json before reinstalling. No records have been changed."); }
            var aliases = new HashSet<string>(StringComparer.Ordinal);
            foreach (var entry in document.Models)
            {
                if (entry is null || !IsAlias(entry.Alias) || !aliases.Add(entry.Alias))
                { throw new InvalidDataException("The local model inventory has invalid or duplicate identifiers."); }
            }
            if (document.PendingCleanups is { } pending)
            {
                var ids = new HashSet<string>(StringComparer.Ordinal);
                if (pending.Count > MaximumModels) { throw new InvalidDataException("Too many pending model cleanups."); }
                foreach (var job in pending)
                {
                    if (job is null || !Guid.TryParseExact(job.Id, "N", out _) || !ids.Add(job.Id) || !IsAlias(job.Alias) ||
                        job.BlobPaths is null || job.BlobPaths.Count > 256)
                    { throw new InvalidDataException("Invalid pending model cleanup metadata. No files were deleted."); }
                    foreach (string path in job.BlobPaths) { _ = ResolveBlobReference(path); }
                }
            }
            return document;
        }
        catch (FileNotFoundException) { return new(SchemaVersion, []); }
        catch (DirectoryNotFoundException) { return new(SchemaVersion, []); }
        catch (JsonException exception)
        { throw new InvalidDataException("The local model inventory contains invalid JSON. The file has not been changed.", exception); }
    }

    /// <summary>Register only verified files with trusted repository hashes/configuration.</summary>
    public async Task RegisterAfterInstallationAsync(PocketTtsModelInventoryEntry entry, CancellationToken cancellationToken = default,
        bool replaceExisting = true)
    {
        ArgumentNullException.ThrowIfNull(entry);
        cancellationToken.ThrowIfCancellationRequested();
        using FileStream modelWriteLease = await PocketTtsModelStore.AcquireWriteAsync(cancellationToken).ConfigureAwait(false);
        if (entry.RequiredFiles is null) { throw new InvalidDataException("The model inventory record has no required files."); }
        var snapshot = entry with { RequiredFiles = entry.RequiredFiles.ToArray() };
        var model = CreateModel(snapshot);
        if (snapshot.ConfigurationYaml is not null) { _ = model.LocalConfiguration(); }
        var leases = await model.OpenVerifiedAssetsAsync(cancellationToken, requireVoiceCloning: false).ConfigureAwait(false);
        try
        {
            // Keep verified files read-locked through publication of their inventory record.
            await UpdateAsync(document =>
            {
                var entries = document.Models;
                if (!replaceExisting && entries.Any(existing => existing.Alias == snapshot.Alias))
                { throw new InvalidOperationException("This model is already registered. Model replacement is not supported."); }
                return document with { Models = entries.Where(existing => existing.Alias != snapshot.Alias).Append(snapshot).ToArray() };
            }, cancellationToken).ConfigureAwait(false);
        }
        finally { foreach (FileStream lease in leases) { lease.Dispose(); } }
    }

    // Caller holds the exclusive model operation lease and installation lock throughout commit/cleanup.
    internal async Task CommitRemovalAsync(string alias, string expectedIdentity, CancellationToken cancellationToken)
    {
        if (!IsAlias(alias)) { throw new ArgumentException("A valid model alias is required.", nameof(alias)); }
        await UpdateAsync(document =>
        {
            var removed = document.Models.SingleOrDefault(entry => entry.Alias == alias);
            if (removed is null || GetIdentity(removed) != expectedIdentity)
            { throw new InvalidOperationException("The model registration changed. Refresh Models and confirm removal again."); }
            // Every entry contributes references, even if unsupported, corrupt, or missing files.
            foreach (var entry in document.Models) { _ = BlobReferences(entry); }
            var job = new PocketTtsModelCleanup(Guid.NewGuid().ToString("N"), alias, BlobReferences(removed));
            return document with { Models = document.Models.Where(entry => entry.Alias != alias).ToArray(),
                PendingCleanups = (document.PendingCleanups ?? []).Append(job).ToArray() };
        }, cancellationToken).ConfigureAwait(false);
    }

    internal Task CompleteCleanupAsync(string id, CancellationToken cancellationToken) =>
        UpdateAsync(document => document with { PendingCleanups = (document.PendingCleanups ?? []).Where(job => job.Id != id).ToArray() }, cancellationToken);

    internal static IReadOnlyList<string> BlobReferences(PocketTtsModelInventoryEntry entry)
    {
        if (!string.Equals(entry.InstallationDirectory, PocketTtsModelStore.Root, StringComparison.OrdinalIgnoreCase) ||
            entry.RequiredFiles is null || entry.RequiredFiles.Count is < 1 or > 256)
        { throw new InvalidDataException("An inventory entry has invalid private asset ownership. Resolve its metadata before removal."); }
        var references = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var file in entry.RequiredFiles)
        {
            if (file is null || file.SizeBytes <= 0 ||
                !(string.Equals(file.HashAlgorithm, "SHA256", StringComparison.OrdinalIgnoreCase) || string.Equals(file.HashAlgorithm, "GitBlobSHA1", StringComparison.OrdinalIgnoreCase)))
            { throw new InvalidDataException("Invalid private asset ownership metadata."); }
            string expected = PocketTtsModelStore.BlobPath(string.Equals(file.HashAlgorithm, "SHA256", StringComparison.OrdinalIgnoreCase), file.ExpectedHash);
            if (!string.Equals(ResolveBlobReference(file.RelativePath), expected, StringComparison.OrdinalIgnoreCase))
            { throw new InvalidDataException("An inventory asset is not its canonical private blob."); }
            references.Add(Path.GetRelativePath(PocketTtsModelStore.Root, expected));
        }
        return references.ToArray();
    }

    internal static string ResolveBlobReference(string reference)
    {
        if (reference is null || !Regex.IsMatch(reference, @"\Ablobs\\(?:sha256\\[a-f0-9]{64}|git-blob-sha1\\[a-f0-9]{40})\z"))
        { throw new InvalidDataException("Cleanup accepts only canonical private blob references."); }
        return Path.Combine(PocketTtsModelStore.Root, reference);
    }

    private async Task UpdateAsync(Func<InventoryDocument, InventoryDocument> update,
        CancellationToken cancellationToken)
    {
        await WriteGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        string? temporary = null;
        PocketTtsDownloadOperation? temporaryOperation = null;
        try
        {
            PocketTtsRuntimeStore.EnsureDirectory(PocketTtsRuntimeStore.LocalDataPath);
            using var inventoryLock = new FileStream(Path.Combine(PocketTtsRuntimeStore.LocalDataPath, "model-inventory.lock"),
                FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
            // An unreadable/corrupt inventory is never overwritten by registration or unregistration.
            var current = await ReadDocumentAsync(cancellationToken).ConfigureAwait(false);
            var next = update(current);
            var models = next.Models.OrderBy(entry => entry.Alias, StringComparer.Ordinal).ToArray();
            if (models.Length > MaximumModels || next.PendingCleanups?.Count > MaximumModels)
            { throw new InvalidDataException("The local model inventory contains too many models or pending cleanups."); }
            var document = next with { Models = models };
            if (JsonSerializer.SerializeToUtf8Bytes(document).Length > MaximumDocumentBytes)
            { throw new InvalidDataException("The local model inventory exceeds its size limit."); }
            temporaryOperation = await PocketTtsDownloadOperation.BeginMetadataAsync("model-inventory", cancellationToken).ConfigureAwait(false);
            temporary = temporaryOperation.MetadataPath;
            await PocketTtsRuntimeStore.WriteJsonAsync(temporary, document, cancellationToken).ConfigureAwait(false);
            cancellationToken.ThrowIfCancellationRequested();
            if (File.Exists(InventoryPath))
            { File.Replace(temporary, InventoryPath, Path.Combine(PocketTtsRuntimeStore.LocalDataPath, "model-inventory.previous.json")); }
            else { File.Move(temporary, InventoryPath); }
        }
        finally
        {
            if (temporaryOperation is not null)
            {
                // Cleanup failure cannot turn a completed atomic commit into an apparent registration failure.
                try { _ = await temporaryOperation.CleanupAsync().ConfigureAwait(false); }
                catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or InvalidDataException) { }
            }
            WriteGate.Release();
        }
    }

    internal static PocketTtsInstalledModel CreateModel(PocketTtsModelInventoryEntry entry)
    {
        if (!IsAlias(entry.Alias) || entry.RepositoryId != PocketTtsModelInstallPlan.Repository || !IsRepository(entry.RepositoryId) || !IsRevision(entry.Revision) ||
            string.IsNullOrWhiteSpace(entry.InstallationDirectory) || !Path.IsPathFullyQualified(entry.InstallationDirectory) ||
            entry.RequiredFiles is null || entry.RequiredFiles.Count is < 1 or > 256 ||
            string.IsNullOrWhiteSpace(entry.ConfigurationYaml) || entry.ConfigurationYaml.Length > 512 * 1024)
        { throw new InvalidDataException("The stored model installation metadata is incomplete or invalid."); }

        string root = Path.GetFullPath(entry.InstallationDirectory);
        if (!string.Equals(root, PocketTtsModelStore.Root, StringComparison.OrdinalIgnoreCase))
        { throw new InvalidDataException("Model inventory paths must reference the private application models directory. External paths are not managed installations."); }
        PocketTtsModelStore.ValidatePath(root);
        if (!Directory.Exists(root)) { throw new InvalidDataException("The registered model installation directory is missing or inaccessible."); }
        var paths = new Dictionary<string, long>(StringComparer.OrdinalIgnoreCase);
        var references = new HashSet<string>(StringComparer.Ordinal);
        var assets = new List<PocketTtsCachedAsset>();
        bool hasPrimaryWeights = false;
        foreach (var file in entry.RequiredFiles)
        {
            if (file is null) { throw new InvalidDataException("The stored model file metadata is invalid."); }
            string repository = file.RepositoryId ?? entry.RepositoryId;
            string revision = file.Revision ?? entry.Revision;
            if (repository != PocketTtsModelInstallPlan.Repository || !IsRepository(repository) || !IsRevision(revision) || file.SizeBytes <= 0 ||
                !IsRelativePath(file.RelativePath) || !IsRelativePath(file.RepositoryPath))
            { throw new InvalidDataException("The stored model file paths or repository identity are invalid."); }
            bool sha256 = string.Equals(file.HashAlgorithm, "SHA256", StringComparison.OrdinalIgnoreCase);
            bool gitBlob = string.Equals(file.HashAlgorithm, "GitBlobSHA1", StringComparison.OrdinalIgnoreCase);
            if ((!sha256 && !gitBlob) || file.ExpectedHash is null ||
                !Regex.IsMatch(file.ExpectedHash, sha256 ? @"\A[a-fA-F0-9]{64}\z" : @"\A[a-fA-F0-9]{40}\z"))
            { throw new InvalidDataException("A stored model file has an invalid expected hash or hash algorithm."); }
            string path = Path.GetFullPath(Path.Combine(root, file.RelativePath.Replace('\\', Path.DirectorySeparatorChar).Replace('/', Path.DirectorySeparatorChar)));
            string expectedPath = PocketTtsModelStore.BlobPath(sha256, file.ExpectedHash);
            string reference = $"hf://{repository}/{file.RepositoryPath.Replace('\\', '/')}@{revision.ToLowerInvariant()}";
            if (!string.Equals(path, expectedPath, StringComparison.OrdinalIgnoreCase) || !references.Add(reference) ||
                (paths.TryGetValue(path, out long existingSize) && existingSize != file.SizeBytes))
            { throw new InvalidDataException("Model assets must use canonical private content-addressed references with consistent sizes."); }
            PocketTtsModelStore.ValidatePath(path);
            paths[path] = file.SizeBytes;
            assets.Add(new(reference, path, file.SizeBytes, file.ExpectedHash, sha256, file.IsWeights));
            hasPrimaryWeights |= file.IsWeights && repository == entry.RepositoryId && string.Equals(revision, entry.Revision, StringComparison.OrdinalIgnoreCase);
        }
        if (!hasPrimaryWeights) { throw new InvalidDataException("The stored model record has no primary repository weights."); }
        var model = new PocketTtsInstalledModel(entry.Alias, entry.ConfigurationYaml, assets.AsReadOnly())
        { InventoryIdentity = GetIdentity(entry) };
        _ = model.LocalConfiguration();
        return model;
    }

    private static bool IsAlias(string? alias) => alias is not null && Regex.IsMatch(alias, @"\A[A-Za-z0-9][A-Za-z0-9_.-]{0,127}\z");
    private static bool IsRepository(string? repository) => repository is not null && repository.Length <= 256 &&
        Regex.IsMatch(repository, @"\A[A-Za-z0-9][A-Za-z0-9_.-]*/[A-Za-z0-9][A-Za-z0-9_.-]*\z");
    private static bool IsRevision(string? revision) => revision is not null && Regex.IsMatch(revision, @"\A[a-fA-F0-9]{40}\z");
    private static bool IsRelativePath(string? path) => !string.IsNullOrWhiteSpace(path) && !Path.IsPathRooted(path) &&
        !path.Any(character => char.IsControl(character) || character is '@' or '#' or '?') &&
        path.Replace('\\', '/').Split('/').All(part => part.Length > 0 && part is not ("." or "..") &&
            !part.EndsWith('.') && !part.EndsWith(' ') && part.IndexOfAny(Path.GetInvalidFileNameChars()) < 0);
}
