#Requires -Version 5.1
# MANUAL copy-only staging. Does not sign, register, install certificates or run a host.
[CmdletBinding()]
param(
    [string]$ProjectRoot = (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent),
    [string]$BuildDirectory = 'build/Release',
    [Parameter(Mandatory = $true)][string]$DeploymentRoot,
    [ValidateSet('WinUI')][string]$BundleType = 'WinUI',
    [string]$PublishDirectory,
    [string]$WinUIBuildDirectory = 'ui/OutLoud.WinUI/bin/x64/Release/net10.0-windows10.0.26100.0/win-x64',
    [string]$SecurityHelperPath = 'build-installer-prerequisites/Release/OutLoudInstallSigning.exe'
)
. (Join-Path $PSScriptRoot 'deployment-common.ps1')
if (-not [Environment]::Is64BitProcess) { throw 'Use x64 PowerShell.' }
$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$DeploymentRoot = [IO.Path]::GetFullPath($DeploymentRoot)
Assert-BundlePlainPath $DeploymentRoot
if ($DeploymentRoot.TrimEnd('\') -eq [IO.Path]::GetPathRoot($DeploymentRoot).TrimEnd('\')) { throw 'DeploymentRoot cannot be a drive root.' }
if (Test-Path -LiteralPath $DeploymentRoot) { throw 'Use a fresh versioned DeploymentRoot; existing deployments are never overwritten.' }
function Source-Path([string]$Path) {
    if (-not [IO.Path]::IsPathRooted($Path)) { $Path = Join-Path $ProjectRoot $Path }
    Assert-BundlePlainPath $Path
    return (Resolve-Path -LiteralPath $Path).Path
}
$BuildDirectory = Source-Path $BuildDirectory
if ($BundleType -eq 'WinUI') {
    if (-not $PublishDirectory) { throw 'WinUI staging requires an explicit Release PublishDirectory.' }
    $PublishDirectory = Source-Path $PublishDirectory
    $WinUIBuildDirectory = Source-Path $WinUIBuildDirectory
    $SecurityHelperPath = Source-Path $SecurityHelperPath
    $inventory = Read-BundleInventory (Join-Path $ProjectRoot 'sapi')
    $sources = [ordered]@{}
    foreach ($entry in $inventory.files) {
        $base = switch ($entry.source) {
            'native' { $BuildDirectory }
            'system' { Join-Path $env:SystemRoot 'System32' }
            'publish' { $PublishDirectory }
            'winui-build' { $WinUIBuildDirectory }
            'repository' { $ProjectRoot }
            'security-helper' { Split-Path $SecurityHelperPath -Parent }
            default { throw 'Unknown inventory source.' }
        }
        $sources[$entry.path] = Join-Path $base $entry.name
    }
    foreach ($name in @('OutLoudVoiceImportHelper.exe', 'OutLoudVoiceManagerNative.dll')) {
        if ((Get-FileHash -LiteralPath (Join-Path $PublishDirectory $name)).Hash -ne
            (Get-FileHash -LiteralPath (Join-Path $BuildDirectory $name)).Hash) { throw "Published native component differs: $name" }
    }
    if ((Get-FileHash -LiteralPath (Join-Path $PublishDirectory 'OutLoud.WinUI.dll')).Hash -ne
        (Get-FileHash -LiteralPath (Join-Path $WinUIBuildDirectory 'OutLoud.WinUI.dll')).Hash) { throw 'WinUI resources and published assembly are from different builds.' }
    $allowedPublished = @($inventory.files | Where-Object { $_.source -in @('publish', 'winui-build') } | ForEach-Object name) +
        @('OutLoudVoiceImportHelper.exe', 'OutLoudVoiceManagerNative.dll', 'OutLoud.WinUI.pdb')
    foreach ($file in Get-BundleFiles $PublishDirectory) {
        $relative = $file.FullName.Substring($PublishDirectory.TrimEnd('\').Length + 1).Replace('\', '/')
        if ($relative -cnotin $allowedPublished) { throw "Unexpected WinUI publish artifact: $relative" }
        if ($relative -ne 'OutLoud.WinUI.pdb' -and (Get-FileHash -LiteralPath $file.FullName).Hash -ne
            (Get-FileHash -LiteralPath $sources[$relative]).Hash) { throw "Published resource differs: $relative" }
    }
}
# Validate every source BEFORE creating the destination. No downloads or installer.
foreach ($relative in $sources.Keys) {
    if (-not (Test-Path -LiteralPath $sources[$relative] -PathType Leaf)) { throw "Missing source: $($sources[$relative])" }
    Assert-BundlePlainPath $sources[$relative]
    if ($BundleType -eq 'WinUI') {
        $entry = $inventory.files | Where-Object path -CEQ $relative
        Assert-BundlePinnedFile $sources[$relative] $entry
        if ($entry.PSObject.Properties.Name -contains 'signing') {
            Assert-BundleImage $sources[$relative] $entry.machine
            if ($entry.signing -eq 'preserve-microsoft') { Assert-MicrosoftSignature $sources[$relative] }
        }
    }
    if ($relative -match '^bin/(msvcp|vcruntime)') { Assert-MicrosoftSignature $sources[$relative] }
    if ([IO.Path]::GetFullPath($sources[$relative]).StartsWith($DeploymentRoot.TrimEnd('\', '/') + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Deployment must not contain its own source files.'
    }
}
New-Item -ItemType Directory -Path $DeploymentRoot -ErrorAction Stop | Out-Null
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
