using System.Reflection;
using RuntimeIdentity = OutLoud.WinUI.Services.PocketTtsRuntimeValidation.RuntimeIdentity;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace OutLoud.WinUI.Services;

public enum PocketTtsModelInstallationState { NotInstalled, InstalledUnverified, InstalledVerified, Incompatible }

public sealed record PocketTtsModelSetupEntry(string Name, PocketTtsModelInstallationState State, string Detail)
{
    public bool? IsRuntimeSupported { get; init; } = true;
    public bool CanActivate => State == PocketTtsModelInstallationState.InstalledVerified && IsRuntimeSupported == true;
    public bool CanInstall { get; init; }
    public bool CanRemove => InventoryIdentity is not null;
    public bool CanManage => CanInstall || CanRemove;
    public string? UpdateCommit { get; init; }
    public bool CanUpdate => CanActivate && InventoryIdentity is not null && UpdateCommit is not null;
    public string UpdateStatusText { get; init; } = "";
    public string UpdateStatusDetail { get; init; } = "";
    internal string? InventoryIdentity { get; init; }
    internal bool InventoryFilesVerified { get; init; }
    public string Revision { get; init; } = "";
    public string DisplayName => Name + (Revision.Length > 0 ? "\nRevision: " + Revision[..Math.Min(12, Revision.Length)] : "");
    public string DisplayDetail => Detail + (Revision.Length > 0 ? "\nRevision: " + Revision : "");
    public string InstallationHint { get; init; } = "Install supported models or remove registered private installations.";
    internal PocketTtsInstalledModel? Model { get; init; }
}

public sealed record PocketTtsModelSetupResult(bool RuntimeReady, string Message, IReadOnlyList<PocketTtsModelSetupEntry> Models,
    string? InventoryMessage = null, string? InventoryError = null);

/// <summary>Offline readiness checks; persists verified runtime identity but never changes model assets.</summary>
public sealed class PocketTtsModelSetupService
{
    public const string ValidatedRuntimeRelease = PocketTtsRuntimeValidation.ValidatedRuntimeRelease;
    public const string ValidatedRuntimeSha256 = PocketTtsRuntimeValidation.ValidatedRuntimeSha256;
    internal static string RuntimeIdentityPath => PocketTtsRuntimeValidation.RuntimeIdentityPath;


    private readonly PocketTtsCliService _cli;
    private readonly string? _runtimePath;
    private IReadOnlyDictionary<string, PocketTtsInstalledModel> _activatableModels = new Dictionary<string, PocketTtsInstalledModel>(StringComparer.Ordinal);
    public PocketTtsInstalledModel? ActiveModel { get; private set; }

    public PocketTtsModelSetupService(PocketTtsCliService cli)
    { _cli = cli; }
    internal PocketTtsModelSetupService(PocketTtsCliService cli, string runtimePath)
    { _cli = cli; _runtimePath = runtimePath; }

    public Task<PocketTtsModelSetupResult> CheckAsync(CancellationToken cancellationToken = default) =>
        Task.Run(() => CheckCoreAsync(cancellationToken), cancellationToken);

    internal static async Task CheckRuntimeIdentityAsync(string runtimePath, CancellationToken cancellationToken)
    {
        _ = await PocketTtsRuntimeStore.VerifyRuntimeAsync(runtimePath, cancellationToken).ConfigureAwait(false);
    }

    internal static async Task<RuntimeIdentity> ReadVerifiedRuntimeIdentityAsync(CancellationToken cancellationToken) =>
        (await PocketTtsRuntimeStore.ReadVerifiedActiveAsync(cancellationToken).ConfigureAwait(false)).Identity;

    internal static Task<string[]> ValidateCliAsync(PocketTtsCliService cli, bool bundled, CancellationToken cancellationToken) =>
        PocketTtsRuntimeValidation.ValidateCliAsync(cli, bundled, cancellationToken);

