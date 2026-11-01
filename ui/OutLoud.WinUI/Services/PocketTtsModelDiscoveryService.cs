using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Text.Json;

namespace OutLoud.WinUI.Services;

/// <summary>Online metadata discovery from the official Hub repository, with no runtime or UI dependency.</summary>
public sealed class PocketTtsModelDiscoveryService
{
    public const string RepositoryId = "kyutai/pocket-tts-without-voice-cloning";
    private const long MaximumResponseBytes = 8 * 1024 * 1024;
    private readonly HttpClient _httpClient;

    /// <summary>The caller owns and reuses the HttpClient. Authentication is not configured here.</summary>
    public PocketTtsModelDiscoveryService(HttpClient httpClient)
    {
        ArgumentNullException.ThrowIfNull(httpClient);
        _httpClient = httpClient;
    }

    public async Task<PocketTtsModelDiscoveryResult> DiscoverAsync(
        string revision = "main", CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(revision);
        cancellationToken.ThrowIfCancellationRequested();
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        using var operation = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, timeout.Token);
        var uri = new Uri($"https://huggingface.co/api/models/{RepositoryId}/revision/{Uri.EscapeDataString(revision)}?blobs=true");
        using var request = new HttpRequestMessage(HttpMethod.Get, uri);
        request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue("application/json"));
        request.Headers.UserAgent.ParseAdd(ApplicationInfo.UserAgent);
        try
        {
            using HttpResponseMessage response = await _httpClient.SendAsync(
                request, HttpCompletionOption.ResponseHeadersRead, operation.Token).ConfigureAwait(false);
            if (!response.IsSuccessStatusCode)
            {
                string reason = response.StatusCode switch
                {
                    HttpStatusCode.Unauthorized or HttpStatusCode.Forbidden =>
                        "Metadata access was denied. Authentication is not implemented for discovery.",
                    HttpStatusCode.NotFound => "The official repository or requested revision was not found.",
                    HttpStatusCode.TooManyRequests => "The Hub rate limit was reached. Retry discovery later.",
                    _ => "The Hub could not provide model metadata. Retry discovery when the service is available.",
                };
                throw new HttpRequestException($"Hugging Face model discovery failed (HTTP {(int)response.StatusCode}). {reason}",
                    null, response.StatusCode);
            }

            string? mediaType = response.Content.Headers.ContentType?.MediaType;
            if (mediaType is null || !(mediaType.Equals("application/json", StringComparison.OrdinalIgnoreCase) ||
                mediaType.EndsWith("+json", StringComparison.OrdinalIgnoreCase)))
            {
                throw InvalidResponse("Expected a JSON model-info response.");
            }

            // model_info returns its complete siblings list, not paginated tree results.
            if (response.Headers.TryGetValues("Link", out IEnumerable<string>? links) &&
                links.Any(link => link.Contains("rel=\"next\"", StringComparison.OrdinalIgnoreCase) ||
                                  link.Contains("rel=next", StringComparison.OrdinalIgnoreCase)))
            {
                throw InvalidResponse("Unexpected pagination in model-info; refusing an incomplete inventory.");
            }

            if (response.Content.Headers.ContentLength > MaximumResponseBytes)
            {
                throw InvalidResponse("Model metadata exceeds the 8 MiB response limit.");
            }

            await response.Content.LoadIntoBufferAsync(MaximumResponseBytes, operation.Token).ConfigureAwait(false);
            using JsonDocument json = JsonDocument.Parse(
                await response.Content.ReadAsByteArrayAsync(operation.Token).ConfigureAwait(false));
            return ParseResult(json.RootElement, revision, operation.Token);
        }
        catch (OperationCanceledException exception) when (cancellationToken.IsCancellationRequested)
        {
            throw new OperationCanceledException("Model discovery was cancelled by the caller.", exception, cancellationToken);
        }
        catch (OperationCanceledException exception)
        {
            throw new TimeoutException("Hugging Face model discovery timed out. Internet access is required; retry when connected.", exception);
        }
        catch (HttpRequestException exception) when (exception.HttpRequestError == HttpRequestError.ConfigurationLimitExceeded)
        {
            throw InvalidResponse("Model metadata exceeds the response buffering limit.", exception);
        }
        catch (HttpRequestException exception) when (exception.StatusCode is null)
        {
            throw new HttpRequestException(
                "Could not retrieve Hugging Face model metadata. Internet access is required; check the connection and retry.", exception);
        }
        catch (JsonException exception)
        {
            throw InvalidResponse("The response contains malformed JSON.", exception);
        }
    }

    private static PocketTtsModelDiscoveryResult ParseResult(
        JsonElement root, string requestedRevision, CancellationToken cancellationToken)
    {
        RequireObject(root);
        if (RequiredString(root, "id") != RepositoryId)
        {
            throw InvalidResponse("The response identifies a different repository.");
        }

        string revision = RequiredString(root, "sha");
        if (!IsHash(revision, 40) || (IsHash(requestedRevision, 40) &&
            !revision.Equals(requestedRevision, StringComparison.OrdinalIgnoreCase)))
        {
            throw InvalidResponse("Missing, invalid, or mismatched immutable repository revision.");
        }

        revision = revision.ToLowerInvariant();
        if (!root.TryGetProperty("siblings", out JsonElement siblings) || siblings.ValueKind != JsonValueKind.Array)
        {
            throw InvalidResponse("The repository file inventory is missing or is not an array.");
        }

        var files = new SortedDictionary<string, PocketTtsModelFile>(StringComparer.Ordinal);
        foreach (JsonElement sibling in siblings.EnumerateArray())
        {
            cancellationToken.ThrowIfCancellationRequested();
            RequireObject(sibling);
            string path = RequiredString(sibling, "rfilename");
            if (path.Contains('\\') || path.Any(char.IsControl) || path.Split('/').Any(part => part is "" or "." or ".."))
            {
                throw InvalidResponse("The inventory contains an invalid repository-relative path.");
            }

            long? size = OptionalSize(sibling, "size");
            string? blobId = OptionalHash(sibling, "blobId", 40);
            PocketTtsLfsMetadata? lfs = null;
            if (sibling.TryGetProperty("lfs", out JsonElement lfsElement) && lfsElement.ValueKind != JsonValueKind.Null)
            {
                RequireObject(lfsElement);
                lfs = new(OptionalHash(lfsElement, "sha256", 64),
                    OptionalSize(lfsElement, "size"), OptionalSize(lfsElement, "pointerSize"));
                if (size is not null && lfs.SizeBytes is not null && size != lfs.SizeBytes)
                {
                    throw InvalidResponse("File size and LFS size disagree.");
                }
            }

            if (!files.TryAdd(path, new(path, revision, size, blobId, lfs)))
            {
                throw InvalidResponse("The inventory contains duplicate file paths.");
            }
        }

        var models = new List<PocketTtsDiscoveredModel>();
        // Discover layout, not a fixed language/version list. Embedding subdirectories are not models.
        var languageWeights = files.Values.Where(file =>
            file.RepositoryPath.Split('/') is ["languages", _, var name] &&
            name.EndsWith(".safetensors", StringComparison.Ordinal));
        foreach (var group in languageWeights.GroupBy(file => file.RepositoryPath[..file.RepositoryPath.LastIndexOf('/')]))
        {
            string prefix = group.Key + "/";
            models.Add(new($"{RepositoryId}:{group.Key}", group.Key["languages/".Length..], RepositoryId, group.Key, revision,
                Array.AsReadOnly(group.Select(file => file.RepositoryPath).ToArray()),
                Array.AsReadOnly(files.Values.Where(file => file.RepositoryPath.StartsWith(prefix, StringComparison.Ordinal)).ToArray())));
        }

        foreach (PocketTtsModelFile rootWeight in files.Values.Where(file =>
            !file.RepositoryPath.Contains('/') && file.RepositoryPath.EndsWith(".safetensors", StringComparison.Ordinal)))
        {
            models.Add(new($"{RepositoryId}:{rootWeight.RepositoryPath}", Path.GetFileNameWithoutExtension(rootWeight.RepositoryPath),
                RepositoryId, "", revision, Array.AsReadOnly(new[] { rootWeight.RepositoryPath }),
                Array.AsReadOnly(files.Values.Where(file => !file.RepositoryPath.Contains('/') &&
                    (!file.RepositoryPath.EndsWith(".safetensors", StringComparison.Ordinal) || file == rootWeight)).ToArray())));
        }

        if (models.Count == 0)
        {
            throw InvalidResponse("No model candidates were recognized. The repository layout may have changed.");
        }

        string? gating = null;
        if (root.TryGetProperty("gated", out JsonElement gated) && gated.ValueKind != JsonValueKind.Null)
        {
            gating = gated.ValueKind switch
            {
                JsonValueKind.String => gated.GetString(),
                JsonValueKind.True => "true",
                JsonValueKind.False => "false",
                _ => throw InvalidResponse("Unexpected repository gating metadata."),
            };
        }

        cancellationToken.ThrowIfCancellationRequested();
        return new(RepositoryId, requestedRevision, revision, DateTimeOffset.UtcNow, gating,
            Array.AsReadOnly(files.Values.ToArray()), Array.AsReadOnly(models.OrderBy(model => model.Id, StringComparer.Ordinal).ToArray()));
    }

    private static void RequireObject(JsonElement element)
    {
        if (element.ValueKind != JsonValueKind.Object) { throw InvalidResponse("Expected a JSON object."); }
    }

    private static string RequiredString(JsonElement element, string name)
    {
        if (!element.TryGetProperty(name, out JsonElement value) || value.ValueKind != JsonValueKind.String ||
            string.IsNullOrWhiteSpace(value.GetString()))
        {
            throw InvalidResponse($"Missing or invalid '{name}'.");
        }

        return value.GetString()!;
    }

    private static long? OptionalSize(JsonElement element, string name)
    {
        if (!element.TryGetProperty(name, out JsonElement value) || value.ValueKind == JsonValueKind.Null) { return null; }
        if (value.ValueKind != JsonValueKind.Number || !value.TryGetInt64(out long size) || size < 0)
        {
            throw InvalidResponse($"Invalid '{name}' file metadata.");
        }

        return size;
    }

    private static string? OptionalHash(JsonElement element, string name, int length)
    {
        if (!element.TryGetProperty(name, out JsonElement value) || value.ValueKind == JsonValueKind.Null) { return null; }
        if (value.ValueKind != JsonValueKind.String) { throw InvalidResponse($"Invalid '{name}' hash metadata."); }
        string hash = value.GetString()!;
        if (hash.Length == length && hash.All(character => character == '*')) { return null; }
        if (!IsHash(hash, length)) { throw InvalidResponse($"Invalid '{name}' hash metadata."); }
        return hash.ToLowerInvariant();
    }

    private static bool IsHash(string value, int length) => value.Length == length && value.All(Uri.IsHexDigit);

    private static InvalidDataException InvalidResponse(string detail, Exception? inner = null) =>
        new("Unexpected Hugging Face model discovery response. " + detail, inner);
}
