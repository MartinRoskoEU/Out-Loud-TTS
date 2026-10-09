#Requires -Version 5.1
# MANUAL shared-engine HKLM Registry64 registration only. No voice-token writes.
# BUILD -> STAGE -> SIGN -> VERIFY -> REGISTER; never run automatically.
[CmdletBinding()]
param(
    [string]$DeploymentRoot = (Split-Path $PSScriptRoot -Parent),
    [string]$ReportDirectory
)
. (Join-Path $PSScriptRoot 'registry-common.ps1')
Assert-RegistryAdministrator
Assert-GameClosed
$DeploymentRoot = (Resolve-Path -LiteralPath $DeploymentRoot).Path
$null = Assert-Bundle $DeploymentRoot $true
$dll = Resolve-BundlePath $DeploymentRoot 'bin/OutLoudPocketSAPI.dll'
if (-not $ReportDirectory) { $ReportDirectory = Join-Path $DeploymentRoot ('reports/register-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')) }
Assert-NoUserOverride
$machine = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, [Microsoft.Win32.RegistryView]::Registry64)
try {
    Assert-OwnedClass $machine $dll
    $before = @{ engine_class = (Read-RegistryTree $machine $PocketClassPath) }
    Save-RegistrySnapshot $ReportDirectory $before
    Assert-GameClosed
    try {
        if ($null -eq $before.engine_class) {
            Set-StringValues $machine $PocketClassPath @{ '' = 'Out Loud Pocket SAPI Engine'; OutLoudPocketOwner = $PocketOwner }
            Set-StringValues $machine "$PocketClassPath\InprocServer32" @{ '' = $dll; ThreadingModel = 'Both' }
        }
        Assert-OwnedClass $machine $dll
    } catch {
        $original = $_
        if ($null -eq $before.engine_class) {
            try { Restore-RegistryTree $machine $PocketClassPath $null }
            catch { throw "Engine registration failed: $original. Rollback failed: $($_.Exception.Message). See $ReportDirectory." }
        }
        throw $original
    }
    Write-Output "REGISTERED shared engine HKLM Registry64: $PocketClassPath. DLL: $dll. Snapshot: $ReportDirectory. Import managed voices through Voice Manager."
} finally { $machine.Dispose() }