    private async Task<PocketTtsModelSetupResult> CheckCoreAsync(CancellationToken cancellationToken)
    {
        try
        {
            HashSet<string>? supported = null;
            string runtimeMessage = "Pocket TTS runtime ready.";
            try
            {
                string runtimePath = _runtimePath ?? PocketTtsRuntimeStore.ResolveActivePath();
                using FileStream runtimeLease = File.OpenRead(runtimePath);
                await CheckRuntimeIdentityAsync(runtimePath, cancellationToken).ConfigureAwait(false);
                var cli = _runtimePath is null ? new PocketTtsCliService(runtimePath) : _cli;
                supported = (await ValidateCliAsync(cli, !PocketTtsRuntimeStore.IsWithin(runtimePath, PocketTtsRuntimeStore.RuntimesPath), cancellationToken).ConfigureAwait(false))
                    .ToHashSet(StringComparer.Ordinal);
            }
            catch (Exception exception) when (exception is IOException or InvalidDataException or JsonException or UnauthorizedAccessException or
                InvalidOperationException or PlatformNotSupportedException or TimeoutException or FormatException)
            { runtimeMessage = exception.Message; }
            var entries = new List<PocketTtsModelSetupEntry>();
            Assembly assembly = typeof(PocketTtsModelSetupService).Assembly;
            var configurations = assembly.GetManifestResourceNames()
                .Where(name => name.Contains(".RuntimeMetadata.") && name.Contains(".configs.") && name.EndsWith(".yaml", StringComparison.Ordinal))
                .ToDictionary(name => name[(name.LastIndexOf(".configs.", StringComparison.Ordinal) + ".configs.".Length)..^5],
                    name => name, StringComparer.Ordinal);
            IReadOnlyList<PocketTtsModelInventoryEntry> inventory;
            using FileStream? modelReadLease = await PocketTtsModelStore.AcquireReadAsync(cancellationToken).ConfigureAwait(false);
            string? inventoryError = null;
            try { inventory = await new PocketTtsModelInventoryService().ReadAsync(cancellationToken).ConfigureAwait(false); }
            catch (Exception exception) when (exception is IOException or InvalidDataException or JsonException or UnauthorizedAccessException)
            {
                inventory = Array.Empty<PocketTtsModelInventoryEntry>();
                inventoryError = "The saved model inventory or pending cleanup metadata is incompatible, invalid, or unreadable. No records were changed. Resolve the metadata before installation or removal.";
            }
            var registeredModels = inventory.ToDictionary(entry => entry.Alias, StringComparer.Ordinal);
            var repositories = new Dictionary<string, JsonDocument>(StringComparer.Ordinal);
            var installationPlans = new HashSet<string>(StringComparer.Ordinal);
            var availableRevisions = new Dictionary<string,string>(StringComparer.Ordinal);
            try
            {
                foreach (string resource in assembly.GetManifestResourceNames().Where(name => name.Contains(".RuntimeMetadata.") && name.EndsWith(".json")))
                {
                    using Stream stream = assembly.GetManifestResourceStream(resource)!;
                    JsonDocument json = await JsonDocument.ParseAsync(stream, cancellationToken: cancellationToken).ConfigureAwait(false);
                    repositories.Add(json.RootElement.GetProperty("id").GetString()! + "@" + json.RootElement.GetProperty("sha").GetString()!, json);
                }
                // Only private registered installations establish installation; bundled data supplies trusted install plans only.
                var names = (supported ?? Enumerable.Empty<string>()).Concat(registeredModels.Keys)
                    .Distinct(StringComparer.Ordinal).Order(StringComparer.Ordinal);
                foreach (string name in names)
                {
                    cancellationToken.ThrowIfCancellationRequested();
                    bool? compatible = supported is null ? null : supported.Contains(name);
                    if (registeredModels.TryGetValue(name, out var registered))
                    {
                        entries.Add(await CheckRegisteredModelAsync(registered, compatible, cancellationToken).ConfigureAwait(false));
                        continue;
                    }
                    if (!configurations.TryGetValue(name, out string? resource))
                    {
                        if (compatible == true) { entries.Add(new(name, PocketTtsModelInstallationState.NotInstalled, "No trusted configuration for installing this runtime variant is available.")); }
                        continue;
                    }
                    using Stream stream = assembly.GetManifestResourceStream(resource)!;
                    using var reader = new StreamReader(stream);
                    string yaml = await reader.ReadToEndAsync(cancellationToken).ConfigureAwait(false);
                    var plan = PocketTtsModelInstallPlan.Create(name, yaml, repositories);
                    bool installable = plan is not null;
                    if (plan is not null) { availableRevisions[name] = plan.Files.Single(f => f.Asset.Weights).Revision; }
                    if (compatible == true && inventoryError is null && installable) { installationPlans.Add(name); }
                    if (compatible == true)
                    {
                        entries.Add(new(name, PocketTtsModelInstallationState.NotInstalled, installable
                            ? "This model has no registered private installation. Verified private files can be reused by Install."
                            : "Complete trusted metadata is unavailable; installation is disabled."));
                    }
                }
            }
            finally { foreach (JsonDocument metadata in repositories.Values) { metadata.Dispose(); } }
            entries = entries.Select(entry => entry with
            {
                CanInstall = entry.IsRuntimeSupported == true && installationPlans.Contains(entry.Name) &&
                    entry.State is PocketTtsModelInstallationState.NotInstalled or PocketTtsModelInstallationState.InstalledUnverified,
                Revision = entry.Revision.Length > 0 ? entry.Revision : availableRevisions.GetValueOrDefault(entry.Name, ""),
                InstallationHint = entry.CanRemove
                    ? "Confirm removal of this registration and private files no longer used by other installed models."
                    : installationPlans.Contains(entry.Name)
                    ? "Install the exact verified model files from kyutai/pocket-tts-without-voice-cloning. Verified private files will be reused; missing files download directly into private storage."
                    : "Installation is unavailable for this model or its current state."
            }).ToList();
            ApplyCheckedModels(entries);
            const string limitation = "Installed status requires a verified private installation record. Unregistered private blobs do not establish installation.";
            string inventoryMessage = supported is null
                ? "Runtime support could not be determined. Verified local files are shown as Unverified. " + limitation
                : limitation;
            return new(supported is not null, runtimeMessage, entries.AsReadOnly(), inventoryMessage, inventoryError);
        }
        catch (Exception exception) when (exception is IOException or InvalidDataException or JsonException or UnauthorizedAccessException or InvalidOperationException or PlatformNotSupportedException or TimeoutException)
        {
            ApplyCheckedModels(Array.Empty<PocketTtsModelSetupEntry>());
            return new(false, exception.Message, Array.Empty<PocketTtsModelSetupEntry>(), "Local models could not be checked. Refresh after resolving the reported error.");
        }
    }

