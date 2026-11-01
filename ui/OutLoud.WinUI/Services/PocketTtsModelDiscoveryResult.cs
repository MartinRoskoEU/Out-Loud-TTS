namespace OutLoud.WinUI.Services;

public enum PocketTtsModelCompatibility
{
    Unknown,
    VerifiedCompatible,
    Incompatible,
}

public enum PocketTtsVoiceCloningSupport
{
    Unknown,
    Supported,
    Unsupported,
}

public sealed record PocketTtsModelFile(
    string RepositoryPath,
    string Revision,
    long? SizeBytes,
    string? GitBlobId,
    PocketTtsLfsMetadata? Lfs);

public sealed record PocketTtsLfsMetadata(string? Sha256, long? SizeBytes, long? PointerSizeBytes);

/// <summary>A path-based discovery candidate, not a validated or installed model.</summary>
public sealed record PocketTtsDiscoveredModel(
    string Id,
    string Name,
    string RepositoryId,
    string RepositoryPath,
    string Revision,
    IReadOnlyList<string> CandidateWeightPaths,
    IReadOnlyList<PocketTtsModelFile> Files)
{
    // Discovery has no evidence to set either of these to a positive or negative verdict.
    public PocketTtsModelCompatibility RuntimeCompatibility => PocketTtsModelCompatibility.Unknown;
    public PocketTtsVoiceCloningSupport VoiceCloningSupport => PocketTtsVoiceCloningSupport.Unknown;
}

public sealed record PocketTtsModelDiscoveryResult(
    string RepositoryId,
    string RequestedRevision,
    string Revision,
    DateTimeOffset RetrievedAtUtc,
    string? Gating,
    IReadOnlyList<PocketTtsModelFile> Files,
    IReadOnlyList<PocketTtsDiscoveredModel> Models);
