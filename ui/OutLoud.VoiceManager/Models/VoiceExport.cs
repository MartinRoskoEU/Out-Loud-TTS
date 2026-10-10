using System;
using System.IO;
using System.Linq;

namespace OutLoud.VoiceManager.Models;

public static class VoiceFileSelection
{
    public const string ImportFilter = "Supported Voice Files (*.safetensors;*.olv)|*.safetensors;*.olv|Prepared Pocket Voice (*.safetensors)|*.safetensors|Out Loud Voice (*.olv)|*.olv";
    public static bool IsOlv(string path) => string.Equals(Path.GetExtension(path), ".olv", StringComparison.OrdinalIgnoreCase);
    public static bool IsSupported(string path) => IsOlv(path) || string.Equals(Path.GetExtension(path), ".safetensors", StringComparison.OrdinalIgnoreCase);
    public static string ExportFileName(string name)
    {
        var invalid = Path.GetInvalidFileNameChars();
        string stem = new(name.Select(c => invalid.Contains(c) ? '_' : c).ToArray());
        stem = stem.Trim().TrimEnd('.', ' ');
        if (stem.Length > 120) stem = stem[..(char.IsHighSurrogate(stem[119]) ? 119 : 120)];
        if (stem.Length == 0) stem = "voice";
        string device = stem.Split('.')[0].ToUpperInvariant();
        if (device is "CON" or "PRN" or "AUX" or "NUL" ||
            device.Length == 4 && (device.StartsWith("COM", StringComparison.Ordinal) || device.StartsWith("LPT", StringComparison.Ordinal)) && device[3] is >= '1' and <= '9') stem = "voice-" + stem;
        return stem + ".olv";
    }
}
public sealed record VoiceExportRequest(string DestinationPath, string ExpectedDestination);
