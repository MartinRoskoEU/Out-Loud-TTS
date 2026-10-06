using System;
using System.Collections.Generic;
using System.Linq;

namespace OutLoud.VoiceManager.Models;

public sealed record BackendError(string Domain, string Code, string Message, string Path = "",
    string LibraryCode = "", string ImportCode = "", string Tensor = "", uint SystemError = 0)
{
    public string Summary => $"{Message} ({Domain}: {Code})";
    public string Diagnostic => $"{Domain}: {Code}\n{Message}\nPath: {Path}\nWin32: {SystemError}\n" +
        $"Library cause: {LibraryCode}\nImport cause: {ImportCode}\nTensor: {Tensor}";
}

public sealed class BackendException : Exception
{
    public BackendError Error { get; }
    public BackendException(BackendError error) : base(error.Message) => Error = error;
}

public sealed record VoiceRecord(string LibraryId, string? DisplayName, string Health,
    string LibraryDetail, string? SapiStatus, string SapiDetail, string? CompatibilityId,
    string? ImportKind, uint? OlvVersion, bool? ProvenanceVerified, BackendError? SapiError = null,
    string? OlvSha256 = null, string? SourceSha256 = null, string? SourceFilename = null, string? ImportedUtc = null)
{
    public string NameText => DisplayName ?? "(Metadata unavailable)";
    public string SapiStatusText => SapiStatus ?? "Unavailable";
    public string OlvVersionText => OlvVersion?.ToString(System.Globalization.CultureInfo.InvariantCulture) ?? "Unavailable";
    public string ImportKindText => ImportKind ?? "Unavailable";
    public string CompatibilityText => CompatibilityId ?? "Unavailable";
    public string ProvenanceText => ProvenanceVerified.HasValue ? (ProvenanceVerified.Value ? "Yes" : "No") : "Unavailable";
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
