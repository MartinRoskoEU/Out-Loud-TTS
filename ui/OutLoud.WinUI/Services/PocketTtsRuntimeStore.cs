using System.Security.Cryptography;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace OutLoud.WinUI.Services;

/// <summary>Local runtime selection, installation receipts, and atomic identity activation.</summary>
internal static class PocketTtsRuntimeStore
{
    internal static string LocalDataPath => Path.GetDirectoryName(PocketTtsRuntimeValidation.RuntimeIdentityPath)!;
    internal static string RuntimesPath => Path.Combine(LocalDataPath, "runtimes");
    internal static string StagingPath => Path.Combine(RuntimesPath, ".staging");
    internal static string BundledPath => Path.Combine(AppContext.BaseDirectory, "runtime", "pocket-tts", "pocket-tts.exe");
    internal sealed record VerifiedRuntime(PocketTtsRuntimeValidation.RuntimeIdentity Identity, string ExecutablePath);
    internal sealed record InstallationReceipt(string ReleaseVersion, string ExecutableSha256, string ArchiveSha256,
        long AssetId, string AssetName, string AssetUrl, string LicenseSha256);

    internal static bool IsHash(string? value) => value is not null && Regex.IsMatch(value, @"\A[0-9a-fA-F]{64}\z");

    internal static string ReleasePath(string tag, string digest)
    {
        PocketTtsRuntimeUpdateService.ValidateReleaseTag(tag);
        if (tag.Length > 128 || !IsHash(digest)) { throw new InvalidDataException("Invalid runtime installation identity."); }
        return Path.Combine(RuntimesPath, tag, digest.ToUpperInvariant());
    }

    internal static string ResolveActivePath()
    {
        if (Directory.Exists(LocalDataPath)) MachineDataStorage.ValidateDirectory(LocalDataPath);
        string identityPath = PocketTtsRuntimeValidation.RuntimeIdentityPath;
        if (!File.Exists(identityPath)) { return BundledPath; }
        var identity = ReadJson<PocketTtsRuntimeValidation.RuntimeIdentity>(identityPath);
        return PathForIdentity(identity);
    }

    private static string PathForIdentity(PocketTtsRuntimeValidation.RuntimeIdentity identity)
    {
        if (string.IsNullOrWhiteSpace(identity.ReleaseVersion) || !IsHash(identity.ExecutableSha256))
        { throw new InvalidDataException("The saved runtime identity is invalid."); }
        if (identity.ReleaseVersion == PocketTtsRuntimeValidation.ValidatedRuntimeRelease)
        {
            if (!string.Equals(identity.ExecutableSha256, PocketTtsRuntimeValidation.ValidatedRuntimeSha256, StringComparison.OrdinalIgnoreCase))
            { throw new InvalidDataException("The saved bundled runtime identity does not match the trusted SHA-256."); }
            return BundledPath;
        }
        return Path.Combine(ReleasePath(identity.ReleaseVersion, identity.ExecutableSha256), "pocket-tts.exe");
    }

    internal static async Task<VerifiedRuntime> ReadVerifiedActiveAsync(CancellationToken cancellationToken, bool initializeIdentity = false)
    {
        if (!File.Exists(PocketTtsRuntimeValidation.RuntimeIdentityPath))
        { return await VerifyRuntimeAsync(BundledPath, cancellationToken).ConfigureAwait(false); }
        var identity = ReadJson<PocketTtsRuntimeValidation.RuntimeIdentity>(PocketTtsRuntimeValidation.RuntimeIdentityPath);
        string path = PathForIdentity(identity);
        var verified = await VerifyRuntimeAsync(path, cancellationToken, initializeIdentity: false).ConfigureAwait(false);
        if (verified.Identity.ReleaseVersion != identity.ReleaseVersion ||
            !string.Equals(verified.Identity.ExecutableSha256, identity.ExecutableSha256, StringComparison.OrdinalIgnoreCase))
        { throw new InvalidDataException("The active runtime does not match its saved identity."); }
        return verified;
    }

