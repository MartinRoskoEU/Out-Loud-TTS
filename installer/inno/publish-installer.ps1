#Requires -Version 5.1
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$BuildDirectory, [switch]$PreserveHistoricalInstallers)
. (Join-Path $PSScriptRoot 'production-common.ps1')
. (Join-Path $PSScriptRoot 'build-components.ps1')
$artifactLock = Enter-OutLoudArtifactLock
try {
$output = [IO.Path]::GetFullPath($BuildDirectory).TrimEnd('\', '/')
$outputParent = Join-Path $ProductionRepository 'generated/installer/compiler-output'
if (-not $output.StartsWith($outputParent + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Use a recorded workspace installer build.' }
Assert-ProductionPlainPath $output
$recordPath = Join-Path $output 'installer-build.json'
Assert-ProductionPlainPath $recordPath
$record = Get-Content -LiteralPath $recordPath -Raw -Encoding UTF8 | ConvertFrom-Json
$product = Get-OutLoudProductMetadata
$installer = Join-Path $output $product.installer_filename
if ($record.status -cne 'public-release' -or $record.version -cne $product.version -or
    $record.installer -cne $installer -or $record.sha256 -cnotmatch '^[0-9a-f]{64}$' -or
    $record.publisher_signed -ne $false -or $record.compiler_version -notmatch '^7\.\d+\.\d+$' -or
    [version]$record.compiler_version -lt [version]'7.0.2') { throw 'Installer build is incomplete or has an unexpected identity.' }
$stage = [IO.Path]::GetFullPath($record.stage)
$stageParent = Join-Path $ProductionRepository 'generated/installer/stages'
if (-not $stage.StartsWith($stageParent + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Build provenance is outside the production stages.' }
[void](Assert-ProductionStage $stage)
$stageRecord = Get-Content -LiteralPath (Join-Path $stage 'build-record.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if ((Get-ProductionHash (Join-Path $stage 'package-manifest.json')) -cne $record.manifest_sha256 -or
    $stageRecord.source.head -cne $record.source.head -or $stageRecord.source.tree_sha256 -cne $record.source.tree_sha256) {
    throw 'Installer build and stage provenance differ.'
}
# Historical validated builds retain their recorded source identity; subsequent
# documentation/build-script edits do not require recompiling their executable.
if ((Get-ProductionHash $installer) -cne $record.sha256 -or (Get-Item -LiteralPath $installer).Length -ne $record.bytes) {
    throw 'Installer differs from its validated build output.'
}
$info = [Diagnostics.FileVersionInfo]::GetVersionInfo($installer)
$numericFile = '{0}.{1}.{2}.{3}' -f $info.FileMajorPart,$info.FileMinorPart,$info.FileBuildPart,$info.FilePrivatePart
$numericProduct = '{0}.{1}.{2}.{3}' -f $info.ProductMajorPart,$info.ProductMinorPart,$info.ProductBuildPart,$info.ProductPrivatePart
if ($numericFile -cne $product.file_version -or $numericProduct -cne $product.file_version -or
    $info.FileVersion.Trim() -cne $product.file_version -or $info.ProductVersion.Trim() -cne $product.version -or
    $info.ProductName.Trim() -cne $product.product_name -or $info.LegalCopyright.Trim() -cne $product.copyright -or
    (Get-AuthenticodeSignature -LiteralPath $installer).Status -ne 'NotSigned') { throw 'Installer product/signature identity differs.' }

$ownedPublicationFiles = @()
$previousPublication = $null
$publicationRecordPath = Join-Path $output 'publication-record.json'
if (Test-Path -LiteralPath $publicationRecordPath) {
    Assert-ProductionPlainPath $publicationRecordPath
    $previousPublication = Get-Content -LiteralPath $publicationRecordPath -Raw | ConvertFrom-Json
    if ($previousPublication.owner -cne 'OutLoud installer workflow' -or $previousPublication.output -cne $output -or
        $previousPublication.stage -cne $stage -or $previousPublication.status -cne 'published') { throw 'Previous publication ownership differs.' }
    Assert-OutLoudArtifactInventory $output $previousPublication.output_inventory @('publication-record.json')
    $ownedPublicationFiles = @($previousPublication.output_inventory.files | ForEach-Object { Resolve-ProductionPath $output $_.path })
}

$release = Join-Path $ProductionRepository 'release'
Assert-ProductionPlainPath $release
if (-not (Test-Path -LiteralPath $release)) { [void](New-Item -ItemType Directory -Path $release) }
$destination = Join-Path $release $product.installer_filename
Assert-ProductionPlainPath $destination
$serial = [Guid]::NewGuid().ToString('N')
$temporary = Join-Path $release ('.publish-' + $serial + '.tmp')
$backup = Join-Path $output ('release-previous-' + $serial + '.exe')
$lockPath = Join-Path $outputParent 'release.publish.lock'
Assert-ProductionPlainPath $lockPath
$lock = [IO.File]::Open($lockPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
$promoted = $false
try {
    # Copy and verify on the destination volume before touching a previous EXE.
    [IO.File]::Copy($installer, $temporary, $false)
    if ((Get-ProductionHash $temporary) -cne $record.sha256) { throw 'Release copy SHA-256 differs.' }
    $previous = Test-Path -LiteralPath $destination
    try {
        if ($previous) { [IO.File]::Replace($temporary, $destination, $backup) }
        else { [IO.File]::Move($temporary, $destination) }
        $promoted = $true
        if ((Get-ProductionHash $destination) -cne $record.sha256) { throw 'Published installer SHA-256 differs.' }
        if ($previous) { $ownedPublicationFiles += $backup }
    } catch {
        # Retain/restore the previous EXE if replacement or final verification fails.
        if (Test-Path -LiteralPath $backup) {
            if (Test-Path -LiteralPath $destination) { [IO.File]::Replace($backup, $destination, $null) }
            else { [IO.File]::Move($backup, $destination) }
        } elseif ($promoted -and -not $previous) { [IO.File]::Delete($destination) }
        throw
    }
    # Only after verified publication, archive older versioned installers outside
    # release/. Preserve unrelated entries. Artifact retention runs only after
    # verified publication and never operates on release/ itself.
    foreach ($old in Get-ChildItem -LiteralPath $release -File -Force) {
        if ($PreserveHistoricalInstallers) { continue }
        if ($old.Name -cne $product.installer_filename -and $old.Name -cmatch '^OutLoud-Setup-\d+\.\d+\.\d+\.exe$') {
            try {
                Assert-ProductionPlainPath $old.FullName
                $archive = Join-Path $output ('release-obsolete-' + $serial + '-' + $old.Name)
                [IO.File]::Move($old.FullName, $archive)
                $ownedPublicationFiles += $archive
            } catch { Write-Warning "Published the current installer, but preserved an obsolete release file: $($old.FullName). $($_.Exception.Message)" }
        }
    }
    Write-Output "PUBLISHED $destination"
    Write-Output "SHA-256 $($record.sha256)"
    try {
        Write-OutLoudPublicationRecord $output $stage $ownedPublicationFiles
        # Re-publication of the same output must not accumulate a new full EXE
        # backup every time. Keep this publication's previous release, and retain
        # externally referenced backups. Unrecorded legacy files are never pruned.
        Remove-OutLoudPublicationBackups $output $ownedPublicationFiles $record.sha256
        $ownedPublicationFiles = @($ownedPublicationFiles | Where-Object { Test-Path -LiteralPath $_ })
        Write-OutLoudPublicationRecord $output $stage $ownedPublicationFiles
        Invoke-OutLoudArtifactRetention $output
    } catch { Write-Warning "Publication succeeded; automatic artifact cleanup retained uncertain artifacts: $($_.Exception.Message)" }
} finally {
    try { if (Test-Path -LiteralPath $temporary) { [IO.File]::Delete($temporary) } }
    finally { $lock.Dispose() }
}
} finally { Exit-OutLoudArtifactLock $artifactLock }
