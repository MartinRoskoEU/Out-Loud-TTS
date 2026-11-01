using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace OutLoud.WinUI.Services;

internal sealed record PocketTtsCachedAsset(string Reference, string Path, long Size, string Digest, bool Lfs, bool Weights);

/// <summary>Verified native model assets in private application-managed storage.</summary>
public sealed class PocketTtsInstalledModel
{
    internal PocketTtsInstalledModel(string name, string configuration, IReadOnlyList<PocketTtsCachedAsset> assets)
    { Name = name; Configuration = configuration; Assets = assets; }
    public string Name { get; }
    internal string Configuration { get; }
    internal IReadOnlyList<PocketTtsCachedAsset> Assets { get; }
    internal string? InventoryIdentity { get; init; }

    internal async Task<IReadOnlyList<FileStream>> OpenVerifiedAssetsAsync(CancellationToken cancellationToken, bool requireVoiceCloning = false)
    {
        var streams = new List<FileStream>();
        try
        {
            foreach (PocketTtsCachedAsset asset in Assets)
            { streams.Add(await OpenVerifiedAssetAsync(asset, cancellationToken, requireVoiceCloning).ConfigureAwait(false)); }
            return streams;
        }
        catch { foreach (FileStream stream in streams) { stream.Dispose(); } throw; }
    }

