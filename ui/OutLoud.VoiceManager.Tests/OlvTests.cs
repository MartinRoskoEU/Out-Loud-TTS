using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using OutLoud.VoiceManager.Models;
using OutLoud.VoiceManager.Services;
using OutLoud.VoiceManager.ViewModels;

namespace OutLoud.VoiceManager.Tests;

internal static class OlvTests
{
    private sealed class Snapshot : IVoiceManagerService
    {
        public int Reads;
        public VoiceSnapshot Next = new("fake", Array.Empty<VoiceRecord>());
        public Task<VoiceSnapshot> ReadAsync() { ++Reads; return Task.FromResult(Next); }
    }
    private sealed class Import : IVoiceImportService, IImportDialogService
    {
        public int Calls, Confirmations;
        public VoiceImportRequest? Request, Last;
        public VoiceRecord? Target;
        public VoiceImportResult Result = new(ImportPhase.Cancelled);
        public Task<VoiceImportRequest?> ChooseAsync() => Task.FromResult(Request);
        public Task<VoiceRecord?> ConfirmReplacementAsync(VoiceImportRequest request, IReadOnlyList<VoiceRecord> candidates)
        { ++Confirmations; return Task.FromResult(Target); }
        public Task<bool> ConfirmRemovalAsync(VoiceRecord target) => Task.FromResult(false);
        public Task<VoiceImportResult> ImportAsync(VoiceImportRequest request, IProgress<ImportPhase> progress)
        { ++Calls; Last=request; return Task.FromResult(Result); }
    }
    private sealed class Export : IVoiceExportService, IVoiceExportDialog
    {
        public int Calls, Dialogs;
        public VoiceExportRequest? Request;
        public VoiceRecord? Last;
        public BackendError? Error;
        public Task? Pending;
        public Task<string> InspectDestinationAsync(string path) => throw new Exception("Native inspection must not run in frontend tests.");
        public Task<VoiceExportRequest?> ChooseAsync(VoiceRecord voice) { ++Dialogs; return Task.FromResult(Request); }
        public Task ExportAsync(VoiceRecord voice, VoiceExportRequest request)
        { ++Calls; Last=voice; return Pending ?? (Error == null ? Task.CompletedTask : Task.FromException(new BackendException(Error))); }
    }
    public static async Task Run(Action<bool,string> check)
    {
        const string id="0123456789abcdef0123456789abcdef";
        var voice = new VoiceRecord(id,"Hlas ž 🎵","valid","","correct","","pinned","olv",1, OlvSha256:new string('a',64));
        check(VoiceFileSelection.ImportFilter.StartsWith("Supported Voice Files (*.safetensors;*.olv)|*.safetensors;*.olv",StringComparison.Ordinal) &&
            VoiceFileSelection.ImportFilter.Contains("Prepared Pocket Voice (*.safetensors)") && VoiceFileSelection.ImportFilter.Contains("Out Loud Voice (*.olv)"), "Both discoverable import filters, default combined.");
        check(VoiceFileSelection.IsSupported("C:\\ž.OLV") && VoiceFileSelection.IsSupported("C:\\ž.safetensors") && !VoiceFileSelection.IsSupported("C:\\ž.wav"), "Supported source formats only.");
        var name = new ImportDialogModel("C:\\fake\\undead_female.olv");
        check(name.DisplayName == "Undead Female" && name.SourceFormat == "Out Loud Voice (.olv)" && name.Confirm().Operation == VoiceOperation.ImportOlv, "OLV source format/filename-derived name.");
        name.DisplayName="  Hlas ž 🎵  "; check(name.Confirm().DisplayName == "Hlas ž 🎵", "OLV editable Unicode name.");
        check(!new ImportDialogModel("C:\\fake\\voice.wav").CanConfirm, "Unsupported confirmation rejected.");
        check(VoiceFileSelection.ExportFileName(voice.DisplayName!) == "Hlas ž 🎵.olv" && VoiceFileSelection.ExportFileName("CON")=="voice-CON.olv" &&
            VoiceFileSelection.ExportFileName("bad:/name..")=="bad__name.olv", "Safe Unicode/default extension/device filename suggestion.");
        string longName=VoiceFileSelection.ExportFileName(new string('a',119)+"🎵"+new string('b',200));
        check(longName.Length<=124 && !char.IsHighSurrogate(longName[^5]), "Long filename bounded without splitting surrogate pair.");
        foreach (var operation in new[] { VoiceOperation.ImportOlv, VoiceOperation.ReplaceOlv }) {
            var request=new VoiceImportRequest("C:\\hlas-ž-🎵.olv","Hlas ž 🎵", operation == VoiceOperation.ReplaceOlv ? id : null, operation == VoiceOperation.ReplaceOlv ? voice.OlvSha256 : null);
            byte[] wire=ImportProtocol.EncodeRequest(request);
            check(BitConverter.ToUInt32(wire,4)==3 && BitConverter.ToUInt32(wire,8)==(uint)operation && BitConverter.ToUInt32(wire,12)==wire.Length-16 && Encoding.UTF8.GetString(wire).Contains("Hlas ž 🎵"), "Additive OLV wire format/version/Unicode identity.");
            var result=ImportProtocol.DecodeResult(ImportTests.Frame(operation==VoiceOperation.ImportOlv ? 1u : 5u,id,operation:operation));
            check(result.Operation==operation && result.Phase==ImportPhase.Succeeded, "OLV result decoding is explicit.");
        }
        bool Reject(Action action) { try { action(); return false; } catch (InvalidDataException) { return true; } }
        check(Reject(()=>ImportProtocol.DecodeResult(ImportTests.Frame(5,id,operation:VoiceOperation.ImportOlv))) &&
            Reject(()=>ImportProtocol.DecodeResult(ImportTests.Frame(1,id,operation:VoiceOperation.ReplaceOlv))) &&
            Reject(()=>ImportProtocol.EncodeRequest(new("C:\\voice.olv","Voice",Kind:VoiceOperation.Import))), "Cross-operation source/outcome rejected.");
        var snapshot=new Snapshot(); var import=new Import(); var export=new Export();
        var model=new MainViewModel(snapshot,import,import,exporter:export,exportDialog:export);
        check(!model.ExportCommand.CanExecute(null),"Export disabled without selection."); await model.ExportAsync();
        check(export.Dialogs==0 && export.Calls==0,"No selection has no dialog/copy.");
        import.Request=new("C:\\fake\\voice.olv",voice.DisplayName!); snapshot.Next=new("fake",new[] {voice});
        import.Result=new(ImportPhase.Succeeded,id,Operation:VoiceOperation.ImportOlv); await model.ImportAsync();
        check(import.Last?.Operation==VoiceOperation.ImportOlv && snapshot.Reads==1 && ReferenceEquals(model.SelectedVoice,voice),"OLV import refresh/select new immutable ID.");
        import.Request=import.Request with { DisplayName="Another" };
        foreach (string code in new[] {"corrupt_voice","incompatible_model","duplicate_content","source_io"}) {
            import.Result=new(ImportPhase.Failed, Error:new("library",code,"Validation failed"),DuplicateIds:code=="duplicate_content" ? new[] {id} : null,Operation:VoiceOperation.ImportOlv);
            await model.ImportAsync(); check(snapshot.Reads==1 && model.SelectedVoice==voice && model.HasImportDiagnostic && model.NotificationSeverity==NotificationSeverity.Error,"OLV validation/duplicate/locked source failure preserves snapshot.");
        }
        import.Result=new(ImportPhase.Partial,id,new("sapi","registry_io","Registration failed"),Operation:VoiceOperation.ImportOlv); await model.ImportAsync();
        check(snapshot.Reads==2 && model.NotificationSeverity==NotificationSeverity.Warning && model.ImportStateText.Contains("kept"),"OLV partial registration is warning, refreshed retained voice.");
        import.Result=new(ImportPhase.Failed,Error:new("helper","outcome_unknown","Unknown"),Operation:VoiceOperation.ImportOlv); await model.ImportAsync();
        check(snapshot.Reads==3 && model.NotificationSeverity==NotificationSeverity.Warning,"Unknown OLV outcome refreshes.");
        import.Request=import.Request with { DisplayName=voice.DisplayName! }; int before=import.Calls; await model.ImportAsync();
        check(import.Confirmations==1 && import.Calls==before && model.ImportPhase==ImportPhase.Cancelled,"OLV replace refusal has no helper/UAC.");
        import.Target=voice; import.Result=new(ImportPhase.Succeeded,id,Operation:VoiceOperation.ReplaceOlv); await model.ImportAsync();
        check(import.Last?.Operation==VoiceOperation.ReplaceOlv && import.Last.TargetId==id && import.Last.ExpectedHash==voice.OlvSha256 && model.SelectedVoice?.LibraryId==id && model.ImportStateText=="Voice replaced successfully.","Confirmed OLV replacement retains exact ID/hash/name and selection.");
        var peer=voice with {LibraryId="fedcba9876543210fedcba9876543210",DisplayName="Different stored name"};
        snapshot.Next=new("fake",new[] {voice,peer}); import.Request=import.Request with {DisplayName="Different import name"}; import.Target=null;
        import.Result=new(ImportPhase.Failed,Error:new("library","duplicate_content","Ambiguous"),DuplicateIds:new[] {id,peer.LibraryId},Operation:VoiceOperation.ImportOlv);
        before=import.Calls; int confirms=import.Confirmations; await model.ImportAsync();
        check(import.Calls==before+1 && import.Confirmations==confirms+1 && model.ImportPhase==ImportPhase.Cancelled,"Ambiguous backend OLV duplicates open exact target selection; refusal does not resubmit.");
        check(new ReplacementDialogModel(import.Request,new[] {voice,peer}).SelectedTarget==null,"Different-name duplicate identities never auto-select.");
        snapshot.Next=new("fake",new[] {voice}); await model.RefreshAsync(); model.SelectedVoice=voice;
        var list=model.Voices; int reads=snapshot.Reads;
        await model.ExportAsync(); check(export.Calls==0 && model.NotificationSeverity==NotificationSeverity.Info && model.ImportStateText.Contains("No file"),"Save cancellation/overwrite refusal no copy.");
        export.Request=new("C:\\fake\\hlas-ž-🎵.olv","absent"); await model.ExportAsync();
        check(export.Calls==1 && ReferenceEquals(export.Last,voice) && snapshot.Reads==reads && ReferenceEquals(model.Voices,list) && ReferenceEquals(model.SelectedVoice,voice),"Successful Unicode OLV export keeps selection/snapshot without refresh.");
        check(model.NotificationSeverity==NotificationSeverity.Success && model.ImportStateText=="Voice exported successfully.","Export reports confirmed success.");
        foreach (string code in new[] {"destination_io","export_conflict","voice_not_found","export_unsupported"}) {
            export.Error=new("library",code,"Export failed",SystemError:code=="destination_io" ? 112u : 0u); await model.ExportAsync();
            check(model.ImportResult?.Error==export.Error && model.HasImportDiagnostic && snapshot.Reads==reads && model.SelectedVoice==voice && model.Voices==list,"Write/disk-full, stale source/destination or old bridge failure preserves diagnostics/list.");
            check(model.NotificationSeverity==(code is "export_conflict" or "voice_not_found" ? NotificationSeverity.Warning : NotificationSeverity.Error),"Export failure severity.");
        }
        model.DismissNotificationCommand.Execute(null); check(!model.HasImportResult && !model.HasImportDiagnostic,"Export notification and diagnostics dismiss together.");
        foreach (var damaged in new[] { voice with {Health="corrupt_voice"},voice with {OlvSha256=null},voice with {OlvSha256="bad"},voice with {LibraryId=new string('z',32)},voice with {DisplayName=null} }) {
            model.SelectedVoice=damaged; check(!model.ExportCommand.CanExecute(null),"Unhealthy/unavailable export disabled.");
        }
        model.SelectedVoice=voice; export.Error=null;
        var completion=new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously); export.Pending=completion.Task;
        Task active=model.ExportAsync(); int copies=export.Calls, dialogs=export.Dialogs, helpers=import.Calls;
        check(model.IsBusy && !model.ExportCommand.CanExecute(null) && !model.ImportCommand.CanExecute(null) && !model.RemoveCommand.CanExecute(null) && !model.RefreshCommand.CanExecute(null),"Export busy blocks conflicting actions.");
        await model.ExportAsync(); await model.ImportAsync(); await model.RemoveAsync(); await model.RefreshAsync();
        check(export.Calls==copies && export.Dialogs==dialogs && import.Calls==helpers && snapshot.Reads==reads,"Repeated concurrent commands suppressed.");
        completion.SetResult(); await active; check(!model.IsBusy && model.ExportCommand.CanExecute(null),"Export completion restores commands.");
        completion=new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously); export.Pending=completion.Task;
        active=model.ExportAsync(); Task closing=model.ShutdownAsync();
        check(!closing.IsCompleted && !model.ExportCommand.CanExecute(null),"Window shutdown waits for active native export cleanup.");
        completion.SetResult(); await active; await closing;
        check(!model.ExportCommand.CanExecute(null),"Closing cannot start another export.");
    }
}
