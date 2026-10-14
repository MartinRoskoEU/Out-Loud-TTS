#Requires -Version 5.1
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$StageDirectory)
. (Join-Path $PSScriptRoot 'production-common.ps1')
$result = Assert-ProductionStage $StageDirectory
Write-Output "Verified Out Loud $($result.version): $($result.files) allowlisted files; AMD64 images, pinned assets, matching native/frontend, icon and licenses."
$product=Get-OutLoudProductMetadata
Write-Output "Version consistency verified: product $($product.version), files $($product.file_version), planned installer $($product.installer_filename); AppId $($product.app_id)."
Write-Output 'Public release packaging verified. Setup was not executed; final manual release approval remains required.'
