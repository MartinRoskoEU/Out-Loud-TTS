using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using OutLoud.VoiceManager.Models;

namespace OutLoud.VoiceManager.Services;

public interface IImportDialogService {
    Task<VoiceImportRequest?> ChooseAsync();
    Task<VoiceRecord?> ConfirmReplacementAsync(VoiceImportRequest request, IReadOnlyList<VoiceRecord> candidates);
}
public interface IVoiceImportService
{
    Task<VoiceImportResult> ImportAsync(VoiceImportRequest request, IProgress<ImportPhase> progress);
}
