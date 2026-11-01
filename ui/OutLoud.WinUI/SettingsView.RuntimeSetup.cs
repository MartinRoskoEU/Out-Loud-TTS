using Microsoft.UI.Xaml;
using OutLoud.WinUI.Services;

namespace OutLoud.WinUI;

public sealed partial class SettingsView
{
    private readonly CancellationTokenSource _setupCancellation = new();
    private bool _closing;
    private Window? _window;

    internal void InitializeRuntimeManagement(Window window)
    {
        _window = window;
        InitializeRuntimeUpdates();
        InitializeModelManagement(window);
        window.Closed += (_, _) =>
        {
            _closing = true;
            _setupCancellation.Cancel();
            _runtimeInstallCancellation?.Cancel();
            _runtimeUpdateHttp.Dispose();
            _runtimeDownloadHttp.Dispose();
        };
        _ = CheckRuntimeAsync(isStartup: true);
    }

    // Runtime refresh also invalidates models/selections through the shared setup service.
    private Task CheckRuntimeAsync(bool isStartup = false) => CheckModelsAsync(isStartup);
}
