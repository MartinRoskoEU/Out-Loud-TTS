using System;
using System.Collections.Generic;
using System.Dynamic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using OutLoud.VoiceManager.Models;
using OutLoud.VoiceManager.Services;
using OutLoud.VoiceManager.ViewModels;

namespace OutLoud.VoiceManager.Tests;

internal static class VoiceTesterTests
{
    private const string Id = "0123456789abcdef0123456789abcdef", Other = "fedcba9876543210fedcba9876543210";
    private const string Root = @"C:\fake-library";
    private static VoiceRecord Voice(string id = Id) => new(id, "Same name", "valid", "", "correct", "", "compat", "olv", 1, false);
    private sealed class Backend : IVoiceTestBackend
    {
        public string Name => "Fake baseline";
        public readonly List<(VoiceTestRequest Request, IProgress<VoiceTestState> Progress, CancellationToken Stop, TaskCompletionSource<VoiceTestOutcome> Done)> Runs = new();
        public Task<VoiceTestOutcome> RunAsync(VoiceTestRequest request, IProgress<VoiceTestState> progress, CancellationToken cancellation)
        {
            var done = new TaskCompletionSource<VoiceTestOutcome>(TaskCreationOptions.RunContinuationsAsynchronously);
            Runs.Add((request, progress, cancellation, done)); return done.Task;
        }
    }
    private sealed class SaveDialog : IWaveExportDialog
    {
        public string? Path;
        public Task<string?> ChoosePathAsync() => Task.FromResult(Path);
    }
    private sealed class Snapshot : IVoiceManagerService
    {
        public VoiceSnapshot Next = new(Root, new[] { Voice(), Voice(Other) });
        public Task<VoiceSnapshot> ReadAsync() => Task.FromResult(Next);
    }
    private sealed class Mutations : IImportDialogService, IVoiceImportService
    {
        public int DialogCalls;
        public Task<VoiceImportRequest?> ChooseAsync() { ++DialogCalls; return Task.FromResult<VoiceImportRequest?>(null); }
        public Task<VoiceRecord?> ConfirmReplacementAsync(VoiceImportRequest request, IReadOnlyList<VoiceRecord> candidates) => throw new Exception("Unexpected replacement dialog.");
        public Task<bool> ConfirmRemovalAsync(VoiceRecord target) { ++DialogCalls; return Task.FromResult(false); }
        public Task<VoiceImportResult> ImportAsync(VoiceImportRequest request, IProgress<ImportPhase> progress) => throw new Exception("Unexpected mutation.");
    }
    private sealed class Progress : IProgress<VoiceTestState>
    {
        public readonly List<VoiceTestState> States = new();
        public void Report(VoiceTestState state) => States.Add(state);
    }
    private sealed class SessionFactory : ISapiTestSessionFactory
    {
        public readonly ManualResetEventSlim Started = new();
        public readonly ManualResetEventSlim Finish = new();
        public bool Disposed, Purged, BadWave, Hold;
        public int Result, Creates, Worker;
        public string? Text, Temporary;
        public Exception? StartFailure;
        public bool Sta = true, SameThread = true;
        public ISapiTestSession Create(VoiceTestRequest request, string? temporaryWavePath)
        { ++Creates; Worker = Environment.CurrentManagedThreadId; Temporary = temporaryWavePath; return new Session(this); }
        private sealed class Session : ISapiTestSession
        {
            private readonly SessionFactory owner;
            public Session(SessionFactory owner) { this.owner = owner; ThreadCheck(); }
            private void ThreadCheck() { owner.Sta &= Thread.CurrentThread.GetApartmentState() == ApartmentState.STA; owner.SameThread &= owner.Worker == Environment.CurrentManagedThreadId; }
            public void Start(string text) { ThreadCheck(); owner.Text = text; owner.Started.Set(); if (owner.StartFailure != null) throw owner.StartFailure; }
            public void Purge() { ThreadCheck(); owner.Purged = true; owner.Finish.Set(); }
            public bool WaitUntilDone(int milliseconds) { ThreadCheck(); return !owner.Hold || owner.Finish.Wait(milliseconds); }
            public int LastResult() { ThreadCheck(); return owner.Result; }
            public void Dispose() {
                ThreadCheck(); owner.Disposed = true;
                if (owner.Temporary != null) {
                    if (owner.BadWave) File.WriteAllText(owner.Temporary, "invalid WAV"); else WriteWave(owner.Temporary);
                }
            }
        }
    }
    private static void WriteWave(string path)
    {
        using var writer = new BinaryWriter(File.Create(path), Encoding.ASCII);
        writer.Write(0x46464952u); writer.Write(38u); writer.Write(0x45564157u);
        writer.Write(0x20746d66u); writer.Write(16u); writer.Write((ushort)1); writer.Write((ushort)1);
        writer.Write(24000u); writer.Write(48000u); writer.Write((ushort)2); writer.Write((ushort)16);
        writer.Write(0x61746164u); writer.Write(2u); writer.Write((short)123);
    }
    private static async Task<VoiceTestError?> Failure(Task<VoiceTestOutcome> task)
    { try { await task; return null; } catch (VoiceTestException error) { return error.Error; } }

