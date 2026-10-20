using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
using OutLoud.VoiceManager.Models;

namespace OutLoud.VoiceManager.NativeInterop;

internal static class NativeVoiceManager
{
    private const string Library = "OutLoudVoiceManagerNative.dll";
    static NativeVoiceManager() => NativeLibrary.SetDllImportResolver(typeof(NativeVoiceManager).Assembly, Resolve);

    private static IntPtr Resolve(string name, Assembly assembly, DllImportSearchPath? searchPath)
    {
        if (name != Library) return IntPtr.Zero;
        if (!Environment.Is64BitProcess)
            throw new BadImageFormatException("The Voice Manager requires an x64 process.");
        return NativeLibrary.Load(Path.Combine(AppContext.BaseDirectory, Library), assembly,
            DllImportSearchPath.UseDllDirectoryForDependencies | DllImportSearchPath.System32);
    }

    public static VoiceSnapshot Read()
    {
        try
        {
            if (AbiVersion() != 1) throw Failure("abi_mismatch", "The native bridge ABI version is unsupported.");
            int status = ReadResult(out IntPtr pointer);
            using var result = new ResultHandle(pointer);
            if (status != 0)
            {
                if (status == 1 && !result.IsInvalid)
                {
                    IntPtr error = ResultError(result);
                    if (error != IntPtr.Zero) throw new BackendException(CopyError(error));
                }
                throw Failure(status switch { 2 => "invalid_argument", 3 => "out_of_memory", 4 => "internal_failure", _ => "protocol_error" },
                    status == 3 ? "The native bridge ran out of memory." : "The native bridge could not return a voice snapshot.");
            }
            if (result.IsInvalid) throw Failure("protocol_error", "The native bridge returned no result.");
            IntPtr snapshotPointer = ResultSnapshot(result);
            if (snapshotPointer == IntPtr.Zero) throw Failure("protocol_error", "The native bridge returned no snapshot.");
            var snapshot = Marshal.PtrToStructure<SnapshotV1>(snapshotPointer);
            if (snapshot.Count > int.MaxValue || (snapshot.Count != 0 && snapshot.Voices == IntPtr.Zero))
                throw Failure("protocol_error", "The native voice array is invalid.");
            var rows = new List<VoiceRecord>(checked((int)snapshot.Count));
            int size = Marshal.SizeOf<VoiceV1>();
            for (int index = 0; index < (int)snapshot.Count; ++index)
            {
                var row = Marshal.PtrToStructure<VoiceV1>(IntPtr.Add(snapshot.Voices, checked(index * size)));
                IntPtr contentPointer = ResultContent(result, (ulong)index);
                if (contentPointer == IntPtr.Zero) throw Failure("protocol_error", "Missing native content metadata.");
                var content = Marshal.PtrToStructure<ContentV1>(contentPointer);
                if (row.MetadataPresent > 1 || row.ReservedMetadata != 0)
                    throw Failure("protocol_error", "The native metadata flags are invalid.");
                rows.Add(new VoiceRecord(Required(row.LibraryId), Optional(row.DisplayName), Required(row.Health),
                    Required(row.LibraryDetail), Optional(row.SapiStatus), Optional(row.SapiDetail) ?? "",
                    Optional(row.CompatibilityId), Optional(row.ImportKind),
                    row.MetadataPresent == 1 ? row.OlvVersion : null,
                    row.SapiError == IntPtr.Zero ? null : CopyError(row.SapiError),
                    Optional(content.OlvSha256), Optional(content.SourceSha256), Optional(content.SourceFilename), Optional(content.ImportedUtc)));
            }
            return new VoiceSnapshot(Required(snapshot.LibraryRoot), rows);
        }
        catch (DllNotFoundException)
        { throw Failure("bridge_not_found", "The native bridge or a required VC runtime could not be loaded. Rebuild the developer output."); }
        catch (BadImageFormatException)
        { throw Failure("architecture_mismatch", "The native bridge and Voice Manager must both be Windows x64."); }
        catch (EntryPointNotFoundException)
        { throw Failure("entry_point_missing", "The native bridge exports do not match this Voice Manager build."); }
    }

