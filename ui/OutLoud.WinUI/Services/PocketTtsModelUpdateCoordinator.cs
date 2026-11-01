using System.Net.Http;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace OutLoud.WinUI.Services;

/// <summary>Schedules the existing anonymous checker and caches only complete successful comparisons.</summary>
internal sealed class PocketTtsModelUpdateCoordinator(HttpClient http)
{
    private static readonly SemaphoreSlim CheckGate = new(1, 1);
    private const int MaximumBytes = 16 * 1024 * 1024;
    private sealed record CachedModel(PocketTtsModelUpdateResult Result, DateTimeOffset LastSuccessfulCheckUtc);
    private sealed record Settings(int SchemaVersion, DateTimeOffset? LastSuccessfulCheckUtc, IReadOnlyList<CachedModel> Models);
    private static Settings EmptySettings => new(1, null, Array.Empty<CachedModel>());
    internal static string SettingsPath => Path.Combine(MachineDataStorage.UpdateCache, "model-update-checks.json");

    internal Task<PocketTtsModelUpdateCheckResult> RunAsync(bool automatic, IReadOnlyDictionary<string, string> requestedIdentities,
        IProgress<PocketTtsModelUpdateResult>? progress, CancellationToken cancellationToken) => MachineDataStorage.IsAdministrator
        ? RunLocalAsync(automatic, requestedIdentities, progress, cancellationToken)
        : RunElevatedAsync(automatic, requestedIdentities, progress, cancellationToken);

    private async Task<PocketTtsModelUpdateCheckResult> RunElevatedAsync(bool automatic, IReadOnlyDictionary<string, string> requestedIdentities,
        IProgress<PocketTtsModelUpdateResult>? progress, CancellationToken cancellationToken)
    {
        // Reading a fresh shared cache never requires elevation. Only a due check publishes shared state.
        var current = await ReadIdentitiesAsync(cancellationToken).ConfigureAwait(false);
        var requested = requestedIdentities.Where(pair => current.TryGetValue(pair.Key, out var identity) && identity == pair.Value)
            .ToDictionary(pair => pair.Key, pair => pair.Value, StringComparer.Ordinal);
        if (requested.Count == 0) return new(Array.Empty<PocketTtsModelUpdateResult>(), "No registered installed models to check.");
        if (automatic)
        {
            var settings = await ReadSettingsAsync(cancellationToken).ConfigureAwait(false);
            var cached = MatchingCache(settings, requested).ToDictionary(model => model.Result.Alias, StringComparer.Ordinal);
            var now = DateTimeOffset.UtcNow;
            if (requested.Keys.All(alias => cached.TryGetValue(alias, out var entry) && now >= entry.LastSuccessfulCheckUtc &&
                now - entry.LastSuccessfulCheckUtc < TimeSpan.FromHours(24)))
                return new(cached.Values.Select(ForDisplay).ToArray());
        }
        return await ElevatedMachineDataService.RunAsync<PocketTtsModelUpdateResult, PocketTtsModelUpdateCheckResult>(
            new("check-model-updates", Automatic: automatic, ModelIdentities: requested), progress, cancellationToken).ConfigureAwait(false);
    }

