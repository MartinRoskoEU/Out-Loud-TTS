using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using OutLoud.VoiceManager.Models;
using OutLoud.VoiceManager.NativeInterop;

namespace OutLoud.VoiceManager.Services;

public sealed class SapiTestSessionFactory : ISapiTestSessionFactory
{
    private readonly Func<string, object> activate;
    private readonly Action<object> release;
    public SapiTestSessionFactory(Func<string, object>? activate = null, Action<object>? release = null)
    {
        this.activate = activate ?? (id => Activator.CreateInstance(Type.GetTypeFromProgID(id, throwOnError: true)!)!);
        this.release = release ?? (value => { if (Marshal.IsComObject(value)) Marshal.FinalReleaseComObject(value); });
    }
    public ISapiTestSession Create(VoiceTestRequest request, string? temporaryWavePath)
    {
        var session = new Session(activate, release);
        try { session.Initialize(request, temporaryWavePath); return session; }
        catch { try { session.Dispose(); } catch { } throw; }
    }
    private sealed class Session : ISapiTestSession
    {
        // Windows SDK sapiaut.idl / sapi.h: ASYNC=1, PURGE=2, IS_NOT_XML=16.
        private const int PlainAsync = 1 | 16, PurgeAsync = 1 | 2 | 16;
        private readonly List<object> owned = new();
        private readonly Func<string, object> activate;
        private readonly Action<object> release;
        public Session(Func<string, object> activate, Action<object> release) { this.activate = activate; this.release = release; }
        private dynamic? voice;
        private dynamic? stream;
        private bool active, closed;
        private string stage = "activation";
        private dynamic Own(object value) { if (!owned.Exists(item => ReferenceEquals(item, value))) owned.Add(value); return value; }
        private dynamic Create(string progId) => Own(activate(progId));
        private void Call(Action action)
        {
            try { action(); }
            catch (VoiceTestException) { throw; }
            catch (Exception error) { throw new VoiceTestException(new("sapi_" + stage + "_failed", "Windows SAPI could not complete " + stage.Replace('_', ' ') + ". No other voice was substituted.", error.HResult, stage)); }
        }
        public void Initialize(VoiceTestRequest request, string? temporaryWavePath) => Call(() => {
            if (request.ValidationError is { } validation) throw new VoiceTestException(new("invalid_test", validation));
            stage = "token_selection";
            dynamic token = Create("SAPI.SpObjectToken");
            token.SetId(request.TokenId, "", false); // Full fixed HKLM identity; NEVER create.
            dynamic key = Own((object)token.DataKey);
            if (!string.Equals((string)token.Id, request.TokenId, StringComparison.OrdinalIgnoreCase) ||
                (string)key.GetStringValue("LibraryId") != request.Voice.LibraryId ||
                (string)key.GetStringValue("OutLoudManagedOwner") != "OutLoud.ManagedVoice.1" ||
                !string.Equals((string)key.GetStringValue("CLSID"), SapiIdentity.EngineClsid, StringComparison.OrdinalIgnoreCase) ||
                !Path.IsPathFullyQualified(request.LibraryRoot) ||
                !string.Equals(Path.GetFullPath((string)key.GetStringValue("VoicePath")), Path.GetFullPath(Path.Combine(request.LibraryRoot, "voices", request.Voice.LibraryId, "voice.olv")), StringComparison.OrdinalIgnoreCase))
                throw new VoiceTestException(new("token_identity_changed", "The installed managed SAPI token changed. Refresh the Voice Library before testing.", Stage: stage));
            stage = "engine_activation";
            voice = Create("SAPI.SpVoice"); voice.Voice = token;
            dynamic assigned = Own((object)voice.Voice);
            if (!string.Equals((string)assigned.Id, request.TokenId, StringComparison.OrdinalIgnoreCase)) throw new VoiceTestException(new("wrong_sapi_voice", "SAPI did not select the requested voice.", Stage: stage));
            voice.Rate = 0; voice.Volume = 100;
            if (temporaryWavePath != null) {
                stage = "export_output";
                stream = Create("SAPI.SpFileStream"); dynamic format = Own((object)stream.Format);
                format.Type = 26; // SAFT24kHz16BitMono, verified SDK enum.
                stream.Open(temporaryWavePath, 3, false); // SSFMCreateForWrite; private temporary.
                voice.AllowAudioOutputFormatChangesOnNextSet = false; voice.AudioOutputStream = stream;
            }
        });
        public void Start(string text) { stage = "speak"; Call(() => { active = true; voice!.Speak(text, PlainAsync); }); }
        public void Purge() { stage = "stop"; Call(() => { voice!.Speak("", PurgeAsync); }); }
        public bool WaitUntilDone(int milliseconds)
        {
            stage = "wait"; bool done = false;
            Call(() => { done = (bool)voice!.WaitUntilDone(milliseconds); if (done) active = false; }); return done;
        }
        public int LastResult()
        {
            stage = "status"; int result = 0;
            Call(() => { dynamic status = Own((object)voice!.Status); result = (int)status.LastHResult; }); return result;
        }
        public void Dispose()
        {
            if (closed) return; closed = true; Exception? failure = null;
            if (active && voice != null) {
                try { Purge(); } catch (Exception error) { failure = error; }
                // A failed purge does not prove completion; still attempt to
                // drain the request before releasing the voice.
                try { while (!WaitUntilDone(20)) { } } catch (Exception error) { failure ??= error; }
            }
            // Session-owned RCWs only; none are shared with WPF or another request.
            // Release SpVoice first to release its engine/Resident before tokens.
            if (voice != null) {
                object value = voice; owned.RemoveAll(item => ReferenceEquals(item, value));
                try { release(value); } catch (Exception error) { failure ??= error; }
                voice = null;
            }
            try { if (stream != null) stream.Close(); } catch (Exception error) { failure ??= error; }
            for (int index = owned.Count - 1; index >= 0; --index) {
                try { release(owned[index]); } catch (Exception error) { failure ??= error; }
            }
            owned.Clear(); stream = null;
            if (failure != null) throw new VoiceTestException(new("sapi_cleanup_failed", "SAPI cleanup reported a failure. Wait for shutdown before changing this voice.", failure.HResult, "cleanup"));
        }
    }
}