    internal static async Task<VerifiedRuntime> VerifyRuntimeAsync(string path, CancellationToken cancellationToken, bool initializeIdentity = true)
    {
        string fullPath = Path.GetFullPath(path);
        if ((File.GetAttributes(fullPath) & FileAttributes.ReparsePoint) != 0)
        { throw new InvalidDataException("Runtime executables cannot be file links or reparse points."); }
        string digest = await HashFileAsync(fullPath, cancellationToken).ConfigureAwait(false);
        if (!IsWithin(fullPath, RuntimesPath))
        {
            if (digest != PocketTtsRuntimeValidation.ValidatedRuntimeSha256)
            { throw new InvalidDataException("This runtime does not match the trusted bundled Pocket TTS v4.0.1 executable."); }
            var identity = new PocketTtsRuntimeValidation.RuntimeIdentity(PocketTtsRuntimeValidation.ValidatedRuntimeRelease, digest);
            if (initializeIdentity && MachineDataStorage.IsAdministrator && !File.Exists(PocketTtsRuntimeValidation.RuntimeIdentityPath))
            {
                EnsureDirectory(LocalDataPath);
                await WriteIdentityAsync(identity, replace: false, cancellationToken).ConfigureAwait(false);
            }
            // A valid stored update must not be overwritten when explicitly checking the fallback.
            if (File.Exists(PocketTtsRuntimeValidation.RuntimeIdentityPath))
            { _ = PathForIdentity(ReadJson<PocketTtsRuntimeValidation.RuntimeIdentity>(PocketTtsRuntimeValidation.RuntimeIdentityPath)); }
            return new(identity, fullPath);
        }

        EnsureNoReparsePoints(Path.GetDirectoryName(fullPath)!);
        var receipt = ReadJson<InstallationReceipt>(Path.Combine(Path.GetDirectoryName(fullPath)!, "runtime-installation.json"));
        if (string.IsNullOrWhiteSpace(receipt.ReleaseVersion) || !IsHash(receipt.ExecutableSha256) ||
            !IsHash(receipt.ArchiveSha256) || !IsHash(receipt.LicenseSha256) ||
            string.IsNullOrWhiteSpace(receipt.AssetName) || string.IsNullOrWhiteSpace(receipt.AssetUrl))
        { throw new InvalidDataException("The runtime installation receipt is invalid."); }
        string expectedPath = Path.Combine(ReleasePath(receipt.ReleaseVersion, receipt.ExecutableSha256), "pocket-tts.exe");
        if (!string.Equals(fullPath, expectedPath, StringComparison.OrdinalIgnoreCase) ||
            !string.Equals(digest, receipt.ExecutableSha256, StringComparison.OrdinalIgnoreCase) ||
            receipt.AssetId <= 0 ||
            !PocketTtsRuntimeUpdateService.IsOfficialAssetUrl(new Uri(receipt.AssetUrl), receipt.ReleaseVersion, receipt.AssetName))
        { throw new InvalidDataException("The installed runtime does not match its verified installation receipt."); }
        string licenseHash = await HashFileAsync(Path.Combine(Path.GetDirectoryName(fullPath)!, "LICENSE"), cancellationToken).ConfigureAwait(false);
        if (!string.Equals(licenseHash, receipt.LicenseSha256, StringComparison.OrdinalIgnoreCase))
        { throw new InvalidDataException("The runtime license does not match its installation receipt."); }
        return new(new(receipt.ReleaseVersion, digest), fullPath);
    }

    internal static async Task<string> HashFileAsync(string path, CancellationToken cancellationToken)
    {
        using FileStream stream = File.OpenRead(path);
        return Convert.ToHexString(await SHA256.HashDataAsync(stream, cancellationToken).ConfigureAwait(false));
    }

    internal static T ReadJson<T>(string path)
    {
        using FileStream stream = new(path, FileMode.Open, FileAccess.Read, FileShare.Read | FileShare.Delete);
        if (stream.Length > 16 * 1024) { throw new InvalidDataException("Runtime identity metadata is too large."); }
        return JsonSerializer.Deserialize<T>(stream) ?? throw new InvalidDataException("Runtime identity metadata is invalid.");
    }

    internal static async Task WriteJsonAsync<T>(string path, T value, CancellationToken cancellationToken)
    {
        await using FileStream stream = new(path, FileMode.CreateNew, FileAccess.Write, FileShare.None);
        await JsonSerializer.SerializeAsync(stream, value, cancellationToken: cancellationToken).ConfigureAwait(false);
        await stream.FlushAsync(cancellationToken).ConfigureAwait(false);
        stream.Flush(flushToDisk: true);
    }

