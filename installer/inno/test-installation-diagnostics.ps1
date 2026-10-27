#Requires -Version 5.1
# Native inspection only. Fixtures/reports stay in a new workspace directory;
# no installation, certificate, key, registration or uninstall command is run.
[CmdletBinding()]
param([string]$HelperPath=(Join-Path $PSScriptRoot '../../build-installer-prerequisites/Release/OutLoudInstallSigning.exe'))
. (Join-Path $PSScriptRoot 'production-common.ps1')
$script:checks=0
function Check([bool]$ok,[string]$label){if(-not $ok){throw "Installation diagnostics check failed: $label"};$script:checks++}
$scratch=Resolve-ProductionPath $ProductionRepository ('generated/installer/diagnostic-tests/'+[guid]::NewGuid().ToString('N'))
Assert-ProductionPlainPath $scratch
[void](New-Item -ItemType Directory -Path $scratch)
$helper=[IO.Path]::GetFullPath($HelperPath)
function Probe([string]$path,[string]$report){
    $start=[Diagnostics.ProcessStartInfo]::new()
    $start.FileName=$helper
    $start.Arguments='--installation-inspect "'+$path+'" "'+$report+'"'
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true
    $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $process=[Diagnostics.Process]::Start($start)
    try{$out=$process.StandardOutput.ReadToEndAsync();$err=$process.StandardError.ReadToEndAsync();$process.WaitForExit()
        return [pscustomobject]@{code=$process.ExitCode;output=$out.Result;error=$err.Result}}
    finally{$process.Dispose()}
}
$report=Join-Path $scratch 'syntax.ini'
$relative='relative-'+[char]0x017d
$probe=Probe $relative $report
Check ($probe.code -eq 2 -and $probe.error -match 'inspection_reason=unsafe_installation_path') 'syntax still refused with exact reason'
Check ($probe.error -match 'inspection_phase=path_syntax' -and $probe.error.Contains('inspection_path=relative-\u017d')) 'phase and Unicode path survive ASCII transport'
Check ($probe.error -match 'inspection_path_utf16=00720065006c00610074006900760065002d017d') 'exact UTF-16 path retained'
Check ((Get-Content -LiteralPath $report -Raw) -match 'location=rejected') 'failure produces a structured report'

$probe=Probe $scratch (Join-Path $scratch 'owner.ini')
Check ($probe.code -eq 2 -and $probe.error -match 'inspection_reason=state_owner_invalid|inspection_reason=state_acl_writable_by_user') 'ordinary-user fixture remains refused'
Check ($probe.error -match 'inspection_phase=directory_security' -and $probe.error.Contains('inspection_path='+$scratch)) 'security refusal identifies inspected directory'
Check ($probe.error -match 'inspection_owner_sid=S-1-' -and $probe.error -match 'inspection_sddl=O:') 'owner and security descriptor recorded'
Check ($probe.error -match 'inspection_win32_error=not_applicable') 'policy refusal does not report stale GetLastError'

$target=Join-Path $scratch 'target';$junction=Join-Path $scratch 'junction'
[void](New-Item -ItemType Directory -Path $target)
[void](New-Item -ItemType Junction -Path $junction -Target $target)
try {
    $probe=Probe (Join-Path $junction 'absent') (Join-Path $scratch 'junction.ini')
    Check ($probe.code -eq 2 -and $probe.error -match 'inspection_reason=unsafe_reparse_path') 'junction ancestor remains refused'
    Check (($probe.error -split "`r?`n") -contains ('inspection_path='+$junction) -and $probe.error -match 'inspection_reparse_tag=0xa0000003') 'exact ancestor and mount-point tag captured'
    Check (-not(Test-Path -LiteralPath (Join-Path $target 'absent'))) 'inspection never creates target directory'
} finally {
    # Delete just this junction, never its target or a recursive tree.
    Check ([IO.Path]::GetDirectoryName($junction) -ceq $scratch) 'junction cleanup containment'
    [IO.Directory]::Delete($junction,$false)
}
Check (Test-Path -LiteralPath $target) 'junction target preserved'

# Select an absent child of the system's existing protected Program Files.
$candidate=Join-Path ([Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFiles)) ('OutLoud-diagnostic-probe-'+[guid]::NewGuid().ToString('N'))
$success=Join-Path $scratch 'success.ini'
$probe=Probe $candidate $success
Check ($probe.code -eq 0 -and (Get-Content -LiteralPath $success -Raw) -match 'location=secure_candidate') 'secure absent directory remains accepted'
Check (-not(Test-Path -LiteralPath $candidate)) 'secure inspection is read-only at destination'
$hash=Get-ProductionHash $success
$probe=Probe $candidate $success
Check ($probe.code -eq 2 -and $probe.error -match 'inspection_category=reporting' -and $probe.error -match 'inspection_reason=report_already_exists_or_denied') 'report collision is reporting failure, not security rejection'
Check ($probe.error -match 'inspection_api=CreateFileW' -and $probe.error -match 'inspection_win32_error=80') 'actual Windows ERROR_FILE_EXISTS captured'
Check ((Get-ProductionHash $success) -ceq $hash) 'existing report never overwritten'
$probe=Probe $candidate (Join-Path $scratch 'missing/report.ini')
Check ($probe.code -eq 2 -and $probe.error -match 'inspection_category=reporting' -and $probe.error -match 'inspection_phase=report_write' -and $probe.error -match 'inspection_reason=report_parent_missing') 'report parent failure identified separately'
$probe=Probe $relative $success
Check ($probe.code -eq 2 -and $probe.error -match 'inspection_reason=unsafe_installation_path' -and $probe.error -match 'report_failure_reason=report_already_exists_or_denied') 'secondary report failure preserves primary security refusal'
Check ((Get-ProductionHash $success) -ceq $hash) 'failure report cannot overwrite an existing report'

# A directory path with a file as an intermediate component produces a real API
# error without changing permissions on any system or installed directory.
$blocker=Join-Path $scratch 'not-a-directory'
[IO.File]::WriteAllText($blocker,'fixture')
$probe=Probe (Join-Path $blocker 'child') (Join-Path $scratch 'blocked.ini')
Check ($probe.code -eq 2) 'file ancestor remains refused'
Check ($probe.error -match 'inspection_phase=parent_search|inspection_phase=path_attributes') 'file ancestor failure phase retained'
Write-Output "PASS: $script:checks installation diagnostic checks. Fixtures: $scratch"
