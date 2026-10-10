using System;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Collections.Generic;

namespace OutLoud.VoiceManager.Models;

public enum VoiceOperation { Import = 1, Replace = 2, Remove = 3, ImportOlv = 4, ReplaceOlv = 5, Export = 6 }
public enum NotificationSeverity { Success, Warning, Error, Info }
public sealed record VoiceImportRequest(string SourcePath, string DisplayName, string? TargetId = null, string? ExpectedHash = null, VoiceOperation? Kind = null, string? ExpectedHealth = null)
{
    public VoiceOperation Operation => Kind ?? (IsOlv ? (TargetId == null ? VoiceOperation.ImportOlv : VoiceOperation.ReplaceOlv) : (TargetId == null ? VoiceOperation.Import : VoiceOperation.Replace));
    public bool IsOlv => string.Equals(Path.GetExtension(SourcePath), ".olv", StringComparison.OrdinalIgnoreCase);
    public bool IsReplacement => Operation is VoiceOperation.Replace or VoiceOperation.ReplaceOlv;
}
public enum ImportPhase { None, Choosing, AwaitingElevation, Importing, Succeeded, Cancelled, Failed, Partial }
public sealed record VoiceImportResult(ImportPhase Phase, string? LibraryId = null, BackendError? Error = null, string[]? DuplicateIds = null,
    VoiceOperation Operation = VoiceOperation.Import, bool Unchanged = false)
{
    public NotificationSeverity Severity => Phase switch {
        ImportPhase.Cancelled => NotificationSeverity.Info,
        ImportPhase.Partial => NotificationSeverity.Warning,
        ImportPhase.Succeeded => Unchanged ? NotificationSeverity.Info : NotificationSeverity.Success,
        _ when Error?.Code is "voice_in_use" or "replacement_conflict" or "removal_conflict" or "removal_partial" or "replacement_pending" or "voice_not_found" or "reconciliation_required" or "outcome_unknown" or "export_conflict" => NotificationSeverity.Warning,
        _ => NotificationSeverity.Error
    };
    public bool HasDiagnostic => Error != null || DuplicateIds is { Length: > 0 };
    public string Message => Phase switch
    {
        ImportPhase.Succeeded when Operation == VoiceOperation.Remove => "Voice removed from Out Loud and Windows speech.",
        ImportPhase.Partial when Operation == VoiceOperation.Remove && Error?.Code == "removal_partial" => "Removal is incomplete. The library entry is detached, but its Windows speech registration still needs review. See the diagnostic details.",
        ImportPhase.Partial when Operation == VoiceOperation.Remove => "Voice removed from Out Loud and Windows speech, but file cleanup is pending. See the diagnostic details.",
        ImportPhase.Cancelled when Operation == VoiceOperation.Remove => "Removal cancelled. No voice was removed.",
        ImportPhase.Succeeded when Unchanged => "Already using this voice data. Nothing was replaced.",
        ImportPhase.Succeeded when Operation is VoiceOperation.Replace or VoiceOperation.ReplaceOlv => "Voice replaced successfully.",
        ImportPhase.Succeeded when Operation == VoiceOperation.Export => "Voice exported successfully.",
        ImportPhase.Succeeded => "Voice imported and added to Windows speech.",
        ImportPhase.Partial when Operation is VoiceOperation.Replace or VoiceOperation.ReplaceOlv => "Voice replaced, but staging cleanup is pending. See the diagnostic details.",
        ImportPhase.Partial => "Voice imported but SAPI registration failed. The imported voice has been kept.",
        ImportPhase.Cancelled when Operation == VoiceOperation.Export => "Export cancelled. No file was written.",
        ImportPhase.Cancelled => "Import cancelled. No voice was added or replaced.",
        _ when Operation == VoiceOperation.Export && Error?.Code is "export_conflict" or "voice_not_found" => "The selected voice or destination changed. Refresh the list or choose the destination again.",
        _ when Error?.Code == "voice_in_use" && Operation == VoiceOperation.Remove => "Voice could not be removed because it is in use. Close WoW and other active speech consumers, then retry.",
        _ when Error?.Code == "voice_in_use" => "Voice could not be replaced because it is in use. Close WoW and other active speech consumers, then retry.",
        _ when Operation == VoiceOperation.Remove && Error?.Code is "removal_conflict" or "voice_not_found" => "The removal target changed or disappeared. Check the refreshed list before confirming again.",
        _ when Error?.Code is "replacement_conflict" or "voice_not_found" => "The replacement target changed. The list has been refreshed; confirm the target again.",
        _ when Error?.Code == "replacement_pending" => "Replacement recovery is pending. Original data was retained; close active consumers and recover the library before retrying.",
        _ when Error?.Code == "outcome_unknown" => "Operation outcome could not be confirmed. Check the refreshed list before retrying.",
        _ when Error?.Code == "duplicate_content" => "This voice is already in the library. No duplicate was added.",
        _ => (Operation switch { VoiceOperation.Remove => "Removal failed: ", VoiceOperation.Replace or VoiceOperation.ReplaceOlv => "Replacement failed: ", VoiceOperation.Export => "Export failed: ", _ => "Import failed: " }) + (Error?.Message ?? "See the diagnostic details.")
    };
    public string Diagnostic => (Error?.Diagnostic ?? "") +
        (DuplicateIds is { Length: > 0 } ? "\nExisting library IDs: " + string.Join(", ", DuplicateIds) : "") +
        (LibraryId != null ? "\nLibrary ID: " + LibraryId : "");
}
public sealed record RemovalDialogModel(VoiceRecord Target)
{
    public string Prompt => $"Remove \"{Target.NameText}\"?";
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
    public string Prompt => Candidates.All(candidate => string.Equals(candidate.DisplayName?.Trim(), Request.DisplayName.Trim(), StringComparison.OrdinalIgnoreCase))
        ? $"A voice named \"{Request.DisplayName}\" already exists. Replace its voice data?"
        : "This voice data matches multiple library entries. Select the exact entry to replace; its ID and stored name will be retained.";
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
    public string SourceFormat => VoiceFileSelection.IsOlv(SourcePath) ? "Out Loud Voice (.olv)" : "Prepared Pocket Voice (.safetensors)";
    public string DisplayName { get; set; }
    public bool CanConfirm => VoiceFileSelection.IsSupported(SourcePath) && !string.IsNullOrWhiteSpace(DisplayName) && !DisplayName.Any(char.IsControl);
    public VoiceImportRequest Confirm() => CanConfirm ? new(SourcePath, DisplayName.Trim()) : throw new InvalidOperationException("Enter a voice name.");
}
