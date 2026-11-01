using System.IO.Compression;
using System.Net;
using System.Net.Http;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;
using System.Text.RegularExpressions;

namespace OutLoud.WinUI.Services;

public sealed record PocketTtsRuntimeInstallProgress(string Message, double? Percent = null, bool CanCancel = true);
public sealed record PocketTtsRuntimeInstallResult(bool Succeeded, bool Cancelled, string Message, string? InstalledReleaseTag = null, bool CleanupIncomplete = false);

/// <summary>Explicitly confirmed runtime installations. Never called by startup update checking.</summary>
public sealed class PocketTtsRuntimeInstallService
{
    private const long MaximumArchiveBytes = 256L * 1024 * 1024;
    private readonly HttpClient _metadataHttp;
    private readonly HttpClient _downloadHttp;

    /// <summary>Caller-owned clients; the download client must disable automatic redirects and cookies.</summary>
    public PocketTtsRuntimeInstallService(HttpClient metadataHttp, HttpClient downloadHttp)
    { _metadataHttp = metadataHttp; _downloadHttp = downloadHttp; }

    public Task<PocketTtsRuntimeInstallResult> InstallAsync(PocketTtsRuntimeUpdateResult confirmedRelease,
        IProgress<PocketTtsRuntimeInstallProgress>? progress = null, CancellationToken cancellationToken = default) =>
        MachineDataStorage.IsAdministrator
            ? Task.Run(() => InstallCoreAsync(confirmedRelease, progress, cancellationToken))
            : ElevatedMachineDataService.RunAsync<PocketTtsRuntimeInstallProgress, PocketTtsRuntimeInstallResult>(
                new("install-runtime", Release: confirmedRelease), progress, cancellationToken);

