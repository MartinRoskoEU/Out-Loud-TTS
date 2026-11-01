using System.Reflection;

namespace OutLoud.WinUI.Services;

internal static class ApplicationInfo
{
    internal static string Version => typeof(ApplicationInfo).Assembly
        .GetCustomAttribute<AssemblyInformationalVersionAttribute>()!.InformationalVersion;
    internal static string UserAgent => "OutLoud.WinUI/" + Version;
}
