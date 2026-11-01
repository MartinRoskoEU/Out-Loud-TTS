using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;
using OutLoud.WinUI.Voices;

namespace OutLoud.WinUI;

internal static class VoiceLibraryPresentation
{
    // A missing folder or a fresh setup-created root has no voices yet.
    // Normalize only that display state; native operations retain all their own checks.
    internal static bool IsUninitializedEmpty(BackendError error, string libraryRoot)
    {
        if (error.Domain != "library" || error.Code != "library_unavailable" || error.SystemError is not (2 or 3))
            return false;
        try
        {
            string errorPath = Path.GetFullPath(error.Path);
            if (!string.Equals(errorPath, libraryRoot, StringComparison.OrdinalIgnoreCase) &&
                !string.Equals(errorPath, Path.Combine(libraryRoot, ".library.lock"), StringComparison.OrdinalIgnoreCase))
                return false;
            var root = new DirectoryInfo(libraryRoot);
            for (DirectoryInfo? directory = root; directory is not null; directory = directory.Parent)
                if (AttributesOrMissing(directory.FullName) is { } attributes && (attributes & FileAttributes.ReparsePoint) != 0) return false;

            FileAttributes? rootAttributes = AttributesOrMissing(libraryRoot);
            if (rootAttributes is null) return true;
            if ((rootAttributes.Value & (FileAttributes.Directory | FileAttributes.ReparsePoint)) != FileAttributes.Directory) return false;

            foreach (FileSystemInfo entry in root.EnumerateFileSystemInfos())
            {
                if ((entry.Attributes & FileAttributes.ReparsePoint) != 0) return false;
                bool isDirectory = (entry.Attributes & FileAttributes.Directory) != 0;
                if (!isDirectory && entry.Name.Equals(".outloud-installation.dat", StringComparison.OrdinalIgnoreCase)) continue;
                if (!isDirectory || !(entry.Name.Equals("voices", StringComparison.OrdinalIgnoreCase) ||
                    entry.Name.Equals(".staging", StringComparison.OrdinalIgnoreCase) || entry.Name.Equals(".trash", StringComparison.OrdinalIgnoreCase)))
                    return false;
                if (((DirectoryInfo)entry).EnumerateFileSystemInfos().Any()) return false;
            }
            return true;
        }
        catch (Exception failure) when (failure is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException or System.Security.SecurityException)
        { return false; }
    }

    private static FileAttributes? AttributesOrMissing(string path)
    {
        try { return File.GetAttributes(path); }
        catch (FileNotFoundException) { return null; }
        catch (DirectoryNotFoundException) { return null; }
    }

    internal static string RefreshFailureText(BackendError error) => (error.Domain, error.Code, error.SystemError) switch
    {
        ("library", "library_busy", _) => "A voice operation is using the library. Wait for it to finish, then select Refresh.",
        ("library", "library_access_denied", _) => "Out Loud doesn't have permission to read the voice library. Check the folder's access permissions and try again.",
        ("library", "library_security_invalid" or "unsafe_path" or "invalid_library_root", _) => "Out Loud can't safely access the voice library folder. Your voice profiles have not been changed.",
        ("library", "library_unavailable", 2 or 3) => "A required voice library file or folder is missing. Your voice profiles have not been changed.",
        ("library", "library_unavailable", _) => "The voice library is currently unavailable. Select Refresh to try again.",
        ("sapi", "engine_registration_missing", _) => "The Out Loud speech engine isn't registered with Windows. Its registration needs to be restored.",
        ("bridge", "bridge_not_found", _) => "Out Loud couldn't load the component used to read voices. Repair the Out Loud installation and try again.",
        ("bridge", "abi_mismatch" or "architecture_mismatch" or "entry_point_missing", _) => "The installed voice components don't match this version of Out Loud. Repair the installation and try again.",
        ("bridge", "out_of_memory", _) => "There isn't enough available memory to refresh voices. Close other applications and try again.",
        _ => "Out Loud couldn't refresh the voice library. Select Refresh to try again.",
    };
}

/// <summary>Apply theme-aware status colors without changing voice state or display text.</summary>
public sealed class VoiceLibraryStatusStyleConverter : IValueConverter
{
    public Style Success { get; set; } = null!;
    public Style Warning { get; set; } = null!;
    public Style Error { get; set; } = null!;
    public Style Neutral { get; set; } = null!;

    public object Convert(object value, Type targetType, object parameter, string language) =>
        (parameter as string, value as string) switch
        {
            ("Library", "valid") => Success,
            ("Library", "missing_voice" or "unsupported_metadata_version" or "unsupported_voice_version" or
                "incompatible_model" or "unavailable" or "replacement_pending" or "removal_pending") => Warning,
            ("Library", "corrupt_voice" or "metadata_corrupt" or "hash_mismatch" or "unsafe_entry") => Error,
            ("Sapi", "correct") => Success,
            ("Sapi", "missing" or "stale_name" or "orphan" or "Unavailable") => Warning,
            ("Sapi", "wrong_path" or "unhealthy_voice" or "malformed" or "conflict") => Error,
            _ => Neutral,
        };

    public object ConvertBack(object value, Type targetType, object parameter, string language) =>
        throw new NotSupportedException();
}
