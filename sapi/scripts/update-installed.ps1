#Requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('Engine', 'VoiceManager', 'Both')][string]$Components = 'Both',
    [ValidateSet('Plan', 'Prepare', 'Deploy')][string]$Action = 'Plan'
)
. (Join-Path $PSScriptRoot 'installed-update-common.ps1')
if (-not [Environment]::Is64BitProcess) { throw 'Use x64 PowerShell for installed Out Loud discovery.' }
# Discover before workspace creation/build. Missing installation has no fallback.
$root = Resolve-OutLoudInstalledRoot (Read-OutLoudUninstallRecord)
if ($Action -eq 'Deploy') { Assert-OutLoudDevelopmentPublication }
$reports = Join-Path $ProductionRepository ('generated/installed-updates/' + [Guid]::NewGuid().ToString('N'))
Assert-ProductionPlainPath $reports
[void](New-Item -ItemType Directory -Path $reports)
$ownership = Invoke-OutLoudUpdateInspection $root $reports
$plan = New-OutLoudUpdatePlan $root $Components $ownership
if ($Action -in @('Prepare', 'Deploy')) { Prepare-OutLoudUpdateCandidates $plan $reports }
if ($Action -eq 'Deploy') { Invoke-OutLoudDevelopmentMaintenance $plan $reports }
$plan | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $reports 'plan.json') -Encoding UTF8
if ($plan.deployment -eq 'committed') { Write-Output "DEPLOY complete: native transaction verified. $reports" }
else { Write-Output "$( $Action.ToUpperInvariant() ) complete: $reports. Installed publication remains blocked; no installation update was performed." }
