using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Threading.Tasks;
using OutLoud.VoiceManager.Models;
using OutLoud.VoiceManager.Services;

namespace OutLoud.VoiceManager.ViewModels;

public sealed class MainViewModel : INotifyPropertyChanged
{
    private readonly IVoiceManagerService service;
    private VoiceSnapshot snapshot = new("", Array.Empty<VoiceRecord>());
    private VoiceRecord? selectedVoice;
    private bool isBusy;
    private BackendError? error;
    private readonly IImportDialogService? dialogs;
    private readonly IVoiceImportService? importer;
    private ImportPhase importPhase;
    private VoiceImportResult? importResult;
    private VoiceOperation activeOperation;
    private bool testerPage, closing;
    private readonly IVoiceExportService? exporter;
    private readonly IVoiceExportDialog? exportDialog;
    private TaskCompletionSource? exportCompletion;
    public MainViewModel(IVoiceManagerService service, IImportDialogService? dialogs = null, IVoiceImportService? importer = null, VoiceTesterViewModel? tester = null, IVoiceExportService? exporter = null, IVoiceExportDialog? exportDialog = null)
    {
        this.service = service; this.exporter = exporter; this.exportDialog = exportDialog;
        this.dialogs = dialogs; this.importer = importer;
        Tester = tester;
        if (Tester != null) Tester.PropertyChanged += (_, e) => { if (e.PropertyName == nameof(VoiceTesterViewModel.IsBusy)) { ImportCommand?.Notify(); RemoveCommand?.Notify(); ExportCommand?.Notify(); } };
        RefreshCommand = new RefreshCommand(RefreshAsync, () => !IsBusy && !closing);
        ImportCommand = new RefreshCommand(ImportAsync, () => !IsBusy && !closing && Tester?.IsBusy != true && dialogs != null && importer != null);
        RemoveCommand = new RefreshCommand(RemoveAsync, () => !IsBusy && !closing && Tester?.IsBusy != true && dialogs != null && importer != null && SelectedVoice is { } target &&
            target.LibraryId.Length == 32 && target.LibraryId.All(c => c is >= '0' and <= '9' or >= 'a' and <= 'f'));
        ExportCommand = new RefreshCommand(ExportAsync, () => !IsBusy && !closing && Tester?.IsBusy != true && exporter != null && exportDialog != null && SelectedVoice is { Health: "valid", DisplayName: not null, OlvSha256: not null } voice &&
            !string.IsNullOrWhiteSpace(voice.DisplayName) && voice.LibraryId.Length == 32 && voice.LibraryId.All(c => c is >= '0' and <= '9' or >= 'a' and <= 'f') &&
            voice.OlvSha256.Length == 64 && voice.OlvSha256.All(c => c is >= '0' and <= '9' or >= 'a' and <= 'f'));
        DismissNotificationCommand = new RefreshCommand(() => { importResult = null; StateChanged(); return Task.CompletedTask; }, () => HasImportResult);
        ShowTesterCommand = new RefreshCommand(() => { if (!testerPage) Tester!.UpdateVoices(snapshot, SelectedVoice?.LibraryId); testerPage = true; StateChanged(); return Task.CompletedTask; }, () => Tester != null && !IsBusy && !closing);
        ShowLibraryCommand = new RefreshCommand(async () => { if (Tester != null) await Tester.StopAsync(); testerPage = false; StateChanged(); }, () => !IsBusy && !closing);
    }
    public event PropertyChangedEventHandler? PropertyChanged;
    public RefreshCommand RefreshCommand { get; }
    public RefreshCommand ImportCommand { get; }
    public RefreshCommand RemoveCommand { get; }
    public RefreshCommand ExportCommand { get; }
    public RefreshCommand DismissNotificationCommand { get; }
    public RefreshCommand ShowTesterCommand { get; }
    public RefreshCommand ShowLibraryCommand { get; }
    public VoiceTesterViewModel? Tester { get; }
    public bool IsTesterPage => testerPage;
    public bool IsLibraryPage => !testerPage;
    public async Task ShutdownAsync() {
        closing = true; StateChanged();
        if (Tester != null) await Tester.CloseAsync();
        if (exportCompletion != null) await exportCompletion.Task;
    }
    public IReadOnlyList<VoiceRecord> Voices => snapshot.Voices;
    public string LibraryRoot => snapshot.LibraryRoot;
    public VoiceRecord? SelectedVoice
    {
        get => selectedVoice;
        set { if (selectedVoice == value) return; selectedVoice = value; Changed(); Changed(nameof(HasSelection)); RemoveCommand.Notify(); ExportCommand.Notify(); }
    }
    public bool HasSelection => SelectedVoice != null;
    public bool IsBusy => isBusy;
    public BackendError? Error => error;
    public bool HasError => Error != null;
    public bool IsEmpty => !IsBusy && !HasError && Voices.Count == 0;
    public bool HasSapiErrors => Voices.Any(voice => voice.SapiError != null);
    public ImportPhase ImportPhase => importPhase;
    public VoiceImportResult? ImportResult => importResult;
    public bool HasImportResult => importResult != null;
    public bool HasImportDiagnostic => importResult?.HasDiagnostic == true;
    public NotificationSeverity NotificationSeverity => importResult?.Severity ?? NotificationSeverity.Info;
    public string NotificationSeverityText => NotificationSeverity.ToString();
    public string NotificationTitle => importResult?.Title ?? NotificationSeverityText;
    public string ImportStateText => importPhase switch {
        ImportPhase.Choosing when activeOperation == VoiceOperation.Remove => "Confirm removal of the selected voice.",
        ImportPhase.Choosing when activeOperation == VoiceOperation.Export => "Choose where to save the Out Loud voice.",
        ImportPhase.Choosing => "Choose a voice file and confirm its name.",
        ImportPhase.AwaitingElevation => "Waiting for Windows Administrator approval...",
        ImportPhase.Importing when activeOperation == VoiceOperation.Remove => "Removing the selected voice and its Windows speech registration...",
        ImportPhase.Importing when activeOperation == VoiceOperation.Export => "Exporting the selected voice...",
        ImportPhase.Importing => "Validating and saving the voice file...",
        _ => importResult?.Message ?? ""
    };
    public string ImportDiagnostic => importResult?.Diagnostic ?? "";
    public string StateText => IsBusy ? (importPhase is ImportPhase.Choosing or ImportPhase.AwaitingElevation or ImportPhase.Importing ? ImportStateText : "Reading machine voice state…") : HasError ? "Refresh failed. Showing the last successful snapshot, if available." :
        $"{Voices.Count} managed voice(s)";

