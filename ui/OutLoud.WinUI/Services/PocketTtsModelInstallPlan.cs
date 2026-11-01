using System.Reflection;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace OutLoud.WinUI.Services;

internal sealed record PocketTtsModelInstallFile(string RepositoryPath, string Revision,
    PocketTtsCachedAsset Asset);

/// <summary>Installation authority comes exclusively from embedded, revision-pinned repository snapshots.</summary>
internal sealed record PocketTtsModelInstallPlan(string Alias, string Configuration,
    IReadOnlyList<PocketTtsModelInstallFile> Files)
{
    internal const string Repository = "kyutai/pocket-tts-without-voice-cloning";

    internal static async Task<PocketTtsModelInstallPlan?> LoadAsync(string alias, CancellationToken cancellationToken)
    {
        Assembly assembly = typeof(PocketTtsModelInstallPlan).Assembly;
        string? config = assembly.GetManifestResourceNames().SingleOrDefault(name =>
            name.Contains(".RuntimeMetadata.") && name.EndsWith($".configs.{alias}.yaml", StringComparison.Ordinal));
        if (config is null) { return null; }
        using Stream stream = assembly.GetManifestResourceStream(config)!;
        using var reader = new StreamReader(stream);
        string yaml = await reader.ReadToEndAsync(cancellationToken).ConfigureAwait(false);
        var repositories = new Dictionary<string, JsonDocument>(StringComparer.Ordinal);
        try
        {
            foreach (string resource in assembly.GetManifestResourceNames().Where(name => name.Contains(".RuntimeMetadata.") && name.EndsWith(".json", StringComparison.Ordinal)))
            {
                using Stream jsonStream = assembly.GetManifestResourceStream(resource)!;
                var document = await JsonDocument.ParseAsync(jsonStream, cancellationToken: cancellationToken).ConfigureAwait(false);
                repositories.Add(document.RootElement.GetProperty("id").GetString()! + "@" + document.RootElement.GetProperty("sha").GetString()!, document);
            }
            return Create(alias, yaml, repositories);
        }
        finally { foreach (var document in repositories.Values) { document.Dispose(); } }
    }

    internal static PocketTtsModelInstallPlan? Create(string alias, string yaml,
        IReadOnlyDictionary<string, JsonDocument> repositories)
    {
        // Select the exact public variant declared by the runtime's own configuration.
        // No alias-to-repository path guesses and no gated-repository fallback.
        var publicWeights = Regex.Match(yaml, @"(?m)^weights_path_without_voice_cloning:\s*(?<reference>hf://kyutai/pocket-tts-without-voice-cloning/[^\s#@]+@[a-f0-9]{40})\s*$");
        if (!publicWeights.Success) { return null; }
        yaml = Regex.Replace(yaml, @"(?m)^weights_path:\s*[^\r\n]*", "weights_path: " + publicWeights.Groups["reference"].Value);
        yaml = Regex.Replace(yaml, @"(?m)^weights_path_without_voice_cloning:[^\r\n]*(?:\r?\n)?", "");
        var matches = Regex.Matches(yaml,
            @"(?m)^\s*(?<key>weights_path|tokenizer_path):\s*(?<ref>hf://(?<repo>[^/]+/[^/]+)/(?<path>[^\s#@]+)@(?<revision>[a-f0-9]{40}))\s*$");
        if (matches.Count != 2 || matches.Select(m => m.Groups["key"].Value).Distinct().Count() != 2) { return null; }
        string primaryRevision = matches.Single(m => m.Groups["key"].Value == "weights_path").Groups["revision"].Value;
        var files = new List<PocketTtsModelInstallFile>();
        foreach (Match match in matches)
        {
            string originalRepo = match.Groups["repo"].Value, originalRevision = match.Groups["revision"].Value;
            string path = match.Groups["path"].Value;
            if (!SafeRepositoryPath(path) || !repositories.TryGetValue(originalRepo + "@" + originalRevision, out var original)) { return null; }
            var expected = FindFile(original, path);
            if (expected is null) { return null; }
            // Older configs reference the separate tokenizer repo. Only a byte-identical file proven by
            // trusted metadata may be substituted from the permitted official pocket-tts repository.
            string? revision = repositories.Where(pair => pair.Key.StartsWith(Repository + "@", StringComparison.Ordinal))
                .OrderBy(pair => pair.Key == Repository + "@" + (originalRepo == Repository ? originalRevision : primaryRevision) ? 0 : 1)
                .ThenBy(pair => pair.Key, StringComparer.Ordinal)
                .Where(pair => FindFile(pair.Value, path) == expected)
                .Select(pair => pair.Key[(Repository.Length + 1)..]).FirstOrDefault();
            if (revision is null || (match.Groups["key"].Value == "weights_path" &&
                (originalRepo != Repository || revision != originalRevision))) { return null; }
            string reference = $"hf://{Repository}/{path}@{revision}";
            string originalReference = match.Groups["ref"].Value;
            yaml = yaml.Replace(originalReference, reference, StringComparison.Ordinal);
            string blob = PocketTtsModelStore.BlobPath(expected.Value.Lfs, expected.Value.Digest);
            var asset = new PocketTtsCachedAsset(reference, blob, expected.Value.Size, expected.Value.Digest,
                expected.Value.Lfs, match.Groups["key"].Value == "weights_path");
            files.Add(new(path, revision, asset));
        }
        var plan = new PocketTtsModelInstallPlan(alias, yaml, files.AsReadOnly());
        try { _ = new PocketTtsInstalledModel(alias, yaml, files.Select(file => file.Asset).ToArray()).LocalConfiguration(); }
        catch (InvalidDataException) { return null; }
        return plan;
    }

    private static (long Size, string Digest, bool Lfs)? FindFile(JsonDocument document, string path)
    {
        JsonElement file = document.RootElement.GetProperty("siblings").EnumerateArray()
            .SingleOrDefault(file => file.GetProperty("rfilename").GetString() == path);
        if (file.ValueKind == JsonValueKind.Undefined || !file.TryGetProperty("size", out var size) || !size.TryGetInt64(out long bytes) || bytes <= 0) { return null; }
        bool lfs = file.TryGetProperty("lfs", out var metadata) && metadata.ValueKind == JsonValueKind.Object;
        string? digest = lfs ? metadata.GetProperty("sha256").GetString() : file.GetProperty("blobId").GetString();
        return digest is not null && Regex.IsMatch(digest, lfs ? @"\A[0-9a-f]{64}\z" : @"\A[0-9a-f]{40}\z")
            ? (bytes, digest, lfs) : null;
    }

    private static bool SafeRepositoryPath(string path) => !Path.IsPathRooted(path) &&
        path.Split('/').All(part => part.Length > 0 && part is not ("." or "..") &&
            !part.EndsWith('.') && !part.EndsWith(' ') && part.IndexOfAny(Path.GetInvalidFileNameChars()) < 0);

}
