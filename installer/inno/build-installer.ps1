#Requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$StageDirectory,
    [string]$OutputDirectory,
    [string]$CompilerPath = 'C:\Program Files\Inno Setup 7\ISCC.exe'
)
. (Join-Path $PSScriptRoot 'production-common.ps1')
. (Join-Path $PSScriptRoot 'build-components.ps1')
$artifactLock = Enter-OutLoudArtifactLock
try {
$stage = [IO.Path]::GetFullPath($StageDirectory)
$stageParent = [IO.Path]::GetFullPath((Join-Path $ProductionRepository 'generated/installer/stages')).TrimEnd('\')
if (-not $stage.StartsWith($stageParent + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Use a verified workspace production stage.' }
$verified = Assert-ProductionStage $stage
$record = Get-Content -LiteralPath (Join-Path $stage 'build-record.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$identity = Get-ProductionSourceIdentity
if ($record.source.head -cne $identity.head -or $record.source.tree_sha256 -cne $identity.tree_sha256) {
    throw 'Source inputs changed since staging. Rebuild the candidate before compilation.'
}
Assert-ProductionPlainPath $CompilerPath
$compilerVersion = (& $CompilerPath --version | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $compilerVersion -notmatch '^7\.\d+\.\d+$' -or [version]$compilerVersion -lt [version]'7.0.2') {
    throw 'An installed Inno Setup 7 compiler version 7.0.2 or newer is required.'
}
$outputParent = [IO.Path]::GetFullPath((Join-Path $ProductionRepository 'generated/installer/compiler-output')).TrimEnd('\')
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $outputParent ('candidate-' + [Guid]::NewGuid().ToString('N')) }
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (-not $output.StartsWith($outputParent + '\', [StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $output)) {
    throw 'OutputDirectory must be a new child of generated/installer/compiler-output; existing candidates are never replaced.'
}
Assert-ProductionPlainPath $output
[void](New-Item -ItemType Directory -Path $output)
# This intent survives a failed/interrupted compiler and pins its input stage.
[ordered]@{ owner = 'OutLoud installer workflow'; status = 'compiling'; stage = $stage } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'build-input.json') -Encoding UTF8
# Compile only. No IDE-configured signer, installer execution or certificate use.
& $CompilerPath --no-ide-signtools ('/DStageRoot=' + $stage) ('/O' + $output) (Join-Path $PSScriptRoot 'OutLoud.iss') | Out-Host
if ($LASTEXITCODE -ne 0) { throw "Inno compilation failed; diagnostic output retained at $output" }
[void](Assert-ProductionStage $stage)
$after = Get-ProductionSourceIdentity
if ($after.head -cne $identity.head -or $after.tree_sha256 -cne $identity.tree_sha256) { throw 'Source changed during compilation. Output is not a qualified candidate.' }
$product = Get-OutLoudProductMetadata
$installer = Join-Path $output $product.installer_filename
Assert-ProductionPlainPath $installer
$info = [Diagnostics.FileVersionInfo]::GetVersionInfo($installer)
$fixedVersion = '{0}.{1}.{2}.{3}' -f $info.FileMajorPart,$info.FileMinorPart,$info.FileBuildPart,$info.FilePrivatePart
$fixedProduct = '{0}.{1}.{2}.{3}' -f $info.ProductMajorPart,$info.ProductMinorPart,$info.ProductBuildPart,$info.ProductPrivatePart
# Inno pads editable resource strings. Keep exact numeric identity checks and
# normalize only that resource padding when comparing displayed text.
if ($fixedVersion -cne $product.file_version -or $fixedProduct -cne $product.file_version -or
    $info.FileVersion.Trim() -cne $product.file_version -or $info.ProductVersion.Trim() -cne $product.version -or
    $info.ProductName.Trim() -cne $product.product_name -or $info.LegalCopyright.Trim() -cne $product.copyright) {
    throw 'Compiled installer product/version/copyright differs.'
}
$signature = Get-AuthenticodeSignature -LiteralPath $installer
if ($signature.Status -ne 'NotSigned') { throw 'Unexpected installer signature. Publisher signing requires a separately reviewed distribution step.' }
[ordered]@{
    status = 'public-release'; version = $product.version; installer = $installer
    sha256 = Get-ProductionHash $installer; bytes = (Get-Item -LiteralPath $installer).Length
    compiler_version = $compilerVersion; publisher_signed = $false
    stage = $stage; manifest_sha256 = Get-ProductionHash (Join-Path $stage 'package-manifest.json')
    source = $identity; executed = $false; validation_scope = 'build_and_packaging'; manual_release_approval = 'pending'
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $output 'installer-build.json') -Encoding UTF8
Write-Output "BUILT $installer"
& (Join-Path $PSScriptRoot 'publish-installer.ps1') -BuildDirectory $output
Write-Output 'Public release installer prepared for final manual approval. Publisher unsigned; setup was not executed.'
} finally { Exit-OutLoudArtifactLock $artifactLock }
