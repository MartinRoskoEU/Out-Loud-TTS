using System.Text.RegularExpressions;
namespace OutLoud.WinUI.Services;

// Runtime-only checks extracted from Studio Lite; no model inventory or synthesis.
internal static class PocketTtsRuntimeValidation
{
    public const string ValidatedRuntimeRelease = "v4.0.1";
    public const string ValidatedRuntimeSha256 = "813CABFC2F8FC7A4E5310DEDA6025426B3164610DD9F3C3098A6078E5CC1895A";
    internal static string RuntimeIdentityPath => Path.Combine(MachineDataStorage.Root, "runtime-identity.json");
    internal sealed record RuntimeIdentity(string ReleaseVersion, string ExecutableSha256);
    internal sealed record RuntimeCheckResult(bool Ready, string Message);

    // Runtime-only portion of Studio Lite's model setup check. Resolve only this
    // application's expected baseline or selected update; never another app's files.
    internal static Task<RuntimeCheckResult> CheckAsync(CancellationToken cancellationToken) => Task.Run(async () =>
    {
        try
        {
            string runtimePath = PocketTtsRuntimeStore.ResolveActivePath();
            using FileStream runtimeLease = File.OpenRead(runtimePath);
            _ = await PocketTtsRuntimeStore.VerifyRuntimeAsync(runtimePath, cancellationToken).ConfigureAwait(false);
            _ = await ValidateCliAsync(new(runtimePath),
                !PocketTtsRuntimeStore.IsWithin(runtimePath, PocketTtsRuntimeStore.RuntimesPath), cancellationToken).ConfigureAwait(false);
            return new RuntimeCheckResult(true, "Pocket TTS runtime ready.");
        }
        catch (Exception exception) when (exception is IOException or InvalidDataException or System.Text.Json.JsonException or
            UnauthorizedAccessException or InvalidOperationException or PlatformNotSupportedException or TimeoutException or FormatException)
        { return new RuntimeCheckResult(false, exception.Message); }
    }, cancellationToken);
    internal static async Task<RuntimeIdentity> ReadVerifiedRuntimeIdentityAsync(CancellationToken cancellationToken) =>
        (await PocketTtsRuntimeStore.ReadVerifiedActiveAsync(cancellationToken).ConfigureAwait(false)).Identity;

    internal static async Task<string[]> ValidateCliAsync(PocketTtsCliService cli, bool bundled, CancellationToken cancellationToken)
    {
        PocketTtsCliResult version = await cli.GetVersionAsync(cancellationToken).ConfigureAwait(false);
        PocketTtsCliResult help = await cli.GetExportVoiceHelpAsync(cancellationToken).ConfigureAwait(false);
        if (!version.Succeeded || !version.StandardOutput.Trim().StartsWith("pocket-tts ", StringComparison.Ordinal) ||
            (bundled && version.StandardOutput.Trim() != "pocket-tts 3.3.0") || !help.Succeeded ||
            !help.StandardOutput.Contains("--config", StringComparison.Ordinal) ||
            !help.StandardOutput.Contains("<AUDIO_PATH>", StringComparison.Ordinal) ||
            !help.StandardOutput.Contains("<EXPORT_PATH>", StringComparison.Ordinal))
        { throw new InvalidDataException("The runtime did not pass its version and local voice-export compatibility diagnostics."); }
        string[] supported = SupportedLanguages(help.StandardOutput);
        if (supported.Length == 0) { throw new InvalidDataException("The runtime reports no compatible model aliases."); }
        return supported;
    }

    internal static string[] SupportedLanguages(string help)
    {
        int start = help.IndexOf("--language", StringComparison.Ordinal);
        int end = start < 0 ? -1 : help.IndexOf("Incompatible", start, StringComparison.Ordinal);
        if (start < 0 || end < 0) { throw new InvalidDataException("This runtime does not expose the expected model selection contract."); }
        return Regex.Matches(help[start..end], "'(?<name>[a-z0-9_\\s-]+)'")
            .Select(match => Regex.Replace(match.Groups["name"].Value, @"\s+", "")).Distinct(StringComparer.Ordinal).Order(StringComparer.Ordinal).ToArray();
    }
}
