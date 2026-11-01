using System.Net.Http;
using System.Net.Http.Headers;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace OutLoud.WinUI.Services;

/// <summary>Read-only latest-release metadata checks after verifying the saved local runtime identity.</summary>
public sealed class PocketTtsRuntimeUpdateService
{
    public const string LatestReleaseUrl = "https://api.github.com/repos/kyutai-labs/pocket-tts/releases/latest";
    private const long MaximumResponseBytes = 2 * 1024 * 1024;
    private static readonly TimeSpan RequestTimeout = TimeSpan.FromSeconds(10);
    private static readonly Regex WindowsAssetName = new(
        @"\Apocket-tts-[0-9A-Za-z][0-9A-Za-z.+-]*-x86_64-pc-windows-msvc\.zip\z",
        RegexOptions.CultureInvariant | RegexOptions.NonBacktracking);
    private readonly HttpClient _httpClient;

    internal static void ValidateReleaseTag(string tag) => _ = SemanticVersion.Parse(tag);

    internal static bool IsOfficialAssetUrl(Uri url, string tag, string name) =>
        WindowsAssetName.IsMatch(name) && url.Scheme == Uri.UriSchemeHttps && url.Host == "github.com" &&
        url.IsDefaultPort && string.IsNullOrEmpty(url.UserInfo) && string.IsNullOrEmpty(url.Query) && string.IsNullOrEmpty(url.Fragment) &&
        Uri.UnescapeDataString(url.AbsolutePath) == $"/kyutai-labs/pocket-tts/releases/download/{tag}/{name}";

    /// <summary>The caller owns and reuses the HttpClient. No authentication is required.</summary>
    public PocketTtsRuntimeUpdateService(HttpClient httpClient)
    {
        ArgumentNullException.ThrowIfNull(httpClient);
        _httpClient = httpClient;
    }

    // Keep file lookup and hashing off the UI thread as well as the network request.
    public Task<PocketTtsRuntimeUpdateResult> CheckAsync(CancellationToken cancellationToken = default) =>
        Task.Run(() => CheckCoreAsync(cancellationToken));

