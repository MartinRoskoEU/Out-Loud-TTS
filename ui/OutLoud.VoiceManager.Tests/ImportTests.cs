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
        public bool RemoveConfirmed;
        public int RemoveCalls;
        public VoiceRecord? RemoveTarget;
        public Task<bool> ConfirmRemovalAsync(VoiceRecord target) { ++RemoveCalls; RemoveTarget=target; return Task.FromResult(RemoveConfirmed); }
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
        byte[] bad = Frame(1, id); bad[4] = 99; check(Reject(bad), "Bad response version rejected.");
        bad = Frame(1, id); bad[8] = 99; check(Reject(bad), "Unknown response outcome rejected.");
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
        // Remove shares the helper transport; only snapshot ID/name/hash cross it.
        var removeService = new SnapshotService(); var removeDialog = new Dialog(); var remover = new Importer();
        var removeModel = new MainViewModel(removeService, removeDialog, remover);
        check(!removeModel.RemoveCommand.CanExecute(null), "Remove disabled without selection.");
        await removeModel.RemoveAsync(); check(removeDialog.RemoveCalls == 0 && remover.Calls == 0, "No selection cannot open removal confirmation/helper.");
        removeService.Next = new("fake", new[] { voice, other }); await removeModel.RefreshAsync();
        removeModel.SelectedVoice = voice;
        check(removeModel.RemoveCommand.CanExecute(null), "Selected managed ID enables Remove.");
        removeModel.SelectedVoice = voice with { LibraryId = "unexpected-folder" };
        check(!removeModel.RemoveCommand.CanExecute(null), "Unexpected/unmanaged folder cannot be a Remove target."); removeModel.SelectedVoice = voice;
        await removeModel.RemoveAsync();
        check(removeDialog.RemoveTarget == voice && remover.Calls == 0 && removeService.Reads == 1 && removeModel.Voices.Count == 2 &&
            removeModel.NotificationSeverity == NotificationSeverity.Info && removeModel.ImportStateText.Contains("No voice was removed"), "Cancel removal preserves state and never launches helper/UAC.");
        removeDialog.RemoveConfirmed = true; remover.Next = new(ImportPhase.Succeeded, id, Operation: VoiceOperation.Remove);
        removeService.Next = new("fake", new[] { other }); await removeModel.RemoveAsync();
        check(remover.Last?.Operation == VoiceOperation.Remove && remover.Last.TargetId == id && remover.Last.SourcePath == "" &&
            remover.Last.DisplayName == voice.DisplayName && remover.Last.ExpectedHash == voice.OlvSha256 && remover.Last.ExpectedHealth == voice.Health && removeDialog.RemoveTarget?.LibraryId == id, "Removal uses selected immutable ID/name/hash/health despite duplicate names.");
        check(removeModel.Voices.Count == 1 && ReferenceEquals(removeModel.SelectedVoice, other) && removeService.Reads == 2 &&
            removeModel.NotificationSeverity == NotificationSeverity.Success && removeModel.ImportStateText == "Voice removed from Out Loud and Windows speech." && !removeModel.HasImportDiagnostic,
            "Successful removal refreshes smaller list, selects remaining row and shows clean success.");
        removeModel.DismissNotificationCommand.Execute(null); check(!removeModel.HasImportResult && !removeModel.HasImportDiagnostic, "Removal success dismisses banner/diagnostic together.");
        remover.Next = new(ImportPhase.Failed, other.LibraryId, new("library", "voice_in_use", "Mapped"), Operation: VoiceOperation.Remove);
        await removeModel.RemoveAsync();
        check(removeModel.HasImportResult && removeModel.NotificationSeverity == NotificationSeverity.Warning && removeModel.HasImportDiagnostic && removeModel.Voices.Count == 1 &&
            removeModel.ImportStateText.Contains("Close WoW") && removeModel.SelectedVoice?.LibraryId == other.LibraryId && removeService.Reads == 3, "In-use removal warning reappears, refreshes, preserves selection and count.");
        remover.Next = new(ImportPhase.Failed, other.LibraryId, new("library", "removal_conflict", "Stale"), Operation: VoiceOperation.Remove); await removeModel.RemoveAsync();
        check(removeService.Reads == 4 && removeModel.NotificationSeverity == NotificationSeverity.Warning, "Stale removal refreshes snapshot with warning.");
        remover.Next = new(ImportPhase.Failed, other.LibraryId, nativeError, Operation: VoiceOperation.Remove); await removeModel.RemoveAsync();
        check(removeModel.NotificationSeverity == NotificationSeverity.Error && removeModel.HasImportDiagnostic && removeModel.ImportStateText.StartsWith("Removal failed:"), "Ordinary removal failure is Error with backend details.");
        var removeCompletion = new TaskCompletionSource<VoiceImportResult>(); remover.Pending = removeCompletion.Task;
        Task removing = removeModel.RemoveAsync(); int removes = remover.Calls, confirms = removeDialog.RemoveCalls;
        check(removeModel.IsBusy && !removeModel.RemoveCommand.CanExecute(null) && !removeModel.ImportCommand.CanExecute(null) && !removeModel.RefreshCommand.CanExecute(null), "Remove disables concurrent mutations/refresh while pending.");
        await removeModel.RemoveAsync(); await removeModel.ImportAsync(); await removeModel.RefreshAsync();
        check(remover.Calls == removes && removeDialog.RemoveCalls == confirms, "Concurrent remove/import cannot launch another helper.");
        removeCompletion.SetResult(new(ImportPhase.Failed, other.LibraryId, new("library", "voice_in_use", "Mapped"), Operation: VoiceOperation.Remove)); await removing; remover.Pending = null;
        removeService.Next = new("fake", Array.Empty<VoiceRecord>()); remover.Next = new(ImportPhase.Partial, other.LibraryId, new("library", "removal_partial", "Retained trash/token"), Operation: VoiceOperation.Remove);
        await removeModel.RemoveAsync();
        check(removeModel.IsEmpty && removeModel.SelectedVoice == null && !removeModel.RemoveCommand.CanExecute(null) && removeModel.NotificationSeverity == NotificationSeverity.Warning &&
            removeModel.ImportStateText.Contains("registration still needs review"), "Partial removal refreshes actual empty state and clearly reports remaining registration.");
        removeService.Next = new("fake", new[] { other }); await removeModel.RefreshAsync(); removeModel.SelectedVoice = other;
        removeService.Next = new("fake", Array.Empty<VoiceRecord>()); remover.Next = new(ImportPhase.Succeeded, other.LibraryId, Operation: VoiceOperation.Remove);
        await removeModel.RemoveAsync(); check(removeModel.IsEmpty && !removeModel.HasSelection && !removeModel.RemoveCommand.CanExecute(null), "Removing last row shows normal empty state.");
        parsed = ImportProtocol.DecodeResult(Frame(8, id, operation: VoiceOperation.Remove));
        check(parsed.Operation == VoiceOperation.Remove && parsed.Phase == ImportPhase.Succeeded && parsed.Severity == NotificationSeverity.Success, "Protocol removed success.");
        parsed = ImportProtocol.DecodeResult(Frame(9, id, new("library", "cleanup_pending", "Trash retained"), operation: VoiceOperation.Remove));
        check(parsed.Phase == ImportPhase.Partial && parsed.Severity == NotificationSeverity.Warning && parsed.Message.Contains("cleanup"), "Protocol removal cleanup warning.");
        parsed = ImportProtocol.DecodeResult(Frame(2, id, nativeError, operation: VoiceOperation.Remove));
        check(parsed.LibraryId == id && parsed.Error == nativeError && parsed.Operation == VoiceOperation.Remove, "Failed removal preserves target ID and structured error.");
        check(Reject(Frame(8, id)) && Reject(Frame(9, id, nativeError, operation: VoiceOperation.Replace)) && Reject(Frame(5, id, operation: VoiceOperation.Remove)), "Removal cross-operation replies rejected.");
        encoded = ImportProtocol.EncodeRequest(new("", voice.DisplayName!, id, voice.OlvSha256, VoiceOperation.Remove, voice.Health));
        using (var reader = new BinaryReader(new MemoryStream(encoded), Encoding.UTF8)) {
            reader.BaseStream.Position = 16;
            string ReadText() => Encoding.UTF8.GetString(reader.ReadBytes((int)reader.ReadUInt32()));
            check(BitConverter.ToUInt32(encoded, 8) == 3 && ReadText() == id && ReadText() == voice.DisplayName && ReadText() == voice.OlvSha256 && ReadText() == voice.Health && reader.BaseStream.Position == encoded.Length,
                "Remove wire schema contains only immutable ID/name/hash/health, no source/root/path/hive.");
        }
        var removeRequest = new VoiceImportRequest("", voice.DisplayName!, id, voice.OlvSha256, VoiceOperation.Remove, voice.Health);
        deniedResult = await new ElevatedVoiceImportService(denied).ImportAsync(removeRequest, new IgnoreProgress());
        check(deniedResult.Phase == ImportPhase.Cancelled && deniedResult.Operation == VoiceOperation.Remove && deniedResult.Severity == NotificationSeverity.Info, "Fake UAC cancellation retains Remove operation and Info severity.");
        pipeLauncher = new PipeLauncher { Reply = Frame(8, id, operation: VoiceOperation.Remove) };
        pipeResult = await new ElevatedVoiceImportService(pipeLauncher).ImportAsync(removeRequest, new IgnoreProgress());
        check(pipeResult.Phase == ImportPhase.Succeeded && pipeResult.Operation == VoiceOperation.Remove && pipeLauncher.Request != null && BitConverter.ToUInt32(pipeLauncher.Request, 8) == 3,
            "Remove uses actual existing secure local pipe with in-process fake peer, no UAC/registry.");
        pipeLauncher.Reply = Frame(5, id, operation: VoiceOperation.Replace);
        pipeResult = await new ElevatedVoiceImportService(pipeLauncher).ImportAsync(removeRequest, new IgnoreProgress());
        check(pipeResult.Error?.Code == "outcome_unknown" && pipeResult.Operation == VoiceOperation.Remove, "Remove rejects another operation outcome and reconciles uncertainty.");
        pipeLauncher.Reply = Frame(2, "", new("helper", "invalid_request", "Malformed request"));
        pipeResult = await new ElevatedVoiceImportService(pipeLauncher).ImportAsync(removeRequest, new IgnoreProgress());
        check(pipeResult.Phase == ImportPhase.Failed && pipeResult.LibraryId == id && pipeResult.Operation == VoiceOperation.Remove && pipeResult.Message.StartsWith("Removal failed:"), "Pre-dispatch generic rejection retains removal identity/presentation.");
    }
}
