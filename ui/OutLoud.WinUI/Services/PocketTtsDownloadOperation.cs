using System.Text.Json;
using System.Text.RegularExpressions;

namespace OutLoud.WinUI.Services;

// Immutable cleanup authority, persisted before creating any download payload.
// Existing model/runtime installation locks also protect recovery and cleanup.
internal sealed class PocketTtsDownloadOperation
{
    private sealed record Record(int SchemaVersion, string OperationId, string Kind, string StoreIdentity, string[] Files);
    internal sealed record Recovery(bool HadWork, long DeletedBytes, bool CleanupPending, string Message);
    private static readonly string[] RuntimeFiles = ["runtime.zip", "payload/pocket-tts.exe", "payload/LICENSE", "payload/runtime-installation.json"];
    private static readonly string[] MetadataFiles = ["value.json"];
    private FileStream? _metadataLock;
    internal string MetadataPath => Path.Combine(DirectoryPath, "value.json");
    private static bool IsMetadata(string kind) => kind is "model-inventory" or "model-selection" or "model-update-checks" or "runtime-identity";
    private readonly string _journal;
    private readonly Record _record;
    internal string DirectoryPath { get; }

    private PocketTtsDownloadOperation(string journal, Record record)
    {
        _journal = journal; _record = record;
        DirectoryPath = Path.Combine(Staging(record.Kind), record.OperationId);
    }

    private static string Staging(string kind) => kind switch
    {
        "model" or "model-inventory" or "model-selection" or "model-update-checks" => PocketTtsModelStore.Staging,
        "runtime" or "runtime-identity" => PocketTtsRuntimeStore.StagingPath,
        _ => throw new InvalidDataException("Unknown download operation kind."),
    };

