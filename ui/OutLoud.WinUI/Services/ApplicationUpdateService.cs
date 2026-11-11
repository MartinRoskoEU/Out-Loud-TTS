using System.Net.Http;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace OutLoud.WinUI.Services;

internal sealed record ApplicationUpdateResult(bool Available, string Message, Uri? ReleaseUrl = null);

/// <summary>Optional, read-only checks of published stable Out Loud releases.</summary>
internal sealed class ApplicationUpdateService(HttpClient http)
{
    internal const string LatestReleaseUrl = "https://api.github.com/repos/MartinRoskoEU/Out-Loud-TTS/releases/latest";

    internal async Task<ApplicationUpdateResult> CheckAsync(CancellationToken cancellationToken)
    {
        try
        {
            using var deadline = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            deadline.CancelAfter(TimeSpan.FromSeconds(10));
            using var request = new HttpRequestMessage(HttpMethod.Get, LatestReleaseUrl);
            request.Headers.UserAgent.ParseAdd(ApplicationInfo.UserAgent);
            request.Headers.Accept.ParseAdd("application/vnd.github+json");
            request.Headers.Add("X-GitHub-Api-Version", "2022-11-28");
            using var response = await http.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, deadline.Token).ConfigureAwait(false);
            response.EnsureSuccessStatusCode();
            if (response.Content.Headers.ContentLength > 1024 * 1024 ||
                response.Content.Headers.ContentType?.MediaType is not { } media ||
                !(media.Equals("application/json", StringComparison.OrdinalIgnoreCase) || media.EndsWith("+json", StringComparison.OrdinalIgnoreCase)))
                throw new InvalidDataException("Invalid application release metadata.");
            await response.Content.LoadIntoBufferAsync(1024 * 1024, deadline.Token).ConfigureAwait(false);
            using var json = JsonDocument.Parse(await response.Content.ReadAsByteArrayAsync(deadline.Token).ConfigureAwait(false));
            return Evaluate(json.RootElement, ApplicationInfo.Version);
        }
        catch (Exception)
        { return new(false, "Unable to check for application updates"); }
    }

    internal static ApplicationUpdateResult Evaluate(JsonElement release, string installed)
    {
        if (release.GetProperty("draft").GetBoolean() || release.GetProperty("prerelease").GetBoolean())
            throw new InvalidDataException("Only published stable releases are supported.");
        string tag = release.GetProperty("tag_name").GetString() ?? "";
        static Version Parse(string value)
        {
            if (!Regex.IsMatch(value, @"\Av?(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\z"))
                throw new InvalidDataException("Invalid application release version.");
            return Version.Parse(value.TrimStart('v'));
        }
        bool available = Parse(tag) > Parse(installed);
        return available ? new(true, $"Update available: {tag}",
            new Uri("https://github.com/MartinRoskoEU/Out-Loud-TTS/releases/tag/" + tag))
            : new(false, "Up to date");
    }
}
