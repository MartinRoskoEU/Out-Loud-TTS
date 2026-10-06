using System;
using System.Collections.Generic;
using System.IO;
using System.Security;
using System.Threading;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Threading;
using Microsoft.Win32;

namespace OutLoud.VoiceManager.Themes;

public sealed class WindowsThemeService : IDisposable
{
    private readonly ThemePalette palette;
    private readonly Dispatcher dispatcher;
    private readonly Func<AppTheme> readTheme;
    private volatile bool disposed;
    private int updatePending;
    private readonly HashSet<Window> windows = new();

    // Injected reader keeps view validation independent of real Windows settings.
    public WindowsThemeService(ThemePalette palette, Dispatcher dispatcher, Func<AppTheme>? readTheme = null)
    {
        this.palette = palette; this.dispatcher = dispatcher;
        this.readTheme = readTheme ?? ReadWindowsTheme;
        Refresh();
        SystemEvents.UserPreferenceChanged += OnPreferenceChanged;
    }

    private static AppTheme ReadWindowsTheme()
    {
        object? value = null;
        try
        {
            using var key = Registry.CurrentUser.OpenSubKey(
                @"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize", writable: false);
            value = key?.GetValue("AppsUseLightTheme");
        }
        catch (Exception error) when (error is SecurityException or UnauthorizedAccessException or IOException)
        { /* A missing/unreadable app setting falls back to light. No writes. */ }
        return ThemePalette.FromWindowsSetting(value, SystemParameters.HighContrast);
    }

    public void Refresh()
    {
        dispatcher.VerifyAccess();
        if (disposed) return;
        AppTheme theme = readTheme();
        if (palette.Current != theme) palette.Apply(theme);
        ApplyTitleBar();
    }

    public void AttachWindow(Window target)
    {
        dispatcher.VerifyAccess();
        if (disposed || !windows.Add(target)) return;
        target.SourceInitialized += OnSourceInitialized;
        target.Closed += OnWindowClosed;
        ApplyTitleBar();
    }
    private void OnWindowClosed(object? sender, EventArgs args)
    {
        if (sender is Window target && windows.Remove(target)) {
            target.SourceInitialized -= OnSourceInitialized; target.Closed -= OnWindowClosed;
        }
    }
    private void OnSourceInitialized(object? sender, EventArgs args) => ApplyTitleBar();
    private void ApplyTitleBar()
    {
        // Keep native caption/buttons. The documented dark-caption attribute is
        // supported on Windows 11; older Windows retains its system caption.
        if (!OperatingSystem.IsWindowsVersionAtLeast(10, 0, 22000)) return;
        foreach (Window window in windows) {
            IntPtr handle = new WindowInteropHelper(window).Handle;
            if (handle == IntPtr.Zero) continue;
            int dark = palette.Current == AppTheme.Dark ? 1 : 0;
            _ = DwmSetWindowAttribute(handle, 20, ref dark, sizeof(int));
        }
    }
    [DllImport("dwmapi.dll", ExactSpelling = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    private static extern int DwmSetWindowAttribute(IntPtr handle, int attribute, ref int value, int size);

    private void OnPreferenceChanged(object sender, UserPreferenceChangedEventArgs args)
    {
        if (disposed || dispatcher.HasShutdownStarted || Interlocked.Exchange(ref updatePending, 1) != 0) return;
        try
        {
            dispatcher.BeginInvoke(DispatcherPriority.Background, new Action(() =>
            {
                Interlocked.Exchange(ref updatePending, 0);
                Refresh();
            }));
        }
        catch (InvalidOperationException) { Interlocked.Exchange(ref updatePending, 0); }
    }

    public void Dispose()
    {
        disposed = true;
        SystemEvents.UserPreferenceChanged -= OnPreferenceChanged;
        foreach (Window window in windows) { window.SourceInitialized -= OnSourceInitialized; window.Closed -= OnWindowClosed; }
        windows.Clear();
    }
}
