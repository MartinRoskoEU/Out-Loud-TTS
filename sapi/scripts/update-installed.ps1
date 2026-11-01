#Requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('Plan', 'Prepare', 'Deploy')][string]$Action = 'Plan',
    [Parameter(Mandatory = $true)][string]$DeploymentRoot,
    [string]$BuildDirectory = 'build-winui-native/Release',
    [string]$PublishDirectory,
    [string]$WinUIBuildDirectory = 'ui/OutLoud.WinUI/bin/x64/Release/net10.0-windows10.0.26100.0/win-x64',
    [string]$CertificateThumbprint,
    [ValidateSet('CurrentUser', 'LocalMachine')][string]$CertificateStoreLocation = 'CurrentUser',
    [string]$SignToolPath,
    [string]$TimestampUrl
)
. (Join-Path $PSScriptRoot 'installed-update-common.ps1')
if (-not [Environment]::Is64BitProcess) { throw 'Use x64 PowerShell for installed Out Loud discovery.' }
# Installer-managed applications use their native ownership/maintenance contract.
$record = Read-OutLoudUninstallRecord
if ($null -ne $record) {
    if ($PublishDirectory -or $CertificateThumbprint -or $SignToolPath -or $TimestampUrl -or
        $BuildDirectory -ne 'build-winui-native/Release' -or
        $WinUIBuildDirectory -ne 'ui/OutLoud.WinUI/bin/x64/Release/net10.0-windows10.0.26100.0/win-x64') {
        throw 'Installed application updates use the current production build and installation-owned signing certificate. Omit development-bundle build/publish/signing overrides.'
    }
    Invoke-OutLoudInstalledApplicationUpdate -Action $Action -DeploymentRoot $DeploymentRoot -Record $record
    return
}
if (Test-Path -LiteralPath $DeploymentRoot) {
    Invoke-OutLoudDevelopmentBundleUpdate -Action $Action -DeploymentRoot $DeploymentRoot -BuildDirectory $BuildDirectory `
        -PublishDirectory $PublishDirectory -WinUIBuildDirectory $WinUIBuildDirectory -CertificateThumbprint $CertificateThumbprint `
        -CertificateStoreLocation $CertificateStoreLocation -SignToolPath $SignToolPath -TimestampUrl $TimestampUrl
    return
}
Invoke-OutLoudCleanDevelopmentDeployment -Action $Action -DeploymentRoot $DeploymentRoot -BuildDirectory $BuildDirectory `
    -PublishDirectory $PublishDirectory -WinUIBuildDirectory $WinUIBuildDirectory -CertificateThumbprint $CertificateThumbprint `
    -CertificateStoreLocation $CertificateStoreLocation -SignToolPath $SignToolPath -TimestampUrl $TimestampUrl
