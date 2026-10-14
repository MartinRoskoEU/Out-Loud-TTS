#Requires -Version 5.1
# Read-only metadata and native inspection only; never invokes --prepare.
function Get-ProductionSigningPolicy {
    $policy = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'signing-policy.json') -Raw | ConvertFrom-Json
    if ($policy.schema -ne 1 -or $policy.target -ne 'bin/OutLoudPocketSAPI.dll' -or
        $policy.scope -ne 'LocalMachine' -or $policy.stores -ne 'My,Root' -or
        $policy.key_provider -ne 'Microsoft Software Key Storage Provider' -or $policy.key_algorithm -ne 'RSA' -or
        $policy.key_bits -ne 3072 -or $policy.file_digest -ne 'SHA256' -or $policy.certificate_signature -ne 'RSA-SHA256' -or
        $policy.code_signing_eku -ne '1.3.6.1.5.5.7.3.3' -or $policy.certificate_years -ne 5 -or
        $policy.key_export_policy -ne 'nonexportable' -or $policy.key_access -ne 'elevated Administrators and SYSTEM only' -or
        $policy.ownership_path -ne 'configuration/signing.ini' -or $policy.pending_path -ne 'configuration/signing.pending.ini' -or
        $policy.require_elevated_machine_authority -ne $true) { throw 'Unexpected production signing policy.' }
    return $policy
}
function Get-ProductionSignatureDescription([string]$Image) {
    Assert-ProductionPlainPath $Image
    # Use the trusted local build, never execute an untrusted staged helper.
    $helper = Join-Path $ProductionRepository 'build-installer-prerequisites/Release/OutLoudInstallSigning.exe'
    Assert-ProductionPlainPath $helper
    $lines = @(& $helper --describe $Image)
    if ($LASTEXITCODE -ne 0) { throw 'Read-only signing inspection failed.' }
    $result = @{}
    foreach ($line in $lines) { if ($line -match '^([^=]+)=(.*)$') { $result[$Matches[1]] = $Matches[2] } }
    if ($result.schema -ne '1' -or $result.sha256 -notmatch '^[0-9a-f]{64}$' -or $result.sip_sha256 -notmatch '^[0-9a-f]{64}$') { throw 'Invalid signing inspection report.' }
    return $result
}
function Get-ProductionSigningIni([string]$UnsignedHash, [string]$SipHash) {
    $policy = Get-ProductionSigningPolicy
    if ($UnsignedHash -notmatch '^[0-9a-f]{64}$' -or $SipHash -notmatch '^[0-9a-f]{64}$') { throw 'Invalid signing input hashes.' }
    return "[signing]`nschema=1`ntarget=$($policy.target)`nscope=$($policy.scope)`nstores=$($policy.stores)`nunsigned_sha256=$UnsignedHash`nsip_sha256=$SipHash`n"
}