    internal static async Task<PocketTtsDownloadOperation> BeginAsync(string kind, string[] files, CancellationToken token, string? operationId = null)
    {
        string staging = Staging(kind);
        PocketTtsRuntimeStore.EnsureDirectory(staging);
        string id = operationId ?? Guid.NewGuid().ToString("N");
        var record = new Record(1, id, kind, InstallationDataOwnership.DirectoryIdentity(MachineDataStorage.Root), files.ToArray());
        string journal = Path.Combine(staging, id + ".json");
        Validate(record, journal, kind);
        string temporary = Path.Combine(staging, id + ".tmp");
        bool created = false;
        try
        {
            await using (var output = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None, 4096, FileOptions.Asynchronous))
            {
                created = true;
                await JsonSerializer.SerializeAsync(output, record, cancellationToken: token).ConfigureAwait(false);
                await output.FlushAsync(token).ConfigureAwait(false);
                output.Flush(flushToDisk: true);
            }
            // A complete temporary record is also recoverable if publication is interrupted.
            File.Move(temporary, journal);
        }
        catch
        {
            if (created)
            {
                try { _ = PocketTtsModelStore.DeleteRegularFile(temporary, sharedData: true); }
                catch (Exception exception) when (exception is IOException or UnauthorizedAccessException) { }
            }
            throw;
        }
        return new(journal, record);
    }

    internal static Task<PocketTtsDownloadOperation> BeginRuntimeAsync(CancellationToken token) => BeginAsync("runtime", RuntimeFiles, token);

    // Only metadata writes use this lock; speech playback never acquires it.
    private static async Task<FileStream> AcquireMetadataLockAsync(CancellationToken token)
    {
        string path = Path.Combine(MachineDataStorage.Root, "metadata-write.lock");
        while (true)
        {
            token.ThrowIfCancellationRequested();
            try
            {
                var file = new FileStream(path, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
                try { PocketTtsModelStore.ValidateFile(path, file.SafeFileHandle, sharedData: true); return file; }
                catch { file.Dispose(); throw; }
            }
            catch (IOException exception) when ((exception.HResult & 0xffff) is 32 or 33)
            { await Task.Delay(100, token).ConfigureAwait(false); }
        }
    }

    internal static async Task<PocketTtsDownloadOperation> BeginMetadataAsync(string kind, CancellationToken token)
    {
        if (!IsMetadata(kind)) throw new InvalidDataException("Unknown metadata operation.");
        PocketTtsRuntimeStore.EnsureDirectory(Staging(kind));
        var guard = await AcquireMetadataLockAsync(token).ConfigureAwait(false);
        try
        {
            var operation = await BeginAsync(kind, MetadataFiles, token).ConfigureAwait(false);
            operation._metadataLock = guard;
            PocketTtsRuntimeStore.EnsureDirectory(operation.DirectoryPath);
            return operation;
        }
        catch { guard.Dispose(); throw; }
    }

    private static void Validate(Record record, string journal, string kind)
    {
        MachineDataStorage.ValidatePath(journal);
        if (record.SchemaVersion != 1 || record.Kind != kind || !Regex.IsMatch(record.OperationId ?? "", @"\A[a-f0-9]{32}\z") ||
            !Regex.IsMatch(record.StoreIdentity ?? "", @"\A[a-f0-9]{48}\z") ||
            record.StoreIdentity != InstallationDataOwnership.DirectoryIdentity(MachineDataStorage.Root) ||
            Path.GetFileNameWithoutExtension(journal) != record.OperationId ||
            !string.Equals(Path.GetDirectoryName(journal), Staging(kind), StringComparison.OrdinalIgnoreCase) ||
            Path.GetExtension(journal) is not (".json" or ".tmp") || record.Files is not { Length: > 0 and <= 256 } ||
            record.Files.Distinct(StringComparer.OrdinalIgnoreCase).Count() != record.Files.Length)
            throw new InvalidDataException("The interrupted download record could not be verified. Its files were preserved.");
        if (IsMetadata(kind))
        {
            if (!record.Files.SequenceEqual(MetadataFiles))
                throw new InvalidDataException("Unknown metadata staging files were preserved.");
        }
        else if (kind == "runtime")
        {
            if (!record.Files.Order(StringComparer.Ordinal).SequenceEqual(RuntimeFiles.Order(StringComparer.Ordinal)))
                throw new InvalidDataException("Unknown runtime staging files were preserved.");
        }
        else if (record.Files.Any(file => file is null || !Regex.IsMatch(file, @"\A[a-f0-9]{32}\.partial\z")))
            throw new InvalidDataException("Unknown model staging files were preserved.");
    }

    private static async Task<PocketTtsDownloadOperation> ReadAsync(string journal, string kind, CancellationToken token)
    {
        MachineDataStorage.ValidatePath(journal);
        await using var stream = new FileStream(journal, FileMode.Open, FileAccess.Read, FileShare.Read, 4096, FileOptions.Asynchronous);
        PocketTtsModelStore.ValidateFile(journal, stream.SafeFileHandle, sharedData: true);
        if (stream.Length == 0) throw new JsonException("The download cleanup record was interrupted before its first write.");
        if (stream.Length > 65536) throw new InvalidDataException("Invalid download cleanup record size. Files were preserved.");
        using var document = await JsonDocument.ParseAsync(stream, cancellationToken: token).ConfigureAwait(false);
        string[] keys = ["SchemaVersion", "OperationId", "Kind", "StoreIdentity", "Files"];
        if (document.RootElement.ValueKind != JsonValueKind.Object ||
            !document.RootElement.EnumerateObject().Select(property => property.Name).Order(StringComparer.Ordinal).SequenceEqual(keys.Order(StringComparer.Ordinal)))
            throw new InvalidDataException("Unknown download cleanup metadata was preserved.");
        var record = document.RootElement.Deserialize<Record>() ?? throw new InvalidDataException("Invalid download cleanup record.");
        if (!string.Equals(Staging(record.Kind), Staging(kind), StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("The metadata cleanup record is in a different store.");
        Validate(record, journal, record.Kind);
        return new(journal, record);
    }

    // Validate the whole operation before deleting anything. Directory removal is
    // nonrecursive, so a new/unrecorded file is preserved even if it arrives later.
    internal Task<long> CleanupAsync()
    {
        try
        {
            Validate(_record, _journal, _record.Kind);
            var allowed = _record.Files.Select(file => Path.Combine(DirectoryPath, file.Replace('/', Path.DirectorySeparatorChar)))
                .ToHashSet(StringComparer.OrdinalIgnoreCase);
            var directories = new HashSet<string>(StringComparer.OrdinalIgnoreCase) { DirectoryPath };
            if (_record.Kind == "runtime") directories.Add(Path.Combine(DirectoryPath, "payload"));
            MachineDataStorage.ValidatePath(DirectoryPath);
            if (Directory.Exists(DirectoryPath))
            {
                var pending = new Stack<string>(); pending.Push(DirectoryPath);
                while (pending.TryPop(out string? current))
                {
                    foreach (string entry in Directory.EnumerateFileSystemEntries(current))
                    {
                        MachineDataStorage.ValidatePath(entry);
                        bool directory = (File.GetAttributes(entry) & FileAttributes.Directory) != 0;
                        if (directory)
                        {
                            if (!directories.Contains(entry)) throw new InvalidDataException("An unrecorded download directory was preserved: " + entry);
                            pending.Push(entry);
                        }
                        else
                        {
                            if (!allowed.Contains(entry)) throw new InvalidDataException("An unrecorded download file was preserved: " + entry);
                            using var file = new FileStream(entry, FileMode.Open, FileAccess.Read, FileShare.Read);
                            PocketTtsModelStore.ValidateFile(entry, file.SafeFileHandle, sharedData: true);
                        }
                    }
                }
            }
            else if (File.Exists(DirectoryPath)) throw new InvalidDataException("The download operation directory changed. It was preserved.");
            long deleted = 0;
            foreach (string file in allowed) deleted += PocketTtsModelStore.DeleteRegularFile(file, sharedData: true);
            foreach (string directory in directories.OrderByDescending(path => path.Length))
            {
                MachineDataStorage.ValidatePath(directory);
                if (Directory.Exists(directory)) Directory.Delete(directory, recursive: false);
            }
            _ = PocketTtsModelStore.DeleteRegularFile(_journal, sharedData: true);
            return Task.FromResult(deleted);
        }
        finally { _metadataLock?.Dispose(); _metadataLock = null; }
    }

    internal static bool HasPending
    {
        get
        {
            foreach (string kind in new[] { "model", "runtime" })
            {
                string staging = Staging(kind);
                MachineDataStorage.ValidatePath(staging);
                if (Directory.Exists(staging) && (Directory.EnumerateFiles(staging).Any(IsRecord) || OrphanDirectories(staging).Any())) return true;
            }
            return false;
        }
    }
    private static bool IsRecord(string path) => Path.GetExtension(path) is ".json" or ".tmp";

    private static IEnumerable<string> OrphanDirectories(string staging) => Directory.EnumerateDirectories(staging)
        .Where(directory => Regex.IsMatch(Path.GetFileName(directory), @"\A[a-f0-9]{32}\z") &&
            !Path.Exists(directory + ".json") && !Path.Exists(directory + ".tmp"));

    // Called only after validating the shared-store binding and acquiring both
    // the installation lock and metadata writer lock. The namespace, exact
    // temporary layout and independent file handles are checked before adoption.
    private static async Task<long> CleanupOrphanAsync(string directory, string kind, CancellationToken token)
    {
        MachineDataStorage.ValidatePath(directory);
        string id = Path.GetFileName(directory);
        if (Path.Exists(directory + ".json") || Path.Exists(directory + ".tmp")) return 0;
        var files = new List<string>();
        foreach (string entry in Directory.EnumerateFileSystemEntries(directory))
        {
            MachineDataStorage.ValidatePath(entry);
            string name = Path.GetFileName(entry);
            if ((File.GetAttributes(entry) & FileAttributes.Directory) == 0) files.Add(name);
            else if (kind == "runtime" && name == "payload")
            {
                foreach (string payload in Directory.EnumerateFileSystemEntries(entry))
                {
                    MachineDataStorage.ValidatePath(payload);
                    if ((File.GetAttributes(payload) & FileAttributes.Directory) != 0)
                        throw new InvalidDataException("An unrecognized temporary directory was preserved.");
                    files.Add("payload/" + Path.GetFileName(payload));
                }
            }
            else throw new InvalidDataException("An unrecognized temporary directory was preserved.");
        }
        string cleanupKind = kind;
        string[] allowed;
        if (files.SequenceEqual(MetadataFiles) && !Directory.EnumerateDirectories(directory).Any())
        {
            cleanupKind = kind == "runtime" ? "runtime-identity" : "model-inventory";
            allowed = MetadataFiles;
        }
        else if (kind == "runtime" && files.All(file => RuntimeFiles.Contains(file, StringComparer.Ordinal))) allowed = RuntimeFiles;
        else if (kind == "model" && files.Count is > 0 and <= 256 && files.All(file => Regex.IsMatch(file, @"\A[a-f0-9]{32}\.partial\z"))) allowed = files.ToArray();
        else if (files.Count == 0 && !Directory.EnumerateDirectories(directory).Any())
        {
            token.ThrowIfCancellationRequested();
            Directory.Delete(directory, recursive: false);
            return 0;
        }
        else throw new InvalidDataException("Unrecognized temporary files were preserved.");

        var leases = new List<FileStream>();
        try
        {
            foreach (string file in files)
            {
                token.ThrowIfCancellationRequested();
                string path = Path.Combine(directory, file.Replace('/', Path.DirectorySeparatorChar));
                var lease = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.None);
                leases.Add(lease);
                PocketTtsModelStore.ValidateFile(path, lease.SafeFileHandle, sharedData: true);
            }
            // Persist normal cleanup authority first, so interruption during
            // deletion is recoverable by the same path as any other operation.
            var cleanup = await BeginAsync(cleanupKind, allowed, token, id).ConfigureAwait(false);
            foreach (var lease in leases) lease.Dispose();
            leases.Clear();
            return await cleanup.CleanupAsync().ConfigureAwait(false);
        }
        finally { foreach (var lease in leases) lease.Dispose(); }
    }

    internal static async Task<Recovery> RecoverAsync(CancellationToken token)
    {
        bool work = false, pending = false; long deleted = 0; string? failure = null;
        foreach (string kind in new[] { "model", "runtime" })
        {
            string staging = Staging(kind);
            bool inactiveRuntimes = kind == "runtime" && PocketTtsRuntimeStore.HasInactiveRuntime;
            if (!Directory.Exists(staging) && !inactiveRuntimes) continue;
            MachineDataStorage.ValidatePath(staging);
            if (!inactiveRuntimes && !Directory.EnumerateFiles(staging).Any(IsRecord) && !OrphanDirectories(staging).Any()) continue;
            work = true;
            try
            {
                InstallationDataOwnership.PrepareNewStore(MachineDataStorage.Root);
                string lockPath = Path.Combine(MachineDataStorage.Root, kind == "model" ? "model-install.lock" : "runtime-update.lock");
                using var operationLock = new FileStream(lockPath, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
                string[] journals = Directory.Exists(staging)
                    ? Directory.EnumerateFiles(staging).Where(IsRecord).Order(StringComparer.Ordinal).ToArray() : [];
                if (journals.Select(Path.GetFileNameWithoutExtension).Distinct(StringComparer.OrdinalIgnoreCase).Count() != journals.Length)
                    throw new InvalidDataException("Conflicting download records were preserved.");
                foreach (string journal in journals)
                {
                    token.ThrowIfCancellationRequested();
                    PocketTtsDownloadOperation operation;
                    try { operation = await ReadAsync(journal, kind, token).ConfigureAwait(false); }
                    catch (JsonException) when (Path.GetExtension(journal) == ".tmp" &&
                        Regex.IsMatch(Path.GetFileNameWithoutExtension(journal), @"\A[a-f0-9]{32}\z") &&
                        !Path.Exists(Path.ChangeExtension(journal, null)) && !Path.Exists(Path.ChangeExtension(journal, ".json")))
                    {
                        // Journal publication precedes all payload creation. A
                        // torn temporary publication with no payload is disposable.
                        string publicationLock = Path.Combine(MachineDataStorage.Root, "metadata-write.lock");
                        using var publicationGuard = new FileStream(publicationLock, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
                        PocketTtsModelStore.ValidateFile(publicationLock, publicationGuard.SafeFileHandle, sharedData: true);
                        deleted += PocketTtsModelStore.DeleteRegularFile(journal, sharedData: true);
                        continue;
                    }
                    // Acquire the same writer guard before touching metadata
                    // payloads. Do not wait on another instance's active write.
                    using var metadataGuard = IsMetadata(operation._record.Kind)
                        ? new FileStream(Path.Combine(MachineDataStorage.Root, "metadata-write.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None)
                        : null;
                    if (metadataGuard is not null) PocketTtsModelStore.ValidateFile(Path.Combine(MachineDataStorage.Root, "metadata-write.lock"), metadataGuard.SafeFileHandle, sharedData: true);
                    deleted += await operation.CleanupAsync().ConfigureAwait(false);
                }
                if (Directory.Exists(staging) && OrphanDirectories(staging).Any())
                {
                    // Metadata writers do not take the model/runtime installation
                    // lock, so also exclude their active work before inspecting orphans.
                    string metadataLock = Path.Combine(MachineDataStorage.Root, "metadata-write.lock");
                    using var metadataGuard = new FileStream(metadataLock, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
                    PocketTtsModelStore.ValidateFile(metadataLock, metadataGuard.SafeFileHandle, sharedData: true);
                    foreach (string directory in OrphanDirectories(staging).ToArray())
                    {
                        token.ThrowIfCancellationRequested();
                        try { deleted += await CleanupOrphanAsync(directory, kind, token).ConfigureAwait(false); }
                        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or InvalidDataException)
                        { pending = true; failure ??= exception.Message; }
                    }
                }
                if (kind == "runtime") deleted += await PocketTtsRuntimeStore.CleanupInactiveAsync(token).ConfigureAwait(false);
            }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or InvalidDataException or InvalidOperationException or JsonException)
            { pending = true; failure ??= exception.Message; }
        }
        return new(work, deleted, pending, pending ? "Temporary or previous runtime cleanup could not finish. Unverified or busy files were preserved. " + failure
            : "Temporary files and inactive runtime updates were cleaned up. Installed models, the active runtime and Settings were preserved.");
    }
}
