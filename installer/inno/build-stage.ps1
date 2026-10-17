#Requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$StageDirectory,
    [string]$ModelPath,
    [string]$TokenizerPath
)
. (Join-Path $PSScriptRoot 'production-common.ps1')
. (Join-Path $PSScriptRoot 'build-components.ps1')
$artifactLock = Enter-OutLoudArtifactLock
try {
$policy = Get-ProductionPolicy
$destination = [IO.Path]::GetFullPath($StageDirectory)
$stageParent = Join-Path $ProductionRepository 'generated/installer/stages'
# Writes are restricted to new workspace stages; existing destinations are never replaced.
if (-not $destination.StartsWith([IO.Path]::GetFullPath($stageParent).TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'StageDirectory must be a new child of generated/installer/stages.'
}
Assert-ProductionPlainPath $destination
if (Test-Path -LiteralPath $destination) { throw 'StageDirectory already exists; choose a new stage, or verify the existing one.' }
$agreement = Get-InstallationAgreementText # Fail on missing/changed legal material before any build.
[void](Get-ProductionSigningPolicy)
if (-not $ModelPath) { $ModelPath = Join-Path $ProductionRepository 'generated/assets/pocket-english.olp' }
if (-not $TokenizerPath) { $TokenizerPath = Join-Path $ProductionRepository 'generated/tokenizer/pocket-english-tokenizer.bin' }
foreach ($asset in @(@{ source = 'model'; path = $ModelPath }, @{ source = 'tokenizer'; path = $TokenizerPath })) {
    $entry = $policy.files | Where-Object { $_.source -eq $asset.source }
    if ((Get-ProductionHash $asset.path) -ne $entry.sha256 -or (Get-Item -LiteralPath $asset.path).Length -ne $entry.bytes) {
        throw "Pinned $($asset.source) asset differs; no conversion or download is performed."
    }
}
$product = Get-OutLoudProductMetadata
Assert-OutLoudInstallerVersionSource
$version = $product.version
$previousCertificateSetting = $env:DOTNET_GENERATE_ASPNET_CERTIFICATE
try {
    $env:DOTNET_GENERATE_ASPNET_CERTIFICATE = 'false'
    $sdk = (& dotnet --version | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $sdk -notmatch '^10\.') { throw 'An installed .NET 10 SDK is required.' }
} finally { $env:DOTNET_GENERATE_ASPNET_CERTIFICATE = $previousCertificateSetting }
$sourceIdentity = Get-ProductionSourceIdentity
$buildWork = Join-Path $ProductionRepository ('generated/installer/builds/' + [Guid]::NewGuid().ToString('N'))
$built = Build-OutLoudComponents -Components Both -WorkDirectory $buildWork -IncludeManagementCli
$native = $built.native
$publish = $built.publish
$apphost = $built.apphost
$apphostHash = $built.apphost_hash
$prerequisites = Get-NativePrerequisiteRequirements $native
$runtimePackages = Get-RuntimePackagePolicy
$vcPackage = $runtimePackages.packages | Where-Object id -eq 'vc'
if ([version]$vcPackage.file_version -lt [version]$prerequisites.vc_minimum) { throw 'Pinned VC package no longer satisfies this native toolchain; review/update its identity.' }
$probeBuild = Join-Path $ProductionRepository 'build-installer-prerequisites'
Assert-ProductionPlainPath $probeBuild
if (Test-Path -LiteralPath (Join-Path $probeBuild 'CMakeCache.txt')) {
    $probeCache = Get-Content -LiteralPath (Join-Path $probeBuild 'CMakeCache.txt') -Raw
    if ($probeCache -notmatch '(?m)^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$' -or
        [IO.Path]::GetFullPath($Matches[1].Trim()) -ne (Join-Path $ProductionRepository 'installer/native')) {
        throw 'Detector cache belongs to a different source directory.'
    }
}
& cmake -S (Join-Path $ProductionRepository 'installer/native') -B $probeBuild -G 'Visual Studio 17 2022' -A x64 "-DOUTLOUD_HOSTFXR_HEADERS=$([IO.Path]::GetDirectoryName($apphost))"
if ($LASTEXITCODE -ne 0) { throw 'Prerequisite detector configuration failed.' }
& cmake --build $probeBuild --config Release --parallel --target OutLoudPrerequisites OutLoudInstallSigning
if ($LASTEXITCODE -ne 0) { throw 'Prerequisite detector build failed.' }
$nativeMatches = @{}
foreach ($name in @('OutLoudVoiceManagerNative.dll', 'OutLoudVoiceImportHelper.exe')) {
    $hash = Get-ProductionHash (Join-Path $native "Release/$name")
    if ((Get-ProductionHash (Join-Path $publish $name)) -ne $hash) { throw "Frontend contains a mismatched native component: $name" }
    $nativeMatches[$name] = $hash
}
$publishedNames = @($policy.files | Where-Object { $_.source -eq 'managed' } | ForEach-Object { $_.name }) + @($nativeMatches.Keys)
foreach ($item in Get-ChildItem -LiteralPath $publish -Force) {
    if ($item.PSIsContainer -or $item.Name -cnotin $publishedNames -or $item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw "Unexpected frontend publish artifact: $($item.Name)"
    }
}
$agreementSource = Join-Path $buildWork 'OutLoud-Installation-Terms.txt'
[IO.File]::WriteAllText($agreementSource, $agreement, (New-Object Text.UTF8Encoding($false)))
$temporary = Join-Path $stageParent ('.partial-' + [Guid]::NewGuid().ToString('N'))
Assert-ProductionPlainPath $temporary
[void](New-Item -ItemType Directory -Path $temporary)
[void](New-Item -ItemType Directory -Path (Join-Path $temporary 'bootstrap'))
[IO.File]::WriteAllText((Join-Path $temporary 'installer-version.iss'), (Get-OutLoudInstallerVersionText $product), [Text.Encoding]::ASCII)
[IO.File]::WriteAllText((Join-Path $temporary 'maintenance-files.iss'), (Get-OutLoudMaintenanceFilesText), [Text.Encoding]::ASCII)
[IO.File]::WriteAllText((Join-Path $temporary 'maintenance-copy.iss'), (Get-OutLoudMaintenanceCopyText), [Text.Encoding]::ASCII)
Copy-Item -LiteralPath (Join-Path $probeBuild 'Release/OutLoudPrerequisites.exe') -Destination (Join-Path $temporary 'bootstrap/OutLoudPrerequisites.exe')
$signingInput = Get-ProductionSignatureDescription (Join-Path $native 'Release/OutLoudPocketSAPI.dll')
if ($signingInput.signature -ne 'unsigned') { throw 'Production engine must remain unsigned until the installation-owned signing operation.' }
[IO.File]::WriteAllText((Join-Path $temporary 'signing-policy.ini'), (Get-ProductionSigningIni $signingInput.sha256 $signingInput.sip_sha256), [Text.Encoding]::ASCII)
[IO.File]::WriteAllText((Join-Path $temporary 'runtime-packages.ini'), (Get-RuntimePackageIni), [Text.Encoding]::ASCII)
@("[requirements]", "vc_minimum=$($prerequisites.vc_minimum)", 'desktop_minimum=10.0.0', 'core_minimum=10.0.0') | Set-Content -LiteralPath (Join-Path $temporary 'prerequisites.ini') -Encoding ASCII
$files = @()
foreach ($entry in $policy.files) {
    switch ($entry.source) {
        'native' { $source = Resolve-ProductionPath (Join-Path $native 'Release') $entry.name }
        'managed' { $source = Resolve-ProductionPath $publish $entry.name }
        'repository' { $source = Resolve-ProductionPath $ProductionRepository $entry.name }
        'consent' { $source = $agreementSource }
        'installer-native' { $source = Resolve-ProductionPath (Join-Path $probeBuild 'Release') $entry.name }
        'model' { $source = [IO.Path]::GetFullPath($ModelPath) }
        'tokenizer' { $source = [IO.Path]::GetFullPath($TokenizerPath) }
        default { throw 'Unknown package source.' }
    }
    $hash = Get-ProductionHash $source
    $target = Resolve-ProductionPath (Join-Path $temporary 'payload') $entry.path
    [void](New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($target)) -Force)
    Copy-Item -LiteralPath $source -Destination $target
    if ((Get-ProductionHash $source) -ne $hash -or (Get-ProductionHash $target) -ne $hash) { throw "Source changed while staging: $($entry.path)" }
    $files += [ordered]@{ path = $entry.path; bytes = (Get-Item -LiteralPath $target).Length; sha256 = $hash }
}
[IO.File]::WriteAllText((Join-Path $temporary 'registration-policy.ini'),
    (Get-ProductionRegistrationIni $files $signingInput.sip_sha256 `
        (Get-ProductionHash (Join-Path $temporary 'bootstrap/OutLoudPrerequisites.exe')) `
        (Get-ProductionHash (Join-Path $temporary 'prerequisites.ini'))), [Text.Encoding]::ASCII)
$record = [ordered]@{
    version = $version; configuration = 'Release'; architecture = 'x64'; build_testing = 'OFF'; development_tools = 'OFF'; avx2 = 'ON'
    file_version = $product.file_version; version_source_sha256 = $product.source_sha256
    product_name = $product.product_name; copyright = $product.copyright
    installer = @{ app_id=$product.app_id; app_version=$version; file_version=$product.file_version; output_filename=$product.installer_filename }
    dotnet_sdk = $sdk; native_build = $native; native_matches = $nativeMatches
    cmake_cache_sha256 = Get-ProductionHash (Join-Path $native 'CMakeCache.txt')
    apphost_assets_sha256 = Get-ProductionHash (Join-Path $ProductionRepository 'ui/OutLoud.VoiceManager/obj/project.assets.json')
    apphost_pack_version = $policy.apphost_pack_version; apphost_source = $apphost; apphost_sha256 = $apphostHash
    source = $sourceIdentity
    supported_platform = 'Windows 11 x64'; prerequisites = $prerequisites
}
[IO.File]::WriteAllText((Join-Path $temporary 'maintenance-input.ini'),
    (Get-OutLoudMaintenanceIni $files (Get-ProductionHash (Join-Path $temporary 'bootstrap/OutLoudPrerequisites.exe')) `
        (Get-ProductionHash (Join-Path $temporary 'prerequisites.ini'))), [Text.Encoding]::ASCII)
$record | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $temporary 'build-record.json') -Encoding UTF8
$manifest = [ordered]@{
    schema = 1; product_id = $policy.product_id; version = $version; status = 'public-release'
    file_version = $product.file_version; version_source_sha256 = $product.source_sha256
    product_name = $product.product_name; copyright = $product.copyright
    installer = $record.installer; installer_version_sha256 = Get-ProductionHash (Join-Path $temporary 'installer-version.iss')
    policy_sha256 = Get-ProductionHash (Join-Path $PSScriptRoot 'package.json')
    icon_sha256 = Get-ProductionHash (Join-Path $ProductionRepository 'ui/OutLoud.VoiceManager/Assets/OutLoud.ico')
    build_record_sha256 = Get-ProductionHash (Join-Path $temporary 'build-record.json'); files = $files
    prerequisites_sha256 = Get-ProductionHash (Join-Path $temporary 'prerequisites.ini')
    detector_sha256 = Get-ProductionHash (Join-Path $temporary 'bootstrap/OutLoudPrerequisites.exe')
    runtime_packages_sha256 = Get-ProductionHash (Join-Path $temporary 'runtime-packages.ini')
    runtime_policy_sha256 = Get-ProductionHash (Join-Path $PSScriptRoot 'runtime-packages.json')
    signing_ini_sha256 = Get-ProductionHash (Join-Path $temporary 'signing-policy.ini')
    signing_policy_sha256 = Get-ProductionHash (Join-Path $PSScriptRoot 'signing-policy.json')
    registration_ini_sha256 = Get-ProductionHash (Join-Path $temporary 'registration-policy.ini')
    maintenance_ini_sha256 = Get-ProductionHash (Join-Path $temporary 'maintenance-input.ini')
    maintenance_files_sha256 = Get-ProductionHash (Join-Path $temporary 'maintenance-files.iss')
    maintenance_copy_sha256 = Get-ProductionHash (Join-Path $temporary 'maintenance-copy.iss')
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $temporary 'package-manifest.json') -Encoding UTF8
$verified = Assert-ProductionStage $temporary
$sourceAfter = Get-ProductionSourceIdentity
if ($sourceAfter.head -ne $sourceIdentity.head -or $sourceAfter.tree_sha256 -ne $sourceIdentity.tree_sha256) {
    throw 'Source inputs changed during the build; partial stage retained, no final stage published.'
}
# Failed/interrupted stages remain for diagnosis. Only this run's completed,
# fully copied intermediate tree is disposable after verified stage publication.
Assert-ProductionPlainPath $destination
if (Test-Path -LiteralPath $destination) { throw 'Destination appeared during staging; nothing replaced.' }
[void](New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($destination)) -Force)
Move-Item -LiteralPath $temporary -Destination $destination
try {
    $workInventory = Get-OutLoudArtifactInventory $buildWork
    Remove-OutLoudOwnedArtifact $buildWork $workInventory 'completed intermediate copied into verified stage'
} catch { Write-Warning "Stage succeeded; intermediate artifacts retained: $($_.Exception.Message)" }
Write-Output "STAGED Out Loud ${version}: $($verified.files) files at $destination"
Write-Output 'Public release stage verified. No installer was compiled or executed.'
return $destination
} finally { Exit-OutLoudArtifactLock $artifactLock }
