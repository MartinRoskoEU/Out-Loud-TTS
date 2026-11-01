namespace OutLoud.WinUI.VoiceTesting;

internal enum VoiceTestModelState { Unknown, NotInstalled, NotSelected, Unavailable, Selected }
internal sealed record VoiceTestNotice(string Title, string Message, NotificationSeverity Severity);

/// <summary>Display guidance only. SAPI errors and playback/cleanup state remain unchanged.</summary>
internal static class VoiceTestPresentation
{
    internal static string ReadinessText(VoiceTestModelState modelState) => modelState switch
    {
        VoiceTestModelState.NotInstalled => "No model installed. Install and select one in Settings to enable Play.",
        VoiceTestModelState.NotSelected => "No model selected. Choose one in Settings to enable Play.",
        VoiceTestModelState.Unavailable => "The model isn't ready to use. Check its status in Settings.",
        VoiceTestModelState.Selected => "",
        _ => "Check model setup in Settings before playing.",
    };

    internal static VoiceTestNotice Describe(VoiceTestError error, VoiceTestModelState modelState)
    {
        // Keep cleanup and file failures visible even when model setup is incomplete.
        if (error.Code == "export_cleanup_failed")
            return Error("Incomplete WAV file retained", "An incomplete WAV file couldn't be removed. Close any app using it and check the temporary file shown in Diagnostic details before trying again.");
        if (error.Code is "sapi_cleanup_failed" or "sapi_stop_failed")
            return Error("Playback did not stop normally", "Windows couldn't finish stopping or cleaning up the voice test normally. Let playback stop before testing again or changing voices.");
        if (error.Code == "invalid_test")
            return Warning("Voice test isn't ready", error.Message);
        if (error.Code == "tester_busy")
            return Warning("Voice test still running", "A voice test is still running or stopping. Wait for it to finish, then try again.");
        if (error.Code == "export_unavailable")
            return Warning("WAV export unavailable", "WAV export isn't available in this session. Close and reopen Out Loud, then try again.");

        bool sapi = error.Code.StartsWith("sapi_", StringComparison.Ordinal);
        // HRESULTs from the Windows SDK's sperror.h; never infer these from generic E_FAIL.
        if (sapi && error.HResult == unchecked((int)0x80045006))
            return Warning("Audio device in use", "Another application is using the audio device. Close or pause it, then try again.");
        if (sapi && error.HResult is unchecked((int)0x80045007) or unchecked((int)0x80045008) or unchecked((int)0x80045009))
            return Error("Audio output unavailable", "Windows couldn't use the audio output device. Check that an output device is connected, enabled, and selected in Windows Sound settings, then try again.");
        if (sapi && error.HResult == unchecked((int)0x80040154))
            return Error("Speech component unavailable", "Windows couldn't load a required speech component. Check the Out Loud installation and Windows speech components before trying again.");
        if (error.Code is "token_identity_changed" or "wrong_sapi_voice" or "sapi_token_selection_failed")
            return Warning("Refresh your voices", "The selected voice is unavailable or its Windows registration has changed. Open Voice Library, select Refresh, then select the voice again.");
        if (error.Code == "sapi_engine_activation_failed")
            return Error("Voice could not be loaded", "Windows couldn't load the selected voice. Refresh Voice Library and check the voice's SAPI status before trying again.");
        if (error.Code == "sapi_export_output_failed" || (error.Code == "test_io_or_initialization_failed" && error.Path.Length > 0))
            return error.HResult is unchecked((int)0x80070020) or unchecked((int)0x80070021)
                ? Warning("WAV file is in use", "The WAV file is open in another application. Close it or choose another file name, then try exporting again.")
                : Error("WAV file could not be saved", "Choose a writable folder and file name, then try exporting again. If the problem continues, see Diagnostic details.");

        if (error.Code is "sapi_synthesis_failed" or "sapi_speak_failed")
        {
            // This is the state already checked by Settings, not an interpretation of the HRESULT.
            if (modelState == VoiceTestModelState.NotInstalled)
                return Warning("No model installed", "To test a voice, open Settings, install a model, and select it under Selected model. Then return here and try again.");
            if (modelState == VoiceTestModelState.NotSelected)
                return Warning("No model selected", "Open Settings and choose an installed model under Selected model. Then return here and try again.");
            if (modelState == VoiceTestModelState.Unavailable)
                return Warning("Model needs attention", "Open Settings and resolve the installed model's reported status, then select an available model and try again.");
            return Error("Voice test could not finish", "Speech couldn't be generated or played. In Settings, check that a model is installed and selected. If it is, check the voice in Voice Library and your Windows audio output, then try again.");
        }
        if (error.Code is "sapi_wait_failed" or "sapi_status_failed")
            return Error("Playback status unavailable", "Windows couldn't confirm whether the voice test finished. Let playback stop, then try again.");
        if (error.Code == "test_io_or_initialization_failed")
            return Error("Voice test could not start", "Out Loud couldn't prepare the voice test. Check the selected model in Settings and refresh Voice Library, then try again.");
        return Error("Voice test could not finish", "The voice test couldn't finish. Check the selected model in Settings and the voice in Voice Library, then try again. Diagnostic details are available below.");
    }

    private static VoiceTestNotice Warning(string title, string message) => new(title, message, NotificationSeverity.Warning);
    private static VoiceTestNotice Error(string title, string message) => new(title, message, NotificationSeverity.Error);
}
