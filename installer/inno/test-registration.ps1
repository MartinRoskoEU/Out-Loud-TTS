#Requires -Version 5.1
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$StageDirectory, [string]$ExpectedInspection)
. (Join-Path $PSScriptRoot 'production-common.ps1')
[void](Assert-ProductionStage $StageDirectory)
$script:RegistrationChecks=0
function Check([bool]$Ok,[string]$Label){if(-not $Ok){throw "Registration check failed: $Label"};$script:RegistrationChecks++}
$iss=Get-Content (Join-Path $PSScriptRoot 'OutLoud.iss') -Raw
$wizard=Get-Content (Join-Path $PSScriptRoot 'registration.iss') -Raw
$native=Get-Content (Join-Path $ProductionRepository 'installer/native/registration.inc') -Raw
$entry=Get-Content (Join-Path $ProductionRepository 'installer/native/signing.cpp') -Raw
$logic=Get-Content (Join-Path $ProductionRepository 'installer/native/registration_policy.hpp') -Raw
Check ($iss -match '^; A03.12' -and $iss -match '#ifndef StageRoot') 'controlled-test source requires validated staging'
Check ($logic -match 'registration_lifecycle_ready=false' -and $entry -match 'foundation_registration_blocked') 'independent native registration gate'
Check ($wizard -match 'if not FoundationLifecycleReady' -and $wizard -match 'not SigningVerified or not CheckProductConsent or not CheckOutLoudPrerequisites') 'consent, signing and prerequisite order'
$maintenance = Get-Content (Join-Path $ProductionRepository 'installer/native/maintenance.hpp') -Raw
Check ($maintenance.IndexOf('op.sign_staged(j)') -lt $maintenance.IndexOf('op.publish_registration(j)')) 'shared controller signs before registration'
Check ($iss -match 'CheckOutLoudRegistrationConflict\(Result\)' -and $iss -match 'CompleteSharedMaintenance') 'early conflicts and native completion check'
Check ($wizard -notmatch 'migration_required|development' -and $logic -notmatch 'migration_required') 'generic registration conflict handling'
Check ($logic -match 'registration_conflict' -and $logic -match 'OutLoudInstallationId' -and $logic -match 'OutLoudCertificateSHA256') 'unverified registration ownership is refused'
Check ($iss -notmatch '(?im)^\[(Registry|Run|UninstallRun|UninstallDelete)\]' -and
    $native -notmatch 'RegDelete|regsvr32|TerminateProcess|CertAdd|NCryptCreate|Speech\\Voices') 'no unsafe cleanup, self-registration, certificate operations or voice writes'
Check ($native -match 'HKEY_LOCAL_MACHINE' -and $native -match 'KEY_WOW64_64KEY' -and $native -match 'KEY_WOW64_32KEY') 'explicit machine view and override inspection'
Check ($native -match 'RegCreateKeyTransactedW' -and $native -match 'RegOpenKeyTransactedW' -and
    $native -match 'CommitTransaction' -and $native -match 'RollbackTransaction') 'checked atomic registry exchange'
Check ($native -match 'REG_CREATED_NEW_KEY' -and $native -match 'registration_changed' -and $native -match 'registry_link_or_unexpected_link_value') 'no overwrite or registry link traversal'
Check ($native -match 'registry_user_classes' -and $native -match 'TokenUser' -and $native -match 'HKEY_USERS,hive' -and
    $native -match 'registration_user_class_hive\(value' -and $logic -match 'target==L"\\\\Registry\\\\User\\\\"\+hive') 'only the exact Windows current-user class-hive link accepted for read-only inspection'
Check ($logic -match 'OutLoudPocketSAPI-2M' -and $logic -match 'OutLoudInstallationId' -and $logic -match 'OutLoudCertificateSHA256') 'existing owner plus installation and certificate identity'
Check ($native -match 'registration_payload_paths' -and $native -match 'registration_payload_incomplete' -and
    $native -match 'file_hash\(signing.file.value\)==signing.record.signed_sha256') 'complete compiled payload and signed identity checks'
