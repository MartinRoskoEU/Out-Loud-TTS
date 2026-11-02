using System.Net;
using System.Net.Http;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace OutLoud.WinUI.Services;

public enum PocketTtsModelUpdateStatus { Checking, UpToDate, UpdateAvailable, Unavailable }

public sealed record PocketTtsModelUpdateResult(string Alias, PocketTtsModelUpdateStatus Status, string Message,
    string? ComparedCommit = null)
{
    public string? InstalledIdentity { get; init; }
    public string StatusText => Status switch
    {
        PocketTtsModelUpdateStatus.Checking => "Checking for updates...",
        PocketTtsModelUpdateStatus.UpToDate => "Up to date",
        PocketTtsModelUpdateStatus.UpdateAvailable => "Update available",
        _ => "Unable to check",
    };
}

public sealed record PocketTtsModelUpdateCheckResult(IReadOnlyList<PocketTtsModelUpdateResult> Models, string? Message = null);

/// <summary>Anonymous comparison only. Never obtains credentials, downloads assets, or writes inventory.</summary>
public sealed class PocketTtsModelUpdateService(HttpClient http)
{
    // Compare the same synthesis-only repository used by Out Loud's install plans.
    private const string Repository = PocketTtsModelInstallPlan.Repository;
    private const string Api = "https://huggingface.co/api/models/" + Repository;
    private const int MaximumMetadataBytes = 16 * 1024 * 1024;
    private sealed record RemoteFile(long Size, string? GitOid, string? PayloadSha256, bool LargeFile);

    internal async Task<PocketTtsModelInstallPlan> ResolveUpdateAsync(string alias, string revision, CancellationToken token)
    {
        if (!IsHash(revision, 40)) throw new InvalidDataException("A pinned model update revision is required.");
        var template = await PocketTtsModelInstallPlan.LoadAsync(alias, token).ConfigureAwait(false)
            ?? throw new InvalidDataException("No trusted configuration exists for this model.");
        using var metadata = await GetMetadataAsync(Api + "/revision/" + revision + "?blobs=true", token).ConfigureAwait(false);
        var root = metadata.RootElement;
        if (root.GetProperty("id").GetString() != Repository || root.GetProperty("sha").GetString() != revision)
            throw new InvalidDataException("The official model update snapshot changed.");
        var listing = ParseFiles(root.GetProperty("siblings"), tree: false);
        var files = new List<PocketTtsModelInventoryFile>();
        foreach (var original in template.Files)
        {
            listing.TryGetValue(original.RepositoryPath, out var remote);
            bool sha = original.Asset.Lfs;
            if (!CanCompare(remote, sha))
            {
                int slash = original.RepositoryPath.LastIndexOf('/');
                string suffix = slash < 0 ? "" : "/" + string.Join('/', original.RepositoryPath[..slash].Split('/').Select(Uri.EscapeDataString));
                using var tree = await GetMetadataAsync(Api + "/tree/" + revision + suffix + "?recursive=false&expand=false", token).ConfigureAwait(false);
                ParseFiles(tree.RootElement, tree: true).TryGetValue(original.RepositoryPath, out remote);
            }
            if (!CanCompare(remote, sha)) throw new InvalidDataException("Complete official hashes and sizes are unavailable for this model update.");
            string hash = (sha ? remote!.PayloadSha256 : remote!.GitOid)!.ToLowerInvariant();
            files.Add(new(Path.GetRelativePath(PocketTtsModelStore.Root, PocketTtsModelStore.BlobPath(sha, hash)),
                original.RepositoryPath, remote.Size, hash, sha ? "SHA256" : "GitBlobSHA1", original.Asset.Weights, Repository, revision));
        }
        return template.WithUpdatedFiles(revision, files);
    }

    public Task<PocketTtsModelUpdateCheckResult> CheckAsync(IProgress<PocketTtsModelUpdateResult>? progress = null,
        CancellationToken cancellationToken = default, IReadOnlyDictionary<string, string>? requestedIdentities = null) =>
        Task.Run(() => CheckCoreAsync(progress, cancellationToken, requestedIdentities), cancellationToken);

