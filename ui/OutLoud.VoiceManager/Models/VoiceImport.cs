using System;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Collections.Generic;

namespace OutLoud.VoiceManager.Models;

public enum VoiceOperation { Import = 1, Replace = 2 }
public enum NotificationSeverity { Success, Warning, Error, Info }
public sealed record VoiceImportRequest(string SourcePath, string DisplayName, string? TargetId = null, string? ExpectedHash = null)
{
    public VoiceOperation Operation => TargetId == null ? VoiceOperation.Import : VoiceOperation.Replace;
}
public enum ImportPhase { None, Choosing, AwaitingElevation, Importing, Succeeded, Cancelled, Failed, Partial }
public sealed record VoiceImportResult(ImportPhase Phase, string? LibraryId = null, BackendError? Error = null, string[]? DuplicateIds = null,
    VoiceOperation Operation = VoiceOperation.Import, bool Unchanged = false)
{
    public NotificationSeverity Severity => Phase switch {
        ImportPhase.Cancelled => NotificationSeverity.Info,
        ImportPhase.Partial => NotificationSeverity.Warning,
        ImportPhase.Succeeded => Unchanged ? NotificationSeverity.Info : NotificationSeverity.Success,
        _ when Error?.Code is "voice_in_use" or "replacement_conflict" or "replacement_pending" or "voice_not_found" or "reconciliation_required" or "outcome_unknown" => NotificationSeverity.Warning,
        _ => NotificationSeverity.Error
    };
    public bool HasDiagnostic => Error != null || DuplicateIds is { Length: > 0 };
    public string Message => Phase switch
    {
        ImportPhase.Succeeded when Unchanged => "Already using this voice data. Nothing was replaced.",
        ImportPhase.Succeeded when Operation == VoiceOperation.Replace => "Voice replaced successfully.",
        ImportPhase.Succeeded => "Voice imported and added to Windows speech.",
        ImportPhase.Partial when Operation == VoiceOperation.Replace => "Voice replaced, but staging cleanup is pending. See the diagnostic details.",
        ImportPhase.Partial => "Voice imported but SAPI registration failed. The imported voice has been kept.",
        ImportPhase.Cancelled => "Import cancelled. No voice was added or replaced.",
        _ when Error?.Code == "voice_in_use" => "Voice could not be replaced because it is in use. Close WoW and other active speech consumers, then retry.",
        _ when Error?.Code is "replacement_conflict" or "voice_not_found" => "The replacement target changed. The list has been refreshed; confirm the target again.",
        _ when Error?.Code == "replacement_pending" => "Replacement recovery is pending. Original data was retained; close active consumers and recover the library before retrying.",
        _ when Error?.Code == "outcome_unknown" => "Import outcome could not be confirmed. Check the refreshed list before retrying.",
        _ when Error?.Code == "duplicate_content" => "This voice is already in the library. No duplicate was added.",
        _ => (Operation == VoiceOperation.Replace ? "Replacement failed: " : "Import failed: ") + (Error?.Message ?? "See the diagnostic details.")
    };
    public string Diagnostic => (Error?.Diagnostic ?? "") +
        (DuplicateIds is { Length: > 0 } ? "\nExisting library IDs: " + string.Join(", ", DuplicateIds) : "") +
        (LibraryId != null ? "\nLibrary ID: " + LibraryId : "");
}
public sealed class ReplacementDialogModel
{
    public ReplacementDialogModel(VoiceImportRequest request, IReadOnlyList<VoiceRecord> candidates)
    {
        Request = request; Candidates = candidates.ToArray(); SelectedTarget = Candidates.Count == 1 ? Candidates[0] : null;
    }
    public VoiceImportRequest Request { get; }
    public IReadOnlyList<VoiceRecord> Candidates { get; }
    public VoiceRecord? SelectedTarget { get; set; }
    public string Prompt => $"A voice named \"{Request.DisplayName}\" already exists. Replace its voice data?";
    public bool CanConfirm => SelectedTarget != null && Candidates.Any(candidate => ReferenceEquals(candidate, SelectedTarget)) && !string.IsNullOrEmpty(SelectedTarget.OlvSha256);
}

public sealed class ImportDialogModel
{
    public ImportDialogModel(string sourcePath)
    {
        SourcePath = sourcePath;
        DisplayName = CultureInfo.CurrentCulture.TextInfo.ToTitleCase(
            Path.GetFileNameWithoutExtension(sourcePath).Replace('_', ' ').Replace('-', ' '));
    }
    public string SourcePath { get; }
    public string DisplayName { get; set; }
    public bool CanConfirm => !string.IsNullOrWhiteSpace(DisplayName) && !DisplayName.Any(char.IsControl);
    public VoiceImportRequest Confirm() => CanConfirm ? new(SourcePath, DisplayName.Trim()) : throw new InvalidOperationException("Enter a voice name.");
}