Check ($native -match 'CreateProcessW' -and $native -match '--requirements' -and $native -match 'registration_prerequisites_missing' -and
    $native -notmatch '--ensure-package') 'fresh read-only runtime detector, no runtime installation'
Check ($native -match 'validate_certificate\(signing.certificate_facts\(\),true\)' -and $native -match 'signing.verify_required\(\)') 'existing certificate and dependency signature verification reused'
Check ($native -match 'FileIdInfo' -and $native -match 'VolumeSerialNumber' -and $native -match 'registration_server_identity_mismatch') 'pinned physical engine identity'
Check ($native -match 'registration.pending.ini' -and $native -match 'encoded_tree\(before\)' -and $native -match 'MOVEFILE_WRITE_THROUGH') 'exact before-state and durable recovery handoff'
$helper=Join-Path $StageDirectory 'payload/bin/OutLoudInstallSigning.exe'
Check ((Get-ProductionHash $helper) -eq (Get-ProductionHash (Join-Path $ProductionRepository 'build-installer-prerequisites/Release/OutLoudInstallSigning.exe'))) 'staged registration helper matches build'
$scratch=Resolve-ProductionPath (Join-Path $ProductionRepository 'generated/installer/registration-tests') ([Guid]::NewGuid().ToString('N'))
Assert-ProductionPlainPath $scratch
[void](New-Item -ItemType Directory -Path $scratch -Force)
function Run-Inspection([string[]]$Arguments){
    $start=New-Object Diagnostics.ProcessStartInfo
    $start.FileName=$helper;$start.Arguments=($Arguments | ForEach-Object {'"'+$_+'"'}) -join ' '
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $process=[Diagnostics.Process]::Start($start)
    try{$out=$process.StandardOutput.ReadToEndAsync();$err=$process.StandardError.ReadToEndAsync();$process.WaitForExit()
        return [pscustomobject]@{code=$process.ExitCode;output=$out.Result;error=$err.Result}
    }finally{$process.Dispose()}
}
# This command is stopped by its hard gate before even parsing missing arguments.
$blocked=Run-Inspection @('--register')
Check ($blocked.code -eq 2 -and $blocked.error -match 'foundation_registration_blocked') 'direct registration invocation is blocked'
Check (-not (Test-Path (Join-Path $scratch 'registration.ini'))) 'blocked command produced no registration state'
$report=Join-Path $scratch 'inspect.ini'
$root=Join-Path $scratch ('Custom Out Loud '+[char]0x017d+'ilina')
$inspection=Run-Inspection @('--registration-inspect',$root,$report)
Check ($inspection.code -eq 0 -and (Test-Path $report)) 'read-only actual registration inspection'
$text=[IO.File]::ReadAllText($report)
Check ($text -match 'hive=HKLM' -and $text -match 'view=64' -and
    $text -match ('clsid='+[regex]::Escape((Get-OutLoudEngineClsid)))) 'exact reported registry identity'
Check ($text -match 'before_encoding=utf16le-typed-tree-v1' -and $text -match 'before=(absent|[0-9a-f]+)') 'raw typed before-state capture'
Check ($text -match 'registration=(absent|owned_candidate|conflict|malformed|override_conflict)') 'no fabricated verified result from preflight'
if($ExpectedInspection){Check ($text -match ('(?m)^registration='+[regex]::Escape($ExpectedInspection)+'\r?$')) 'expected current registration classification'}
$oldHash=Get-ProductionHash $report
$repeat=Run-Inspection @('--registration-inspect',$root,$report)
Check ($repeat.code -eq 2 -and $repeat.error -match 'report_already_exists_or_denied' -and
    (Get-ProductionHash $report) -eq $oldHash) 'inspection never replaces a prior recovery report'
$wrong=Run-Inspection @('--registration-inspect','relative-path',(Join-Path $scratch 'wrong.ini'))
Check ($wrong.code -eq 2 -and $wrong.error -match 'unsafe_installation_path' -and
    -not (Test-Path (Join-Path $scratch 'wrong.ini'))) 'relative installation root refused'
Write-Output "PASS: $script:RegistrationChecks registration integration checks; no live registry or certificate mutation."