    private async Task<PocketTtsRuntimeInstallResult> InstallCoreAsync(PocketTtsRuntimeUpdateResult confirmed,
        IProgress<PocketTtsRuntimeInstallProgress>? progress, CancellationToken cancellationToken)
    {
        string? staging = null;
        FileStream? updateLock = null;
        PocketTtsDownloadOperation? download = null;
        PocketTtsRuntimeInstallResult result;
        using var deadline = new CancellationTokenSource(TimeSpan.FromMinutes(15));
        using var operation = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, deadline.Token);
        CancellationToken token = operation.Token;
        try
        {
            try
            {
                PocketTtsCliService.EnsureSupportedProcessor();
                PocketTtsRuntimeStore.EnsureDirectory(PocketTtsRuntimeStore.LocalDataPath);
                updateLock = new FileStream(Path.Combine(PocketTtsRuntimeStore.LocalDataPath, "runtime-update.lock"),
                    FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
                var previous = await PocketTtsRuntimeStore.ReadVerifiedActiveAsync(token).ConfigureAwait(false);
                if (confirmed.Status != PocketTtsRuntimeUpdateStatus.UpdateAvailable || confirmed.Asset is null ||
                    confirmed.InstalledReleaseTag != previous.Identity.ReleaseVersion)
                { throw new InvalidOperationException("The installed runtime changed. Check for updates and confirm again."); }

                progress?.Report(new("Verifying the latest official release…"));
                var latest = await new PocketTtsRuntimeUpdateService(_metadataHttp).CheckAsync(token).ConfigureAwait(false);
                token.ThrowIfCancellationRequested();
                if (latest.Status != PocketTtsRuntimeUpdateStatus.UpdateAvailable || latest.Asset is null ||
                    latest.LatestReleaseTag != confirmed.LatestReleaseTag || latest.Asset.Id != confirmed.Asset.Id ||
                    latest.InstalledReleaseTag != previous.Identity.ReleaseVersion)
                { throw new InvalidOperationException("The latest release could not be confirmed or has changed. Check for updates and confirm again."); }
                string tag = latest.LatestReleaseTag!;
                var asset = latest.Asset;
                if (asset.SizeBytes <= 0 || asset.SizeBytes > MaximumArchiveBytes ||
                    !PocketTtsRuntimeUpdateService.IsOfficialAssetUrl(asset.DownloadUrl, tag, asset.Name))
                { throw new InvalidDataException("The official runtime ZIP metadata is not supported."); }
                Match digestMatch = Regex.Match(asset.Digest ?? "", @"\Asha256:(?<hash>[a-fA-F0-9]{64})\z");
                if (!digestMatch.Success)
                { throw new InvalidDataException("GitHub did not publish a SHA-256 digest for this runtime ZIP. The update was not installed."); }
                string archiveHash = digestMatch.Groups["hash"].Value.ToUpperInvariant();

                PocketTtsRuntimeStore.EnsureDirectory(PocketTtsRuntimeStore.StagingPath);
                download = await PocketTtsDownloadOperation.BeginRuntimeAsync(token).ConfigureAwait(false);
                staging = download.DirectoryPath;
                PocketTtsRuntimeStore.EnsureDirectory(staging);
                string zipPath = Path.Combine(staging, "runtime.zip");
                await DownloadAsync(asset, zipPath, progress, token).ConfigureAwait(false);
                progress?.Report(new("Verifying the downloaded ZIP SHA-256…"));
                using FileStream archiveLease = File.OpenRead(zipPath);
                if (await PocketTtsRuntimeStore.HashFileAsync(zipPath, token).ConfigureAwait(false) != archiveHash)
                { throw new InvalidDataException("The runtime ZIP failed its official SHA-256 verification. No runtime was activated."); }

                string payload = Path.Combine(staging, "payload");
                PocketTtsRuntimeStore.EnsureDirectory(payload);
                progress?.Report(new("Extracting the verified runtime…"));
                var payloadHashes = await ExtractAsync(zipPath, payload, token).ConfigureAwait(false);
                string executable = Path.Combine(payload, "pocket-tts.exe");
                ValidateExecutableArchitecture(executable);
                string executableHash = payloadHashes.ExecutableHash;
                using (FileStream executableLease = File.OpenRead(executable))
                {
                    if (await PocketTtsRuntimeStore.HashFileAsync(executable, token).ConfigureAwait(false) != executableHash)
                    { throw new InvalidDataException("The extracted executable differs from the verified ZIP payload."); }
                    progress?.Report(new("Validating local voice-export compatibility…"));
                    using FileStream previousLease = File.OpenRead(previous.ExecutablePath);
                    if (!string.Equals(await PocketTtsRuntimeStore.HashFileAsync(previous.ExecutablePath, token).ConfigureAwait(false),
                        previous.Identity.ExecutableSha256, StringComparison.OrdinalIgnoreCase))
                    { throw new InvalidDataException("The previous runtime changed during update preparation."); }
                    string[] previousAliases = await PocketTtsRuntimeValidation.ValidateCliAsync(new(previous.ExecutablePath),
                        previous.Identity.ReleaseVersion == PocketTtsRuntimeValidation.ValidatedRuntimeRelease, token).ConfigureAwait(false);
                    string[] candidateAliases = await PocketTtsRuntimeValidation.ValidateCliAsync(new(executable), bundled: false, token).ConfigureAwait(false);
                    if (previousAliases.Except(candidateAliases, StringComparer.Ordinal).Any())
                    { throw new InvalidDataException("The new runtime does not preserve the installed runtime's model-selection contract."); }
                }

                var receipt = new PocketTtsRuntimeStore.InstallationReceipt(tag, executableHash, archiveHash, asset.Id,
                    asset.Name, asset.DownloadUrl.AbsoluteUri, payloadHashes.LicenseHash);
                await PocketTtsRuntimeStore.WriteJsonAsync(Path.Combine(payload, "runtime-installation.json"), receipt, token).ConfigureAwait(false);
                string destination = PocketTtsRuntimeStore.ReleasePath(tag, executableHash);
                PocketTtsRuntimeStore.EnsureDirectory(Path.GetDirectoryName(destination)!);
                token.ThrowIfCancellationRequested();
                if (!Directory.Exists(destination)) { Directory.Move(payload, destination); }
                executable = Path.Combine(destination, "pocket-tts.exe");
                var stored = await PocketTtsRuntimeStore.VerifyRuntimeAsync(executable, token, initializeIdentity: false).ConfigureAwait(false);
                if (stored.Identity.ReleaseVersion != tag || stored.Identity.ExecutableSha256 != executableHash)
                { throw new InvalidDataException("An existing runtime directory does not match this verified installation."); }
                if (PocketTtsRuntimeStore.ReadJson<PocketTtsRuntimeStore.InstallationReceipt>(Path.Combine(destination, "runtime-installation.json")) != receipt)
                { throw new InvalidDataException("An existing installation receipt differs from this verified release asset."); }
                progress?.Report(new("Activating the verified runtime…", CanCancel: false));
                await PocketTtsRuntimeStore.ActivateAsync(previous, executable, token).ConfigureAwait(false);
                result = new(true, false, $"Pocket TTS Runtime {tag} was installed and activated.", tag);
                try
                {
                    progress?.Report(new("Removing previous runtime files…", CanCancel: false));
                    _ = await PocketTtsRuntimeStore.CleanupInactiveAsync(CancellationToken.None).ConfigureAwait(false);
                }
                catch (Exception)
                {
                    result = result with { CleanupIncomplete = true, Message = result.Message +
                        " Previous runtime cleanup could not finish. Close applications using Out Loud and restart Out Loud to retry cleanup." };
                }
            }
            catch (OperationCanceledException)
            { result = new(false, cancellationToken.IsCancellationRequested, cancellationToken.IsCancellationRequested
                ? "Runtime update cancelled. The previous runtime and identity are unchanged."
                : "Runtime update timed out. The previous runtime and identity are unchanged."); }
            catch (HttpRequestException)
            { result = new(false, false, "The runtime download failed. Check your connection and retry. The previous runtime is unchanged."); }
            catch (TimeoutException)
            { result = new(false, false, "The runtime download stopped responding. The previous runtime is unchanged."); }
            catch (Exception exception) when (exception is InvalidDataException or InvalidOperationException or PlatformNotSupportedException)
            { result = new(false, false, exception.Message); }
            catch (UnauthorizedAccessException)
            { result = new(false, false, "Local runtime storage is not writable. The previous runtime and identity are unchanged."); }
            catch (IOException)
            { result = new(false, false, "Runtime storage is busy or unavailable, or the download was incomplete. The previous runtime and identity are unchanged; retry when no other instance is updating."); }
            catch (Exception)
            { result = new(false, false, "The runtime update could not complete. The previous runtime files have been retained."); }

            if (download is not null)
            {
                try { _ = await download.CleanupAsync().ConfigureAwait(false); }
                catch (Exception) { result = result with { Message = result.Message + " Temporary update cleanup remains recorded and will be retried at startup. Unverified files were preserved.", CleanupIncomplete = true }; }
            }
            return result;
        }
        finally { updateLock?.Dispose(); }
    }