    private async Task<PocketTtsRuntimeUpdateResult> CheckCoreAsync(CancellationToken cancellationToken)
    {
        string? installedTag = null;
        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            var identity = await PocketTtsRuntimeValidation.ReadVerifiedRuntimeIdentityAsync(cancellationToken).ConfigureAwait(false);
            installedTag = identity.ReleaseVersion;
            SemanticVersion installedVersion = SemanticVersion.Parse(installedTag);

            using var timeout = new CancellationTokenSource(RequestTimeout);
            using var operation = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, timeout.Token);
            using var request = new HttpRequestMessage(HttpMethod.Get, LatestReleaseUrl);
            request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue("application/vnd.github+json"));
            request.Headers.UserAgent.ParseAdd(ApplicationInfo.UserAgent);
            request.Headers.Add("X-GitHub-Api-Version", "2022-11-28");
            using HttpResponseMessage response = await _httpClient.SendAsync(
                request, HttpCompletionOption.ResponseHeadersRead, operation.Token).ConfigureAwait(false);
            if (!response.IsSuccessStatusCode)
            {
                return new(PocketTtsRuntimeUpdateStatus.Unavailable,
                    $"GitHub could not provide the latest runtime release (HTTP {(int)response.StatusCode}).", installedTag);
            }

            string? mediaType = response.Content.Headers.ContentType?.MediaType;
            if (mediaType is null || !(mediaType.Equals("application/json", StringComparison.OrdinalIgnoreCase) ||
                mediaType.EndsWith("+json", StringComparison.OrdinalIgnoreCase)) ||
                response.Content.Headers.ContentLength > MaximumResponseBytes)
            { throw new InvalidDataException("Invalid latest-release response."); }

            await response.Content.LoadIntoBufferAsync(MaximumResponseBytes, operation.Token).ConfigureAwait(false);
            using JsonDocument json = JsonDocument.Parse(
                await response.Content.ReadAsByteArrayAsync(operation.Token).ConfigureAwait(false));
            JsonElement release = json.RootElement;
            string latestTag = RequiredString(release, "tag_name");
            SemanticVersion latestVersion = SemanticVersion.Parse(latestTag);
            PocketTtsRuntimeUpdateAsset? asset = FindWindowsAsset(release, latestTag);
            operation.Token.ThrowIfCancellationRequested();
            if (latestVersion.CompareTo(installedVersion) <= 0)
            {
                return new(PocketTtsRuntimeUpdateStatus.UpToDate,
                    "No newer Pocket TTS runtime release is available.", installedTag, latestTag, asset);
            }
            return asset is null
                ? new(PocketTtsRuntimeUpdateStatus.NoCompatibleWindowsRuntimeAsset,
                    "The latest release has no compatible Windows x64 Rust runtime ZIP asset.", installedTag, latestTag)
                : new(PocketTtsRuntimeUpdateStatus.UpdateAvailable,
                    "A newer Pocket TTS runtime release is available.", installedTag, latestTag, asset);
        }
        catch (OperationCanceledException)
        {
            return new(PocketTtsRuntimeUpdateStatus.Unavailable,
                cancellationToken.IsCancellationRequested ? "Runtime update check was cancelled." : "Runtime update check timed out.", installedTag);
        }
        catch (Exception)
        {
            // Optional metadata must never interrupt startup or local voice export.
            return new(PocketTtsRuntimeUpdateStatus.Unavailable, installedTag is null
                ? "The installed runtime identity could not be verified. Update check skipped."
                : "The latest runtime release could not be checked. Local operation remains available.", installedTag);
        }
    }

    private static PocketTtsRuntimeUpdateAsset? FindWindowsAsset(JsonElement release, string releaseTag)
    {
        if (!release.TryGetProperty("assets", out JsonElement assets) || assets.ValueKind != JsonValueKind.Array)
        { throw new InvalidDataException("Missing latest-release asset metadata."); }
        foreach (JsonElement asset in assets.EnumerateArray())
        {
            string name = RequiredString(asset, "name");
            if (!WindowsAssetName.IsMatch(name) || RequiredString(asset, "state") != "uploaded") { continue; }
            if (!Uri.TryCreate(RequiredString(asset, "browser_download_url"), UriKind.Absolute, out Uri? url) ||
                !IsOfficialAssetUrl(url, releaseTag, name))
            { throw new InvalidDataException("Invalid official runtime download URL."); }
            long id = asset.GetProperty("id").GetInt64();
            long size = asset.GetProperty("size").GetInt64();
            if (id <= 0 || size <= 0) { throw new InvalidDataException("Invalid runtime asset metadata."); }
            string? digest = asset.TryGetProperty("digest", out JsonElement value) ? value.GetString() : null;
            return new(id, name, url, size, digest);
        }
        return null;
    }

    private static string RequiredString(JsonElement element, string property)
    {
        if (element.ValueKind != JsonValueKind.Object || !element.TryGetProperty(property, out JsonElement value) ||
            value.ValueKind != JsonValueKind.String || string.IsNullOrWhiteSpace(value.GetString()))
        { throw new InvalidDataException("Missing or invalid latest-release metadata."); }
        return value.GetString()!;
    }

    // SemVer 2.0 precedence; GitHub's optional 'v' tag prefix and build metadata do not affect ordering.
    private sealed record SemanticVersion(string Major, string Minor, string Patch, string[] Prerelease)
    {
        private static readonly Regex Pattern = new(
            @"\Av?(?<major>0|[1-9][0-9]*)\.(?<minor>0|[1-9][0-9]*)\.(?<patch>0|[1-9][0-9]*)(?:-(?<pre>[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?\z",
            RegexOptions.CultureInvariant | RegexOptions.NonBacktracking);

        public static SemanticVersion Parse(string tag)
        {
            if (tag.Length > 256) { throw new InvalidDataException("Release tag is too long."); }
            Match match = Pattern.Match(tag);
            if (!match.Success) { throw new InvalidDataException("Release tag is not a semantic version."); }
            string[] prerelease = match.Groups["pre"].Success ? match.Groups["pre"].Value.Split('.') : Array.Empty<string>();
            if (prerelease.Any(part => IsNumeric(part) && part.Length > 1 && part[0] == '0'))
            { throw new InvalidDataException("Numeric prerelease identifiers cannot have leading zeroes."); }
            return new(match.Groups["major"].Value, match.Groups["minor"].Value, match.Groups["patch"].Value, prerelease);
        }

        public int CompareTo(SemanticVersion other)
        {
            int comparison = CompareNumber(Major, other.Major);
            if (comparison != 0) { return comparison; }
            comparison = CompareNumber(Minor, other.Minor);
            if (comparison != 0) { return comparison; }
            comparison = CompareNumber(Patch, other.Patch);
            if (comparison != 0) { return comparison; }
            if (Prerelease.Length == 0 || other.Prerelease.Length == 0)
            { return (Prerelease.Length == 0).CompareTo(other.Prerelease.Length == 0); }
            for (int index = 0; index < Math.Min(Prerelease.Length, other.Prerelease.Length); index++)
            {
                string left = Prerelease[index], right = other.Prerelease[index];
                bool leftNumeric = IsNumeric(left), rightNumeric = IsNumeric(right);
                comparison = leftNumeric && rightNumeric ? CompareNumber(left, right)
                    : leftNumeric != rightNumeric ? (leftNumeric ? -1 : 1) : string.CompareOrdinal(left, right);
                if (comparison != 0) { return comparison; }
            }
            return Prerelease.Length.CompareTo(other.Prerelease.Length);
        }

        private static bool IsNumeric(string value) => value.All(character => character is >= '0' and <= '9');
        private static int CompareNumber(string left, string right) =>
            left.Length != right.Length ? left.Length.CompareTo(right.Length) : string.CompareOrdinal(left, right);
    }
}
