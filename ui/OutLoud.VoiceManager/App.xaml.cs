using System.Windows;
using OutLoud.VoiceManager.Services;
using OutLoud.VoiceManager.ViewModels;
using OutLoud.VoiceManager.Views;
using OutLoud.VoiceManager.Themes;

namespace OutLoud.VoiceManager;

public partial class App : Application
{
    private WindowsThemeService? themeService;
    protected override async void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        themeService = new WindowsThemeService(new ThemePalette(Resources), Dispatcher);
        var tester = new VoiceTesterViewModel(new SapiVoiceTestBackend(), new WaveExportDialog());
        var model = new MainViewModel(new NativeVoiceManagerService(), new ImportDialogService(themeService.AttachWindow), new ElevatedVoiceImportService(), tester);
        var window = new MainWindow { DataContext = model };
        MainWindow = window;
        themeService.AttachWindow(window);
        window.Show();
        await model.RefreshAsync();
    }
    protected override void OnExit(ExitEventArgs e)
    {
        themeService?.Dispose();
        base.OnExit(e);
    }
}
