using System;
using System.Linq;

namespace OutLoud.VoiceManager.Models;

public enum VoiceTestState { Idle, Preparing, Speaking, Exporting, Stopping, Completed, Stopped, Failed }
public sealed record VoiceTestRequest(VoiceRecord Voice, string LibraryRoot, string Text, string? ExportPath = null)
{
    public const int MaximumTextLength = 12000;
    public static bool ManagedId(string id) => id.Length == 32 && id.All(c => c is >= '0' and <= '9' or >= 'a' and <= 'f');
    public static bool Suitable(VoiceRecord voice) => ManagedId(voice.LibraryId) && voice.Health == "valid" && voice.SapiStatus == "correct" && voice.SapiError == null;
    public string TokenId => @"HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Speech\Voices\Tokens\OutLoud_Pocket_" + Voice.LibraryId;
    public string? ValidationError => !Suitable(Voice) ? "Select a managed voice with valid library health and correct SAPI registration." :
        string.IsNullOrWhiteSpace(Text) ? "Enter some English text to test." : Text.Length > MaximumTextLength ? $"Use at most {MaximumTextLength:N0} characters; the text has not been shortened." :
        Text.Contains('\0') ? "Text cannot contain a NUL character." : null;
}
public sealed record VoiceTestError(string Code, string Message, int HResult = 0, string Stage = "", string Path = "")
{
    public string Diagnostic => $"{Code}\nStage: {Stage}\nHRESULT: 0x{HResult:X8}\nPath: {Path}";
}
public sealed class VoiceTestException : Exception
{
    public VoiceTestError Error { get; }
    public VoiceTestException(VoiceTestError error) : base(error.Message) => Error = error;
}
public sealed record VoiceTestOutcome(bool Cancelled, string? ExportPath = null);