    private static async Task WriteIdentityAsync(PocketTtsRuntimeValidation.RuntimeIdentity identity, bool replace, CancellationToken cancellationToken)
    {
        var temporaryOperation = await PocketTtsDownloadOperation.BeginMetadataAsync("runtime-identity", cancellationToken).ConfigureAwait(false);
        string temporary = temporaryOperation.MetadataPath;
        try
        {
            await WriteJsonAsync(temporary, identity, cancellationToken).ConfigureAwait(false);
            cancellationToken.ThrowIfCancellationRequested();
            if (replace)
            { File.Replace(temporary, PocketTtsRuntimeValidation.RuntimeIdentityPath,
                Path.Combine(LocalDataPath, "runtime-identity.previous.json")); }
            else
            {
                try { File.Move(temporary, PocketTtsRuntimeValidation.RuntimeIdentityPath); }
                catch (IOException) when (File.Exists(PocketTtsRuntimeValidation.RuntimeIdentityPath))
                { _ = PathForIdentity(ReadJson<PocketTtsRuntimeValidation.RuntimeIdentity>(PocketTtsRuntimeValidation.RuntimeIdentityPath)); }
            }
        }
        finally
        {
            // A cleanup error must not turn a successful atomic commit into an apparent activation failure.
            try { _ = await temporaryOperation.CleanupAsync().ConfigureAwait(false); }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or InvalidDataException) { }
        }
    }

    internal static async Task ActivateAsync(VerifiedRuntime previous, string executablePath, CancellationToken cancellationToken)
    {
        var candidate = await VerifyRuntimeAsync(executablePath, cancellationToken, initializeIdentity: false).ConfigureAwait(false);
        var current = await ReadVerifiedActiveAsync(cancellationToken).ConfigureAwait(false);
        if (current.Identity != previous.Identity) { throw new InvalidOperationException("The active runtime changed. Check for updates and confirm again."); }
        cancellationToken.ThrowIfCancellationRequested();
        bool committed = false;
        try
        {
            // Cancellation ends at the atomic commit. A committed update is either verified or rolled back.
            await WriteIdentityAsync(candidate.Identity, replace: true, CancellationToken.None).ConfigureAwait(false);
            committed = true;
            _ = await ReadVerifiedActiveAsync(CancellationToken.None).ConfigureAwait(false);
        }
        catch (Exception activationFailure)
        {
            if (committed)
            {
                try
                {
                    // Restore the exact previous bytes by rename; rollback needs no new temporary-file allocation.
                    File.Replace(Path.Combine(LocalDataPath, "runtime-identity.previous.json"), PocketTtsRuntimeValidation.RuntimeIdentityPath, null);
                    _ = await ReadVerifiedActiveAsync(CancellationToken.None).ConfigureAwait(false);
                }
                catch (Exception rollbackFailure)
                { throw new InvalidOperationException("Runtime activation and identity recovery failed. Previous runtime files are retained; inspect the saved runtime identity and its previous backup before retrying.", new AggregateException(activationFailure, rollbackFailure)); }
            }
            throw new InvalidOperationException("Runtime activation failed. The previous runtime and identity have been retained.", activationFailure);
        }
    }

    // Only update management/startup recovery calls this; synthesis never scans old versions.
    private static IEnumerable<string> InactiveRuntimeDirectories()
    {
        MachineDataStorage.ValidatePath(RuntimesPath);
        if (!Directory.Exists(RuntimesPath)) yield break;
        string active = Path.GetDirectoryName(ResolveActivePath())!;
        foreach (string release in Directory.EnumerateDirectories(RuntimesPath))
        {
            if (Path.GetFileName(release) == ".staging") continue;
            MachineDataStorage.ValidatePath(release);
            foreach (string directory in Directory.EnumerateDirectories(release))
            {
                if (!IsHash(Path.GetFileName(directory)) || string.Equals(directory, active, StringComparison.OrdinalIgnoreCase)) continue;
                MachineDataStorage.ValidatePath(directory);
                if (File.Exists(Path.Combine(directory, "runtime-installation.json"))) yield return directory;
            }
        }
    }

    internal static bool HasInactiveRuntime => InactiveRuntimeDirectories().Any();

    // Caller holds runtime-update.lock. Reuse the existing runtime staging record:
    // a verified inactive payload is moved there atomically before checked deletion.
    internal static async Task<long> CleanupInactiveAsync(CancellationToken token)
    {
        string[] directories = InactiveRuntimeDirectories().ToArray();
        if (directories.Length == 0) return 0;
        _ = await ReadVerifiedActiveAsync(token).ConfigureAwait(false);
        long deleted = 0;
        foreach (string directory in directories)
        {
            token.ThrowIfCancellationRequested();
            if (string.Equals(directory, Path.GetDirectoryName(ResolveActivePath()), StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("The active runtime changed during cleanup.");
            string[] names = ["pocket-tts.exe", "LICENSE", "runtime-installation.json"];
            if (!Directory.EnumerateFileSystemEntries(directory).Select(Path.GetFileName).Order(StringComparer.Ordinal)
                .SequenceEqual(names.Order(StringComparer.Ordinal)))
                throw new InvalidDataException("Unrecorded files in an old runtime directory were preserved.");
            var verified = await VerifyRuntimeAsync(Path.Combine(directory, "pocket-tts.exe"), token, initializeIdentity: false).ConfigureAwait(false);
            var receipt = ReadJson<InstallationReceipt>(Path.Combine(directory, "runtime-installation.json"));
            var leases = new List<FileStream>();
            PocketTtsDownloadOperation? cleanup = null;
            try
            {
                // Deny new readers/writers and detect existing speech leases before moving anything.
                foreach (string name in names)
                {
                    string path = Path.Combine(directory, name);
                    var lease = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Delete);
                    leases.Add(lease);
                    PocketTtsModelStore.ValidateFile(path, lease.SafeFileHandle, sharedData: true);
                }
                if (Convert.ToHexString(await SHA256.HashDataAsync(leases[0], token).ConfigureAwait(false)) != verified.Identity.ExecutableSha256 ||
                    !string.Equals(Convert.ToHexString(await SHA256.HashDataAsync(leases[1], token).ConfigureAwait(false)), receipt.LicenseSha256, StringComparison.OrdinalIgnoreCase) ||
                    await JsonSerializer.DeserializeAsync<InstallationReceipt>(leases[2], cancellationToken: token).ConfigureAwait(false) != receipt)
                    throw new InvalidDataException("The old runtime changed during cleanup. It was preserved.");
                cleanup = await PocketTtsDownloadOperation.BeginRuntimeAsync(token).ConfigureAwait(false);
                EnsureDirectory(cleanup.DirectoryPath);
                token.ThrowIfCancellationRequested();
                // Windows directory rename itself refuses open child files, including
                // our validation leases. Release those before the atomic move.
                foreach (var lease in leases) lease.Dispose();
                leases.Clear();
                Directory.Move(directory, Path.Combine(cleanup.DirectoryPath, "payload"));
            }
            finally
            {
                foreach (var lease in leases) lease.Dispose();
                // Once moved, interrupted deletion is recoverable through the existing record.
                if (cleanup is not null) deleted += await cleanup.CleanupAsync().ConfigureAwait(false);
            }
            string release = Path.GetDirectoryName(directory)!;
            if (!Directory.EnumerateFileSystemEntries(release).Any()) Directory.Delete(release, recursive: false);
        }
        return deleted;
    }

    internal static bool IsWithin(string path, string root) => Path.GetFullPath(path).StartsWith(
        Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);

    internal static void EnsureDirectory(string path)
    {
        string fullPath = Path.GetFullPath(path);
        if (!string.Equals(fullPath, LocalDataPath, StringComparison.OrdinalIgnoreCase) && !IsWithin(fullPath, LocalDataPath))
        { throw new InvalidDataException("Runtime storage must remain inside shared Out Loud application data."); }
        EnsureNoReparsePoints(fullPath);
        if (!MachineDataStorage.IsAdministrator)
        {
            MachineDataStorage.ValidateDirectory(LocalDataPath);
            if (!Directory.Exists(fullPath)) throw new UnauthorizedAccessException("Administrator approval is required to create shared Out Loud storage.");
            return;
        }
        MachineDataStorage.Prepare();
        Directory.CreateDirectory(fullPath);
        EnsureNoReparsePoints(fullPath);
    }

    private static void EnsureNoReparsePoints(string path)
    {
        for (DirectoryInfo? directory = new(path); directory is not null; directory = directory.Parent)
        {
            if (directory.Exists && (directory.Attributes & FileAttributes.ReparsePoint) != 0)
            { throw new InvalidDataException("Runtime storage cannot use directory links or reparse points."); }
            if (string.Equals(directory.FullName, LocalDataPath, StringComparison.OrdinalIgnoreCase)) { break; }
        }
    }

    internal static void DeleteStagingDirectory(string path)
    {
        string fullPath = Path.GetFullPath(path);
        if (!IsWithin(fullPath, StagingPath)) { throw new InvalidOperationException("Refusing to delete outside runtime staging."); }
        if (Directory.Exists(fullPath)) { EnsureNoReparsePoints(fullPath); Directory.Delete(fullPath, recursive: true); }
    }
}
