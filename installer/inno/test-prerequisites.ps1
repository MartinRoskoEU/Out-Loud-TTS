#Requires -Version 5.1
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$StageDirectory)
. (Join-Path $PSScriptRoot 'production-common.ps1')
[void](Assert-ProductionStage $StageDirectory)
$detector = Join-Path $StageDirectory 'bootstrap/OutLoudPrerequisites.exe'
$requirements = Join-Path $StageDirectory 'prerequisites.ini'
$runtimeConfig = Join-Path $StageDirectory 'payload/bin/OutLoud.VoiceManager.runtimeconfig.json'
$testParent = Join-Path $ProductionRepository 'generated/installer/prerequisite-tests'
$scratch = Resolve-ProductionPath $testParent ([Guid]::NewGuid().ToString('N'))
Assert-ProductionPlainPath $scratch
[void](New-Item -ItemType Directory -Path $scratch -Force)
$originalRootX64 = $env:DOTNET_ROOT_X64
$originalRoot = $env:DOTNET_ROOT
$script:Checks = 0
function Check([bool]$Passed, [string]$Label) {
    if (-not $Passed) { throw "Prerequisite check failed: $Label" }
    $script:Checks++
}
function Probe([string]$Config) {
    $output = & $detector --requirements $requirements --runtime-config $Config
    $exitCode = $LASTEXITCODE
    $values = @{}
    foreach ($line in $output) { if ($line -match '^([^=]+)=(.*)$') { $values[$Matches[1]] = $Matches[2] } }
    Check ($values.schema -eq '1') 'structured detector output'
    return @{ exit = $exitCode; values = $values }
}
try {
    $iss = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'OutLoud.iss') -Raw
    $code = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'prerequisites.iss') -Raw
    Check ($iss -match '(?m)^MinVersion=10\.0\.22000\r?$') 'Windows 11 build floor'
    Check ($iss -match '(?m)^ArchitecturesAllowed=x64os\r?$') 'native x64 Inno architecture'
    Check ($code -match 'Version\.Major = 10' -and $code -match 'Version\.Minor = 0' -and $code -match 'Version\.Build >= 22000') 'NT build check'
    Check ($code -match 'Version\.ProductType = VER_NT_WORKSTATION' -and $code -match 'ProcessorArchitecture = paX64') 'Server and ARM exclusion'
    Check ($iss -match '#include "prerequisites.iss"' -and $iss -match 'if not CheckOutLoudWindows' -and $iss -match 'if not ReadOutLoudPrerequisites') 'Inno preflight wiring'
    Check ($iss -match '#ifndef StageRoot' -and $iss -match 'if WizardSilent' -and $iss -notmatch '(?im)^\[(Registry|Run|UninstallRun|UninstallDelete)\]') 'interactive staged lifecycle without unchecked operations'
    Check (@([regex]::Matches($iss, '(?m)^Source: .+Flags: dontcopy')).Count -eq 7) 'temporary bootstrap inputs'
    Check ($code -match "ExitCode = 0" -and $code -match "'schema', '', Report\) <> '1'" -and $code -match 'FileExists\(Report\)') 'fail-closed result handling'
    Check ($code -notmatch 'RegWrite|RegDelete|DownloadTemporaryFile|ShellExec|DeleteFile') 'no premature installation actions'
    $live = Probe $runtimeConfig
    Check ($live.exit -eq 0 -and $live.values.windows -eq 'compatible' -and $live.values.vc -eq 'compatible' -and $live.values.dotnet -eq 'compatible') 'developer machine compatible'
    $config = Get-Content -LiteralPath $runtimeConfig -Raw | ConvertFrom-Json
    $config.runtimeOptions | Add-Member -NotePropertyName rollForward -NotePropertyValue LatestPatch -Force
    $patchConfig = Join-Path $scratch 'patch.runtimeconfig.json'
    $config | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $patchConfig -Encoding UTF8
    $patch = Probe $patchConfig
    Check ($patch.exit -eq 0 -and $patch.values.dotnet -eq 'compatible') 'actual hostfxr servicing roll-forward'
    $config.runtimeOptions.rollForward = 'Disable'
    foreach ($framework in $config.runtimeOptions.frameworks) { $framework.version = '10.0.65534' }
    $strictConfig = Join-Path $scratch 'strict.runtimeconfig.json'
    $config | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $strictConfig -Encoding UTF8
    $strict = Probe $strictConfig
    Check ($strict.exit -eq 1 -and $strict.values.dotnet -eq 'incompatible_version') 'exact unavailable runtime is rejected'
    $badConfig = Join-Path $scratch 'bad.runtimeconfig.json'
    [IO.File]::WriteAllText($badConfig, '{ malformed')
    $bad = Probe $badConfig
    Check ($bad.exit -eq 2 -and $bad.values.dotnet -eq 'detection_failed') 'malformed runtimeconfig is detection failure'
    $env:DOTNET_ROOT_X64 = Join-Path $scratch 'absent-dotnet'
    $absent = Probe $runtimeConfig
    Check ($absent.exit -eq 0 -and $absent.values.dotnet -eq 'compatible') 'nonexistent environment root follows apphost fallback'
    $fakeRoot = Join-Path $scratch 'wrong-architecture'
    [void](New-Item -ItemType Directory -Path $fakeRoot)
    $fakeImage = [IO.File]::ReadAllBytes($detector)
    $pe = [BitConverter]::ToUInt32($fakeImage, 60)
    $fakeImage[$pe + 4] = 0x4c; $fakeImage[$pe + 5] = 0x01
    [IO.File]::WriteAllBytes((Join-Path $fakeRoot 'dotnet.exe'), $fakeImage)
    $env:DOTNET_ROOT_X64 = $fakeRoot
    $wrong = Probe $runtimeConfig
    Check ($wrong.exit -eq 1 -and $wrong.values.dotnet -eq 'incorrect_architecture') 'controlled x86 dotnet image refused without execution'
    [IO.File]::WriteAllBytes((Join-Path $fakeRoot 'dotnet.exe'), [IO.File]::ReadAllBytes($detector))
    [void](New-Item -ItemType Directory -Path (Join-Path $fakeRoot 'sdk/10.0.401') -Force)
    $sdkOnly = Probe $runtimeConfig
    Check ($sdkOnly.exit -eq 1 -and $sdkOnly.values.dotnet -eq 'missing') 'SDK directory alone is not Desktop Runtime'
    [void](New-Item -ItemType Directory -Path (Join-Path $fakeRoot 'shared/Microsoft.NETCore.App/10.0.12') -Force)
    $coreOnly = Probe $runtimeConfig
    Check ($coreOnly.exit -eq 1 -and $coreOnly.values.dotnet -eq 'missing') 'Core runtime directory alone is not Desktop Runtime'
    $env:DOTNET_ROOT_X64 = $originalRootX64
    $report = Join-Path $scratch 'result.ini'
    & $detector --requirements $requirements --runtime-config $runtimeConfig --report $report | Out-Null
    Check ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $report)) 'explicit report creation'
    $hash = Get-ProductionHash $report
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = $detector
    $start.Arguments = '--requirements "{0}" --runtime-config "{1}" --report "{2}"' -f $requirements,$runtimeConfig,$report
    $start.UseShellExecute = $false; $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true; $start.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($start)
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        Check ($process.ExitCode -eq 2 -and (Get-ProductionHash $report) -eq $hash -and $stderr.Result -match 'Report destination exists') 'existing report never overwritten'
        [void]$stdout.Result
    } finally { $process.Dispose() }
    Write-Output "$script:Checks prerequisite integration checks PASS; only test-owned files written, no installed runtime changed."
} finally {
    $env:DOTNET_ROOT_X64 = $originalRootX64
    $env:DOTNET_ROOT = $originalRoot
    $verified = Resolve-ProductionPath $testParent ([IO.Path]::GetFileName($scratch))
    if ($verified -ne $scratch) { throw 'Prerequisite scratch containment changed; cleanup refused.' }
    Assert-ProductionPlainPath $scratch
    Remove-Item -LiteralPath $scratch -Recurse -Force
}
