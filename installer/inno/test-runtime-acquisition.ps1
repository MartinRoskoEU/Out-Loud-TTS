#Requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$StageDirectory,
    [string]$PackageDirectory
)
. (Join-Path $PSScriptRoot 'production-common.ps1')
[void](Assert-ProductionStage $StageDirectory)
$probe = Join-Path $StageDirectory 'bootstrap/OutLoudPrerequisites.exe'
$manifest = Join-Path $StageDirectory 'runtime-packages.ini'
$testParent = Join-Path $ProductionRepository 'generated/installer/acquisition-tests'
$scratch = Resolve-ProductionPath $testParent ([Guid]::NewGuid().ToString('N'))
Assert-ProductionPlainPath $scratch
[void](New-Item -ItemType Directory -Path $scratch -Force)
$script:AcquisitionChecks = 0
function Check([bool]$Passed, [string]$Label) {
    if (-not $Passed) { throw "Runtime acquisition test failed: $Label" }
    $script:AcquisitionChecks++
}
function Verify([string]$Policy, [string]$Component, [string]$File) {
    # This wrapper intentionally exposes only the read-only command, never ensure-package.
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = $probe
    $start.Arguments = '--verify-package "{0}" {1} "{2}"' -f $Policy,$Component,$File
    $start.UseShellExecute = $false; $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true; $start.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($start)
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync(); $stderr = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        return @{ exit=$process.ExitCode; output=$stdout.Result; error=$stderr.Result }
    } finally { $process.Dispose() }
}
try {
    $iss = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'OutLoud.iss') -Raw
    $code = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'runtime-install.iss') -Raw
    $native = Get-Content -LiteralPath (Join-Path $ProductionRepository 'installer/native/packages.inc') -Raw
    Check ($iss -match '#ifndef StageRoot' -and $iss -match 'EncodeVer\(7,0,2\)' -and $code -match 'function FoundationLifecycleReady\(\): Boolean;[\s\S]*?Result := IsAdmin;') 'compiler version, stage and elevated lifecycle checks'
    Check ($iss -match 'PrepareToInstall' -and $iss -match 'InitializeRuntimeWizard' -and $iss -match '#include "runtime-install.iss"') 'wizard event integration'
    Check ($code -match 'RuntimeConsentPage.Values\[0\] := False;' -and $code -match 'not RuntimeConsentPage.Values\[0\]') 'explicit unchecked installation consent'
    Check ($code -match 'CreateDownloadPage' -and $code -match 'RuntimeDownloadPage.Add\(Url, FileName, Hash\)' -and $code -match 'AbortedByUser') 'supported hashed cancellable download'
    Check ($code -match "https://download.visualstudio.microsoft.com/" -and $code -match "https://builds.dotnet.microsoft.com/dotnet/") 'official HTTPS source restriction'
    Check ($code -match 'GetSpaceOnDisk64' -and $code -match '536870912' -and $code -match '1610612736') 'space preflight'
    Check ($code -match 'finally[\s\S]*?RuntimeDownloadPage.Hide' -and $code -match 'DeleteFile\(PackagePath\)') 'download/page cleanup'
    Check ($code -match 'ReadOutLoudPrerequisites\(Report\)' -and $code -match "Component \+ '_action'") 'reuse existing probe and native plan'
    Check ($iss -match 'NeedsRestart := False' -and $code -match 'restart Windows manually' -and $code -notmatch 'RestartComputer|RestartReplace|RunOnce') 'manual restart and rerun'
    Check ($iss -match 'RuntimeCancelRequested := True' -and $iss -match 'if RuntimeExecuting then' -and $iss -match 'Cancel := False') 'cancellation waits for vendor quiescence'
    Check ($native -match 'WinVerifyTrust' -and $native -match 'WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT' -and $native -match 'szOID_ORGANIZATION_NAME' -and $native -match 'szOID_COMMON_NAME') 'Windows trust and Microsoft identity'
    Check ($native -match 'FILE_SHARE_READ' -and $native -match 'CreateProcessW' -and $native -match 'ensure_runtime\(' -and $native -notmatch 'FILE_SHARE_WRITE|FILE_SHARE_DELETE|TerminateProcess|/uninstall') 'pinned verified execution and shared ownership'
    Check ($native -match '/install /passive /norestart /log' -and $native -match 'IsUserAnAdmin') 'vendor command and elevation'
    Check ($iss -notmatch '(?im)^\[(Registry|Run|UninstallRun|UninstallDelete)\]' -and $code -notmatch 'RegWrite|RegDelete|CreateCertificate|ShellExec') 'unfinished system lifecycle disabled'
    $policy = Get-RuntimePackagePolicy
    foreach ($package in $policy.packages) {
        Check ($package.url.StartsWith('https://') -and $package.architecture -eq 'x64' -and $package.sha256.Length -eq 64 -and $package.bootstrap_machine -eq '014c') "pinned $($package.id) metadata"
    }
    # An unsigned project image with matching test-only size/hash must fail trust.
    $fakeManifest = Join-Path $scratch 'unsigned.ini'
    $content = [IO.File]::ReadAllText($manifest)
    $vcPolicy = $policy.packages | Where-Object id -eq 'vc'
    $unsignedPolicy = $content.Replace("bytes=$($vcPolicy.bytes)", "bytes=$((Get-Item $probe).Length)").Replace("sha256=$($vcPolicy.sha256)", "sha256=$(Get-ProductionHash $probe)")
    [IO.File]::WriteAllText($fakeManifest, $unsignedPolicy)
    $unsigned = Verify $fakeManifest 'vc' $probe
    Check ($unsigned.exit -eq 1 -and $unsigned.output -match 'package=invalid_signature') 'actual unsigned image refused by WinVerifyTrust'
    if ($PackageDirectory) {
        Assert-ProductionPlainPath $PackageDirectory
        foreach ($package in $policy.packages) {
            $file = Join-Path $PackageDirectory $package.filename
            $verified = Verify $manifest $package.id $file
            Check ($verified.exit -eq 0 -and $verified.output -match 'package=verified') "actual Microsoft $($package.id) signature/hash/version"
            if ($package.id -eq 'dotnet') {
                Check ((Get-FileHash -LiteralPath $file -Algorithm SHA512).Hash.ToLowerInvariant() -eq $package.source_sha512) 'official .NET release checksum'
            }
            foreach ($case in @('hash','publisher','version','architecture','size')) {
                $changed = $content
                switch ($case) {
                    hash { $changed=$changed.Replace("sha256=$($package.sha256)", 'sha256=' + ('0'*64)); $expected='invalid_hash' }
                    publisher { $cn=if($package.signer_cn -eq '.NET') {'Microsoft Corporation'} else {'.NET'}; $changed=$changed.Replace("signer_cn=$($package.signer_cn)", "signer_cn=$cn"); $expected='unexpected_publisher' }
                    version { $changed=$changed.Replace("file_version=$($package.file_version)", 'file_version=10.0.0.0'); $expected='unexpected_version' }
                    architecture { $changed=$changed.Replace('architecture=x64', 'architecture=x86'); $expected='Only pinned x64' }
                    size { $changed=$changed.Replace("bytes=$($package.bytes)", 'bytes=1'); $expected='invalid_size' }
                }
                $changedPath=Join-Path $scratch "$($package.id)-$case.ini"
                [IO.File]::WriteAllText($changedPath, $changed)
                $rejected=Verify $changedPath $package.id $file
                Check ($rejected.exit -ne 0 -and ($rejected.output + $rejected.error) -match $expected) "$($package.id) changed $case rejected"
            }
        }
    }
    Write-Output "$script:AcquisitionChecks acquisition integration checks PASS; no installer executed or downloaded by tests."
} finally {
    $verifiedScratch = Resolve-ProductionPath $testParent ([IO.Path]::GetFileName($scratch))
    if ($verifiedScratch -ne $scratch) { throw 'Acquisition scratch containment changed; cleanup refused.' }
    Assert-ProductionPlainPath $scratch
    Remove-Item -LiteralPath $scratch -Recurse -Force
}
