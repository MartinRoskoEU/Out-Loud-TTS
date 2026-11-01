using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using System.Runtime.InteropServices;
using Windows.Graphics;

namespace OutLoud.WinUI;

public sealed partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();
        InitializeShell();
        SettingsPageHost.InitializeRuntimeManagement(this);
        VoiceLibraryPageHost.BusyChanged += busy =>
        {
            VoiceLibraryNavigationItem.IsEnabled = VoiceTesterNavigationItem.IsEnabled = SettingsNavigationItem.IsEnabled = !busy;
        };
        VoiceLibraryPageHost.InitializeVoiceLibrary(this);
        VoiceTesterPageHost.OwnerWindow = this;
        SettingsPageHost.VoiceTestModelStateChanged += VoiceTesterPageHost.RefreshModelState;
        VoiceTesterPageHost.RefreshModelState();
        AppWindow.Closing += TesterWindow_Closing;
    }

    private void InitializeShell()
    {
        AppWindow.SetIcon(Path.Combine(AppContext.BaseDirectory, "Assets", "OutLoud.ico"));
        ExtendsContentIntoTitleBar = true;
        SetTitleBar(AppTitleBar);
        AppWindow.TitleBar.PreferredTheme = TitleBarTheme.UseDefaultAppMode;
        AppWindow.TitleBar.ButtonBackgroundColor = Colors.Transparent;
        AppWindow.TitleBar.ButtonInactiveBackgroundColor = Colors.Transparent;
        AppTitleBar.SizeChanged += (_, _) => UpdateTitleBarInsets();
        AppWindow.Changed += (_, _) => UpdateTitleBarInsets();
        SetInitialWindowBounds();
        UpdateTitleBarInsets();
        ShellNavigation.SelectionChanged += ShellNavigation_SelectionChanged;
        ShellNavigation.SelectedItem = VoiceLibraryNavigationItem;
    }

    private async void ShellNavigation_SelectionChanged(NavigationView sender, NavigationViewSelectionChangedEventArgs args)
    {
        if (args.SelectedItemContainer is not NavigationViewItem item) { return; }
        if (_testerNavigationPending || _testerShutdown != null) { return; }

        if (VoiceTesterPageHost.Visibility == Visibility.Visible && item != VoiceTesterNavigationItem)
        {
            _testerNavigationPending = true;
            VoiceLibraryNavigationItem.IsEnabled = VoiceTesterNavigationItem.IsEnabled = SettingsNavigationItem.IsEnabled = false;
            try { await VoiceTesterPageHost.StopAsync(); }
            finally
            {
                _testerNavigationPending = false;
                VoiceLibraryNavigationItem.IsEnabled = VoiceTesterNavigationItem.IsEnabled = SettingsNavigationItem.IsEnabled = _testerShutdown == null;
            }
            if (_testerShutdown != null) { return; }
        }

        if (item == VoiceTesterNavigationItem) { UpdateTesterVoices(); }

        VoiceLibraryPageHost.Visibility = item == VoiceLibraryNavigationItem ? Visibility.Visible : Visibility.Collapsed;
        VoiceTesterPageHost.Visibility = item == VoiceTesterNavigationItem ? Visibility.Visible : Visibility.Collapsed;
        SettingsPageHost.Visibility = item == SettingsNavigationItem ? Visibility.Visible : Visibility.Collapsed;

        if (sender.IsLoaded && sender.DisplayMode != NavigationViewDisplayMode.Expanded) { sender.IsPaneOpen = false; }
    }

    // Match Studio Lite's initial size, display centering, and title-bar scaling.
    private void SetInitialWindowBounds()
    {
        double scale = WindowScale();
        DisplayArea display = DisplayArea.GetFromWindowId(AppWindow.Id, DisplayAreaFallback.Nearest);
        RectInt32 work = display.WorkArea;
        int width = Math.Min((int)Math.Round(1180 * scale), work.Width);
        int height = Math.Min((int)Math.Round(780 * scale), work.Height);
        AppWindow.MoveAndResize(new RectInt32(work.X + (work.Width - width) / 2,
            work.Y + (work.Height - height) / 2, width, height), display);
    }

    private double WindowScale() => GetDpiForWindow(WinRT.Interop.WindowNative.GetWindowHandle(this)) / 96.0;

    private void UpdateTitleBarInsets() =>
        TitleBarRightInset.Width = new GridLength(AppWindow.TitleBar.RightInset / WindowScale());

    [DllImport("user32.dll", ExactSpelling = true)]
    private static extern uint GetDpiForWindow(IntPtr hwnd);
}