    private static void RequireExportCapability()
    {
        try { if (AbiVersion() != 1 || (Capabilities() & 1) == 0) throw Failure("export_unsupported", "Rebuild the native bridge with OLV export support."); }
        catch (EntryPointNotFoundException) { throw Failure("export_unsupported", "This native bridge supports listing only. Rebuild the Voice Manager output."); }
        catch (DllNotFoundException) { throw Failure("bridge_not_found", "The native bridge or a required VC runtime could not be loaded."); }
        catch (BadImageFormatException) { throw Failure("architecture_mismatch", "The native bridge and Voice Manager must be x64."); }
    }
    private static ResultHandle ExportResult(int status, IntPtr pointer)
    {
        var result = new ResultHandle(pointer);
        if (status == 0 && !result.IsInvalid) return result;
        try {
            if (status == 1 && !result.IsInvalid && ResultError(result) is var error && error != IntPtr.Zero)
                throw new BackendException(CopyError(error));
            throw Failure(status == 3 ? "out_of_memory" : "export_failure", "Native voice export could not complete.");
        } finally { result.Dispose(); }
    }
    public static string InspectExportDestination(string path)
    {
        RequireExportCapability();
        int status = ExportDestination(path, out IntPtr pointer);
        using var result = ExportResult(status, pointer);
        return Required(ResultDestination(result));
    }
    public static void Export(VoiceRecord voice, VoiceExportRequest request)
    {
        RequireExportCapability();
        int status = ExportVoice(voice.LibraryId, voice.DisplayName ?? "", voice.OlvSha256 ?? "", request.DestinationPath,
            request.ExpectedDestination, out IntPtr pointer);
        using var result = ExportResult(status, pointer);
    }
    [DllImport(Library, EntryPoint = "outloud_vm_capabilities", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    private static extern uint Capabilities();
    [DllImport(Library, EntryPoint = "outloud_vm_export_destination", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    private static extern int ExportDestination([MarshalAs(UnmanagedType.LPUTF8Str)] string destination, out IntPtr result);
    [DllImport(Library, EntryPoint = "outloud_vm_result_destination", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    private static extern IntPtr ResultDestination(ResultHandle result);
    [DllImport(Library, EntryPoint = "outloud_vm_export", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    private static extern int ExportVoice([MarshalAs(UnmanagedType.LPUTF8Str)] string id, [MarshalAs(UnmanagedType.LPUTF8Str)] string name,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string hash, [MarshalAs(UnmanagedType.LPUTF8Str)] string destination,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string expectedDestination, out IntPtr result);

    private static string Required(IntPtr pointer) => Optional(pointer) ??
        throw Failure("protocol_error", "A required native string is missing.");
    private static string? Optional(IntPtr pointer) => pointer == IntPtr.Zero ? null : Marshal.PtrToStringUTF8(pointer);
    private static BackendException Failure(string code, string message) => new(new BackendError("bridge", code, message));
    private static BackendError CopyError(IntPtr pointer)
    {
        var error = Marshal.PtrToStructure<ErrorV1>(pointer);
        return new BackendError(Required(error.Domain), Required(error.Code), Required(error.Message),
            Required(error.Path), Required(error.LibraryCode), Required(error.ImportCode), Required(error.Tensor), error.SystemError);
    }

    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal struct SnapshotV1 { public IntPtr LibraryRoot; public ulong Count; public IntPtr Voices; }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal struct ContentV1 { public IntPtr OlvSha256, SourceSha256, SourceFilename, ImportedUtc; }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal struct VoiceV1
    {
        public IntPtr LibraryId, DisplayName, Health, LibraryDetail, SapiStatus, SapiDetail, CompatibilityId, ImportKind, SapiError;
        public uint MetadataPresent, OlvVersion, ReservedMetadata, Reserved;
    }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    internal struct ErrorV1
    {
        public IntPtr Domain, Code, Message, Path, LibraryCode, ImportCode, Tensor;
        public uint SystemError, Reserved;
    }
    private sealed class ResultHandle : SafeHandleZeroOrMinusOneIsInvalid
    {
        public ResultHandle(IntPtr pointer) : base(true) => SetHandle(pointer);
        protected override bool ReleaseHandle() { FreeResult(handle); return true; }
    }
    [DllImport(Library, EntryPoint = "outloud_vm_abi_version", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    private static extern uint AbiVersion();
    [DllImport(Library, EntryPoint = "outloud_vm_read", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    private static extern int ReadResult(out IntPtr result);
    [DllImport(Library, EntryPoint = "outloud_vm_result_snapshot", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    private static extern IntPtr ResultSnapshot(ResultHandle result);
    [DllImport(Library, EntryPoint = "outloud_vm_result_content", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    private static extern IntPtr ResultContent(ResultHandle result, ulong row);
    [DllImport(Library, EntryPoint = "outloud_vm_result_error", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    private static extern IntPtr ResultError(ResultHandle result);
    [DllImport(Library, EntryPoint = "outloud_vm_result_free", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    private static extern void FreeResult(IntPtr result);
}
