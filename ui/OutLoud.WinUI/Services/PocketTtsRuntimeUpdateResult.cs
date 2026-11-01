namespace OutLoud.WinUI.Services;

public enum PocketTtsRuntimeUpdateStatus
{
    UpToDate,
    UpdateAvailable,
    NoCompatibleWindowsRuntimeAsset,
    Unavailable,
}

public sealed record PocketTtsRuntimeUpdateAsset(
    long Id, string Name, Uri DownloadUrl, long SizeBytes, string? Digest);

public sealed record PocketTtsRuntimeUpdateResult(
    PocketTtsRuntimeUpdateStatus Status,
    string Message,
    string? InstalledReleaseTag = null,
    string? LatestReleaseTag = null,
    PocketTtsRuntimeUpdateAsset? Asset = null);
