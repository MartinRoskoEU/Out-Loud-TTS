using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Threading.Tasks;
using OutLoud.VoiceManager.Models;
using OutLoud.VoiceManager.Services;

namespace OutLoud.VoiceManager.ViewModels;

public sealed class VoiceTesterViewModel : INotifyPropertyChanged
{
    private readonly IVoiceTestBackend backend;
    private readonly IWaveExportDialog? exportDialog;
    private IReadOnlyList<VoiceRecord> voices = Array.Empty<VoiceRecord>();
    private VoiceRecord? selected;
    private string root = "", text = "Adventurer, listen carefully: at dawn, three scouts vanished near the shattered watchtower. Search the ruins, retrieve the silver key, and protect the wounded prisoner before the seventh bell. But tell me—did you hear a strange whisper beneath the storm, or was it only the wind?";
    private bool busy, closed;
    private long revision;
    private CancellationTokenSource? stop;
    private Task active = Task.CompletedTask;
    private VoiceTestState state;
    private VoiceTestError? error;
    private string? notification;
    private NotificationSeverity severity = NotificationSeverity.Info;
    public VoiceTesterViewModel(IVoiceTestBackend backend, IWaveExportDialog? exportDialog = null)
    {
        this.backend = backend; this.exportDialog = exportDialog;
        PlayCommand = new RefreshCommand(PlayAsync, () => !IsBusy && !closed && ValidationText.Length == 0);
        StopCommand = new RefreshCommand(StopAsync, () => IsBusy && State != VoiceTestState.Stopping);
        ExportCommand = new RefreshCommand(ExportAsync, () => exportDialog != null && PlayCommand.CanExecute(null));
        DismissCommand = new RefreshCommand(() => { notification = null; error = null; ChangedAll(); return Task.CompletedTask; }, () => HasNotification);
    }
    public event PropertyChangedEventHandler? PropertyChanged;
    public RefreshCommand PlayCommand { get; }
    public RefreshCommand StopCommand { get; }
    public RefreshCommand ExportCommand { get; }
    public RefreshCommand DismissCommand { get; }
    public string BackendName => backend.Name;
    public IReadOnlyList<VoiceRecord> Voices => voices;
    public VoiceRecord? SelectedVoice
    {
        get => selected;
        set { if (ReferenceEquals(selected, value)) return; if (selected?.LibraryId != value?.LibraryId && IsBusy) RequestStop(); selected = value; ChangedAll(); }
    }
    public string Text { get => text; set { text = value ?? ""; ChangedAll(); } }
    public string CharacterCount => $"{Text.Length:N0} / {VoiceTestRequest.MaximumTextLength:N0} characters";
    public bool IsBusy => busy;
    public bool CanEdit => !IsBusy && !closed;
    public VoiceTestState State => state;
    public string StatusText => state switch {
        VoiceTestState.Preparing => "Preparing the selected SAPI voice…",
        VoiceTestState.Speaking => "Speaking — SAPI is synthesizing or playing audio.",
        VoiceTestState.Exporting => "Synthesizing a new WAV through SAPI…",
        VoiceTestState.Stopping => "Stopping — waiting for SAPI and resource cleanup…",
        VoiceTestState.Completed => "Completed.", VoiceTestState.Stopped => "Stopped.", VoiceTestState.Failed => "Test failed.", _ => "Ready to test."
    };
    public string ValidationText => selected == null ? (voices.Count == 0 ? "No managed voices are available. Refresh the Voice Library." : "Select a voice to test.") :
        new VoiceTestRequest(selected, root, Text).ValidationError ?? "";
    public bool HasNotification => notification != null;
    public string NotificationText => notification ?? "";
    public NotificationSeverity Severity => severity;
    public string SeverityText => severity.ToString();
    public bool HasDiagnostic => HasNotification && error != null;
    public string Diagnostic => error?.Diagnostic ?? "";
    public void UpdateVoices(VoiceSnapshot snapshot, string? preferredId = null)
    {
        string? id = preferredId ?? selected?.LibraryId;
        voices = snapshot.Voices.Where(voice => VoiceTestRequest.ManagedId(voice.LibraryId)).ToArray(); root = snapshot.LibraryRoot;
        var next = voices.FirstOrDefault(voice => voice.LibraryId == id);
        if (IsBusy && (next == null || !VoiceTestRequest.Suitable(next) || next.LibraryId != selected?.LibraryId)) RequestStop();
        Changed(nameof(Voices)); selected = next; ChangedAll();
    }
    public Task PlayAsync() => Begin(false);
    public Task ExportAsync() => Begin(true);
    private Task Begin(bool exporting)
    {
        if (busy || closed) return Task.CompletedTask;
        if (ValidationText.Length != 0) { Fail(new("invalid_test", ValidationText)); return Task.CompletedTask; }
        if (exporting && exportDialog == null) { Fail(new("export_unavailable", "WAV export is unavailable.")); return Task.CompletedTask; }
        // Capture voice/text before any await; never mutate a live session's token.
        var request = new VoiceTestRequest(selected!, root, Text);
        stop = new CancellationTokenSource(); var cancellation = stop; long id = ++revision;
        busy = true; state = VoiceTestState.Preparing; notification = null; error = null; ChangedAll();
        active = Execute(request, exporting, cancellation, id); return active;
    }
    private sealed class TestProgress : IProgress<VoiceTestState>
    {
        private readonly Action<VoiceTestState> receive;
        private readonly SynchronizationContext? context = SynchronizationContext.Current;
        public TestProgress(Action<VoiceTestState> receive) => this.receive = receive;
        public void Report(VoiceTestState value) { if (context == null || SynchronizationContext.Current == context) receive(value); else context.Post(_ => receive(value), null); }
    }
    private async Task Execute(VoiceTestRequest request, bool exporting, CancellationTokenSource cancellation, long id)
    {
        try {
            if (exporting) {
                string? path = await exportDialog!.ChoosePathAsync();
                if (path == null) { state = VoiceTestState.Idle; Notify("Export cancelled. No WAV was saved.", NotificationSeverity.Info); return; }
                request = request with { ExportPath = path };
            }
            if (cancellation.IsCancellationRequested) { state = VoiceTestState.Stopped; return; }
            var progress = new TestProgress(value => {
                if (revision != id || !busy || closed || cancellation.IsCancellationRequested) return;
                if (value is VoiceTestState.Preparing or VoiceTestState.Speaking or VoiceTestState.Exporting) { state = value; ChangedAll(); }
            });
            VoiceTestOutcome result = await backend.RunAsync(request, progress, cancellation.Token);
            if (result.Cancelled || (cancellation.IsCancellationRequested && result.ExportPath == null)) {
                state = VoiceTestState.Stopped; Notify("Test stopped. SAPI resources have been released.", NotificationSeverity.Info);
            } else {
                state = VoiceTestState.Completed; Notify(result.ExportPath == null ? "SAPI playback completed." : "WAV saved: " + result.ExportPath, NotificationSeverity.Success);
            }
        }
        catch (OperationCanceledException) { state = VoiceTestState.Stopped; Notify("Test stopped.", NotificationSeverity.Info); }
        catch (VoiceTestException failure) { Fail(failure.Error); }
        catch (Exception failure) { Fail(new("test_failed", "The voice test could not finish. See diagnostic details.", failure.HResult, failure.GetType().Name)); }
        finally { busy = false; stop = null; cancellation.Dispose(); ChangedAll(); }
    }
    private void RequestStop() { if (!busy || stop == null || stop.IsCancellationRequested) return; state = VoiceTestState.Stopping; stop.Cancel(); ChangedAll(); }
    public async Task StopAsync() { RequestStop(); await active; }
    public async Task CloseAsync() { closed = true; RequestStop(); await active; ChangedAll(); }
    private void Notify(string message, NotificationSeverity value) { notification = message; severity = value; error = null; }
    private void Fail(VoiceTestError value) { state = VoiceTestState.Failed; error = value; notification = value.Message; severity = NotificationSeverity.Error; ChangedAll(); }
    private void ChangedAll()
    {
        foreach (string property in new[] { nameof(Voices), nameof(SelectedVoice), nameof(Text), nameof(CharacterCount), nameof(IsBusy), nameof(CanEdit), nameof(State), nameof(StatusText), nameof(ValidationText), nameof(HasNotification), nameof(NotificationText), nameof(Severity), nameof(SeverityText), nameof(HasDiagnostic), nameof(Diagnostic) }) Changed(property);
        PlayCommand.Notify(); StopCommand.Notify(); ExportCommand.Notify(); DismissCommand.Notify();
    }
    private void Changed([CallerMemberName] string? name = null) => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}