    internal static async Task<FileStream> OpenVerifiedAssetAsync(PocketTtsCachedAsset asset, CancellationToken cancellationToken,
        bool requireVoiceCloning = false)
    {
        PocketTtsModelStore.ValidatePath(asset.Path);
        var stream = new FileStream(asset.Path, FileMode.Open, FileAccess.Read, FileShare.Read, 65536, FileOptions.Asynchronous);
        try
        {
            PocketTtsModelStore.ValidateFile(asset.Path, stream.SafeFileHandle);
            if (stream.Length != asset.Size) { throw new InvalidDataException("A private model asset has changed size. Return to Model Setup and check again."); }
            string digest;
            if (asset.Lfs)
            { digest = Convert.ToHexString(await SHA256.HashDataAsync(stream, cancellationToken).ConfigureAwait(false)); }
            else
            {
                using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA1);
                hash.AppendData(Encoding.ASCII.GetBytes($"blob {stream.Length}\0"));
                byte[] buffer = new byte[65536];
                int count;
                while ((count = await stream.ReadAsync(buffer, cancellationToken).ConfigureAwait(false)) > 0)
                { hash.AppendData(buffer.AsSpan(0, count)); }
                digest = Convert.ToHexString(hash.GetHashAndReset());
            }
            if (!digest.Equals(asset.Digest, StringComparison.OrdinalIgnoreCase))
            { throw new InvalidDataException("A private model asset does not match its official pinned revision. It remains unverified."); }
            if (asset.Weights)
            {
                stream.Position = 0;
                byte[] length = new byte[8];
                await stream.ReadExactlyAsync(length, cancellationToken).ConfigureAwait(false);
                ulong headerLength = System.Buffers.Binary.BinaryPrimitives.ReadUInt64LittleEndian(length);
                if (headerLength > 16 * 1024 * 1024 || headerLength > (ulong)Math.Max(0, stream.Length - 8))
                { throw new InvalidDataException("Invalid model tensor header."); }
                byte[] header = new byte[(int)headerLength];
                await stream.ReadExactlyAsync(header, cancellationToken).ConfigureAwait(false);
                using JsonDocument tensors = JsonDocument.Parse(header);
                if (tensors.RootElement.ValueKind != JsonValueKind.Object ||
                    !tensors.RootElement.EnumerateObject().Any(t => t.Name.StartsWith("flow_lm.", StringComparison.Ordinal)) ||
                    !tensors.RootElement.EnumerateObject().Any(t => t.Name.StartsWith("mimi.decoder.", StringComparison.Ordinal)))
                { throw new InvalidDataException("The private weights do not contain the Pocket TTS language model and decoder."); }
                long payloadBytes = stream.Length - 8 - (long)headerLength;
                var regions = new List<(long Start, long End)>();
                var names = new HashSet<string>(StringComparer.Ordinal);
                foreach (var tensor in tensors.RootElement.EnumerateObject())
                {
                    if (!names.Add(tensor.Name)) { throw new InvalidDataException("Duplicate model tensor name."); }
                    if (tensor.Name == "__metadata__") { continue; }
                    var info = tensor.Value;
                    if (!info.TryGetProperty("data_offsets", out var offsets) || offsets.GetArrayLength() != 2 ||
                        !info.TryGetProperty("shape", out var shape) || shape.ValueKind != JsonValueKind.Array ||
                        !info.TryGetProperty("dtype", out var dtype)) { throw new InvalidDataException("Invalid model tensor metadata."); }
                    int elementBytes = dtype.GetString() switch
                    { "F64" or "I64" or "U64" => 8, "F32" or "I32" or "U32" => 4, "F16" or "BF16" or "I16" or "U16" => 2, "I8" or "U8" or "BOOL" => 1, _ => throw new InvalidDataException("Unsupported model tensor dtype.") };
                    long elements = 1;
                    foreach (var dimension in shape.EnumerateArray())
                    { long size = dimension.GetInt64(); if (size < 0) { throw new InvalidDataException("Invalid tensor dimension."); } elements = checked(elements * size); }
                    long start = offsets[0].GetInt64(), end = offsets[1].GetInt64();
                    if (start < 0 || end < start || end > payloadBytes || end - start != checked(elements * elementBytes))
                    { throw new InvalidDataException("Model tensor offsets and shape disagree."); }
                    regions.Add((start, end));
                }
                long position = 0;
                foreach (var region in regions.OrderBy(r => r.Start).ThenBy(r => r.End))
                { if (region.Start != position) { throw new InvalidDataException("Model tensor payload has overlapping regions or gaps."); } position = region.End; }
                if (position != payloadBytes) { throw new InvalidDataException("Model tensor payload is incomplete."); }
            }
            return stream;
        }
        catch (Exception exception) when (exception is OverflowException or InvalidOperationException or FormatException or KeyNotFoundException or IndexOutOfRangeException)
        { stream.Dispose(); throw new InvalidDataException("The model tensor metadata is invalid.", exception); }
        catch { stream.Dispose(); throw; }
    }

    internal string LocalConfiguration()
    {
        string yaml = System.Text.RegularExpressions.Regex.Replace(Configuration,
            @"(?m)^\s*weights_path_without_voice_cloning:.*\r?\n?", "");
        var declarations = System.Text.RegularExpressions.Regex.Matches(yaml,
            @"(?m)^\s*(?<key>[A-Za-z0-9_]*_path)\s*:\s*(?<value>[^\r\n]*)");
        var used = new HashSet<string>(StringComparer.Ordinal);
        foreach (System.Text.RegularExpressions.Match declaration in declarations)
        {
            string reference = declaration.Groups["value"].Value.Trim();
            var asset = Assets.SingleOrDefault(asset => asset.Reference == reference);
            if (asset is null || (declaration.Groups["key"].Value == "weights_path" && !asset.Weights))
            { throw new InvalidDataException("Every native model file path must reference an inventoried private asset."); }
            used.Add(reference);
        }
        if (Assets.Count == 0 || Assets.Any(asset => !used.Contains(asset.Reference)))
        { throw new InvalidDataException("The native configuration does not describe every required private asset."); }
        foreach (PocketTtsCachedAsset asset in Assets)
        {
            PocketTtsModelStore.ValidatePath(asset.Path);
            yaml = yaml.Replace(asset.Reference, "'" + asset.Path.Replace("'", "''") + "'", StringComparison.Ordinal);
        }
        string activeLines = string.Join('\n', yaml.Split('\n').Where(line => !line.TrimStart().StartsWith('#')));
        if (activeLines.Contains("hf://", StringComparison.Ordinal) || activeLines.Contains("https://", StringComparison.Ordinal) ||
            activeLines.Contains("http://", StringComparison.Ordinal))
        { throw new InvalidDataException("This configuration has unresolved remote assets and cannot be used as an installed offline model."); }
        return yaml;
    }
}
