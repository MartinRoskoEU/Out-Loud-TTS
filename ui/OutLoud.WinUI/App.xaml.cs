using Microsoft.UI.Xaml;

namespace OutLoud.WinUI;

public partial class App : Application
{
    private Window? _window;

    public App()
    {
        InitializeComponent();
    }

    protected override async void OnLaunched(LaunchActivatedEventArgs args)
    {
        if (Services.ElevatedMachineDataService.IsWorker)
        {
            try { await Services.ElevatedMachineDataService.RunWorkerAsync(); }
            catch (Exception) { Environment.ExitCode = 1; }
            Exit();
            return;
        }
        _window = new MainWindow();
        _window.Activate();
    }
}
