using System;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using OutLoud.VoiceManager.Models;

namespace OutLoud.VoiceManager.Services;

public static class ImportProtocol
{
    public const uint Magic = 0x314d4c4f, Version = 3;
    public const int Limit = 65536;
    private static readonly UTF8Encoding Utf8 = new(false, true);
    public static byte[] EncodeRequest(VoiceImportRequest request)
    {
        using var stream = new MemoryStream(); using var writer = new BinaryWriter(stream, Utf8, true);
        writer.Write(Magic); writer.Write(Version); writer.Write((uint)request.Operation); writer.Write(0u);
        if (request.Operation == VoiceOperation.Remove) {
            if (request.TargetId == null || request.ExpectedHash == null || string.IsNullOrEmpty(request.ExpectedHealth) || request.SourcePath.Length != 0) throw new InvalidDataException("Removal identity is missing or a source path was supplied.");
            WriteText(writer, request.TargetId); WriteText(writer, request.DisplayName); WriteText(writer, request.ExpectedHash); WriteText(writer, request.ExpectedHealth);
        } else {
            if (request.Operation is not (VoiceOperation.Import or VoiceOperation.Replace or VoiceOperation.ImportOlv or VoiceOperation.ReplaceOlv)) throw new InvalidDataException("Unsupported voice operation.");
            if (!VoiceFileSelection.IsSupported(request.SourcePath) ||
                (request.Operation is VoiceOperation.ImportOlv or VoiceOperation.ReplaceOlv) != request.IsOlv)
                throw new InvalidDataException("Source format does not match the operation.");
            WriteText(writer, request.SourcePath); WriteText(writer, request.DisplayName);
        }
        if (request.IsReplacement) {
            if (request.TargetId == null || request.ExpectedHash == null) throw new InvalidDataException("Replacement identity is missing.");
            WriteText(writer, request.TargetId); WriteText(writer, request.ExpectedHash);
        }
        if (stream.Length > Limit) throw new InvalidDataException("Import request exceeds the protocol limit.");
        stream.Position = 12; writer.Write((uint)stream.Length - 16); return stream.ToArray();
    }
    private static void WriteText(BinaryWriter writer, string value)
    {
        if (value.Contains('\0')) throw new InvalidDataException("Invalid request text.");
        var bytes = Utf8.GetBytes(value); if (bytes.Length > 32768) throw new InvalidDataException("Import text exceeds the protocol limit.");
        writer.Write((uint)bytes.Length); writer.Write(bytes);
    }
    private static string ReadText(BinaryReader reader)
    {
        uint size = reader.ReadUInt32();
        if (size > 32768 || size > reader.BaseStream.Length - reader.BaseStream.Position) throw new InvalidDataException("Invalid helper text length.");
        string value = Utf8.GetString(reader.ReadBytes((int)size));
        if (value.Contains('\0')) throw new InvalidDataException("Invalid helper text."); return value;
    }
    public static uint ValidateHeader(byte[] header)
    {
        if (header.Length != 16 || BitConverter.ToUInt32(header, 0) != Magic || BitConverter.ToUInt32(header, 4) != Version ||
            BitConverter.ToUInt32(header, 12) > Limit - 16) throw new InvalidDataException("Invalid helper response header.");
        return BitConverter.ToUInt32(header, 8);
    }
    public static VoiceImportResult DecodeResult(byte[] bytes)
    {
        if (bytes.Length < 16 || bytes.Length > Limit) throw new InvalidDataException("Invalid helper response size.");
        uint kind = ValidateHeader(bytes[..16]);
        if (kind is < 1 or > 9 or 4 || BitConverter.ToUInt32(bytes, 12) != bytes.Length - 16) throw new InvalidDataException("Invalid helper response schema.");
        using var reader = new BinaryReader(new MemoryStream(bytes), Utf8); reader.BaseStream.Position = 16;
        string id = ReadText(reader); uint operation = reader.ReadUInt32();
        string domain = ReadText(reader), code = ReadText(reader), message = ReadText(reader),
            path = ReadText(reader), library = ReadText(reader), import = ReadText(reader), tensor = ReadText(reader);
        uint system = reader.ReadUInt32(), count = reader.ReadUInt32();
        if (count > 2048) throw new InvalidDataException("Too many duplicate IDs.");
        var ids = new string[count]; for (int i = 0; i < ids.Length; ++i) ids[i] = ReadText(reader);
        static bool ValidId(string value) => value.Length == 32 && value.All(c => c is >= '0' and <= '9' or >= 'a' and <= 'f');
        if (reader.BaseStream.Position != bytes.Length || ids.Any(value => !ValidId(value)) ||
            (operation is not (1 or 2 or 3 or 4 or 5)) || (kind is 1 or 3 && operation is not (1 or 4)) || (kind is 5 or 6 or 7 && operation is not (2 or 5)) || (kind is 8 or 9 && operation != 3) ||
            (kind != 2 || operation is not (1 or 4) ? !ValidId(id) : id.Length != 0) ||
            (kind is 1 or 5 or 6 or 8 ? domain.Length != 0 || code.Length != 0 : domain.Length == 0 || code.Length == 0))
            throw new InvalidDataException("Inconsistent helper outcome.");
        return new(kind is 1 or 5 or 6 or 8 ? ImportPhase.Succeeded : kind is 3 or 7 or 9 ? ImportPhase.Partial : ImportPhase.Failed,
            id.Length == 0 ? null : id, kind is 1 or 5 or 6 or 8 ? null : new BackendError(domain, code, message, path, library, import, tensor, system), ids, (VoiceOperation)operation, kind == 6);
    }
    public static async Task<byte[]> ReadFrameAsync(Stream pipe, CancellationToken cancellation)
    {
        var header = new byte[16]; await pipe.ReadExactlyAsync(header, cancellation).ConfigureAwait(false); ValidateHeader(header);
        int length = (int)BitConverter.ToUInt32(header, 12); var frame = new byte[16 + length]; header.CopyTo(frame, 0);
        await pipe.ReadExactlyAsync(frame.AsMemory(16), cancellation).ConfigureAwait(false); return frame;
    }
}
