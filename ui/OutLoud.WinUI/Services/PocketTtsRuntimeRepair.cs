using System.Net.Http;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Text.Json;

namespace OutLoud.WinUI.Services;

public sealed partial class PocketTtsRuntimeInstallService
{
    internal sealed record RepairTarget(string Path, PocketTtsRuntimeValidation.RuntimeIdentity Identity,
        PocketTtsRuntimeStore.InstallationReceipt? Receipt);

    // Metadata is authoritative even when the executable has disappeared. Never infer a new version.
    internal static RepairTarget ReadRepairTarget()
    {
        MachineDataStorage.ValidateDirectory(MachineDataStorage.Root);
        var identity = !IsMissing(PocketTtsRuntimeValidation.RuntimeIdentityPath)
            ? ReadRegularJson<PocketTtsRuntimeValidation.RuntimeIdentity>(PocketTtsRuntimeValidation.RuntimeIdentityPath)
            : new(PocketTtsRuntimeValidation.ValidatedRuntimeRelease, PocketTtsRuntimeValidation.ValidatedRuntimeSha256);
        string path = PocketTtsRuntimeStore.ResolveActivePath();
        PocketTtsRuntimeUpdateService.ValidateReleaseTag(identity.ReleaseVersion);
        if (!PocketTtsRuntimeStore.IsHash(identity.ExecutableSha256)) throw new InvalidDataException("Invalid saved runtime hash.");
        PocketTtsRuntimeStore.InstallationReceipt? receipt = null;
        if (identity.ReleaseVersion != PocketTtsRuntimeValidation.ValidatedRuntimeRelease)
        {
            receipt = ReadRegularJson<PocketTtsRuntimeStore.InstallationReceipt>(Path.Combine(Path.GetDirectoryName(path)!, "runtime-installation.json"));
            if (receipt.ReleaseVersion != identity.ReleaseVersion ||
                !string.Equals(receipt.ExecutableSha256, identity.ExecutableSha256, StringComparison.OrdinalIgnoreCase) ||
                !PocketTtsRuntimeStore.IsHash(receipt.ArchiveSha256) || !PocketTtsRuntimeStore.IsHash(receipt.LicenseSha256) ||
                receipt.AssetId <= 0 || !Uri.TryCreate(receipt.AssetUrl, UriKind.Absolute, out var url) ||
                !PocketTtsRuntimeUpdateService.IsOfficialAssetUrl(url, receipt.ReleaseVersion, receipt.AssetName))
                throw new InvalidDataException("The saved runtime repair receipt is invalid.");
        }
        return new(path, identity, receipt);
    }

    private static T ReadRegularJson<T>(string path)
    {
        using var file = File.OpenRead(path);
        PocketTtsModelStore.ValidateFile(path, file.SafeFileHandle, sharedData: true);
        return PocketTtsRuntimeStore.ReadJson<T>(path);
    }

    internal static bool IsMissing(string path)
    {
        try { _ = File.GetAttributes(path); return false; }
        catch (FileNotFoundException) { return true; }
        catch (DirectoryNotFoundException) { return true; }
    }

    internal Task<PocketTtsRuntimeInstallResult> RepairAsync(string expectedIdentity,
        IProgress<PocketTtsRuntimeInstallProgress>? progress = null, CancellationToken cancellationToken = default) =>
        MachineDataStorage.IsAdministrator
            ? Task.Run(() => RepairCoreAsync(expectedIdentity, progress, cancellationToken))
            : ElevatedMachineDataService.RunAsync<PocketTtsRuntimeInstallProgress, PocketTtsRuntimeInstallResult>(
                new("repair-runtime", Identity: expectedIdentity), progress, cancellationToken);

    internal static string RepairIdentity(RepairTarget target) => JsonSerializer.Serialize(target);

