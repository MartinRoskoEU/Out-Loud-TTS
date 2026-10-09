using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using OutLoud.VoiceManager.Models;
using OutLoud.VoiceManager.Services;
using OutLoud.VoiceManager.ViewModels;
using OutLoud.VoiceManager.NativeInterop;

namespace OutLoud.VoiceManager.Tests;

internal static class Program
{
    private static int checks;
    private static void Check(bool value, string message)
    { ++checks; if (!value) throw new Exception(message); }
    private static VoiceRecord Voice(string id, string name, string status = "missing") =>
        new(id, name, "Valid", "", status, "Native detail", "pocket-english-v1", "olv", 1, false);
    private sealed class FakeService : IVoiceManagerService
    {
        public int Reads;
        public readonly Queue<Task<VoiceSnapshot>> Replies = new();
        public Task<VoiceSnapshot> ReadAsync() { ++Reads; return Replies.Dequeue(); }
        public void Ready(params VoiceRecord[] voices) => Replies.Enqueue(Task.FromResult(new VoiceSnapshot("fake-root", voices)));
    }
    public static async Task<int> Main()
    {
        try
        {
            Check(Environment.Is64BitProcess, "Tests must run x64.");
            Check(Marshal.SizeOf<NativeVoiceManager.SnapshotV1>() == 24, "Snapshot ABI size.");
            Check(Marshal.SizeOf<NativeVoiceManager.VoiceV1>() == 88, "Voice ABI size.");
            Check(Marshal.SizeOf<NativeVoiceManager.ErrorV1>() == 64, "Error ABI size.");
            Check(Marshal.SizeOf<NativeVoiceManager.ContentV1>() == 32, "Additive read-only content ABI size.");
            Check(Marshal.OffsetOf<NativeVoiceManager.VoiceV1>(nameof(NativeVoiceManager.VoiceV1.MetadataPresent)).ToInt64() == 72,
                "Metadata ABI offset.");
            // Type layout checks do not call the native wrapper or load its DLL.
            var fake = new FakeService(); var model = new MainViewModel(fake);
            fake.Ready(); await model.RefreshAsync();
            Check(model.IsEmpty && !model.HasError && !model.IsBusy, "Empty state.");
            Check(model.Voices.Count == 0 && model.LibraryRoot == "fake-root", "Empty snapshot.");
            Check(model.RefreshCommand.CanExecute(null), "Refresh enabled after completion.");
            var first = Voice("first", "Hlas ž 🎵"); var second = Voice("second", "Second", "StaleName");
            fake.Ready(first, second); await model.RefreshAsync();
            Check(model.Voices.Count == 2 && !model.IsEmpty, "Multiple voices.");
            Check(model.Voices[0].DisplayName == "Hlas ž 🎵" && model.Voices[1].SapiStatus == "StaleName", "Native labels/Unicode preserved.");
            model.SelectedVoice = second;
            Check(model.HasSelection && model.SelectedVoice.LibraryId == "second", "Selected detail state.");
            Check(model.SelectedVoice.OlvVersionText == "1" && model.SelectedVoice.ProvenanceText == "No", "Metadata details.");
            var previousList = model.Voices;
            var replacement = Voice("second", "Renamed", "Correct");
            // Model the real DataGrid clearing SelectedItem as ItemsSource changes.
            model.PropertyChanged += (_, e) => { if (e.PropertyName == nameof(model.Voices)) model.SelectedVoice = null; };
            fake.Ready(replacement); await model.RefreshAsync();
            Check(!ReferenceEquals(previousList, model.Voices) && model.Voices.Count == 1, "Atomic collection replacement.");
            Check(ReferenceEquals(model.SelectedVoice, replacement) && model.SelectedVoice.DisplayName == "Renamed", "Selection preserved by ID.");
            var error = new BackendError("library", "LibraryAccessDenied", "Access denied.", "fake-root", SystemError: 5);
            fake.Replies.Enqueue(Task.FromException<VoiceSnapshot>(new BackendException(error)));
            await model.RefreshAsync();
            Check(ReferenceEquals(model.Error, error) && model.HasError && !model.IsBusy, "Structured backend failure surfaced.");
            Check(model.Error!.Diagnostic.Contains("Win32: 5") && model.Error.Code == "LibraryAccessDenied", "Structured diagnostics retained.");
            Check(model.Voices.Count == 1 && ReferenceEquals(model.SelectedVoice, replacement), "Failure preserves last good snapshot.");
            var completion = new TaskCompletionSource<VoiceSnapshot>(TaskCreationOptions.RunContinuationsAsynchronously);
            fake.Replies.Enqueue(completion.Task); int before = fake.Reads;
            Task active = model.RefreshAsync();
            Check(model.IsBusy && !model.HasError && !model.RefreshCommand.CanExecute(null), "Loading and disabled command.");
            await model.RefreshAsync(); model.RefreshCommand.Execute(null);
            Check(fake.Reads == before + 1 && !active.IsCompleted, "Duplicate refresh rejected.");
            completion.SetResult(new VoiceSnapshot("fake-root", new[] { first })); await active;
            Check(model.SelectedVoice == null && !model.HasSelection, "Removed selection cleared.");
            Check(!model.IsBusy && model.RefreshCommand.CanExecute(null), "Refresh recovered.");
            var rowFailure = new BackendError("sapi", "RegistryAccessDenied", "SAPI access denied.", SystemError: 5);
            var damaged = new VoiceRecord("damaged", null, "MetadataCorrupt", "Invalid metadata.", null, "", null, null, null, null, rowFailure);
            fake.Ready(damaged); await model.RefreshAsync(); model.SelectedVoice = damaged;
            Check(model.HasSapiErrors && !model.HasError, "Per-row SAPI error retains usable list.");
            Check(damaged.NameText == "(Metadata unavailable)" && damaged.OlvVersionText == "Unavailable" && damaged.ProvenanceText == "Unavailable",
                "Missing metadata is not invented.");
            Check(damaged.SapiDiagnostic.Contains("RegistryAccessDenied"), "Row diagnostic.");
            fake.Replies.Enqueue(Task.FromException<VoiceSnapshot>(new InvalidOperationException("private exception details")));
            await model.RefreshAsync();
            Check(model.Error?.Code == "unexpected_failure" && !model.Error.Message.Contains("private exception"), "Unexpected failure safe UI state.");
            fake.Ready(); await model.RefreshAsync();
            Check(model.IsEmpty && !model.HasError && !model.HasSapiErrors && model.SelectedVoice == null, "Empty refresh clears prior state.");
            var mutable = new List<VoiceRecord> { first }; var immutable = new VoiceSnapshot("fake", mutable); mutable.Clear();
            Check(immutable.Voices.Count == 1, "Snapshot copies mutable input.");
            await ImportTests.Run(Check);
            await VoiceTesterTests.Run(Check);
            Console.WriteLine($"PASS: {checks} frontend checks (fake services/elevation; no native read/registry/library mutation).");
            return 0;
        }
        catch (Exception failure) { Console.Error.WriteLine(failure); return 1; }
    }
}
