#Requires -Version 5.1
[CmdletBinding()]
param([string]$CompilerPath)
. (Join-Path $PSScriptRoot '../../sapi/scripts/installed-update-common.ps1')
$script:checks = 0
function Check([bool]$Value, [string]$Message) { if (-not $Value) { throw "Maintenance check failed: $Message" }; $script:checks++ }
function Refuses([scriptblock]$Action, [string]$Message) {
    $refused = $false
    try { & $Action | Out-Null } catch { $refused = $_.Exception.Message -match [regex]::Escape($Message) }
    Check $refused $Message
}
$scratch = Join-Path $ProductionRepository ('generated/maintenance-validation/integration-' + [Guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $scratch)
$hash = 'a' * 64
$policy = Get-ProductionPolicy
$files = @($policy.files | ForEach-Object { @{path=$_.path; sha256=$hash} })
$manifest = Get-OutLoudMaintenanceIni $files $hash $hash
Check ([regex]::Matches($manifest, '(?m)^\[file\d+\]$').Count -eq @($policy.files).Count) 'complete explicit package'
Check ($manifest -match ('clsid=' + [regex]::Escape((Get-OutLoudEngineClsid)))) 'authoritative CLSID'
foreach ($mode in @('Engine','VoiceManager','Both')) {
    $selection = @(Get-OutLoudUpdateFiles $mode)
    $count = if ($mode -eq 'Engine') { 1 } elseif ($mode -eq 'VoiceManager') { 6 } else { 7 }
    Check ($selection.Count -eq $count) "exact $mode selection"
    $input = Get-OutLoudMaintenanceIni @($selection | ForEach-Object { @{path=$_.path;sha256=$hash} }) $hash $hash
    Check ([regex]::Matches($input, '(?m)^\[file\d+\]$').Count -eq $count) "bounded $mode request"
    Check ($input -notmatch 'runtime/pocket|\.olv') "no assets or voices in $mode"
}
Refuses { Get-OutLoudMaintenanceIni @($files[0],$files[0]) $hash $hash } 'duplicate'
Refuses { Get-OutLoudMaintenanceIni @(@{path='../escape';sha256=$hash}) $hash $hash } 'allowlist'
Refuses { Get-OutLoudMaintenanceIni $files 'bad' $hash } 'bootstrap hash'
foreach ($file in @('installer/inno/build-stage.ps1','installer/inno/production-common.ps1','installer/inno/maintenance-common.ps1',
    'sapi/scripts/update-installed.ps1','sapi/scripts/installed-update-common.ps1')) {
    $tokens=$null; $errors=$null
    [void][Management.Automation.Language.Parser]::ParseFile((Join-Path $ProductionRepository $file),[ref]$tokens,[ref]$errors)
    Check ($errors.Count -eq 0) "PowerShell parses: $file"
}
$iss = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'OutLoud.iss'))
Check ($iss -match '#ifndef StageRoot' -and $iss -match 'if WizardSilent') 'validated stage and interactive initialization required'
Check ($iss -notmatch '(?m)^Source: .*DestDir:' -and $iss -match 'maintenance-files.iss') 'Inno never directly publishes product files'
Check ([regex]::Matches((Get-OutLoudMaintenanceFilesText), 'Flags: dontcopy').Count -eq 23) '23 extracted payload entries'
Check ($iss -match 'PrepareSharedMaintenance\(Result\)' -and $iss -match 'CompleteSharedMaintenance') 'shared transaction and host completion wired'
$native = [IO.File]::ReadAllText((Join-Path $ProductionRepository 'installer/native/maintenance.hpp'))
Check ($native -match 'maintenance_lifecycle_ready=true') 'shared native lifecycle enabled'
$helper = Get-OutLoudMaintenanceHelper
function Probe([string]$Arguments) {
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName=$helper; $start.Arguments=$Arguments; $start.UseShellExecute=$false; $start.CreateNoWindow=$true
    $start.RedirectStandardOutput=$true; $start.RedirectStandardError=$true
    $process=[Diagnostics.Process]::Start($start)
    try { $out=$process.StandardOutput.ReadToEndAsync(); $err=$process.StandardError.ReadToEndAsync(); $process.WaitForExit()
        return @{code=$process.ExitCode; text=$out.Result+$err.Result} } finally { $process.Dispose() }
}
foreach ($command in @('--maintain','--maintenance-recover','--maintenance-finalize','--maintenance-reconcile','--maintenance-fresh-cleanup')) {
    $result = Probe $command
    Check ($result.code -eq 2 -and $result.text -match 'invalid_arguments') "$command requires complete arguments before resource access"
}
Assert-OutLoudDevelopmentPublication
Check $true 'development workflow accepts the shared native lifecycle status without publication'
# Compile actual Pascal units in a separate inert harness. No production gate is
# removed, preprocessed away, or overridden. This harness always refuses to run.
if ($CompilerPath) {
    if (-not (Test-Path -LiteralPath $CompilerPath -PathType Leaf)) { throw 'Compiler not found.' }
    [IO.File]::WriteAllText((Join-Path $scratch 'maintenance-copy.iss'), (Get-OutLoudMaintenanceCopyText), [Text.Encoding]::ASCII)
    $lines = @(('#define StageRoot "' + $scratch + '"'), ('#define ProductVersion "' + (Get-OutLoudProductMetadata).version + '"'),
        ('#define EngineClsid "' + (Get-OutLoudEngineClsid) + '"'), '[Setup]', 'AppName=Out Loud inert compile check',
        'AppVersion=0.1.0', 'CreateAppDir=no', 'Uninstallable=no', 'PrivilegesRequired=lowest', ('OutputDir=' + $scratch),
        'OutputBaseFilename=INERT-Maintenance-Compile-Check', '[Code]')
    foreach ($unit in @('prerequisites.iss','consent.iss','runtime-install.iss','installation.iss','signing.iss','registration.iss','maintenance.iss')) {
        $lines += '#include "' + (Join-Path $PSScriptRoot $unit) + '"'
    }
    $lines += 'function InitializeSetup(): Boolean; begin Result := False; end;'
    $harness = Join-Path $scratch 'compile-check.iss'
    [IO.File]::WriteAllText($harness, ($lines -join "`n") + "`n", [Text.Encoding]::UTF8)
    & $CompilerPath /Q $harness | Out-Host
    Check ($LASTEXITCODE -eq 0) 'actual Pascal units compile in inert harness'
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName=$CompilerPath; $start.Arguments='/Q "' + (Join-Path $PSScriptRoot 'OutLoud.iss') + '"'
    $start.UseShellExecute=$false; $start.CreateNoWindow=$true; $start.RedirectStandardOutput=$true; $start.RedirectStandardError=$true
    $process=[Diagnostics.Process]::Start($start)
    try { $out=$process.StandardOutput.ReadToEndAsync(); $err=$process.StandardError.ReadToEndAsync(); $process.WaitForExit()
        [IO.File]::WriteAllText((Join-Path $scratch 'production-gate.log'),$out.Result+$err.Result)
        Check ($process.ExitCode -ne 0 -and $err.Result -match 'StageRoot') 'real installer refuses compilation without a stage'
    } finally { $process.Dispose() }
}
[ordered]@{ checks=$script:checks; compiler_check=[bool]$CompilerPath; live_operations=$false } | ConvertTo-Json | Set-Content (Join-Path $scratch 'results.json') -Encoding UTF8
Write-Output "PASS: $script:checks integration checks. Evidence: $scratch. No installation or live certificate/registry operation."
