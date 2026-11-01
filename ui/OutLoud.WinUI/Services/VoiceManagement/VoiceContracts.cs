namespace OutLoud.WinUI.Voices;

// Profiles carry no model association. The SAPI adapter reads the
// machine-wide explicit Settings selection independently at synthesis time.
public enum VoiceOperation { Remove = 3, ImportNative = 7 }
public enum ImportPhase { None, AwaitingElevation, Importing, Succeeded, Cancelled, Failed, Partial }
public sealed record BackendError(string Domain, string Code, string Message, string Path = "",
    string LibraryCode = "", string ImportCode = "", string Tensor = "", uint SystemError = 0)
{
    public string Summary => $"{Message} ({Domain}: {Code})";
    public string Diagnostic => $"{Domain}: {Code}\n{Message}\nPath: {Path}\nWin32: {SystemError}\n" +
        $"Library cause: {LibraryCode}\nImport cause: {ImportCode}\nTensor: {Tensor}";
}
public sealed record VoiceImportRequest(string SourcePath, string DisplayName, string? TargetId = null,
    string? ExpectedHash = null, VoiceOperation Operation = VoiceOperation.ImportNative, string? ExpectedHealth = null);
public sealed record VoiceImportResult(ImportPhase Phase, string? LibraryId = null, BackendError? Error = null,
    string[]? DuplicateIds = null, VoiceOperation Operation = VoiceOperation.ImportNative);
public interface IVoiceImportService
{
    Task<VoiceImportResult> ImportAsync(VoiceImportRequest request, IProgress<ImportPhase> progress);
}

public sealed record VoiceExportRequest(string DestinationPath, string ExpectedDestination);