    private Task<PocketTtsModelUpdateCheckResult> RunLocalAsync(bool automatic, IReadOnlyDictionary<string, string> requestedIdentities,
        IProgress<PocketTtsModelUpdateResult>? progress, CancellationToken cancellationToken) =>
        Task.Run(async () =>
        {
            if (!await CheckGate.WaitAsync(0, cancellationToken).ConfigureAwait(false))
            { return new PocketTtsModelUpdateCheckResult(Array.Empty<PocketTtsModelUpdateResult>(), "A model update check is already running."); }
            try { return await RunCoreAsync(automatic, requestedIdentities, progress, cancellationToken).ConfigureAwait(false); }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or InvalidDataException or JsonException)
            {
                return new(requestedIdentities.Select(pair => new PocketTtsModelUpdateResult(pair.Key, PocketTtsModelUpdateStatus.Unavailable,
                    "Local inventory or update settings are unavailable. Retry later.") { InstalledIdentity = pair.Value }).ToArray(),
                    "Unable to check model updates. Local inventory or update settings are unavailable.");
            }
            finally { CheckGate.Release(); }
        }, cancellationToken);

    private async Task<PocketTtsModelUpdateCheckResult> RunCoreAsync(bool automatic, IReadOnlyDictionary<string, string> requestedIdentities,
        IProgress<PocketTtsModelUpdateResult>? progress, CancellationToken cancellationToken)
    {
        var current = await ReadIdentitiesAsync(cancellationToken).ConfigureAwait(false);
        var requested = requestedIdentities.Where(pair => current.TryGetValue(pair.Key, out var identity) && identity == pair.Value)
            .ToDictionary(pair => pair.Key, pair => pair.Value, StringComparer.Ordinal);
        if (requested.Count == 0)
        { return new(Array.Empty<PocketTtsModelUpdateResult>(), "No registered installed models to check."); }

        MachineDataStorage.ValidateDirectory(MachineDataStorage.UpdateCache);
        FileStream checkLock;
        try { checkLock = new(Path.Combine(MachineDataStorage.UpdateCache, "model-update-checks.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None); }
        catch (IOException exception) when ((exception.HResult & 0xFFFF) is 32 or 33)
        {
            var existing = await ReadSettingsAsync(cancellationToken).ConfigureAwait(false);
            return new(MatchingCache(existing, requested).Select(ForDisplay).ToArray(), "Another application instance is checking model updates.");
        }
        using (checkLock)
        {
            Settings settings = await ReadSettingsAsync(cancellationToken).ConfigureAwait(false);
            DateTimeOffset now = DateTimeOffset.UtcNow;
            var cached = MatchingCache(settings, requested).ToDictionary(model => model.Result.Alias, StringComparer.Ordinal);
            foreach (var model in cached.Values) { progress?.Report(ForDisplay(model)); }
            var due = requested.Where(pair => !automatic || !cached.TryGetValue(pair.Key, out var model) ||
                    now - model.LastSuccessfulCheckUtc >= TimeSpan.FromHours(24) || now < model.LastSuccessfulCheckUtc)
                .ToDictionary(pair => pair.Key, pair => pair.Value, StringComparer.Ordinal);
            if (due.Count == 0) { return new(cached.Values.Select(ForDisplay).ToArray()); }

            // The existing service owns all local integrity checks and remote comparison logic.
            var comparison = await new PocketTtsModelUpdateService(http).CheckAsync(progress, cancellationToken, due).ConfigureAwait(false);
            current = await ReadIdentitiesAsync(cancellationToken).ConfigureAwait(false);
            var compared = comparison.Models.Where(model => due.TryGetValue(model.Alias, out var expected) && model.InstalledIdentity == expected &&
                    current.TryGetValue(model.Alias, out var actual) && actual == expected)
                .ToDictionary(model => model.Alias, StringComparer.Ordinal);
            bool complete = due.All(pair => current.TryGetValue(pair.Key, out var actual) && actual == pair.Value &&
                compared.TryGetValue(pair.Key, out var model) && Successful(model));
            var displayed = cached.Values.Where(model => !due.ContainsKey(model.Result.Alias) &&
                    current.TryGetValue(model.Result.Alias, out var identity) && identity == model.Result.InstalledIdentity)
                .Select(ForDisplay).ToDictionary(model => model.Alias, StringComparer.Ordinal);
            foreach (var model in compared.Values) { displayed[model.Alias] = model; }
            foreach (var pair in due.Where(pair => !compared.ContainsKey(pair.Key) &&
                current.TryGetValue(pair.Key, out var identity) && identity == pair.Value))
            {
                displayed[pair.Key] = new(pair.Key, PocketTtsModelUpdateStatus.Unavailable,
                    "The model update check could not complete. Retry later.") { InstalledIdentity = pair.Value };
            }
            string? message = comparison.Message;
            if (complete)
            {
                DateTimeOffset successfulUtc = DateTimeOffset.UtcNow;
                var retained = settings.Models.Where(model => current.TryGetValue(model.Result.Alias, out var identity) &&
                    identity == model.Result.InstalledIdentity && !due.ContainsKey(model.Result.Alias));
                var completed = compared.Values.Select(model => new CachedModel(model, successfulUtc)).ToArray();
                var next = new Settings(1, successfulUtc, retained.Concat(completed).OrderBy(model => model.Result.Alias, StringComparer.Ordinal).ToArray());
                try
                {
                    await SaveSettingsAsync(next, cancellationToken).ConfigureAwait(false);
                    foreach (var model in completed) { displayed[model.Result.Alias] = ForDisplay(model); }
                }
                catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or InvalidDataException)
                { message = "Model comparisons completed, but their check times could not be saved. Automatic checks may be retried."; }
            }
            // A failed/incomplete batch does not change any persisted successful timestamp or cache entry.
            else { message ??= "Some model updates could not be checked. No successful check time was saved."; }
            return new(displayed.Values.ToArray(), message);
        }
    }

    private static bool Successful(PocketTtsModelUpdateResult result) =>
        result.Status is PocketTtsModelUpdateStatus.UpToDate or PocketTtsModelUpdateStatus.UpdateAvailable;

    private static IEnumerable<CachedModel> MatchingCache(Settings settings, IReadOnlyDictionary<string, string> identities) =>
        settings.Models.Where(model => identities.TryGetValue(model.Result.Alias, out var identity) && identity == model.Result.InstalledIdentity);

    private static PocketTtsModelUpdateResult ForDisplay(CachedModel model) => model.Result with
    { Message = model.Result.Message + $" Last successfully checked: {model.LastSuccessfulCheckUtc:yyyy-MM-dd HH:mm:ss} UTC." };

    private static async Task<Dictionary<string, string>> ReadIdentitiesAsync(CancellationToken cancellationToken)
    {
        var inventory = new PocketTtsModelInventoryService();
        var pending = (await inventory.ReadPendingAsync(cancellationToken).ConfigureAwait(false)).Select(job => job.Alias).ToHashSet(StringComparer.Ordinal);
        return (await inventory.ReadAsync(cancellationToken).ConfigureAwait(false)).Where(entry => !pending.Contains(entry.Alias))
            .ToDictionary(entry => entry.Alias, PocketTtsModelInventoryService.GetIdentity, StringComparer.Ordinal);
    }

    internal static async Task InvalidateAsync(string alias, CancellationToken cancellationToken)
    {
        if (!await CheckGate.WaitAsync(0, cancellationToken).ConfigureAwait(false))
        { throw new InvalidOperationException("Model update results are busy; invalidation remains pending."); }
        try
        {
            using var checkLock = new FileStream(Path.Combine(MachineDataStorage.UpdateCache, "model-update-checks.lock"),
                FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
            var settings = await ReadSettingsAsync(cancellationToken).ConfigureAwait(false);
            await SaveSettingsAsync(settings with { Models = settings.Models.Where(model => model.Result.Alias != alias).ToArray() }, cancellationToken).ConfigureAwait(false);
        }
        finally { CheckGate.Release(); }
    }

    private static async Task<Settings> ReadSettingsAsync(CancellationToken cancellationToken)
    {
        try
        {
            await using var stream = new FileStream(SettingsPath, FileMode.Open, FileAccess.Read,
                FileShare.Read | FileShare.Delete, 65536, FileOptions.Asynchronous);
            if (stream.Length > MaximumBytes) { return EmptySettings; }
            var settings = await JsonSerializer.DeserializeAsync<Settings>(stream, cancellationToken: cancellationToken).ConfigureAwait(false);
            if (settings is null || settings.SchemaVersion != 1 || settings.Models is null || settings.Models.Count > 4096 ||
                settings.LastSuccessfulCheckUtc is not { Offset: var offset } last || offset != TimeSpan.Zero || last > DateTimeOffset.UtcNow)
            { return EmptySettings; }
            var aliases = new HashSet<string>(StringComparer.Ordinal);
            foreach (var model in settings.Models)
            {
                if (model?.Result is not { } result || string.IsNullOrEmpty(result.Alias) || !aliases.Add(result.Alias) || !Successful(result) ||
                    !Hash(result.InstalledIdentity, 64) || !Hash(result.ComparedCommit, 40) || result.Message is null || result.Message.Length > 4096 ||
                    model.LastSuccessfulCheckUtc.Offset != TimeSpan.Zero || model.LastSuccessfulCheckUtc > last)
                { return EmptySettings; }
            }
            return settings;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or JsonException or InvalidDataException)
        { return EmptySettings; }
    }

    private static bool Hash(string? value, int length) => value is not null && value.Length == length && Regex.IsMatch(value, @"\A[a-fA-F0-9]+\z");

    private static async Task SaveSettingsAsync(Settings settings, CancellationToken cancellationToken)
    {
        if (JsonSerializer.SerializeToUtf8Bytes(settings).Length > MaximumBytes) { throw new InvalidDataException("Model update cache is too large."); }
        var temporaryOperation = await PocketTtsDownloadOperation.BeginMetadataAsync("model-update-checks", cancellationToken).ConfigureAwait(false);
        string temporary = temporaryOperation.MetadataPath;
        try
        {
            await PocketTtsRuntimeStore.WriteJsonAsync(temporary, settings, cancellationToken).ConfigureAwait(false);
            cancellationToken.ThrowIfCancellationRequested();
            if (File.Exists(SettingsPath))
            { File.Replace(temporary, SettingsPath, Path.Combine(MachineDataStorage.UpdateCache, "model-update-checks.previous.json")); }
            else { File.Move(temporary, SettingsPath); }
        }
        finally
        {
            try { _ = await temporaryOperation.CleanupAsync().ConfigureAwait(false); }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or InvalidDataException) { }
        }
    }
}
