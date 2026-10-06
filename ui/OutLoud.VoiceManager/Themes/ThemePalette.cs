using System;
using System.Windows;

namespace OutLoud.VoiceManager.Themes;

public enum AppTheme { Light, Dark, HighContrast }

// Owns only the palette slot; shared styles and live views remain in place.
public sealed class ThemePalette
{
    private readonly ResourceDictionary resources;
    public AppTheme Current { get; private set; } = AppTheme.Light;
    public ThemePalette(ResourceDictionary resources) => this.resources = resources;

    public void Apply(AppTheme theme)
    {
        var palette = new ResourceDictionary
        {
            Source = new Uri($"/OutLoud.VoiceManager;component/Themes/{theme}.xaml", UriKind.Relative)
        };
        // App.xaml defines the palette first and the common styles second.
        resources.MergedDictionaries[0] = palette;
        Current = theme;
    }

    public static AppTheme FromWindowsSetting(object? appsUseLightTheme, bool highContrast)
        => highContrast ? AppTheme.HighContrast : appsUseLightTheme is int value && value == 0 ? AppTheme.Dark : AppTheme.Light;
}
