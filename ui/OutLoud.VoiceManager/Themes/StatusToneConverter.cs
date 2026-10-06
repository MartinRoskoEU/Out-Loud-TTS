using System;
using System.Globalization;
using System.Windows.Data;

namespace OutLoud.VoiceManager.Themes;

public enum StatusTone { Neutral, Success, Attention, Error }

// Presentation only. Native labels/health/token semantics are never changed.
public sealed class StatusToneConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
    {
        string? status = value as string;
        return parameter as string == "library" ? status switch
        {
            "valid" => StatusTone.Success,
            "missing_voice" or "unavailable" or "replacement_pending" => StatusTone.Attention,
            "corrupt_voice" or "metadata_corrupt" or "unsupported_metadata_version" or
            "unsupported_voice_version" or "incompatible_model" or "hash_mismatch" or "unsafe_entry" => StatusTone.Error,
            _ => StatusTone.Neutral
        } : status switch
        {
            "correct" => StatusTone.Success,
            "missing" or "stale_name" or "orphan" => StatusTone.Attention,
            "wrong_path" or "unhealthy_voice" or "malformed" or "conflict" => StatusTone.Error,
            _ => StatusTone.Neutral
        };
    }
    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        => throw new NotSupportedException();
}
