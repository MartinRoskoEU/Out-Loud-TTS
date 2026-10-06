using System;
using System.Threading.Tasks;
using System.Windows.Input;

namespace OutLoud.VoiceManager.ViewModels;

public sealed class RefreshCommand : ICommand
{
    private readonly Func<Task> execute;
    private readonly Func<bool> canExecute;
    public RefreshCommand(Func<Task> execute, Func<bool> canExecute)
    { this.execute = execute; this.canExecute = canExecute; }
    public event EventHandler? CanExecuteChanged;
    public bool CanExecute(object? parameter) => canExecute();
    public async void Execute(object? parameter)
    { if (CanExecute(parameter)) await execute(); }
    internal void Notify() => CanExecuteChanged?.Invoke(this, EventArgs.Empty);
}
