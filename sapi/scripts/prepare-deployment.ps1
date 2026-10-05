#Requires -Version 5.1
# MANUAL copy-only staging. Does not sign, register, install certificates or run a host.
[CmdletBinding()]
param(
    [string]$ProjectRoot = (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent),
    [string]$BuildDirectory = 'build/Release',
    [Parameter(Mandatory = $true)][string]$DeploymentRoot,
    [string]$ModelPath = 'generated/assets/pocket-english.olp',
    [string]$TokenizerPath = 'generated/tokenizer/pocket-english-tokenizer.bin',
    [string]$VoicePath = 'generated/assets/gnome_male.olv'
)
. (Join-Path $PSScriptRoot 'deployment-common.ps1')
if (-not [Environment]::Is64BitProcess) { throw 'Use x64 PowerShell.' }
$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$DeploymentRoot = [IO.Path]::GetFullPath($DeploymentRoot)
if (Test-Path -LiteralPath $DeploymentRoot) { throw 'Use a fresh versioned DeploymentRoot; existing deployments are never overwritten.' }
function Source-Path([string]$Path) {
    if (-not [IO.Path]::IsPathRooted($Path)) { $Path = Join-Path $ProjectRoot $Path }
    return (Resolve-Path -LiteralPath $Path).Path
}
$BuildDirectory = Source-Path $BuildDirectory
$sources = [ordered]@{
    'bin/OutLoudPocketSAPI.dll' = (Join-Path $BuildDirectory 'OutLoudPocketSAPI.dll')
    'bin/outloud-sapi-check.exe' = (Join-Path $BuildDirectory 'outloud-sapi-check.exe')
    'bin/msvcp140.dll' = (Join-Path $env:SystemRoot 'System32/msvcp140.dll')
    'bin/msvcp140_atomic_wait.dll' = (Join-Path $env:SystemRoot 'System32/msvcp140_atomic_wait.dll')
    'bin/vcruntime140.dll' = (Join-Path $env:SystemRoot 'System32/vcruntime140.dll')
    'bin/vcruntime140_1.dll' = (Join-Path $env:SystemRoot 'System32/vcruntime140_1.dll')
    'runtime/pocket-english.olp' = (Source-Path $ModelPath)
    'runtime/pocket-english-tokenizer.bin' = (Source-Path $TokenizerPath)
    'voices/gnome_male.olv' = (Source-Path $VoicePath)
    'deployment-inventory.json' = (Join-Path $ProjectRoot 'sapi/deployment-inventory.json')
    'docs/SAPI_ADAPTER.md' = (Join-Path $ProjectRoot 'docs/SAPI_ADAPTER.md')
    'docs/SAPI_CANCELLATION.md' = (Join-Path $ProjectRoot 'docs/SAPI_CANCELLATION.md')
    'RNG_NOTICES.md' = (Join-Path $ProjectRoot 'RNG_NOTICES.md')
}
foreach ($name in @('deployment-common.ps1', 'prepare-deployment.ps1', 'sign-deployment.ps1', 'verify-deployment.ps1',
    'registry-common.ps1', 'register.ps1', 'unregister.ps1')) {
    $sources["scripts/$name"] = Join-Path $PSScriptRoot $name
}
# Validate every source BEFORE creating the destination. No downloads or installer.
foreach ($relative in $sources.Keys) {
    if (-not (Test-Path -LiteralPath $sources[$relative] -PathType Leaf)) { throw "Missing source: $($sources[$relative])" }
    if ($relative -match '^bin/') { Assert-X64Image $sources[$relative] }
    if ($relative -match '^bin/(msvcp|vcruntime)') { Assert-MicrosoftSignature $sources[$relative] }
    if ([IO.Path]::GetFullPath($sources[$relative]).StartsWith($DeploymentRoot.TrimEnd('\', '/') + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Deployment must not contain its own source files.'
    }
}
$files = @()
foreach ($relative in $sources.Keys) {
    $source = $sources[$relative]
    $before = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
    $target = Resolve-BundlePath $DeploymentRoot $relative
    New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $target -ErrorAction Stop
    $after = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($before -ne $after -or (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $before) { throw "Copy/source changed: $relative" }
    $files += [ordered]@{ path = $relative; source = $source; source_sha256 = $before; sha256 = $after; size_bytes = (Get-Item -LiteralPath $target).Length }
}
[ordered]@{ schema = 1; created_utc = [DateTime]::UtcNow.ToString('o'); signing_executed = $false; files = $files } |
    ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $DeploymentRoot 'deployment.json') -Encoding UTF8
$null = Assert-Bundle $DeploymentRoot $false
Write-Output "COPY ONLY complete: $DeploymentRoot. Next: manual sign-deployment.ps1, then verify-deployment.ps1 -RequireSignatures."
