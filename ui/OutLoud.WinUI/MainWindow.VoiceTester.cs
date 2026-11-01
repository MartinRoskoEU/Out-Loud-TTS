using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml.Controls;
using OutLoud.WinUI.Voices;
using OutLoud.WinUI.VoiceTesting;

namespace OutLoud.WinUI;

public sealed partial class MainWindow
{
    private bool _testerNavigationPending;
    private Task? _testerShutdown;

    internal VoiceTestModelState GetVoiceTestModelState() => SettingsPageHost.GetVoiceTestModelState();

    private void UpdateTesterVoices()
    {
        // Consume the library's existing displayed snapshot and selection at the
        // shell boundary, without changing the library or enumerating a second list.
        var list = (ListView)VoiceLibraryPageHost.FindName("VoiceList");
        var root = (TextBlock)VoiceLibraryPageHost.FindName("LibraryRootText");
        VoiceTesterPageHost.UpdateVoices(new VoiceSnapshot(root.Text,
            list.ItemsSource as IEnumerable<VoiceRecord> ?? []), (list.SelectedItem as VoiceRecord)?.LibraryId);
    }

    private async void TesterWindow_Closing(AppWindow sender, AppWindowClosingEventArgs args)
    {
        // Existing Settings/library close handlers may defer or cancel closing.
        // Stop playback in that case, but leave the tester usable if the window stays open.
        if (args.Cancel)
        {
            await VoiceTesterPageHost.StopAsync();
            return;
        }
        if (_testerShutdown != null)
        {
            if (!_testerShutdown.IsCompleted) { args.Cancel = true; }
            return;
        }
        _testerShutdown = VoiceTesterPageHost.CloseAsync();
        if (_testerShutdown.IsCompleted) { return; }
        args.Cancel = true;
        VoiceLibraryNavigationItem.IsEnabled = VoiceTesterNavigationItem.IsEnabled = SettingsNavigationItem.IsEnabled = false;
        await _testerShutdown;
        // Defer Close past the current Closing event even for a completion race.
        DispatcherQueue.TryEnqueue(() => Close());
    }
}
