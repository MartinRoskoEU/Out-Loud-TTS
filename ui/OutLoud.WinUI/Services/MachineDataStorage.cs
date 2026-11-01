using System.Security.AccessControl;
using System.Security.Principal;

namespace OutLoud.WinUI.Services;

/// <summary>Machine-wide persistent state. Shared changes require administrator privileges.</summary>
internal static class MachineDataStorage
{
    internal static string Root => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),
        "OutLoud", "ApplicationData");
    internal static string UpdateCache => Path.Combine(Root, "update-checks");
    internal static bool IsAdministrator
    {
        get
        {
            using var identity = WindowsIdentity.GetCurrent();
            return new WindowsPrincipal(identity).IsInRole(WindowsBuiltInRole.Administrator);
        }
    }

    private const string ProtectedDirectory = "O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FRFX;;;BU)";

    internal static void ValidatePath(string path)
    {
        string full = Path.GetFullPath(path);
        if (!string.Equals(full, Root, StringComparison.OrdinalIgnoreCase) && !PocketTtsRuntimeStore.IsWithin(full, Root))
            throw new InvalidDataException("Shared data paths must remain inside Out Loud application data.");
        for (string? current = full; current is not null; current = Path.GetDirectoryName(current))
        {
            try
            {
                if ((File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0)
                    throw new InvalidDataException("Shared data cannot contain file or directory links.");
            }
            catch (FileNotFoundException) { }
            catch (DirectoryNotFoundException) { }
            if (string.Equals(current, Root, StringComparison.OrdinalIgnoreCase)) break;
        }
    }

    internal static void ValidateDirectory(string path)
    {
        if ((File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0)
            throw new InvalidDataException("Shared Out Loud storage cannot use directory links.");
        var actual = new DirectoryInfo(path).GetAccessControl(AccessControlSections.Owner | AccessControlSections.Access);
        var expected = new DirectorySecurity();
        expected.SetSecurityDescriptorSddlForm(ProtectedDirectory);
        if (!actual.AreAccessRulesProtected || actual.GetOwner(typeof(SecurityIdentifier))?.Value is not ("S-1-5-32-544" or "S-1-5-18") ||
            actual.GetSecurityDescriptorSddlForm(AccessControlSections.Access) != expected.GetSecurityDescriptorSddlForm(AccessControlSections.Access))
            throw new InvalidDataException("Shared Out Loud storage permissions are not valid. No permissions were changed.");
    }

    internal static void Prepare()
    {
        if (!IsAdministrator) throw new UnauthorizedAccessException("Administrator approval is required to change shared Out Loud data.");
        string parent = Path.GetDirectoryName(Root)!;
        CreateProtected(parent);
        InstallationDataOwnership.PrepareNewStore(Root);
        CreateProtected(Root);
        Directory.CreateDirectory(PocketTtsModelStore.Root);
        string lease = Path.Combine(PocketTtsModelStore.Root, ".operations.lock");
        if (!File.Exists(lease))
            using (new FileStream(lease, FileMode.CreateNew, FileAccess.ReadWrite, FileShare.Read)) { }
        CreateProtected(UpdateCache);
    }

    internal static void CreateProtected(string path)
    {
        if (!Directory.Exists(path))
        {
            var security = new DirectorySecurity();
            security.SetSecurityDescriptorSddlForm(ProtectedDirectory);
            new DirectoryInfo(path).Create(security);
        }
        ValidateDirectory(path);
    }
}
