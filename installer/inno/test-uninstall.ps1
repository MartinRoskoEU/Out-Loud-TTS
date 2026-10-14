#Requires -Version 5.1
[CmdletBinding()]
param([string]$CompilerPath = 'C:\Program Files\Inno Setup 7\ISCC.exe')
. (Join-Path $PSScriptRoot '../../sapi/scripts/installed-update-common.ps1')
$script:checks = 0
function Check([bool]$Value,[string]$Why) { if (-not $Value) { throw "Uninstall check failed: $Why" }; $script:checks++ }
$scratch = Join-Path $ProductionRepository ('generated/uninstall-validation/integration-' + [Guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $scratch)
$helper = Join-Path $ProductionRepository 'build-installer-prerequisites/Release/OutLoudInstallSigning.exe'
foreach ($command in @('--uninstall-check','--uninstall','--uninstall-finalize','--uninstall-helper-check')) {
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName=$helper; $start.Arguments=$command; $start.UseShellExecute=$false; $start.CreateNoWindow=$true
    $start.RedirectStandardOutput=$true; $start.RedirectStandardError=$true
    $process=[Diagnostics.Process]::Start($start)
    try { $out=$process.StandardOutput.ReadToEndAsync(); $err=$process.StandardError.ReadToEndAsync(); $process.WaitForExit()
        Check ($process.ExitCode -eq 2 -and ($out.Result+$err.Result) -match 'invalid_arguments') "$command requires complete arguments before resource access"
    } finally { $process.Dispose() }
}
$status = Join-Path $scratch 'status.ini'
& $helper --uninstall-status $status
Check ($LASTEXITCODE -eq 0 -and [IO.File]::ReadAllText($status) -match 'uninstall=enabled') 'shared uninstall status enabled'
$iss = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'OutLoud.iss'))
$unit = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'uninstall.iss'))
Check ($iss -match '#ifndef StageRoot' -and $iss -match 'AppId=OutLoud') 'stage requirement and AppId unchanged'
Check ($iss -notmatch '(?m)^\[Uninstall(Delete|Run)\]') 'no wildcard or unchecked uninstall entries'
Check ($iss -match '#include "uninstall.iss"') 'native lifecycle included'
Check ($unit -match 'if not FoundationLifecycleReady' -and $unit -match 'if UninstallSilent') 'runtime and interactive confirmation gates'
Check ($unit -match 'MB_YESNO or MB_DEFBUTTON2' -and $unit -match 'ALL managed voices' -and $unit -match 'permanently deleted') 'explicit complete-removal warning defaults to no'
Check ($unit -match 'usUninstall' -and $unit -match "'awaiting_host'" -and $unit -match 'mbError, MB_OK\); Abort;') 'native failure aborts before Inno host processing'
Check ($unit -match 'usPostUninstall' -and $unit -match "'--uninstall-finalize'") 'cleanup retains helper and receipt through host processing'
Check ($unit -match 'GetSHA256OfFile\(SourceHelper\)' -and $unit -match 'GetSHA256OfFile\(UninstallHelper\)') 'helper verified before execution'
Check ($unit -notmatch 'TerminateProcess|taskkill|DelTree|RmShutdown') 'no process termination or recursive deletion'
$native = [IO.File]::ReadAllText((Join-Path $ProductionRepository 'installer/native/uninstall_windows.inc'))
Check ($native -match 'receipt\(maintenance.files.root/L"\.outloud-uninstall.dat"\)' -and
    $native.IndexOf('RemoveDirectoryW(configuration.c_str())') -lt $native.IndexOf('Handle record;storage().security(receipt)')) 'removal receipt survives configuration-directory cleanup failure'
$version = Get-OutLoudProductMetadata
Check ($version.version -eq '0.1.0') 'version remains 0.1.0'
if ($CompilerPath) {
    [IO.File]::WriteAllText((Join-Path $scratch 'maintenance-copy.iss'),(Get-OutLoudMaintenanceCopyText),[Text.Encoding]::ASCII)
    $lines = @(('#define StageRoot "' + $scratch + '"'), '#define ProductVersion "0.1.0"',
        ('#define EngineClsid "' + (Get-OutLoudEngineClsid) + '"'), '[Setup]', 'AppName=Out Loud inert uninstall compile check',
        'AppVersion=0.1.0', 'CreateAppDir=no', 'Uninstallable=no', 'PrivilegesRequired=lowest', ('OutputDir=' + $scratch),
        'OutputBaseFilename=INERT-Uninstall-Compile-Check', '[Code]')
    foreach ($item in @('prerequisites.iss','consent.iss','runtime-install.iss','installation.iss','signing.iss','registration.iss','maintenance.iss','uninstall.iss')) {
        $lines += '#include "' + (Join-Path $PSScriptRoot $item) + '"'
    }
    $lines += 'function InitializeSetup(): Boolean; begin Result := False; end;'
    $harness = Join-Path $scratch 'compile-check.iss'
    [IO.File]::WriteAllText($harness,($lines -join "`n")+"`n",[Text.Encoding]::UTF8)
    & $CompilerPath /Q $harness | Out-Host
    Check ($LASTEXITCODE -eq 0) 'actual uninstall units compile in always-refusing inert harness'
    $start=New-Object Diagnostics.ProcessStartInfo
    $start.FileName=$CompilerPath; $start.Arguments='/Q "' + (Join-Path $PSScriptRoot 'OutLoud.iss') + '"'
    $start.UseShellExecute=$false; $start.CreateNoWindow=$true; $start.RedirectStandardOutput=$true; $start.RedirectStandardError=$true
    $process=[Diagnostics.Process]::Start($start)
    try { $out=$process.StandardOutput.ReadToEndAsync(); $err=$process.StandardError.ReadToEndAsync(); $process.WaitForExit()
        Check ($process.ExitCode -ne 0 -and ($out.Result+$err.Result) -match 'StageRoot') 'production compiler refuses missing stage'
    } finally { $process.Dispose() }
}
[ordered]@{checks=$script:checks;compiler_check=[bool]$CompilerPath;live_operations=$false} | ConvertTo-Json | Set-Content (Join-Path $scratch 'results.json') -Encoding UTF8
Write-Output "PASS: $script:checks focused uninstall integration checks. Evidence: $scratch. No live cleanup."