    private static async Task<PocketTtsModelSetupEntry> CheckRegisteredModelAsync(PocketTtsModelInventoryEntry registered,
        bool? compatible, CancellationToken cancellationToken)
    {
        string inventoryIdentity = PocketTtsModelInventoryService.GetIdentity(registered);
        try
        {
            var model = PocketTtsModelInventoryService.CreateModel(registered);
            var leases = await model.OpenVerifiedAssetsAsync(cancellationToken, requireVoiceCloning: false).ConfigureAwait(false);
            try
            {
                if (compatible == true)
                {
                    if (string.IsNullOrWhiteSpace(model.Configuration))
                    { throw new InvalidDataException("The installed model's native configuration is unavailable; offline model verification is incomplete."); }
                    _ = model.LocalConfiguration();
                    return new(registered.Alias, PocketTtsModelInstallationState.InstalledVerified,
                        "Registered installation files and public model configuration verified locally.")
                    { Model = model, Revision = registered.Revision, InventoryIdentity = inventoryIdentity, InventoryFilesVerified = true };
                }
                return compatible == false
                    ? new(registered.Alias, PocketTtsModelInstallationState.Incompatible,
                        "Registered installation files are verified, but the active runtime does not support this model alias.")
                    { IsRuntimeSupported = false, Revision = registered.Revision, InventoryIdentity = inventoryIdentity, InventoryFilesVerified = true }
                    : new(registered.Alias, PocketTtsModelInstallationState.InstalledUnverified,
                        "Registered installation files are verified, but runtime compatibility could not be determined.")
                    { IsRuntimeSupported = null, Revision = registered.Revision, InventoryIdentity = inventoryIdentity, InventoryFilesVerified = true };
            }
            finally { foreach (FileStream lease in leases) { lease.Dispose(); } }
        }
        catch (Exception exception) when (exception is IOException or InvalidDataException or UnauthorizedAccessException or JsonException or
            InvalidOperationException or ArgumentException or NotSupportedException)
        {
            return new(registered.Alias, PocketTtsModelInstallationState.InstalledUnverified,
                "The registered installation could not be verified: " + exception.Message)
            { IsRuntimeSupported = compatible, Revision = registered.Revision, InventoryIdentity = inventoryIdentity };
        }
    }

