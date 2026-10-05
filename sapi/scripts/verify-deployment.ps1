#Requires -Version 5.1
# Read-only validation; does not load the DLL or touch the registry/certificates.
[CmdletBinding()]
param([string]$DeploymentRoot = (Split-Path $PSScriptRoot -Parent), [switch]$RequireSignatures)
. (Join-Path $PSScriptRoot 'deployment-common.ps1')
$DeploymentRoot = (Resolve-Path -LiteralPath $DeploymentRoot).Path
$null = Assert-Bundle $DeploymentRoot $RequireSignatures.IsPresent
foreach ($entry in (Read-BundleInventory $DeploymentRoot).binaries) {
    $path = Resolve-BundlePath $DeploymentRoot $entry.path
    $signature = Get-AuthenticodeSignature -LiteralPath $path
    Write-Output "$($entry.path): x64, $($signature.Status), policy=$($entry.signing)"
}
Write-Output 'PASS: inventory, file hashes, architecture and signature policy. No registry operations.'