    private async Task<PocketTtsModelUpdateCheckResult> CheckCoreAsync(IProgress<PocketTtsModelUpdateResult>? progress,
        CancellationToken cancellationToken, IReadOnlyDictionary<string, string>? requestedIdentities)
    {
        IReadOnlyList<PocketTtsModelInventoryEntry> inventory;
        try { inventory = await new PocketTtsModelInventoryService().ReadAsync(cancellationToken).ConfigureAwait(false); }
        catch (Exception exception) when (exception is IOException or InvalidDataException or UnauthorizedAccessException or JsonException)
        { return new(Array.Empty<PocketTtsModelUpdateResult>(), "Unable to check: the saved model inventory could not be read. It has not been changed."); }
        var identities = inventory.ToDictionary(entry => entry.Alias, PocketTtsModelInventoryService.GetIdentity, StringComparer.Ordinal);
        if (requestedIdentities is not null)
        { inventory = inventory.Where(entry => requestedIdentities.TryGetValue(entry.Alias, out var identity) && identity == identities[entry.Alias]).ToArray(); }
        if (inventory.Count == 0)
        { return new(Array.Empty<PocketTtsModelUpdateResult>(), "No registered model installations to check."); }

        var results = new Dictionary<string, PocketTtsModelUpdateResult>(StringComparer.Ordinal);
        var installed = new List<PocketTtsModelInventoryEntry>();
        void Report(PocketTtsModelUpdateResult result)
        {
            result = result with { InstalledIdentity = identities[result.Alias] };
            results[result.Alias] = result;
            progress?.Report(result);
        }
        foreach (var entry in inventory)
        {
            cancellationToken.ThrowIfCancellationRequested();
            Report(new(entry.Alias, PocketTtsModelUpdateStatus.Checking, "Verifying the registered local installation before comparing metadata."));
            try
            {
                using FileStream? modelReadLease = await PocketTtsModelStore.AcquireReadAsync(cancellationToken).ConfigureAwait(false);
                var model = PocketTtsModelInventoryService.CreateModel(entry);
                if (entry.RepositoryId != Repository || entry.RequiredFiles.Any(file =>
                    (file.RepositoryId ?? entry.RepositoryId) != Repository ||
                    (file.IsWeights && !file.HashAlgorithm.Equals("SHA256", StringComparison.OrdinalIgnoreCase))))
                { throw new InvalidDataException("Unsupported source or weight identity."); }
                // An inventory record alone must not turn an absent or damaged model into an installed model.
                // Compatibility is independent: verified models unsupported by the runtime can still be compared.
                var leases = await model.OpenVerifiedAssetsAsync(cancellationToken, requireVoiceCloning: false).ConfigureAwait(false);
                foreach (var lease in leases) { lease.Dispose(); }
                installed.Add(entry);
            }
            catch (Exception exception) when (exception is IOException or InvalidDataException or UnauthorizedAccessException or
                JsonException or ArgumentException or InvalidOperationException or NotSupportedException)
            { Report(new(entry.Alias, PocketTtsModelUpdateStatus.Unavailable, "The registered local files or their expected identities could not be verified. No remote check was made for this model.")); }
        }
        if (installed.Count == 0)
        { return new(results.Values.ToArray(), "Unable to check: no registered installation passed local verification."); }

        string? commit = null;
        try
        {
            using JsonDocument metadata = await GetMetadataAsync(Api + "/revision/main?blobs=true", cancellationToken).ConfigureAwait(false);
            JsonElement root = metadata.RootElement;
            if (root.GetProperty("id").GetString() != Repository || !root.TryGetProperty("sha", out var sha) || !IsHash(sha.GetString(), 40))
            { throw new InvalidDataException("Missing main identity."); }
            commit = sha.GetString()!;
            var mainFiles = ParseFiles(root.GetProperty("siblings"), tree: false);
            // Fallbacks are pinned to this exact main snapshot, not to a branch that can move between requests.
            var treeCache = new Dictionary<string, IReadOnlyDictionary<string, RemoteFile>?>(StringComparer.Ordinal);
            foreach (var entry in installed)
            {
                cancellationToken.ThrowIfCancellationRequested();
                bool changed = false;
                bool complete = true;
                foreach (var file in entry.RequiredFiles)
                {
                    string path = file.RepositoryPath.Replace('\\', '/');
                    bool payload = file.HashAlgorithm.Equals("SHA256", StringComparison.OrdinalIgnoreCase);
                    mainFiles.TryGetValue(path, out var remote);
                    if (!CanCompare(remote, payload))
                    {
                        int separator = path.LastIndexOf('/');
                        string directory = separator < 0 ? "" : path[..separator];
                        if (!treeCache.TryGetValue(directory, out var tree))
                        {
                            try
                            {
                                string suffix = directory.Length == 0 ? "" : "/" + string.Join('/', directory.Split('/').Select(Uri.EscapeDataString));
                                using var listing = await GetMetadataAsync(Api + "/tree/" + commit + suffix + "?recursive=false&expand=false", cancellationToken).ConfigureAwait(false);
                                tree = ParseFiles(listing.RootElement, tree: true);
                            }
                            catch (Exception exception) when (exception is HttpRequestException or IOException or JsonException or InvalidDataException or
                                InvalidOperationException or KeyNotFoundException or FormatException or TimeoutException)
                            { tree = null; }
                            treeCache[directory] = tree;
                        }
                        remote = tree is not null && tree.TryGetValue(path, out var fallback) ? fallback : null;
                    }
                    if (!CanCompare(remote, payload)) { complete = false; break; }
                    string digest = (payload ? remote!.PayloadSha256 : remote!.GitOid)!;
                    bool sameHash = digest.Equals(file.ExpectedHash, StringComparison.OrdinalIgnoreCase);
                    // Identical content identifiers with different sizes are inconsistent metadata, not an update.
                    if (sameHash && remote.Size != file.SizeBytes) { complete = false; break; }
                    changed |= !sameHash || remote.Size != file.SizeBytes;
                }
                Report(!complete
                    ? new(entry.Alias, PocketTtsModelUpdateStatus.Unavailable, "Complete, unredacted file identities and consistent sizes were not available anonymously for every required file.", commit)
                    : changed
                        ? new(entry.Alias, PocketTtsModelUpdateStatus.UpdateAvailable, "Required file content differs from the installed inventory. No update has been downloaded or installed.", commit)
                        : new(entry.Alias, PocketTtsModelUpdateStatus.UpToDate, "Every recorded required file hash and size matches this main revision. This does not check for new model aliases or configuration changes.", commit));
            }
        }
        catch (Exception exception) when (exception is HttpRequestException or IOException or JsonException or InvalidDataException or
            InvalidOperationException or KeyNotFoundException or FormatException or TimeoutException)
        {
            foreach (var entry in installed.Where(entry => !results.TryGetValue(entry.Alias, out var result) || result.Status == PocketTtsModelUpdateStatus.Checking))
            { Report(new(entry.Alias, PocketTtsModelUpdateStatus.Unavailable, "Anonymous Hugging Face metadata is unavailable. Check your connection or retry later; sign-in is not requested.", commit)); }
        }
        return new(results.Values.ToArray());
    }