    private async Task<PocketTtsRuntimeInstallResult> RepairCoreAsync(string expectedIdentity,
        IProgress<PocketTtsRuntimeInstallProgress>? progress, CancellationToken cancellationToken)
    {
        PocketTtsDownloadOperation? download = null;
        PocketTtsRuntimeInstallResult result;
        using var deadline = new CancellationTokenSource(TimeSpan.FromMinutes(15));
        using var operation = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, deadline.Token);
        CancellationToken token = operation.Token;
        using var parents = PocketTtsModelStore.LockParentDirectories(Path.Combine(MachineDataStorage.Root, "runtime-update.lock"));
        PocketTtsRuntimeStore.EnsureDirectory(MachineDataStorage.Root);
        using var updateLock = new FileStream(Path.Combine(MachineDataStorage.Root, "runtime-update.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
        PocketTtsModelStore.ValidateFile(updateLock.Name, updateLock.SafeFileHandle, sharedData: true);
        try
        {
            PocketTtsCliService.EnsureSupportedProcessor();
            var target = ReadRepairTarget();
            if (RepairIdentity(target) != expectedIdentity) throw new InvalidOperationException("The selected runtime changed. Refresh Settings and confirm repair again.");
            if (!IsMissing(target.Path)) throw new InvalidOperationException("The runtime executable already exists. Repair never overwrites existing files.");
            using var destinationGuard = PocketTtsModelStore.LockParentDirectories(target.Path);
            ValidateRepairDirectory(Path.GetDirectoryName(target.Path)!);
            progress?.Report(new("Checking the official runtime release…"));
            var asset = await ReadRepairAssetAsync(target, token).ConfigureAwait(false);
            if (asset.SizeBytes <= 0 || asset.SizeBytes > MaximumArchiveBytes)
                throw new InvalidDataException("Unsupported runtime archive size.");
            string archiveHash = asset.Digest is { } digest && System.Text.RegularExpressions.Regex.IsMatch(digest, @"\Asha256:[a-fA-F0-9]{64}\z")
                ? digest[7..].ToUpperInvariant() : throw new InvalidDataException("The official runtime archive has no SHA-256 digest.");
            if (target.Receipt is { } receipt && (receipt.AssetId != asset.Id || receipt.AssetName != asset.Name ||
                receipt.AssetUrl != asset.DownloadUrl.AbsoluteUri || !receipt.ArchiveSha256.Equals(archiveHash, StringComparison.OrdinalIgnoreCase)))
                throw new InvalidDataException("The official release differs from the saved runtime installation receipt.");
            download = await PocketTtsDownloadOperation.BeginRuntimeAsync(token).ConfigureAwait(false);
            PocketTtsRuntimeStore.EnsureDirectory(download.DirectoryPath);
            string archive = Path.Combine(download.DirectoryPath, "runtime.zip");
            await DownloadAsync(asset, archive, progress, token).ConfigureAwait(false);
            using var archiveLease = File.OpenRead(archive);
            if (await PocketTtsRuntimeStore.HashFileAsync(archive, token).ConfigureAwait(false) != archiveHash)
                throw new InvalidDataException("The downloaded runtime archive failed SHA-256 verification.");
            string payload = Path.Combine(download.DirectoryPath, "payload");
            PocketTtsRuntimeStore.EnsureDirectory(payload);
            var hashes = await ExtractAsync(archive, payload, token).ConfigureAwait(false);
            if (!hashes.ExecutableHash.Equals(target.Identity.ExecutableSha256, StringComparison.OrdinalIgnoreCase) ||
                (target.Receipt is not null && !hashes.LicenseHash.Equals(target.Receipt.LicenseSha256, StringComparison.OrdinalIgnoreCase)))
                throw new InvalidDataException("The downloaded runtime does not match the saved content identity.");
            string candidate = Path.Combine(payload, "pocket-tts.exe");
            ValidateExecutableArchitecture(candidate);
            using (var lease = File.OpenRead(candidate))
            {
                PocketTtsModelStore.ValidateFile(candidate, lease.SafeFileHandle, sharedData: true);
                if (await PocketTtsRuntimeStore.HashFileAsync(candidate, token).ConfigureAwait(false) != hashes.ExecutableHash)
                    throw new InvalidDataException("The verified runtime candidate changed.");
                await PocketTtsRuntimeValidation.ValidateCliAsync(new(candidate), target.Receipt is null, token).ConfigureAwait(false);
            }
            string license = Path.Combine(Path.GetDirectoryName(target.Path)!, "LICENSE");
            using var licenseLease = File.OpenRead(license);
            PocketTtsModelStore.ValidateRegularFile(license, licenseLease.SafeFileHandle);
            if (await PocketTtsRuntimeStore.HashFileAsync(license, token).ConfigureAwait(false) != hashes.LicenseHash)
                throw new InvalidDataException("The installed runtime license is missing or changed. Use installer repair.");
            if (RepairIdentity(ReadRepairTarget()) != expectedIdentity)
                throw new InvalidOperationException("The selected runtime changed during repair. No file was restored.");
            token.ThrowIfCancellationRequested();
            progress?.Report(new("Restoring the verified runtime…", CanCancel: false));
            // MoveFileW is same-volume, atomic and never overwrites. Staging remains journaled on failure.
            if (!MoveFile(candidate, target.Path)) throw new IOException("The missing runtime could not be published.",
                new System.ComponentModel.Win32Exception(System.Runtime.InteropServices.Marshal.GetLastWin32Error()));
            var verified = await PocketTtsRuntimeStore.ReadVerifiedActiveAsync(CancellationToken.None).ConfigureAwait(false);
            if (verified.Identity.ReleaseVersion != target.Identity.ReleaseVersion ||
                !verified.Identity.ExecutableSha256.Equals(target.Identity.ExecutableSha256, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("The restored runtime identity changed.");
            await PocketTtsRuntimeValidation.ValidateCliAsync(new(target.Path), target.Receipt is null, CancellationToken.None).ConfigureAwait(false);
            result = new(true, false, $"Pocket TTS Runtime {target.Identity.ReleaseVersion} was repaired and verified.", target.Identity.ReleaseVersion);
        }
        catch (OperationCanceledException)
        { result = new(false, cancellationToken.IsCancellationRequested, cancellationToken.IsCancellationRequested ? "Runtime repair cancelled." : "Runtime repair timed out."); }
        catch (Exception exception)
        { result = new(false, false, "Runtime repair could not complete. " + exception.Message + " If Windows Security blocked the file, review Protection history. No security settings were changed."); }
        if (download is not null)
        {
            try { await download.CleanupAsync().ConfigureAwait(false); }
            catch (Exception) { result = result with { CleanupIncomplete = true, Message = result.Message + " Temporary cleanup remains recorded for startup recovery." }; }
        }
        return result;
    }

    private async Task<PocketTtsRuntimeUpdateAsset> ReadRepairAssetAsync(RepairTarget target, CancellationToken token)
    {
        string tag = target.Identity.ReleaseVersion;
        using var request = new HttpRequestMessage(HttpMethod.Get,
            "https://api.github.com/repos/kyutai-labs/pocket-tts/releases/tags/" + Uri.EscapeDataString(tag));
        request.Headers.UserAgent.ParseAdd(ApplicationInfo.UserAgent);
        request.Headers.Accept.ParseAdd("application/vnd.github+json");
        request.Headers.Add("X-GitHub-Api-Version", "2022-11-28");
        using var response = await _metadataHttp.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, token).ConfigureAwait(false);
        response.EnsureSuccessStatusCode();
        if (response.Content.Headers.ContentLength > 2 * 1024 * 1024 ||
            response.Content.Headers.ContentType?.MediaType is not { } media ||
            !(media.Equals("application/json", StringComparison.OrdinalIgnoreCase) || media.EndsWith("+json", StringComparison.OrdinalIgnoreCase)))
            throw new InvalidDataException("Invalid runtime release metadata.");
        await response.Content.LoadIntoBufferAsync(2 * 1024 * 1024, token).ConfigureAwait(false);
        using var json = JsonDocument.Parse(await response.Content.ReadAsByteArrayAsync(token).ConfigureAwait(false));
        if (json.RootElement.GetProperty("tag_name").GetString() != tag || json.RootElement.GetProperty("draft").GetBoolean())
            throw new InvalidDataException("The official runtime release does not match the selected version.");
        return PocketTtsRuntimeUpdateService.FindWindowsAsset(json.RootElement, tag)
            ?? throw new InvalidDataException("This release has no compatible Windows runtime archive.");
    }

    private static void ValidateRepairDirectory(string path)
    {
        // Do not publish an executable into a directory writable by ordinary users.
        var security = new DirectoryInfo(path).GetAccessControl(AccessControlSections.Owner | AccessControlSections.Access);
        static bool Trusted(string? sid) => sid is "S-1-5-18" or "S-1-5-32-544" or "S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464";
        if (!Trusted(security.GetOwner(typeof(SecurityIdentifier))?.Value)) throw new InvalidDataException("The runtime directory owner is not trusted.");
        const FileSystemRights writes = FileSystemRights.Write | FileSystemRights.Delete | FileSystemRights.DeleteSubdirectoriesAndFiles | FileSystemRights.ChangePermissions | FileSystemRights.TakeOwnership;
        foreach (FileSystemAccessRule rule in security.GetAccessRules(true, true, typeof(SecurityIdentifier)))
            if (rule.AccessControlType == AccessControlType.Allow && (rule.FileSystemRights & writes) != 0 &&
                !Trusted(((SecurityIdentifier)rule.IdentityReference).Value) && (rule.PropagationFlags & PropagationFlags.InheritOnly) == 0)
                throw new InvalidDataException("The runtime directory permits untrusted changes. Permissions were not changed.");
    }

    [System.Runtime.InteropServices.DllImport("kernel32.dll", EntryPoint = "MoveFileW", CharSet = System.Runtime.InteropServices.CharSet.Unicode, SetLastError = true)]
    [return: System.Runtime.InteropServices.MarshalAs(System.Runtime.InteropServices.UnmanagedType.Bool)]
    private static extern bool MoveFile(string source, string destination);
}
