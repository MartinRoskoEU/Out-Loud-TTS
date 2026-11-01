using Microsoft.Win32.SafeHandles;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;

namespace OutLoud.WinUI.Services;

/// <summary>Installation binding only. Existing, unmarked stores are never adopted.</summary>
internal static class InstallationDataOwnership
{
    internal static void PrepareNewStore(string root)
    {
        if (!string.Equals(Path.GetFullPath(root), MachineDataStorage.Root, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("The shared data root is invalid.");
        string receipt = Path.Combine(AppContext.BaseDirectory, "configuration", "signing.ini");
        // Development bundles without production ownership remain unbound; an installer never adopts them.
        if (!File.Exists(receipt))
        {
            if (File.Exists(Path.Combine(root, ".outloud-installation.dat")))
                throw new InvalidDataException("A development bundle cannot modify another installation's bound shared data.");
            MachineDataStorage.CreateProtected(root);
            return;
        }
        var fields = File.ReadAllLines(receipt).Where(line => line.Contains('=')).Select(line => line.Split('=', 2))
            .ToDictionary(pair => pair[0], pair => pair[1], StringComparer.Ordinal);
        if (fields.GetValueOrDefault("product") != "OutLoud" || fields.GetValueOrDefault("phase") != "verified" ||
            fields.GetValueOrDefault("scope") != "LocalMachine" ||
            !Regex.IsMatch(fields.GetValueOrDefault("installation_id") ?? "", @"\A[0-9a-f]{32}\z"))
            throw new InvalidDataException("The installation data ownership receipt is invalid.");
        bool existed = Directory.Exists(root);
        MachineDataStorage.CreateProtected(root);
        using SafeFileHandle directory = CreateFile(root, 0x80, 1 | 2, IntPtr.Zero, 3, 0x02200000, IntPtr.Zero);
        if (directory.IsInvalid || !GetFileInformationByHandleEx(directory, 18, out FileId identity, 24))
            throw new IOException("Shared data ownership could not be verified.");
        string appRoot = Path.TrimEndingDirectorySeparator(Path.GetFullPath(AppContext.BaseDirectory));
        string physicalId = Convert.ToHexString(identity.Bytes).ToLowerInvariant();
        string marker = "OutLoud.WinUI.Data.2\n" + fields["installation_id"] + "\n" +
            Convert.ToHexString(Encoding.Unicode.GetBytes(appRoot)).ToLowerInvariant() + "\n" + physicalId + "\n";
        string markerPath = Path.Combine(root, ".outloud-installation.dat");
        if (existed)
        {
            if (!File.Exists(markerPath) || new FileInfo(markerPath).Length > 8192)
                throw new InvalidDataException("Existing shared data is not bound to this Out Loud installation. It was preserved.");
            string[] binding = File.ReadAllLines(markerPath);
            if (binding.Length != 4 || binding[0] != "OutLoud.WinUI.Data.2" || binding[1] != fields["installation_id"] ||
                !string.Equals(binding[3], physicalId, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("Existing shared data belongs to another installation or directory. It was preserved.");
            string boundRoot;
            try { boundRoot = Encoding.Unicode.GetString(Convert.FromHexString(binding[2])); }
            catch (FormatException exception) { throw new InvalidDataException("The shared data binding path is invalid.", exception); }
            if (!string.Equals(Path.TrimEndingDirectorySeparator(Path.GetFullPath(boundRoot)), appRoot, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("Existing shared data belongs to another installation path. It was preserved.");
            return;
        }
        using var output = new FileStream(markerPath, FileMode.CreateNew, FileAccess.Write, FileShare.None);
        byte[] bytes = Encoding.ASCII.GetBytes(marker);
        output.Write(bytes);
        output.Flush(flushToDisk: true);
    }

    internal static string DirectoryIdentity(string path)
    {
        MachineDataStorage.ValidatePath(path);
        using SafeFileHandle directory = CreateFile(path, 0x80, 1 | 2, IntPtr.Zero, 3, 0x02200000, IntPtr.Zero);
        if (directory.IsInvalid || !GetFileInformationByHandleEx(directory, 18, out FileId identity, 24))
            throw new IOException("Shared data directory identity could not be verified.");
        return Convert.ToHexString(identity.Bytes).ToLowerInvariant();
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct FileId { [MarshalAs(UnmanagedType.ByValArray, SizeConst = 24)] public byte[] Bytes; }
    [DllImport("kernel32.dll", EntryPoint = "CreateFileW", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern SafeFileHandle CreateFile(string path, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)] private static extern bool GetFileInformationByHandleEx(SafeFileHandle handle, int informationClass, out FileId information, uint size);
}
