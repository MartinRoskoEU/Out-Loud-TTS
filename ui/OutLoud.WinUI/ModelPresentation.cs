using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;
using OutLoud.WinUI.Services;

namespace OutLoud.WinUI;

/// <summary>Hide update labels when no registered-installation comparison is available.</summary>
public sealed class ModelUpdateVisibilityConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, string language) =>
        string.IsNullOrEmpty(value as string) ? Visibility.Collapsed : Visibility.Visible;
    public object ConvertBack(object value, Type targetType, object parameter, string language) => throw new NotSupportedException();
}

public sealed class ModelInstallationGlyphConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, string language) => value switch
    {
        PocketTtsModelInstallationState.InstalledVerified => "\uE73E", // Check mark.
        PocketTtsModelInstallationState.NotInstalled => "",
        PocketTtsModelInstallationState.InstalledUnverified => "\uE7BA", // Warning.
        PocketTtsModelInstallationState.Incompatible => "\uE7BA", // Warning.
        _ => ""
    };

    public object ConvertBack(object value, Type targetType, object parameter, string language) =>
        throw new NotSupportedException();
}
/// <summary>Choose theme-aware row styles without changing the model state.</summary>
public sealed class ModelInstallationStyleConverter : IValueConverter
{
    public Style Installed { get; set; } = null!;
    public Style NotInstalled { get; set; } = null!;
    public Style Unverified { get; set; } = null!;
    public Style Incompatible { get; set; } = null!;

    public object Convert(object value, Type targetType, object parameter, string language) => value switch
    {
        PocketTtsModelInstallationState.InstalledVerified => Installed,
        PocketTtsModelInstallationState.NotInstalled => NotInstalled,
        PocketTtsModelInstallationState.Incompatible => Incompatible,
        _ => Unverified,
    };

    public object ConvertBack(object value, Type targetType, object parameter, string language) => throw new NotSupportedException();
}

/// <summary>Registered models remain removable regardless of verification or runtime compatibility.</summary>
public sealed class ModelManagementActionConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, string language)
    {
        string? action = value switch
        {
            PocketTtsModelSetupEntry { CanRemove: true } => "Remove",
            PocketTtsModelSetupEntry { State: PocketTtsModelInstallationState.NotInstalled } => "Install",
            PocketTtsModelSetupEntry { CanInstall: true } => "Install",
            _ => null,
        };
        return parameter as string == "Visibility" ? (action is null ? Visibility.Collapsed : Visibility.Visible) : action ?? "";
    }

    public object ConvertBack(object value, Type targetType, object parameter, string language) => throw new NotSupportedException();
}

public sealed class ModelInstallationStatusConverter : IValueConverter
{
    public static string StatusText(PocketTtsModelInstallationState state) => state switch
    {
        PocketTtsModelInstallationState.NotInstalled => "Not installed",
        PocketTtsModelInstallationState.InstalledVerified => "Installed",
        PocketTtsModelInstallationState.Incompatible => "Incompatible",
        _ => "Unverified"
    };

    public object Convert(object value, Type targetType, object parameter, string language) =>
        value is PocketTtsModelInstallationState state ? StatusText(state) : "";

    public object ConvertBack(object value, Type targetType, object parameter, string language) =>
        throw new NotSupportedException();
}
