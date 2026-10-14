#Requires -Version 5.1
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$StageDirectory, [string]$SignedReference)
. (Join-Path $PSScriptRoot 'production-common.ps1')
[void](Assert-ProductionStage $StageDirectory)
$script:SigningChecks=0
function Check([bool]$Ok,[string]$Label){if(-not $Ok){throw "Signing check failed: $Label"};$script:SigningChecks++}
$native=Get-Content -LiteralPath (Join-Path $ProductionRepository 'installer/native/signing.cpp') -Raw
$dependencies=Get-Content -LiteralPath (Join-Path $ProductionRepository 'installer/native/signing_dependencies.inc') -Raw
$logic=Get-Content -LiteralPath (Join-Path $ProductionRepository 'installer/native/signing_policy.hpp') -Raw
$iss=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'OutLoud.iss') -Raw
$wizard=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'signing.iss') -Raw
$policy=Get-ProductionSigningPolicy
$helper=Join-Path $StageDirectory 'payload/bin/OutLoudInstallSigning.exe'
Check ($policy.target -eq 'bin/OutLoudPocketSAPI.dll' -and $policy.stores -eq 'My,Root' -and $policy.scope -eq 'LocalMachine') 'one engine DLL, exact machine stores'
Check ($native -notmatch 'CERT_SYSTEM_STORE_CURRENT_USER|TrustedPublisher|NCryptExportKey|PFXExport|New-SelfSignedCertificate|signtool|RegSetValue') 'no per-user/additional publisher trust, exported key or external signing tool'
Check ($native -match 'BCryptGenRandom' -and $native -match 'BCRYPT_USE_SYSTEM_PREFERRED_RNG' -and $logic -match 'OutLoud.Install.') 'unique OS-generated ownership ID'
Check ($native -match 'NCryptCreatePersistedKey' -and $native -match 'MS_KEY_STORAGE_PROVIDER' -and $native -match 'DWORD bits=3072,export_policy=0,usage=NCRYPT_ALLOW_SIGNING_FLAG') 'modern nonexportable signing-only RSA key'
Check ($native -match 'O:BAG:BAD:P\(A;;GA;;;SY\)\(A;;GA;;;BA\)' -and $native -match 'NCRYPT_SECURITY_DESCR_PROPERTY' -and $native -match 'system_admin_dacl' -and $logic -match 'admin_system_dacl') 'explicit and checked Administrators/SYSTEM-only key ACL'
Check ($native -match 'CERT_NCRYPT_KEY_SPEC' -and $native -match 'szOID_PKIX_KP_CODE_SIGNING' -and $native -match 'CERT_DIGITAL_SIGNATURE_KEY_USAGE' -and $native -match 'szOID_RSA_SHA256RSA') 'certificate EKU, digital-signature and SHA256 properties'
Check ($native -match 'CertCreateSelfSignCertificate' -and $native -match 'end.wYear\+5' -and $native -match 'end.wDay==29' -and $native -match 'CertVerifyTimeValidity') 'checked five-year validity with leap-day handling'
Check ($native -match 'IsUserAnAdmin\(\) && owner==L"S-1-5-32-544"' -and $native -match 'NCRYPT_MACHINE_KEY_FLAG\|NCRYPT_SILENT_FLAG' -and $native -match 'HCCE_LOCAL_MACHINE' -and $native -notmatch 'WTSQuerySessionInformationW') 'elevated machine authority, key and chain scope'
Check ($native -match 'LOAD_LIBRARY_SEARCH_SYSTEM32' -and $native -match 'SignerSignEx2' -and $native -match 'signature.algidHash=CALG_SHA_256') 'OS-native SHA256 Authenticode signing'
Check ($native -match 'CertVerifyCertificateChainPolicy\(CERT_CHAIN_POLICY_AUTHENTICODE' -and $native -match 'WinVerifyTrust' -and $native -match 'WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT') 'chain and Authenticode verification'
Check ($native -match 'signature.thumbprint==record.thumbprint' -and $native -match 'signature.certificate_sha256==record.certificate_sha256' -and $native -match 'sip_digest\(target,file.value\)==expected_digest') 'exact signer and code identity after signing'
Check ($native -match 'FILE_SHARE_READ' -and $native -match 'nNumberOfLinks==1' -and $native -match 'require_installation_location' -and $native -match 'state_acl_writable_by_user') 'locked engine and protected state/path boundaries'
Check ($logic -match 'else \{[\s\S]*?op.load_ownership\(\)' -and $native -match 'owned_certificate_missing' -and $native -match 'NCryptOpenKey' -and $native -notmatch 'CERT_FIND_SUBJECT_STR') 'upgrade exact lookup without arbitrary subject selection'
Check ($native -match 'signing.pending.ini' -and $native -match 'MOVEFILE_WRITE_THROUGH' -and $native -match 'FlushFileBuffers' -and $native -match 'signing.previous.ini') 'durable owned recovery metadata'
Check ($native -notmatch 'CertDeleteCertificateFromStore|NCryptDeleteKey|TerminateProcess' -and $logic -match 'op.verify_required\(\);[\s\S]*?op.commit\(\);') 'no premature cleanup or false success'
Check ($logic -match 'signing_lifecycle_ready=false' -and $native -match 'signing_require\(mutation_gate\(\),"foundation_signing_blocked"\)' -and $iss -match '#ifndef StageRoot') 'standalone signing blocked and stage required'
Check ($wizard -match 'if not FoundationLifecycleReady' -and $wizard -match 'not CheckProductConsent or not CheckOutLoudPrerequisites' -and $iss -match 'PrepareSharedMaintenance\(Result\)') 'consent, runtimes and shared transaction integration'
Check ($wizard -match 'SigningVerified := False' -and $wizard -match "'signing', '', Report\) <> 'verified'" -and $iss -match 'CompleteSharedMaintenance') 'native commit verification required before completion'
Check ($iss -notmatch '(?im)^\[(Registry|Run|UninstallRun|UninstallDelete)\]') 'no unchecked registry/run/uninstall sections'
Check (@((Get-ProductionPolicy).files | Where-Object source -eq 'installer-native').Count -eq 1 -and
    (Get-OutLoudMaintenanceFilesText) -match 'payload\\bin\\OutLoudInstallSigning.exe"; DestName: "maintenance-\d+\.bin"; Flags: dontcopy') 'production helper in native-owned explicit inventory'
