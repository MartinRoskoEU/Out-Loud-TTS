using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Text;
using System.Threading.Tasks;
using System.Collections.Generic;
using OutLoud.VoiceManager.Models;
using OutLoud.VoiceManager.Services;
using OutLoud.VoiceManager.ViewModels;

namespace OutLoud.VoiceManager.Tests;

internal static class ImportTests
{
    private sealed class SnapshotService : IVoiceManagerService
    {
        public int Reads;
        public VoiceSnapshot Next = new("fake", Array.Empty<VoiceRecord>());
        public BackendError? Error;
        public Task<VoiceSnapshot> ReadAsync() { ++Reads; return Error == null ? Task.FromResult(Next) : Task.FromException<VoiceSnapshot>(new BackendException(Error)); }
    }
    private sealed class Dialog : IImportDialogService
    {
        public int Calls;
        public VoiceImportRequest? Next;
        public int ReplaceCalls;
        public VoiceRecord? NextTarget;
        public IReadOnlyList<VoiceRecord>? Candidates;
        public Task<VoiceImportRequest?> ChooseAsync() { ++Calls; return Task.FromResult(Next); }
        public Task<VoiceRecord?> ConfirmReplacementAsync(VoiceImportRequest request, IReadOnlyList<VoiceRecord> candidates)
        { ++ReplaceCalls; Candidates = candidates; return Task.FromResult(NextTarget); }
    }
    private sealed class Importer : IVoiceImportService
    {
        public int Calls;
        public VoiceImportRequest? Last;
        public VoiceImportResult Next = new(ImportPhase.Succeeded, "0123456789abcdef0123456789abcdef");
        public Task<VoiceImportResult>? Pending;
        public Task<VoiceImportResult> ImportAsync(VoiceImportRequest request, IProgress<ImportPhase> progress)
        {
            ++Calls; Last = request; progress.Report(ImportPhase.AwaitingElevation); progress.Report(ImportPhase.Importing);
            return Pending ?? Task.FromResult(Next);
        }
    }
    private sealed class DeniedLauncher : IElevationLauncher
    {
        public int Calls;
        public Task<IImportHelperProcess> LaunchAsync(string executable, string endpoint, int parentId)
        { ++Calls; return Task.FromException<IImportHelperProcess>(new Win32Exception(1223)); }
    }
    private sealed class PipeLauncher : IElevationLauncher, IImportHelperProcess
    {
        public byte[] Reply = Array.Empty<byte>();
        public byte[]? Request;
        public bool BadPeer, Started = true, Truncate;
        private Task client = Task.CompletedTask;
        public int Id => BadPeer ? -1 : Environment.ProcessId;
        public Task<IImportHelperProcess> LaunchAsync(string executable, string endpoint, int parentId)
        {
            client = Task.Run(async () => {
                using var pipe = new NamedPipeClientStream(".", endpoint, PipeDirection.InOut, PipeOptions.Asynchronous,
                    System.Security.Principal.TokenImpersonationLevel.Identification);
                await pipe.ConnectAsync(5000);
                if (BadPeer) return;
                Request = await ImportProtocol.ReadFrameAsync(pipe, default);
                if (Started) {
                    var notification = new byte[16]; BitConverter.GetBytes(ImportProtocol.Magic).CopyTo(notification, 0);
                    BitConverter.GetBytes(ImportProtocol.Version).CopyTo(notification, 4); BitConverter.GetBytes(4u).CopyTo(notification, 8);
                    await pipe.WriteAsync(notification);
                }
                await pipe.WriteAsync(Truncate ? Reply[..^1] : Reply);
            });
            return Task.FromResult<IImportHelperProcess>(this);
        }
        public Task WaitForExitAsync() => client;
        public void Dispose() { } // In-process transport fake, never starts an elevated/native process.
    }
    private sealed class IgnoreProgress : IProgress<ImportPhase> { public void Report(ImportPhase value) { } }
    private static byte[] Frame(uint kind, string id, BackendError? error = null, string[]? duplicates = null, VoiceOperation operation = VoiceOperation.Import)
    {
        using var stream = new MemoryStream(); using var writer = new BinaryWriter(stream, new UTF8Encoding(false, true), true);
        writer.Write(ImportProtocol.Magic); writer.Write(ImportProtocol.Version); writer.Write(kind); writer.Write(0u);
        void Text(string value) { byte[] text = Encoding.UTF8.GetBytes(value); writer.Write((uint)text.Length); writer.Write(text); }
        Text(id); writer.Write((uint)operation); Text(error?.Domain ?? ""); Text(error?.Code ?? ""); Text(error?.Message ?? ""); Text(error?.Path ?? "");
        Text(error?.LibraryCode ?? ""); Text(error?.ImportCode ?? ""); Text(error?.Tensor ?? ""); writer.Write(error?.SystemError ?? 0u);
        writer.Write((uint)(duplicates?.Length ?? 0)); foreach (string value in duplicates ?? Array.Empty<string>()) Text(value);
        stream.Position = 12; writer.Write((uint)stream.Length - 16); return stream.ToArray();
    }
    public static async Task Run(Action<bool, string> check)
    {
        const string id = "0123456789abcdef0123456789abcdef";
        var dialogModel = new ImportDialogModel("C:\\fake\\undead_female.safetensors");
        check(dialogModel.DisplayName == "Undead Female" && dialogModel.CanConfirm, "Filename-derived display name.");
        dialogModel.DisplayName = "  Hlas ž 🎵  "; var request = dialogModel.Confirm();
        check(request.DisplayName == "Hlas ž 🎵", "Display name trim/Unicode.");
        dialogModel.DisplayName = " \t "; check(!dialogModel.CanConfirm, "Whitespace-only confirmation disabled.");
        dialogModel.DisplayName = "bad\nname"; check(!dialogModel.CanConfirm, "Presentation control characters rejected.");
        var service = new SnapshotService(); var dialog = new Dialog(); var importer = new Importer();
        var model = new MainViewModel(service, dialog, importer);
        await model.ImportAsync();
        check(model.ImportPhase == ImportPhase.Cancelled && importer.Calls == 0 && service.Reads == 0 && !model.IsBusy, "Cancel before elevation.");
        dialog.Next = request; importer.Next = new(ImportPhase.Cancelled); await model.ImportAsync();
        check(model.ImportPhase == ImportPhase.Cancelled && service.Reads == 0 && model.ImportCommand.CanExecute(null), "UAC cancellation UI recovery.");
        var voice = new VoiceRecord(id, "Different loaded name", "valid", "", "correct", "", "compat", "pocket_prepared", 1, false);
        service.Next = new("fake", new[] { voice }); importer.Next = new(ImportPhase.Succeeded, id);
        model.PropertyChanged += (_, e) => { if (e.PropertyName == nameof(model.Voices)) model.SelectedVoice = null; };
        await model.ImportAsync();
        check(service.Reads == 1 && ReferenceEquals(model.SelectedVoice, voice) && importer.Last == request, "Success refresh + new-ID selection + request propagation.");
        check(model.ImportResult?.Message.Contains("Windows speech") == true && model.HasImportResult, "Success presentation.");
        var nativeError = new BackendError("sapi", "registry_access_denied", "Denied", SystemError: 5);
        importer.Next = new(ImportPhase.Partial, id, nativeError); await model.ImportAsync();
        check(service.Reads == 2 && model.SelectedVoice == voice && ReferenceEquals(model.ImportResult?.Error, nativeError) &&
            model.ImportResult.Message.Contains("SAPI registration failed") && model.ImportDiagnostic.Contains("Win32: 5"), "Partial result refresh/error preserved.");
        importer.Next = new(ImportPhase.Failed, Error: new BackendError("library", "duplicate_content", "Duplicate"), DuplicateIds: new[] { id });
        await model.ImportAsync();
        check(service.Reads == 2 && model.ImportResult?.Message.Contains("already in the library") == true && model.ImportDiagnostic.Contains(id), "Duplicate IDs and failure without refresh.");
        var completion = new TaskCompletionSource<VoiceImportResult>(TaskCreationOptions.RunContinuationsAsynchronously); importer.Pending = completion.Task;
        Task active = model.ImportAsync(); int calls = importer.Calls, dialogs = dialog.Calls;
        check(model.IsBusy && model.ImportPhase == ImportPhase.Importing && !model.ImportCommand.CanExecute(null) && !model.RefreshCommand.CanExecute(null), "Import busy + commands disabled.");
        await model.ImportAsync(); await model.RefreshAsync();
        check(importer.Calls == calls && dialog.Calls == dialogs && service.Reads == 2, "Concurrent import/refresh suppressed.");
        completion.SetResult(new(ImportPhase.Succeeded, id)); await active; importer.Pending = null;
        check(!model.IsBusy && model.ImportPhase == ImportPhase.Succeeded && service.Reads == 3, "Pending import completes cleanly.");
        service.Error = nativeError; importer.Next = new(ImportPhase.Partial, id, nativeError); await model.ImportAsync();
        check(model.HasError && model.ImportPhase == ImportPhase.Partial && model.SelectedVoice == voice && !model.IsBusy, "Refresh failure preserves successful import and previous snapshot.");
        service.Error = null; importer.Next = new(ImportPhase.Failed, Error: new BackendError("helper", "outcome_unknown", "Unknown outcome")); await model.ImportAsync();
        check(service.Reads == 5 && model.ImportPhase == ImportPhase.Failed, "Unknown outcome triggers read-only reconciliation.");

        byte[] encoded = ImportProtocol.EncodeRequest(new("C:\\hlas-ž-🎵.safetensors", "Hlas ž 🎵"));
        check(BitConverter.ToUInt32(encoded, 12) == encoded.Length - 16 && Encoding.UTF8.GetString(encoded).Contains("Hlas ž 🎵"), "UTF-8 request framing.");
        var parsed = ImportProtocol.DecodeResult(Frame(1, id)); check(parsed.Phase == ImportPhase.Succeeded && parsed.LibraryId == id, "Success binary result.");
        parsed = ImportProtocol.DecodeResult(Frame(3, id, nativeError)); check(parsed.Phase == ImportPhase.Partial && parsed.Error == nativeError, "Partial binary result.");
        parsed = ImportProtocol.DecodeResult(Frame(2, "", new("library", "duplicate_content", "Already imported", "C:\\ž"), new[] { id }));
        check(parsed.DuplicateIds?[0] == id && parsed.Error?.Path == "C:\\ž", "Duplicate result Unicode/error transport.");
        bool Reject(byte[] bytes) { try { ImportProtocol.DecodeResult(bytes); return false; } catch (Exception) { return true; } }
        byte[] bad = Frame(1, id); bad[4] = 9; check(Reject(bad), "Bad response version rejected.");
        bad = Frame(1, id); bad[8] = 9; check(Reject(bad), "Unknown response outcome rejected.");
        bad = Frame(1, id); check(Reject(bad[..^1]), "Truncated response rejected.");
        check(Reject(Frame(1, "invalid")) && Reject(Frame(2, id, nativeError)), "Inconsistent outcome/ID rejected.");
        var denied = new DeniedLauncher();
        var deniedResult = await new ElevatedVoiceImportService(denied).ImportAsync(request, new IgnoreProgress());
        check(denied.Calls == 1 && deniedResult.Phase == ImportPhase.Cancelled && deniedResult.Error == null, "Fake elevation launcher maps actual Win32 UAC denial to cancellation (no helper launch).");
        var pipeLauncher = new PipeLauncher { Reply = Frame(1, id) };
        var pipeResult = await new ElevatedVoiceImportService(pipeLauncher).ImportAsync(request, new IgnoreProgress());
        check(pipeResult.Phase == ImportPhase.Succeeded && pipeLauncher.Request != null &&
            Encoding.UTF8.GetString(pipeLauncher.Request).Contains(request.DisplayName), "Actual secured local pipe round trip with fake process/native outcome.");
        pipeLauncher.Reply = Frame(3, id, nativeError);
        pipeResult = await new ElevatedVoiceImportService(pipeLauncher).ImportAsync(request, new IgnoreProgress());
        check(pipeResult.Phase == ImportPhase.Partial && pipeResult.Error == nativeError, "Pipe partial outcome preserved.");
        pipeLauncher.Started = false; pipeLauncher.Reply = Frame(2, "", new("helper", "invalid_request", "Malformed request"));
        pipeResult = await new ElevatedVoiceImportService(pipeLauncher).ImportAsync(request, new IgnoreProgress());
        check(pipeResult.Phase == ImportPhase.Failed && pipeResult.Error?.Code == "invalid_request", "Pipe rejection before started notification.");
        pipeLauncher.Started = true; pipeLauncher.Truncate = true;
        pipeResult = await new ElevatedVoiceImportService(pipeLauncher).ImportAsync(request, new IgnoreProgress());
        check(pipeResult.Error?.Code == "outcome_unknown", "Transport loss after dispatch does not assert no mutation.");
        pipeLauncher.BadPeer = true;
        pipeResult = await new ElevatedVoiceImportService(pipeLauncher).ImportAsync(request, new IgnoreProgress());
        check(pipeResult.Error?.Code == "launch_or_transport_failed", "Wrong process pipe peer is rejected before sending request.");
        // Import-or-replace routing and notification lifecycle use fake dialogs/services.
        voice = voice with { DisplayName = "Undead Female", OlvSha256 = new string('a', 64) };
        service.Next = new("fake", new[] { voice }); await model.RefreshAsync();
        dialog.Next = new("C:\\fake\\new.safetensors", "  undead FEMALE  ");
        int beforeReplace = importer.Calls; int previousDialog = dialog.ReplaceCalls;
        await model.ImportAsync();
        check(dialog.ReplaceCalls == previousDialog + 1 && importer.Calls == beforeReplace && model.ImportPhase == ImportPhase.Cancelled, "One name collision requires confirmation; cancellation never launches helper.");
        dialog.NextTarget = voice; importer.Next = new(ImportPhase.Succeeded, id, Operation: VoiceOperation.Replace);
        await model.ImportAsync();
        check(importer.Last?.Operation == VoiceOperation.Replace && importer.Last.TargetId == id && importer.Last.ExpectedHash == voice.OlvSha256 &&
            importer.Last.DisplayName == "Undead Female" && model.SelectedVoice?.LibraryId == id, "Case-insensitive trimmed collision routes exact immutable identity/name/hash, and refresh selects same ID.");
        check(model.NotificationSeverity == NotificationSeverity.Success && model.ImportStateText == "Voice replaced successfully." && !model.HasImportDiagnostic, "Replacement success severity and no meaningless diagnostic expander.");
        model.DismissNotificationCommand.Execute(null);
        check(!model.HasImportResult && !model.HasImportDiagnostic && !model.DismissNotificationCommand.CanExecute(null), "Explicit dismiss command hides notification + diagnostics.");
        importer.Next = new(ImportPhase.Succeeded, id, Operation: VoiceOperation.Replace, Unchanged: true); await model.ImportAsync();
        check(model.HasImportResult && model.NotificationSeverity == NotificationSeverity.Info && model.ImportStateText.Contains("Already using"), "Next operation reappears; unchanged is informational.");
        var other = voice with { LibraryId = "fedcba9876543210fedcba9876543210" };
        service.Next = new("fake", new[] { voice, other }); await model.RefreshAsync(); dialog.NextTarget = null;
        beforeReplace = importer.Calls; await model.ImportAsync();
        var replacementModel = new ReplacementDialogModel(dialog.Next, dialog.Candidates!);
        check(dialog.Candidates?.Count == 2 && importer.Calls == beforeReplace && !replacementModel.CanConfirm && replacementModel.SelectedTarget == null,
            "Multiple same-name voices never get an arbitrary automatic target.");
        replacementModel.SelectedTarget = other; check(replacementModel.CanConfirm, "Ambiguous target can be explicitly selected.");
        dialog.NextTarget = other; importer.Next = new(ImportPhase.Failed, other.LibraryId, new("library", "voice_in_use", "Close consumers"), Operation: VoiceOperation.Replace);
        await model.ImportAsync(); check(model.NotificationSeverity == NotificationSeverity.Warning && model.HasImportDiagnostic && model.ImportStateText.Contains("Close WoW"), "In-use warning + actionable text/diagnostic.");
        importer.Next = new(ImportPhase.Failed, other.LibraryId, new("library", "import_failed", "Incompatible prepared voice", ImportCode: "incompatible_model"), Operation: VoiceOperation.Replace);
        await model.ImportAsync(); check(model.NotificationSeverity == NotificationSeverity.Error && model.HasImportDiagnostic, "Replacement failure has error severity/diagnostics.");
        check(new VoiceImportResult(ImportPhase.Partial, id, nativeError).Severity == NotificationSeverity.Warning &&
            new VoiceImportResult(ImportPhase.Cancelled).Severity == NotificationSeverity.Info &&
            new VoiceImportResult(ImportPhase.Succeeded, id).Severity == NotificationSeverity.Success, "Import warning/info/success severity mapping.");
        parsed = ImportProtocol.DecodeResult(Frame(5, id, operation: VoiceOperation.Replace));
        check(parsed.Operation == VoiceOperation.Replace && parsed.Phase == ImportPhase.Succeeded && !parsed.Unchanged, "Protocol replaced success.");
        parsed = ImportProtocol.DecodeResult(Frame(6, id, operation: VoiceOperation.Replace)); check(parsed.Unchanged && parsed.Severity == NotificationSeverity.Info, "Protocol unchanged outcome.");
        parsed = ImportProtocol.DecodeResult(Frame(7, id, new("library", "cleanup_pending", "Retained staging"), operation: VoiceOperation.Replace));
        check(parsed.Phase == ImportPhase.Partial && parsed.Severity == NotificationSeverity.Warning, "Protocol replaced cleanup warning.");
        parsed = ImportProtocol.DecodeResult(Frame(2, id, nativeError, operation: VoiceOperation.Replace)); check(parsed.Error == nativeError && parsed.LibraryId == id, "Protocol replacement failure retains target ID.");
        check(Reject(Frame(5, id)) && Reject(Frame(3, id, nativeError, operation: VoiceOperation.Replace)), "Cross-operation outcomes rejected.");
        encoded = ImportProtocol.EncodeRequest(new("C:\\ž.safetensors", "Hlas", id, new string('a', 64)));
        check(BitConverter.ToUInt32(encoded, 8) == 2 && Encoding.UTF8.GetString(encoded).Contains(id), "Replace request kind and optimistic identity transported.");
    }
}
