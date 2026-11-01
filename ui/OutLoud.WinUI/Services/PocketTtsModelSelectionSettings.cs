using System.Text.Json;
using System.Text.RegularExpressions;

namespace OutLoud.WinUI.Services;

/// <summary>Only a saved usable choice is restored; absent or invalidated choices require explicit selection.</summary>
internal sealed class PocketTtsModelSelectionSettings
{
    private sealed record Settings(int SchemaVersion, string? SelectedModelAlias);
    private readonly string _path;

    internal PocketTtsModelSelectionSettings() : this(Path.Combine(PocketTtsRuntimeStore.LocalDataPath, "model-selection.json")) { }
    internal PocketTtsModelSelectionSettings(string path) => _path = path;

    internal async Task<PocketTtsModelSetupEntry?> GetStartupModelAsync(
        IReadOnlyList<PocketTtsModelSetupEntry> usableModels, CancellationToken cancellationToken = default)
    {
        var settings = await ReadAsync(cancellationToken).ConfigureAwait(false);
        var selected = settings is null ? null : usableModels.FirstOrDefault(entry => entry.Name == settings.SelectedModelAlias);
        if (settings?.SelectedModelAlias is not null && selected is null)
        { await SaveAsync(null, cancellationToken, settings.SelectedModelAlias).ConfigureAwait(false); }
        return selected;
    }

    internal async Task ForgetRemovedModelAsync(string alias, string? activeAlias, CancellationToken cancellationToken = default)
    {
        // Also clear a saved alias that is currently unavailable and therefore has no active model.
        if (activeAlias == alias || (await ReadAsync(cancellationToken).ConfigureAwait(false))?.SelectedModelAlias == alias)
        { await SaveAsync(null, cancellationToken, alias).ConfigureAwait(false); }
    }

    private async Task<Settings?> ReadAsync(CancellationToken cancellationToken)
    {
        try
        {
            await using var stream = new FileStream(_path, FileMode.Open, FileAccess.Read,
                FileShare.Read | FileShare.Delete, 4096, FileOptions.Asynchronous);
            if (stream.Length > 4096) { throw new InvalidDataException("Saved model selection is too large."); }
            var settings = await JsonSerializer.DeserializeAsync<Settings>(stream, cancellationToken: cancellationToken).ConfigureAwait(false);
            if (settings is null || settings.SchemaVersion != 1 || !ValidAlias(settings.SelectedModelAlias))
            { throw new InvalidDataException("Saved model selection is invalid or incompatible."); }
            return settings;
        }
        catch (FileNotFoundException) { return null; }
        catch (DirectoryNotFoundException) { return null; }
    }

    internal async Task SaveAsync(string? alias, CancellationToken cancellationToken = default, string? expectedAlias = null)
    {
        if (!ValidAlias(alias)) { throw new ArgumentException("A valid model alias is required.", nameof(alias)); }
        if (string.Equals(_path, Path.Combine(MachineDataStorage.Root, "model-selection.json"), StringComparison.OrdinalIgnoreCase) &&
            !MachineDataStorage.IsAdministrator)
        {
            _ = await ElevatedMachineDataService.RunAsync<string, bool>(new("select-model", alias, ExpectedSelection: expectedAlias), null, cancellationToken).ConfigureAwait(false);
            return;
        }
        string directory = Path.GetDirectoryName(_path)!;
        if (string.Equals(directory, PocketTtsRuntimeStore.LocalDataPath, StringComparison.OrdinalIgnoreCase))
            InstallationDataOwnership.PrepareNewStore(directory);
        Directory.CreateDirectory(directory);
        // Serialize shared choices and re-check conditional invalidation after elevation.
        using var selectionLock = new FileStream(Path.Combine(directory, "model-selection.lock"),
            FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
        if (expectedAlias is not null && (await ReadAsync(cancellationToken).ConfigureAwait(false))?.SelectedModelAlias != expectedAlias) return;
        var temporaryOperation = string.Equals(directory, MachineDataStorage.Root, StringComparison.OrdinalIgnoreCase)
            ? await PocketTtsDownloadOperation.BeginMetadataAsync("model-selection", cancellationToken).ConfigureAwait(false) : null;
        string temporary = temporaryOperation?.MetadataPath ?? Path.Combine(directory, $".model-selection-{Guid.NewGuid():N}.tmp");
        try
        {
            await PocketTtsRuntimeStore.WriteJsonAsync(temporary, new Settings(1, alias), cancellationToken).ConfigureAwait(false);
            cancellationToken.ThrowIfCancellationRequested();
            if (File.Exists(_path)) { File.Replace(temporary, _path, null); }
            else { File.Move(temporary, _path); }
        }
        finally
        {
            try
            {
                if (temporaryOperation is not null) _ = await temporaryOperation.CleanupAsync().ConfigureAwait(false);
                else File.Delete(temporary);
            }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or InvalidDataException) { }
        }
    }

    private static bool ValidAlias(string? alias) => alias is null || Regex.IsMatch(alias, @"\A[A-Za-z0-9][A-Za-z0-9_.-]{0,127}\z");
}
