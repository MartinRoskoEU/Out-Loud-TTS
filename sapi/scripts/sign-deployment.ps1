#Requires -Version 5.1
# MANUAL SIGNING ONLY. Never signs build sources, creates/imports certificates or registers.
[CmdletBinding()]
param(
    [string]$DeploymentRoot = (Split-Path $PSScriptRoot -Parent),
    [Parameter(Mandatory = $true)][ValidatePattern('^[0-9a-fA-F]{40}$')][string]$CertificateThumbprint,
    [ValidateSet('CurrentUser', 'LocalMachine')][string]$CertificateStoreLocation = 'CurrentUser',
    [string]$SignToolPath,
    [string]$TimestampUrl
)
. (Join-Path $PSScriptRoot 'deployment-common.ps1')
$DeploymentRoot = (Resolve-Path -LiteralPath $DeploymentRoot).Path
$manifest = Assert-Bundle $DeploymentRoot $false
$inventory = Read-BundleInventory $DeploymentRoot
$cert = Get-Item -LiteralPath "Cert:\$CertificateStoreLocation\My\$CertificateThumbprint"
if (-not $cert.HasPrivateKey) { throw 'Selected existing My-store certificate has no accessible private key.' }
$usages = @($cert.Extensions | Where-Object { $_ -is [Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension] } |
    ForEach-Object { $_.EnhancedKeyUsages } | Where-Object { $_.Value -eq '1.3.6.1.5.5.7.3.3' })
if ($usages.Count -eq 0) {
    throw 'Select an existing code-signing certificate.'
}
if (-not $SignToolPath) {
    $SignToolPath = Get-ChildItem -LiteralPath (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/bin') -Filter signtool.exe -Recurse |
        Where-Object { $_.Directory.Name -eq 'x64' } | Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $SignToolPath -or -not (Test-Path -LiteralPath $SignToolPath -PathType Leaf)) { throw 'Supply an existing SDK SignToolPath.' }
if ($TimestampUrl -and $TimestampUrl -notmatch '^https?://') { throw 'TimestampUrl must be an explicit HTTP(S) RFC3161 server.' }
# Preflight all targets and signer identities BEFORE any signing operation.
foreach ($entry in $inventory.binaries) {
    $path = Resolve-BundlePath $DeploymentRoot $entry.path
    $signature = Get-AuthenticodeSignature -LiteralPath $path
    if ($entry.signing -eq 'project-sign' -and $signature.Status -eq 'Valid' -and
        $signature.SignerCertificate.Thumbprint -ne $CertificateThumbprint) { throw "Unexpected existing project signer: $path" }
}
foreach ($entry in $inventory.binaries) {
    $target = Resolve-BundlePath $DeploymentRoot $entry.path
    $signature = Get-AuthenticodeSignature -LiteralPath $target
    if ($entry.signing -eq 'preserve-microsoft') {
        Assert-MicrosoftSignature $target
        Write-Output "PRESERVED Microsoft signature: $($entry.path)"
    } else {
        if ($signature.Status -eq 'NotSigned') {
            $arguments = @('sign', '/sha1', $CertificateThumbprint, '/s', 'My', '/fd', 'SHA256')
            if ($CertificateStoreLocation -eq 'LocalMachine') { $arguments += '/sm' }
            if ($TimestampUrl) { $arguments += @('/tr', $TimestampUrl, '/td', 'SHA256') }
            $arguments += $target
            & $SignToolPath @arguments
            if ($LASTEXITCODE -ne 0) { throw "SignTool signing failed: $target. Staged bundle may be partly signed; do not register it." }
            Write-Output "SIGNED: $($entry.path)"
        } else { Write-Output "ALREADY SIGNED by selected certificate: $($entry.path)" }
        $signature = Get-AuthenticodeSignature -LiteralPath $target
        if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Thumbprint -ne $CertificateThumbprint) { throw "Project signature verification failed: $target" }
    }
    & $SignToolPath verify /pa /all /v $target
    if ($LASTEXITCODE -ne 0) { throw "SignTool verification failed: $target" }
    $record = $manifest.files | Where-Object { $_.path -eq $entry.path }
    $record.sha256 = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
    $record.size_bytes = (Get-Item -LiteralPath $target).Length
}
$manifest.signing_executed = $true
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $DeploymentRoot 'deployment.json') -Encoding UTF8
$null = Assert-Bundle $DeploymentRoot $true
Write-Output 'PASS: all six shipped PE images have valid signatures. Registration is a separate manual action.'