    public async Task RefreshAsync()
    {
        // Called on the UI context. Set the guard before the first await.
        if (isBusy || closing) return;
        isBusy = true; error = null; StateChanged();
        try { await ReadSnapshotAsync(); }
        finally { isBusy = false; StateChanged(); }
    }
    private async Task ReadSnapshotAsync(string? newSelectionId = null, bool selectRemaining = false)
    {
        error = null;
        try
        {
            VoiceSnapshot next = await service.ReadAsync();
            string? selectionId = newSelectionId ?? SelectedVoice?.LibraryId;
            snapshot = next; // replace the whole immutable snapshot, never Clear/Add
            Changed(nameof(Voices)); Changed(nameof(LibraryRoot));
            // Replace even an equal-valued record with the new snapshot's object.
            // DataGrid can clear SelectedItem when ItemsSource changes above.
            selectedVoice = next.Voices.FirstOrDefault(voice => voice.LibraryId == selectionId);
            if (selectedVoice == null && selectRemaining) selectedVoice = next.Voices.FirstOrDefault();
            Changed(nameof(SelectedVoice));
            Changed(nameof(HasSelection)); Changed(nameof(HasSapiErrors));
            Tester?.UpdateVoices(next);
        }
        catch (BackendException failure) { error = failure.Error; }
        catch (Exception failure)
        {
            error = new BackendError("frontend", "unexpected_failure", "Unable to refresh the voice list.",
                LibraryCode: failure.GetType().Name);
        }
    }
    // Direct progress on the captured UI SynchronizationContext avoids queued
    // stale notifications overwriting the final outcome of a fast operation.
    private sealed class ImportProgress : IProgress<ImportPhase>
    {
        private readonly MainViewModel owner;
        private readonly System.Threading.SynchronizationContext? context = System.Threading.SynchronizationContext.Current;
        public ImportProgress(MainViewModel owner) => this.owner = owner;
        public void Report(ImportPhase value)
        {
            void Update() { if (owner.importPhase is ImportPhase.Choosing or ImportPhase.AwaitingElevation or ImportPhase.Importing) { owner.importPhase = value; owner.StateChanged(); } }
            if (System.Threading.SynchronizationContext.Current == context) Update();
            else context?.Post(_ => Update(), null);
        }
    }
    public async Task ImportAsync()
    {
        if (isBusy || closing || Tester?.IsBusy == true || dialogs == null || importer == null) return;
        activeOperation = VoiceOperation.Import;
        isBusy = true; importPhase = ImportPhase.Choosing; importResult = null; StateChanged();
        try
        {
            VoiceImportRequest? request = await dialogs.ChooseAsync();
            if (request != null) {
                var candidates = Voices.Where(voice => voice.DisplayName != null && string.Equals(voice.DisplayName.Trim(), request.DisplayName.Trim(), StringComparison.OrdinalIgnoreCase)).ToArray();
                if (candidates.Length != 0) {
                    VoiceRecord? target = await dialogs.ConfirmReplacementAsync(request, candidates);
                    if (target == null) request = null;
                    else if (!candidates.Any(candidate => ReferenceEquals(candidate, target)) || target.OlvSha256 == null)
                        throw new InvalidOperationException("Replacement requires an unambiguous snapshot target with content identity.");
                    else request = new VoiceImportRequest(request.SourcePath, target.DisplayName!, target.LibraryId, target.OlvSha256);
                }
            }
            importResult = request == null ? new(ImportPhase.Cancelled) : await importer.ImportAsync(request, new ImportProgress(this));
            if (request?.IsOlv == true && importResult.Error?.Code == "duplicate_content" && importResult.DuplicateIds is { Length: > 1 } ids) {
                // Resolve every reported identity in a fresh snapshot before asking;
                // never infer an arbitrary replacement from names or array order.
                await ReadSnapshotAsync();
                var candidates = Voices.Where(voice => ids.Contains(voice.LibraryId) && voice.DisplayName != null && voice.OlvSha256 != null).ToArray();
                if (!HasError && candidates.Length == ids.Distinct().Count()) {
                    VoiceRecord? target = await dialogs.ConfirmReplacementAsync(request, candidates);
                    if (target == null) { request = null; importResult = new(ImportPhase.Cancelled, Operation: VoiceOperation.ImportOlv); }
                    else if (!candidates.Any(candidate => ReferenceEquals(candidate, target))) throw new InvalidOperationException("Choose an exact duplicate snapshot target.");
                    else {
                        request = new(request.SourcePath, target.DisplayName!, target.LibraryId, target.OlvSha256);
                        importResult = await importer.ImportAsync(request, new ImportProgress(this));
                    }
                }
            }
            importPhase = importResult.Phase; StateChanged();
            if (importResult.Phase is ImportPhase.Succeeded or ImportPhase.Partial || importResult.Error?.Code == "outcome_unknown" ||
                request?.IsReplacement == true)
                await ReadSnapshotAsync(importResult.LibraryId);
        }
        catch (Exception failure) {
            importResult = new(ImportPhase.Failed, Error: new BackendError("frontend", "import_failure", "Unable to complete the import workflow.", LibraryCode: failure.GetType().Name));
            importPhase = ImportPhase.Failed;
        }
        finally { isBusy = false; StateChanged(); }
    }
    public async Task ExportAsync()
    {
        if (!ExportCommand.CanExecute(null) || exporter == null || exportDialog == null || SelectedVoice is not { } target) return;
        activeOperation = VoiceOperation.Export;
        var completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        exportCompletion = completion;
        isBusy = true; importPhase = ImportPhase.Choosing; importResult = null; StateChanged();
        try {
            VoiceExportRequest? request = await exportDialog.ChooseAsync(target);
            if (closing) request = null;
            if (request == null) importResult = new(ImportPhase.Cancelled, Operation: VoiceOperation.Export);
            else {
                importPhase = ImportPhase.Importing; StateChanged();
                await exporter.ExportAsync(target, request);
                importResult = new(ImportPhase.Succeeded, target.LibraryId, Operation: VoiceOperation.Export);
            }
        } catch (BackendException failure) { importResult = new(ImportPhase.Failed, target.LibraryId, failure.Error, Operation: VoiceOperation.Export); }
        catch (Exception failure) { importResult = new(ImportPhase.Failed, target.LibraryId,
            new BackendError("frontend", "export_failure", "Unable to export the voice file.", LibraryCode: failure.GetType().Name), Operation: VoiceOperation.Export); }
        finally {
            importPhase = importResult?.Phase ?? ImportPhase.Failed; isBusy = false;
            exportCompletion = null; completion.SetResult(); StateChanged();
        }
        // Export never mutates the source snapshot or refreshes/clears selection.
    }
    public async Task RemoveAsync()
    {
        if (!RemoveCommand.CanExecute(null) || dialogs == null || importer == null || SelectedVoice is not { } target) return;
        activeOperation = VoiceOperation.Remove;
        isBusy = true; importPhase = ImportPhase.Choosing; importResult = null; StateChanged();
        try
        {
            if (!await dialogs.ConfirmRemovalAsync(target))
                importResult = new(ImportPhase.Cancelled, Operation: VoiceOperation.Remove);
            else {
                var request = new VoiceImportRequest("", target.DisplayName ?? "", target.LibraryId, target.OlvSha256 ?? "", VoiceOperation.Remove, target.Health);
                importResult = await importer.ImportAsync(request, new ImportProgress(this));
                importPhase = importResult.Phase; StateChanged();
                // Refresh failures/conflicts too: the snapshot is never authority.
                await ReadSnapshotAsync(target.LibraryId, selectRemaining: true);
            }
            importPhase = importResult.Phase;
        }
        catch (Exception failure) {
            importResult = new(ImportPhase.Failed, target.LibraryId, new BackendError("frontend", "removal_failure", "Unable to complete the removal workflow.", LibraryCode: failure.GetType().Name), Operation: VoiceOperation.Remove);
            importPhase = ImportPhase.Failed;
        }
        finally { isBusy = false; StateChanged(); }
    }
    private void StateChanged()
    {
        Changed(nameof(IsBusy)); Changed(nameof(Error)); Changed(nameof(HasError));
        Changed(nameof(IsEmpty)); Changed(nameof(StateText)); RefreshCommand.Notify();
        Changed(nameof(ImportPhase)); Changed(nameof(ImportResult)); Changed(nameof(HasImportResult));
        Changed(nameof(ImportStateText)); Changed(nameof(ImportDiagnostic)); ImportCommand.Notify();
        RemoveCommand.Notify(); ExportCommand.Notify();
        Changed(nameof(HasImportDiagnostic)); Changed(nameof(NotificationSeverity)); Changed(nameof(NotificationSeverityText)); Changed(nameof(NotificationTitle)); DismissNotificationCommand.Notify();
        Changed(nameof(IsTesterPage)); Changed(nameof(IsLibraryPage)); ShowTesterCommand.Notify(); ShowLibraryCommand.Notify();
    }
    private void Changed([CallerMemberName] string? name = null) => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}
