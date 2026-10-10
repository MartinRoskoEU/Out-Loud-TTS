using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using System.Windows;
using Microsoft.Win32;
using OutLoud.VoiceManager.Models;
using OutLoud.VoiceManager.Views;

namespace OutLoud.VoiceManager.Services;

public sealed class ImportDialogService : IImportDialogService
{
    private readonly Action<Window>? attachTheme;
    public ImportDialogService(Action<Window>? attachTheme = null) => this.attachTheme = attachTheme;
    public Task<VoiceImportRequest?> ChooseAsync()
    {
        var picker = new OpenFileDialog { Title = "Select a voice file", Filter = VoiceFileSelection.ImportFilter, FilterIndex = 1,
            CheckFileExists = true, Multiselect = false };
        var owner = Application.Current.MainWindow;
        if (picker.ShowDialog(owner) != true) return Task.FromResult<VoiceImportRequest?>(null);
        var dialog = new ImportVoiceDialog(picker.FileName) { Owner = owner };
        attachTheme?.Invoke(dialog);
        return Task.FromResult(dialog.ShowDialog() == true ? dialog.Model.Confirm() : null);
    }
    public Task<VoiceRecord?> ConfirmReplacementAsync(VoiceImportRequest request, IReadOnlyList<VoiceRecord> candidates)
    {
        var model = new ReplacementDialogModel(request, candidates);
        var dialog = new ReplaceVoiceDialog(model) { Owner = Application.Current.MainWindow };
        attachTheme?.Invoke(dialog);
        return Task.FromResult(dialog.ShowDialog() == true ? model.SelectedTarget : null);
    }
    public Task<bool> ConfirmRemovalAsync(VoiceRecord target)
    {
        var dialog = new RemoveVoiceDialog(new RemovalDialogModel(target)) { Owner = Application.Current.MainWindow };
        attachTheme?.Invoke(dialog);
        return Task.FromResult(dialog.ShowDialog() == true);
    }
}
