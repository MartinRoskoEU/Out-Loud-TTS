#Requires -Version 5.1
# MANUAL removal of one explicitly named Pocket token. No wildcard deletion/HKCU cleanup.
# Retains the shared COM class while ANY other token references it, including other voice packs.
[CmdletBinding()]
param(
    [string]$TokenName = 'OutLoud_Pocket_GnomeMale_Development',
    [Parameter(Mandatory = $true)][string]$ReportDirectory
)
. (Join-Path $PSScriptRoot 'registry-common.ps1')
Assert-RegistryAdministrator
Assert-GameClosed
Assert-NoUserOverride
$tokenPath = Get-PocketTokenPath $TokenName
$machine = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, [Microsoft.Win32.RegistryView]::Registry64)
try {
    Assert-OwnedClass $machine
    Assert-OwnedToken $machine $tokenPath
    $before = @{ engine_class = (Read-RegistryTree $machine $PocketClassPath); voice_token = (Read-RegistryTree $machine $tokenPath) }
    if ($null -eq $before.voice_token) { throw 'Selected Pocket token is not registered; nothing changed.' }
    $otherUsers = @()
    $voices = $machine.OpenSubKey($PocketVoiceRoot)
    try {
        foreach ($name in $voices.GetSubKeyNames()) {
            if ($name -eq $TokenName) { continue }
            $key = $voices.OpenSubKey($name)
            try { if ($key.GetValue('CLSID') -eq $PocketClsid) { $otherUsers += $name } } finally { $key.Dispose() }
        }
    } finally { $voices.Dispose() }
    Save-RegistrySnapshot $ReportDirectory $before
    Assert-GameClosed
    try {
        $machine.DeleteSubKeyTree($tokenPath, $false)
        if ($otherUsers.Count -eq 0) { $machine.DeleteSubKeyTree($PocketClassPath, $false) }
    } catch {
        $original = $_
        $rollbackErrors = @()
        try { Restore-RegistryTree $machine $tokenPath $before.voice_token } catch { $rollbackErrors += $_.Exception.Message }
        if ($otherUsers.Count -eq 0) {
            try { Restore-RegistryTree $machine $PocketClassPath $before.engine_class } catch { $rollbackErrors += $_.Exception.Message }
        }
        if ($rollbackErrors.Count) { throw "Unregistration failed: $original. Rollback failed: $($rollbackErrors -join '; '). See $ReportDirectory." }
        throw $original
    }
    Write-Output "UNREGISTERED only HKLM Registry64 $tokenPath. Retained other shared-engine tokens: $($otherUsers -join ', '). Snapshot: $ReportDirectory"
} finally { $machine.Dispose() }
