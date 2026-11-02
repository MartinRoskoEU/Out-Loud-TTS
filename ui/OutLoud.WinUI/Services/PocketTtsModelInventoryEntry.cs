namespace OutLoud.WinUI.Services;

/// <summary>Schema-2 private installation identity, independent of runtime alias discovery.</summary>
public sealed record PocketTtsModelInventoryEntry(
    string Alias,
    string RepositoryId,
    string Revision,
    string InstallationDirectory,
    IReadOnlyList<PocketTtsModelInventoryFile> RequiredFiles,
    string ConfigurationYaml)
{
    // Independent immutable receipt, stored in the same verified content-addressed blob store.
    [System.Text.Json.Serialization.JsonIgnore(Condition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull)]
    public string? UpdateReceiptSha256 { get; init; }
}

/// <summary>RelativePath is a canonical private blob reference; matching references across records share one file.</summary>
public sealed record PocketTtsModelInventoryFile(
    string RelativePath,
    string RepositoryPath,
    long SizeBytes,
    string ExpectedHash,
    string HashAlgorithm,
    bool IsWeights,
    string? RepositoryId = null,
    string? Revision = null);

/// <summary>Committed with unregistration; paths are private canonical blob references, never external files.</summary>
internal sealed record PocketTtsModelCleanup(string Id, string Alias, IReadOnlyList<string> BlobPaths);