    internal void ApplyCheckedModels(IReadOnlyList<PocketTtsModelSetupEntry> entries)
    {
        _activatableModels = entries.Where(entry => entry.CanActivate && entry.Model is not null)
            .ToDictionary(entry => entry.Name, entry => entry.Model!, StringComparer.Ordinal);
        if (ActiveModel is not null) { ActiveModel = _activatableModels.GetValueOrDefault(ActiveModel.Name); }
    }

    private bool IsCurrentActivatableModel(PocketTtsModelSetupEntry entry) =>
        entry.CanActivate && entry.Model is not null && _activatableModels.TryGetValue(entry.Name, out var current) && ReferenceEquals(current, entry.Model);

    internal IReadOnlyList<PocketTtsModelSetupEntry> GetSelectableModels(IEnumerable<PocketTtsModelSetupEntry> entries) =>
        entries.Where(IsCurrentActivatableModel).ToArray();

    internal void ClearActiveModel() => ActiveModel = null;

    public void Activate(PocketTtsModelSetupEntry entry)
    {
        if (!IsCurrentActivatableModel(entry))
        { throw new InvalidOperationException("Only models verified as supported by the current runtime check can be activated."); }
        ActiveModel = entry.Model;
    }

    public async Task ActivateAsync(PocketTtsModelSetupEntry entry, CancellationToken cancellationToken = default)
    {
        var supported = await ValidateCliAsync(new PocketTtsCliService(),
            !PocketTtsRuntimeStore.IsWithin(PocketTtsRuntimeStore.ResolveActivePath(), PocketTtsRuntimeStore.RuntimesPath), cancellationToken).ConfigureAwait(false);
        if (!supported.Contains(entry.Name, StringComparer.Ordinal)) { throw new InvalidOperationException("The active runtime no longer supports this model."); }
        using var operation = await PocketTtsModelStore.AcquireReadAsync(cancellationToken).ConfigureAwait(false);
        var registration = (await new PocketTtsModelInventoryService().ReadAsync(cancellationToken).ConfigureAwait(false))
            .SingleOrDefault(model => model.Alias == entry.Name);
        if (registration is null || PocketTtsModelInventoryService.GetIdentity(registration) != entry.InventoryIdentity)
        { throw new InvalidOperationException("The model registration changed. Refresh Models before activation."); }
        var leases = await entry.Model!.OpenVerifiedAssetsAsync(cancellationToken, requireVoiceCloning: false).ConfigureAwait(false);
        try { _ = entry.Model.LocalConfiguration(); Activate(entry); }
        finally { foreach (var lease in leases) { lease.Dispose(); } }
    }

    public void Deactivate(string alias)
    {
        if (ActiveModel?.Name == alias) { ActiveModel = null; }
        _activatableModels = _activatableModels.Where(pair => pair.Key != alias).ToDictionary(pair => pair.Key, pair => pair.Value, StringComparer.Ordinal);
    }

    internal static string[] SupportedLanguages(string help) => PocketTtsRuntimeValidation.SupportedLanguages(help);

}
