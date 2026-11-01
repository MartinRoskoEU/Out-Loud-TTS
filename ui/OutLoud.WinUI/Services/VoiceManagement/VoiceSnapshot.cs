namespace OutLoud.WinUI.Voices;

public sealed class BackendException : Exception
{
    public BackendError Error { get; }
    public BackendException(BackendError error) : base(error.Message) => Error = error;
}

public sealed record VoiceRecord(string LibraryId, string? DisplayName, string Health,
    string LibraryDetail, string? SapiStatus, string SapiDetail,
    string? ImportKind, BackendError? SapiError = null,
    string? SourceSha256 = null, string? SourceFilename = null, string? ImportedUtc = null,
    string? ProfilePath = null, string? ProfileSha256 = null)
{
    public string ContentSha256 => ProfileSha256 ?? "";
    public bool CanRemove => IsHex(LibraryId, 32);
    public bool CanExport => CanRemove && Health == "valid" && ImportKind == "pocket_native" &&
        !string.IsNullOrWhiteSpace(DisplayName) && IsHex(ProfileSha256, 64);
    private static bool IsHex(string? value, int length) => value?.Length == length && value.All(c => c is >= '0' and <= '9' or >= 'a' and <= 'f');
    public string NameText => DisplayName ?? "(Metadata unavailable)";
    public string SapiStatusText => SapiStatus ?? "Unavailable";
    public string ImportKindText => ImportKind ?? "Unavailable";
    public string SapiDiagnostic => SapiError?.Diagnostic ?? SapiDetail;
}

public sealed class VoiceSnapshot
{
    public string LibraryRoot { get; }
    public IReadOnlyList<VoiceRecord> Voices { get; }
    public VoiceSnapshot(string libraryRoot, IEnumerable<VoiceRecord> voices)
    {
        LibraryRoot = libraryRoot;
        Voices = Array.AsReadOnly(voices.ToArray());
    }
}
