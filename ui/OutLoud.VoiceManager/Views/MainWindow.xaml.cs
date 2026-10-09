using System.Windows;
using System.ComponentModel;
using System;
using OutLoud.VoiceManager.ViewModels;

namespace OutLoud.VoiceManager.Views;

public partial class MainWindow : Window
{
    public MainWindow() => InitializeComponent();
    private bool shutdownStarted, mayClose;
    protected override async void OnClosing(CancelEventArgs e)
    {
        base.OnClosing(e);
        if (mayClose || DataContext is not MainViewModel model) return;
        if (shutdownStarted) { e.Cancel = true; return; }
        shutdownStarted = true;
        var cleanup = model.ShutdownAsync(); // ViewModel/service own cancellation.
        if (cleanup.IsCompletedSuccessfully) { mayClose = true; return; }
        e.Cancel = true;
        await cleanup;
        mayClose = true;
        // Defer the second Close past the original Closing event, even if the
        // cleanup task happened to complete immediately after the check above.
        await Dispatcher.InvokeAsync(new Action(Close));
    }
}