    private async Task DownloadAsync(PocketTtsRuntimeUpdateAsset asset, string destination,
        IProgress<PocketTtsRuntimeInstallProgress>? progress, CancellationToken token)
    {
        Uri url = asset.DownloadUrl;
        for (int redirects = 0; redirects <= 5; redirects++)
        {
            if (url.Scheme != Uri.UriSchemeHttps || !url.IsDefaultPort || !string.IsNullOrEmpty(url.UserInfo) ||
                url.Host is not ("github.com" or "release-assets.githubusercontent.com" or "objects.githubusercontent.com"))
            { throw new InvalidDataException("The download redirected outside GitHub's official HTTPS asset hosts."); }
            using var request = new HttpRequestMessage(HttpMethod.Get, url);
            request.Headers.UserAgent.ParseAdd(ApplicationInfo.UserAgent);
            request.Headers.Accept.ParseAdd("application/octet-stream");
            using var idle = CancellationTokenSource.CreateLinkedTokenSource(token);
            idle.CancelAfter(TimeSpan.FromSeconds(30));
            HttpResponseMessage response;
            try { response = await _downloadHttp.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, idle.Token).ConfigureAwait(false); }
            catch (OperationCanceledException) when (!token.IsCancellationRequested) { throw new TimeoutException(); }
            using (response)
            {
                if (response.StatusCode is HttpStatusCode.MovedPermanently or HttpStatusCode.Found or HttpStatusCode.SeeOther or
                    HttpStatusCode.TemporaryRedirect or HttpStatusCode.PermanentRedirect)
                {
                    url = response.Headers.Location is { } location ? new Uri(url, location)
                        : throw new InvalidDataException("The download redirect is missing its destination.");
                    continue;
                }
                response.EnsureSuccessStatusCode();
                if (response.StatusCode != HttpStatusCode.OK ||
                    (response.Content.Headers.ContentLength is { } length && length != asset.SizeBytes))
                { throw new InvalidDataException("The runtime download length does not match official metadata."); }
                await using Stream source = await response.Content.ReadAsStreamAsync(token).ConfigureAwait(false);
                await using FileStream output = new(destination, FileMode.CreateNew, FileAccess.Write, FileShare.None, 65536, FileOptions.Asynchronous);
                byte[] buffer = new byte[65536];
                long received = 0;
                int lastPercent = -1;
                while (true)
                {
                    idle.CancelAfter(TimeSpan.FromSeconds(30));
                    int read;
                    try { read = await source.ReadAsync(buffer, idle.Token).ConfigureAwait(false); }
                    catch (OperationCanceledException) when (!token.IsCancellationRequested) { throw new TimeoutException(); }
                    if (read == 0) { break; }
                    received += read;
                    if (received > asset.SizeBytes || received > MaximumArchiveBytes)
                    { throw new InvalidDataException("The runtime download exceeds its expected size."); }
                    await output.WriteAsync(buffer.AsMemory(0, read), token).ConfigureAwait(false);
                    int percent = (int)(received * 100 / asset.SizeBytes);
                    if (percent != lastPercent) { progress?.Report(new($"Downloading runtime… {percent}%", percent)); lastPercent = percent; }
                }
                if (received != asset.SizeBytes) { throw new InvalidDataException("The runtime ZIP download was incomplete."); }
                await output.FlushAsync(token).ConfigureAwait(false);
                return;
            }
        }
        throw new InvalidDataException("The runtime download exceeded the allowed redirect limit.");
    }

    private static async Task<(string ExecutableHash, string LicenseHash)> ExtractAsync(string zipPath, string destination, CancellationToken token)
    {
        using ZipArchive archive = ZipFile.OpenRead(zipPath);
        if (archive.Entries.Count > 16) { throw new InvalidDataException("The runtime ZIP contains unsupported extra files."); }
        var found = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        string? executableHash = null, licenseHash = null;
        string? parent = null;
        foreach (ZipArchiveEntry entry in archive.Entries)
        {
            token.ThrowIfCancellationRequested();
            string[] parts = entry.FullName.Split('/', StringSplitOptions.RemoveEmptyEntries);
            if (entry.FullName.Contains('\\') || entry.FullName.StartsWith('/') || parts.Length == 0 || parts.Length > 2 ||
                parts.Any(part => part is "." or ".." || part.EndsWith('.') || part.EndsWith(' ') || part.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0) ||
                ((entry.ExternalAttributes >> 16) & 0xF000) == 0xA000 || (entry.ExternalAttributes & (int)FileAttributes.ReparsePoint) != 0)
            { throw new InvalidDataException("The runtime ZIP contains an unsafe path or link."); }
            if (entry.FullName.EndsWith('/')) { if (parts.Length != 1) { throw new InvalidDataException("Unsupported runtime ZIP directories."); } continue; }
            string file = parts[^1].Equals("pocket-tts.exe", StringComparison.OrdinalIgnoreCase) ? "pocket-tts.exe"
                : parts[^1].Equals("LICENSE", StringComparison.OrdinalIgnoreCase) ? "LICENSE"
                : throw new InvalidDataException("The runtime ZIP contains files outside the supported executable/license layout.");
            string entryParent = parts.Length == 2 ? parts[0] : "";
            if (parent is not null && parent != entryParent) { throw new InvalidDataException("The runtime ZIP has mixed directory layouts."); }
            parent = entryParent;
            long maximum = file == "LICENSE" ? 1024 * 1024 : MaximumArchiveBytes;
            if (!found.Add(file) || entry.Length <= 0 || entry.Length > maximum)
            { throw new InvalidDataException("The runtime ZIP contains duplicate, empty, or oversized entries."); }
            // Only these fixed leaf filenames are written; no archive-supplied path is used for extraction.
            await using Stream source = entry.Open();
            await using FileStream target = new(Path.Combine(destination, file), FileMode.CreateNew, FileAccess.Write, FileShare.None);
            using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
            byte[] buffer = new byte[65536];
            long written = 0;
            int count;
            while ((count = await source.ReadAsync(buffer, token).ConfigureAwait(false)) != 0)
            {
                written += count;
                if (written > entry.Length || written > maximum) { throw new InvalidDataException("An extracted runtime entry exceeds its expected size."); }
                hash.AppendData(buffer, 0, count);
                await target.WriteAsync(buffer.AsMemory(0, count), token).ConfigureAwait(false);
            }
            if (written != entry.Length) { throw new InvalidDataException("The extracted runtime entry is incomplete."); }
            string digest = Convert.ToHexString(hash.GetHashAndReset());
            if (file == "pocket-tts.exe") { executableHash = digest; } else { licenseHash = digest; }
        }
        if (found.Count != 2) { throw new InvalidDataException("The runtime ZIP must contain pocket-tts.exe and LICENSE."); }
        return (executableHash!, licenseHash!);
    }

    private static void ValidateExecutableArchitecture(string executable)
    {
        using var stream = File.OpenRead(executable);
        using var pe = new PEReader(stream);
        if (pe.PEHeaders.CoffHeader.Machine != Machine.Amd64 || pe.PEHeaders.PEHeader is null ||
            (pe.PEHeaders.CoffHeader.Characteristics & Characteristics.Dll) != 0)
        { throw new InvalidDataException("The downloaded runtime is not a Windows x64 executable."); }
    }
}