    public static async Task Run(Action<bool, string> check)
    {
        var a = Voice(); var b = Voice(Other); var backend = new Backend(); var save = new SaveDialog();
        var vm = new VoiceTesterViewModel(backend, save);
        check(!vm.PlayCommand.CanExecute(null) && vm.ValidationText.Contains("No managed"), "Tester empty list blocks Play.");
        vm.UpdateVoices(new(Root, new[] { a, b }), b.LibraryId);
        check(vm.SelectedVoice == b && vm.Voices.Count == 2 && vm.PlayCommand.CanExecute(null), "Duplicate names select immutable ID.");
        vm.Text = " \n "; await vm.PlayAsync();
        check(vm.State == VoiceTestState.Failed && backend.Runs.Count == 0, "Empty input fails before SAPI.");
        vm.Text = new string('x', VoiceTestRequest.MaximumTextLength + 1);
        check(!vm.PlayCommand.CanExecute(null) && vm.Text.Length == 12001, "Oversized text rejected without truncation.");
        vm.Text = "a\0b"; check(!vm.PlayCommand.CanExecute(null), "NUL text rejected.");
        const string text = "  Wait, traveler; <not XML> & return!\nSecond line?  "; vm.Text = text;
        Task first = vm.PlayAsync(); var run = backend.Runs[0];
        check(vm.IsBusy && vm.State == VoiceTestState.Preparing && !vm.PlayCommand.CanExecute(null) && vm.StopCommand.CanExecute(null), "Preparing command state.");
        check(run.Request.Text == text && run.Request.Voice.LibraryId == Other && run.Request.TokenId.EndsWith("OutLoud_Pocket_" + Other), "Unmodified punctuation and exact token routing.");
        await vm.PlayAsync(); check(backend.Runs.Count == 1, "Rapid repeated Play rejected.");
        run.Progress.Report(VoiceTestState.Speaking); check(vm.State == VoiceTestState.Speaking, "Truthful speaking state.");
        Task stopped = vm.StopAsync();
        check(run.Stop.IsCancellationRequested && vm.State == VoiceTestState.Stopping && vm.IsBusy && !stopped.IsCompleted, "Stop waits for cleanup.");
        run.Progress.Report(VoiceTestState.Speaking); check(vm.State == VoiceTestState.Stopping, "Late progress cannot undo Stop.");
        run.Done.SetResult(new(false)); await first; await stopped;
        check(vm.State == VoiceTestState.Stopped && !vm.IsBusy && vm.PlayCommand.CanExecute(null), "Late success after Stop stays stopped.");
        Task second = vm.PlayAsync(); var next = backend.Runs[1]; run.Progress.Report(VoiceTestState.Exporting);
        check(vm.State == VoiceTestState.Preparing, "Old revision callback cannot change new operation.");
        next.Done.SetResult(new(false)); await second;
        check(vm.State == VoiceTestState.Completed && vm.Severity == NotificationSeverity.Success, "Repeated generation completes.");
        Task changing = vm.PlayAsync(); vm.SelectedVoice = a;
        check(backend.Runs[2].Stop.IsCancellationRequested && vm.State == VoiceTestState.Stopping, "Voice switch cancels captured request.");
        backend.Runs[2].Done.SetResult(new(true)); await changing;
        check(vm.SelectedVoice == a && vm.Text == text, "Voice switch preserves edited text.");
        Task removed = vm.PlayAsync(); vm.UpdateVoices(new(Root, new[] { b }));
        check(vm.SelectedVoice == null && backend.Runs[3].Stop.IsCancellationRequested && !vm.PlayCommand.CanExecute(null), "Unavailable selection cancels without fallback.");
        backend.Runs[3].Done.SetResult(new(true)); await removed;
        vm.UpdateVoices(new(Root, new[] { a with { SapiStatus = "missing" } }), Id);
        check(!vm.PlayCommand.CanExecute(null), "Unsuitable SAPI row is visible but cannot play.");
        vm.UpdateVoices(new(Root, new[] { a }), Id);
        Task failed = vm.PlayAsync(); backend.Runs[4].Done.SetException(new VoiceTestException(new("missing_token", "Refresh this voice.", unchecked((int)0x8004503A), "token_selection"))); await failed;
        check(vm.State == VoiceTestState.Failed && vm.Diagnostic.Contains("8004503A") && vm.HasDiagnostic, "Structured SAPI failure remains observable.");
        vm.DismissCommand.Execute(null); check(!vm.HasNotification && !vm.HasDiagnostic, "Tester severity banner is dismissible.");
        int count = backend.Runs.Count; await vm.ExportAsync();
        check(backend.Runs.Count == count && vm.State == VoiceTestState.Idle, "Save-dialog cancellation does not synthesize.");
        save.Path = @"C:\fake-export.wav"; Task export = vm.ExportAsync();
        check(backend.Runs[^1].Request.ExportPath == save.Path, "WAV destination passes through same backend.");
        backend.Runs[^1].Progress.Report(VoiceTestState.Exporting); check(vm.State == VoiceTestState.Exporting, "Export status.");
        backend.Runs[^1].Done.SetResult(new(false, save.Path)); await export;
        check(vm.NotificationText.Contains(save.Path) && vm.State == VoiceTestState.Completed, "WAV completion reports published path.");
        Task closingRun = vm.PlayAsync(); Task closed = vm.CloseAsync();
        check(!closed.IsCompleted && backend.Runs[^1].Stop.IsCancellationRequested, "Shutdown awaits active release.");
        backend.Runs[^1].Done.SetResult(new(true)); await closingRun; await closed; await vm.PlayAsync();
        check(backend.Runs.Count == count + 2 && !vm.PlayCommand.CanExecute(null), "Closed tester cannot restart.");

        var navigationBackend = new Backend(); var tester = new VoiceTesterViewModel(navigationBackend); var service = new Snapshot();
        var mutations = new Mutations();
        var main = new MainViewModel(service, mutations, mutations, tester); await main.RefreshAsync(); main.SelectedVoice = b;
        check(main.ImportCommand.CanExecute(null) && main.RemoveCommand.CanExecute(null), "Library actions enabled before tester work.");
        main.ShowTesterCommand.Execute(null); tester.Text = text;
        check(main.IsTesterPage && !main.IsLibraryPage && tester.SelectedVoice?.LibraryId == Other, "Library selection propagates through navigation.");
        Task navigationRun = tester.PlayAsync(); main.ShowLibraryCommand.Execute(null);
        check(main.IsTesterPage && tester.State == VoiceTestState.Stopping && !main.ImportCommand.CanExecute(null) && !main.RemoveCommand.CanExecute(null), "Leaving Tester waits before library mutations.");
        await main.ImportAsync(); await main.RemoveAsync(); check(mutations.DialogCalls == 0, "Direct mutation calls also reject active tester work.");
        navigationBackend.Runs[0].Done.SetResult(new(true)); await navigationRun;
        // Command is async-void; this property notification is the observable navigation completion.
        await Until(() => main.IsLibraryPage);
        check(main.ImportCommand.CanExecute(null) && main.RemoveCommand.CanExecute(null), "Library actions resume after tester release.");
        await main.RefreshAsync(); check(main.SelectedVoice?.LibraryId == Other, "Library refresh still preserves selection after testing.");
        main.ShowTesterCommand.Execute(null); check(tester.Text == text, "Navigation preserves test text.");
        Task shutdownRun = tester.PlayAsync(); Task shutdown = main.ShutdownAsync();
        check(!shutdown.IsCompleted && !main.ShowTesterCommand.CanExecute(null), "Application shutdown blocks navigation and waits.");
        navigationBackend.Runs[^1].Done.SetResult(new(true)); await shutdownRun; await shutdown;

        await BackendChecks(check, a, text);
        AutomationChecks(check, a, text);
    }
    private static async Task Until(Func<bool> ready)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        while (!ready()) await Task.Delay(1, timeout.Token);
    }
    private static async Task BackendChecks(Action<bool, string> check, VoiceRecord voice, string text)
    {
        var request = new VoiceTestRequest(voice, Root, text); var progress = new Progress(); var factory = new SessionFactory();
        var result = await new SapiVoiceTestBackend(factory).RunAsync(request, progress, default);
        check(!result.Cancelled && factory.Disposed && factory.Sta && factory.SameThread && factory.Text == text, "Completion joins STA and disposes session with exact text.");
        check(progress.States.SequenceEqual(new[] { VoiceTestState.Preparing, VoiceTestState.Speaking }), "Service emits only observed states.");
        var held = new SessionFactory { Hold = true }; var backend = new SapiVoiceTestBackend(held); using var stop = new CancellationTokenSource();
        Task<VoiceTestOutcome> running = backend.RunAsync(request, new Progress(), stop.Token);
        check(held.Started.Wait(5000), "Fake SAPI worker started.");
        var overlap = await Failure(backend.RunAsync(request, new Progress(), default));
        check(overlap?.Code == "tester_busy" && held.Creates == 1, "Service itself rejects overlap.");
        stop.Cancel(); result = await running;
        check(result.Cancelled && held.Purged && held.Disposed && held.SameThread, "Stop purges on owning STA then releases before returning.");
        var pre = new SessionFactory(); using var cancelled = new CancellationTokenSource(); cancelled.Cancel();
        check((await new SapiVoiceTestBackend(pre).RunAsync(request, new Progress(), cancelled.Token)).Cancelled && pre.Creates == 0, "Pre-cancelled request does not activate SAPI.");
        var failureFactory = new SessionFactory { Result = unchecked((int)0x80004005) };
        check((await Failure(new SapiVoiceTestBackend(failureFactory).RunAsync(request, new Progress(), default)))?.Code == "sapi_synthesis_failed" && failureFactory.Disposed, "Asynchronous HRESULT failure disposes session.");
        failureFactory = new SessionFactory { StartFailure = new COMException("fake activation", unchecked((int)0x80040154)) };
        check((await Failure(new SapiVoiceTestBackend(failureFactory).RunAsync(request, new Progress(), default)))?.HResult == unchecked((int)0x80040154) && failureFactory.Disposed, "Start failure retains HRESULT and releases session.");
        string directory = Path.Combine(Path.GetTempPath(), "outloud-tester-check-" + Guid.NewGuid().ToString("N")); Directory.CreateDirectory(directory);
        try {
            string destination = Path.Combine(directory, "output.wav"); var output = request with { ExportPath = destination };
            var wave = new SessionFactory(); result = await new SapiVoiceTestBackend(wave).RunAsync(output, new Progress(), default);
            WaveFile.Validate(destination);
            check(result.ExportPath == destination && wave.Disposed && Directory.GetFiles(directory).Length == 1, "Validated PCM WAV published after disposal; no partial remains.");
            byte[] previous = File.ReadAllBytes(destination); wave = new SessionFactory { BadWave = true };
            check(await Failure(new SapiVoiceTestBackend(wave).RunAsync(output, new Progress(), default)) != null && previous.SequenceEqual(File.ReadAllBytes(destination)) && Directory.GetFiles(directory).Length == 1, "Invalid WAV preserves existing destination and deletes temporary.");
            wave = new SessionFactory { Hold = true }; using var waveStop = new CancellationTokenSource();
            running = new SapiVoiceTestBackend(wave).RunAsync(output, new Progress(), waveStop.Token); check(wave.Started.Wait(5000), "Export fake started."); waveStop.Cancel(); result = await running;
            check(result.Cancelled && previous.SequenceEqual(File.ReadAllBytes(destination)) && Directory.GetFiles(directory).Length == 1, "Cancelled export discards partial and preserves destination.");
            output = request with { ExportPath = Path.Combine(directory, "missing", "output.wav") };
            check(await Failure(new SapiVoiceTestBackend(new SessionFactory()).RunAsync(output, new Progress(), default)) != null, "Export I/O error is structured.");
        } finally {
            // Only files directly created in this unique test-owned directory.
            foreach (string file in Directory.GetFiles(directory)) File.Delete(file);
            Directory.Delete(directory);
        }
    }
    private static void AutomationChecks(Action<bool, string> check, VoiceRecord voiceRecord, string text)
    {
        var request = new VoiceTestRequest(voiceRecord, Root, text);
        dynamic token = new ExpandoObject(), key = new ExpandoObject(), voice = new ExpandoObject(), status = new ExpandoObject(), stream = new ExpandoObject(), format = new ExpandoObject();
        string? selectedId = null; bool create = true; string? category = null; bool missing = false;
        key.GetStringValue = (Func<string, string>)(name => name switch {
            "LibraryId" => Id, "OutLoudManagedOwner" => "OutLoud.ManagedVoice.1", "CLSID" => "{F38C3398-3552-43B9-8350-7DD301327D70}",
            "VoicePath" => Path.Combine(Root, "voices", Id, "voice.olv"), _ => throw new Exception("Unexpected token query") });
        token.Id = request.TokenId; token.DataKey = key;
        token.SetId = (Action<string, string, bool>)((id, root, allowCreate) => { selectedId = id; category = root; create = allowCreate; if (missing) throw new COMException("missing", unchecked((int)0x8004503A)); });
        var calls = new List<(string Text, int Flags)>(); voice.Speak = (Action<string, int>)((value, flags) => calls.Add((value, flags)));
        voice.WaitUntilDone = (Func<int, bool>)(_ => true); status.LastHResult = 0; voice.Status = status;
        stream.Format = format; format.Type = 0; string? opened = null; int mode = 0; bool closed = false;
        stream.Open = (Action<string, int, bool>)((path, value, events) => { opened = path; mode = value; }); stream.Close = (Action)(() => closed = true);
        var activated = new List<string>(); var released = new List<object>();
        object Activate(string id) { activated.Add(id); return id switch { "SAPI.SpObjectToken" => token, "SAPI.SpVoice" => voice, "SAPI.SpFileStream" => stream, _ => throw new Exception("Unexpected activation") }; }
        var factory = new SapiTestSessionFactory(Activate, value => released.Add(value));
        using (var session = factory.Create(request, "fake-temporary.wav")) {
            session.Start(text); session.Purge(); check(session.WaitUntilDone(20) && session.LastResult() == 0, "Automation wait/status are consumed.");
        }
        check(selectedId == request.TokenId && category == "" && !create, "Actual automation branch selects full HKLM token with create=false.");
        check(calls.SequenceEqual(new[] { (text, 17), ("", 19) }), "Actual automation uses plain-text async and purge flags.");
        check((int)format.Type == 26 && mode == 3 && opened == "fake-temporary.wav" && closed, "Actual automation requests 24k mono PCM16 file stream and closes it.");
        check(ReferenceEquals(released[0], voice) && released.Count == 6 && released.Distinct(ReferenceEqualityComparer.Instance).Count() == 6, "SpVoice released first; all owned automation objects released once.");
        released.Clear(); activated.Clear(); missing = true;
        VoiceTestError? error = null; try { factory.Create(request, null); } catch (VoiceTestException failure) { error = failure.Error; }
        check(error?.Stage == "token_selection" && activated.SequenceEqual(new[] { "SAPI.SpObjectToken" }) && released.Count == 1, "Missing token never activates or substitutes default voice; failed initialization releases token.");
        missing = false; token.Id = request.TokenId + "wrong"; activated.Clear(); released.Clear();
        try { factory.Create(request, null); error = null; } catch (VoiceTestException failure) { error = failure.Error; }
        check(error?.Code == "token_identity_changed" && activated.Count == 1 && released.Count == 2, "Changed token identity fails before engine activation and releases key/token.");
        token.Id = request.TokenId; activated.Clear(); released.Clear();
        var failedActivation = new SapiTestSessionFactory(id => id == "SAPI.SpVoice" ? throw new COMException("engine unavailable", unchecked((int)0x80040154)) : Activate(id), value => released.Add(value));
        try { failedActivation.Create(request, null); error = null; } catch (VoiceTestException failure) { error = failure.Error; }
        check(error?.Stage == "engine_activation" && error.HResult == unchecked((int)0x80040154) && released.Count == 2, "Engine activation failure releases token and data key.");
        released.Clear(); calls.Clear(); int waits = 0;
        voice.WaitUntilDone = (Func<int, bool>)(_ => { ++waits; return true; });
        voice.Speak = (Action<string, int>)((value, flags) => { calls.Add((value, flags)); if (flags == 19) throw new COMException("purge failed"); });
        var failingStop = factory.Create(request, null); failingStop.Start(text);
        try { failingStop.Dispose(); error = null; } catch (VoiceTestException failure) { error = failure.Error; }
        check(error?.Code == "sapi_cleanup_failed" && waits == 1 && released.Count == 3, "Purge failure still drains and releases every object, reporting cleanup failure.");
        failingStop.Dispose(); check(released.Count == 3, "Disposal is idempotent after failure.");
        released.Clear(); voice.Speak = (Action<string, int>)((_, _) => { });
        var failingRelease = new SapiTestSessionFactory(Activate, value => { released.Add(value); if (ReferenceEquals(value, voice)) throw new COMException("release failed"); });
        var releaseSession = failingRelease.Create(request, null);
        try { releaseSession.Dispose(); error = null; } catch (VoiceTestException failure) { error = failure.Error; }
        check(error?.Code == "sapi_cleanup_failed" && released.Count == 3, "A release failure does not skip other owned objects.");
    }
}
