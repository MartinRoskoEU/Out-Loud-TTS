namespace OutLoud.WinUI.Services;

public sealed record PocketTtsCliResult(
    string ExecutablePath,
    int ExitCode,
    string StandardOutput,
    string StandardError)
{
    public bool Succeeded => ExitCode == 0;
}