Check ((Get-ProductionHash $helper) -eq (Get-ProductionHash (Join-Path $ProductionRepository 'build-installer-prerequisites/Release/OutLoudInstallSigning.exe'))) 'staged helper equals built production component'
Check ($dependencies -match 'IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT' -and $dependencies -match 'CryptCATAdminEnumCatalogFromHash' -and
    $dependencies -match 'WTD_CHOICE_CATALOG' -and $dependencies -match 'vendor_signature_invalid' -and $logic -match 'op.preflight\(\); op.verify_dependencies\(\)') 'transitive import/catalog validation before certificate operations'
$parent=Join-Path $ProductionRepository 'generated/installer/signing-tests'
$scratch=Resolve-ProductionPath $parent ([Guid]::NewGuid().ToString('N'))
Assert-ProductionPlainPath $scratch
[void](New-Item -ItemType Directory -Path $scratch -Force)
function Run-Inspection([string[]]$Arguments){
    $start=New-Object Diagnostics.ProcessStartInfo
    $start.FileName=$helper;$start.Arguments=($Arguments | ForEach-Object {'"'+$_+'"'}) -join ' '
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $process=[Diagnostics.Process]::Start($start)
    try{$out=$process.StandardOutput.ReadToEndAsync();$err=$process.StandardError.ReadToEndAsync();$process.WaitForExit();
        return @{exit=$process.ExitCode;output=$out.Result;error=$err.Result}}
    finally{$process.Dispose()}
}
try{
    $identity=Join-Path $scratch 'identity.ini'
    $probe=Run-Inspection @('--identity',$identity)
    Check ($probe.exit -eq 0 -and (Get-Content -LiteralPath $identity -Raw) -match 'scope=LocalMachine') 'read-only native machine authority probe'
    $again=Run-Inspection @('--identity',$identity)
    Check ($again.exit -ne 0 -and $again.error -match 'report_already_exists') 'existing report cannot be overwritten'
    $image=Join-Path $StageDirectory 'payload/bin/OutLoudPocketSAPI.dll'
    $description=Run-Inspection @('--describe',$image)
    Check ($description.exit -eq 0 -and $description.output -match 'signature=unsigned' -and $description.output -match 'sip_sha256=[0-9a-f]{64}') 'actual unsigned engine and SIP digest inspection'
    $inventory=Run-Inspection @('--dependencies',$image)
    Check ($inventory.exit -eq 0 -and $inventory.output -match 'vendor_signatures=verified' -and
        $inventory.output -match 'msvcp140_atomic_wait.dll,[0-9a-f]{64},valid') 'actual resolved Microsoft dependency closure signature verification'
    # This direct call is an explicit gate test: no Windows operation is reachable.
    $blocked=Run-Inspection @('--prepare')
    Check ($blocked.exit -eq 2 -and $blocked.error -match 'foundation_signing_blocked') 'even direct mutation command is refused before parsing/operations'
    if($SignedReference){
        Assert-ProductionPlainPath $SignedReference
        $hash=Get-ProductionHash $SignedReference
        $signed=Run-Inspection @('--describe',$SignedReference)
        Check ($signed.exit -eq 0 -and $signed.output -match 'signature=valid' -and $signed.output -match 'thumbprint=[0-9a-f]{40}' -and
            $signed.output -match 'certificate_sha256=[0-9a-f]{64}') 'actual existing valid signature and signer identity'
        Check ((Get-ProductionHash $SignedReference) -eq $hash) 'signed reference inspected without mutation'
        $broken=Join-Path $scratch 'broken.dll'
        Copy-Item -LiteralPath $SignedReference -Destination $broken
        $stream=[IO.File]::Open($broken,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
        try{$stream.Position=1024;$value=$stream.ReadByte();$stream.Position=1024;$stream.WriteByte($value -bxor 1)}finally{$stream.Dispose()}
        $invalid=Run-Inspection @('--describe',$broken)
        Check ($invalid.exit -eq 0 -and $invalid.output -match 'signature=invalid') 'actual invalid signature detected on test-owned damaged copy'
    }
    Write-Output "$script:SigningChecks signing integration checks PASS; no keys or certificates created, no files signed."
}finally{
    $verified=Resolve-ProductionPath $parent ([IO.Path]::GetFileName($scratch))
    if($verified -ne $scratch){throw 'Signing scratch containment changed; cleanup refused.'}
    Assert-ProductionPlainPath $scratch
    Remove-Item -LiteralPath $scratch -Recurse -Force
}