    private static bool CanCompare(RemoteFile? file, bool payload) => file is { Size: > 0 } &&
        (payload ? IsHash(file.PayloadSha256, 64) : !file.LargeFile && IsHash(file.GitOid, 40));

    private static bool IsHash(string? value, int length) => value is not null && value.Length == length &&
        Regex.IsMatch(value, @"\A[a-fA-F0-9]+\z");

    private static IReadOnlyDictionary<string, RemoteFile> ParseFiles(JsonElement files, bool tree)
    {
        if (files.ValueKind != JsonValueKind.Array) { throw new InvalidDataException("Invalid metadata listing."); }
        var result = new Dictionary<string, RemoteFile>(StringComparer.Ordinal);
        foreach (var file in files.EnumerateArray())
        {
            if (tree && file.TryGetProperty("type", out var type) && type.GetString() != "file") { continue; }
            string? path = file.GetProperty(tree ? "path" : "rfilename").GetString();
            if (string.IsNullOrEmpty(path)) { throw new InvalidDataException("Invalid file path."); }
            long size = file.TryGetProperty("size", out var sizeElement) && sizeElement.TryGetInt64(out long bytes) ? bytes : -1;
            string? oid = file.TryGetProperty(tree ? "oid" : "blobId", out var id) ? id.GetString() : null;
            bool lfs = file.TryGetProperty("lfs", out var lfsElement) && lfsElement.ValueKind == JsonValueKind.Object;
            string? digest = lfs && lfsElement.TryGetProperty(tree ? "oid" : "sha256", out var hash) ? hash.GetString() : null;
            if (lfs && lfsElement.TryGetProperty("size", out var lfsSize) &&
                (!lfsSize.TryGetInt64(out long payloadBytes) || payloadBytes != size)) { size = -1; }
            // xetHash is a separate storage identifier, never a substitute for a payload SHA-256.
            bool largeFile = lfs || (file.TryGetProperty("xetHash", out var xet) && xet.ValueKind != JsonValueKind.Null);
            if (!result.TryAdd(path, new(size, oid, digest, largeFile))) { throw new InvalidDataException("Duplicate file metadata."); }
        }
        return result;
    }

    private async Task<JsonDocument> GetMetadataAsync(string url, CancellationToken cancellationToken)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(10));
        try
        {
            if (http.DefaultRequestHeaders.Authorization is not null)
            { throw new InvalidOperationException("Model update metadata requires an anonymous client."); }
            using var request = new HttpRequestMessage(HttpMethod.Get, url);
            request.Headers.Accept.ParseAdd("application/json");
            request.Headers.UserAgent.ParseAdd(ApplicationInfo.UserAgent);
            using var response = await http.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, timeout.Token).ConfigureAwait(false);
            if (response.StatusCode != HttpStatusCode.OK) { throw new HttpRequestException("Metadata request failed.", null, response.StatusCode); }
            if (response.Content.Headers.ContentLength > MaximumMetadataBytes) { throw new InvalidDataException("Metadata too large."); }
            await using Stream source = await response.Content.ReadAsStreamAsync(timeout.Token).ConfigureAwait(false);
            using var buffer = new MemoryStream();
            byte[] block = new byte[65536];
            int count;
            while ((count = await source.ReadAsync(block, timeout.Token).ConfigureAwait(false)) > 0)
            {
                if (buffer.Length + count > MaximumMetadataBytes) { throw new InvalidDataException("Metadata too large."); }
                buffer.Write(block, 0, count);
            }
            return JsonDocument.Parse(buffer.GetBuffer().AsMemory(0, (int)buffer.Length));
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        { throw new TimeoutException("Metadata request timed out."); }
    }
}
