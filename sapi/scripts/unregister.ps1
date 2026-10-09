#Requires -Version 5.1
# MANUAL shared-engine removal only. Refuses while any HKLM voice references it.
# Does not delete voice tokens, library entries or deployment files.
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$ReportDirectory)
. (Join-Path $PSScriptRoot 'registry-common.ps1')
Assert-RegistryAdministrator
Assert-GameClosed
Assert-NoUserOverride
$machine = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, [Microsoft.Win32.RegistryView]::Registry64)
try {
    Assert-OwnedClass $machine
    $before = @{ engine_class = (Read-RegistryTree $machine $PocketClassPath) }
    if ($null -eq $before.engine_class) { throw 'Shared Pocket engine is not registered; nothing changed.' }
    function Assert-NoMachineVoiceReferences {
        $voices = $machine.OpenSubKey($PocketVoiceRoot)
        if ($null -eq $voices) { return }
        try {
            foreach ($name in $voices.GetSubKeyNames()) {
                $key = $voices.OpenSubKey($name)
                if ($null -eq $key) { throw 'Voice token changed during engine dependency check; retry after all voice operations finish.' }
                try {
                    if ($key.GetValue('CLSID') -eq $PocketClsid) {
                        throw "Shared engine is still referenced by machine voice '$name'. Remove managed voices through Voice Manager first; unowned registrations need separate manual review."
                    }
                } finally { $key.Dispose() }
            }
        } finally { $voices.Dispose() }
    }
    Assert-NoMachineVoiceReferences
    Save-RegistrySnapshot $ReportDirectory $before
    Assert-GameClosed
    Assert-OwnedClass $machine
    Assert-NoMachineVoiceReferences
    try { $machine.DeleteSubKeyTree($PocketClassPath, $false) }
    catch {
        $original = $_
        try { Restore-RegistryTree $machine $PocketClassPath $before.engine_class }
        catch { throw "Engine unregistration failed: $original. Rollback failed: $($_.Exception.Message). See $ReportDirectory." }
        throw $original
    }
    Write-Output "UNREGISTERED only shared engine HKLM Registry64 $PocketClassPath. Snapshot: $ReportDirectory. No voice token or library changes."
} finally { $machine.Dispose() }
