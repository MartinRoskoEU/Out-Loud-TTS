[CmdletBinding()]
param(
    [string]$Compiler,
    [string]$VcRedistributable,
    [string]$StageRoot,
    [switch]$SkipComponentBuild,
    [switch]$PrepareOnly
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE = '1'
$env:DOTNET_GENERATE_ASPNET_CERTIFICATE = 'false'
$env:DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE = '1'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $repository 'sapi/scripts/deployment-common.ps1')
if (!$StageRoot) { $StageRoot = Join-Path $repository ('generated/production-installer/' + [Guid]::NewGuid().ToString('N')) }
$StageRoot = [IO.Path]::GetFullPath($StageRoot)
if (!$PrepareOnly -and (!$Compiler -or !(Test-Path -LiteralPath $Compiler -PathType Leaf))) { throw 'Provide the reviewed Inno Setup 7.0.2+ ISCC.exe.' }
if (Test-Path -LiteralPath $StageRoot) { throw 'Staging requires a new directory; existing outputs are never cleaned or adopted.' }
if (!$StageRoot.StartsWith($repository + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Keep build staging inside the repository.' }
[xml]$versionFile = Get-Content -LiteralPath (Join-Path $repository 'ProductVersion.props')
$version = [string]$versionFile.Project.PropertyGroup.OutLoudProductVersion
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid authoritative product version.' }
$vcHash = '843068991daaa1f73ad9f6239bce4d0f6a07a51f18c37ea2a867e9beca71295c'
if (!$PrepareOnly) {
    if (!$VcRedistributable) { throw 'Provide the reviewed official VC x64 redistributable.' }
    if ((Get-FileHash -LiteralPath $VcRedistributable -Algorithm SHA256).Hash.ToLowerInvariant() -cne $vcHash -or
        (Get-Item -LiteralPath $VcRedistributable).Length -ne 18731856) { throw 'VC redistributable does not match the reviewed official 14.51.36247 x64 package.' }
    $vcSignature = Get-AuthenticodeSignature -LiteralPath $VcRedistributable
    if ($vcSignature.Status -ne 'Valid' -or $vcSignature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') { throw 'VC redistributable signature is not valid Microsoft Authenticode.' }
}
function Invoke-Checked([string]$File, [string[]]$Arguments) {
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$File failed with exit code $LASTEXITCODE" }
}
function Write-Ascii([string]$Path, [string]$Value) { [IO.File]::WriteAllText($Path, $Value, [Text.Encoding]::ASCII) }
function Add-DependencyNotices([string]$NugetRoot, [string]$AssetsPath) {
    # Retain the original legal files for the packages resolved by this publish.
    $assets = Get-Content -LiteralPath $AssetsPath -Raw | ConvertFrom-Json
    foreach ($package in $assets.libraries.PSObject.Properties) {
        if ($package.Value.type -ne 'package') { continue }
        $packagePath = Join-Path $NugetRoot $package.Value.path
        foreach ($notice in Get-ChildItem -LiteralPath $packagePath -File -Recurse | Where-Object {
            $_.Name -match '^(license|licence|notice|third[-_. ]?party[-_. ]?notices?|copying|sdk_license)([._ -]|$)'
        }) {
            Assert-BundlePlainPath $notice.FullName
            $relative = 'licenses/nuget/' + $package.Value.path + '/' + $notice.FullName.Substring($packagePath.TrimEnd('\').Length + 1).Replace('\', '/')
            $destination = Join-Path $payload $relative
            $null = New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force
            Copy-Item -LiteralPath $notice.FullName -Destination $destination
            Add-Row $relative $false $false $false
        }
    }
    $runtimeNotices = Join-Path $repository 'licenses/pocket-tts'
    foreach ($name in 'Cargo.lock','components.json','THIRD-PARTY-NOTICES.txt','SUPPLEMENTAL-NOTICES.txt','SOURCE-AVAILABILITY.txt') {
        Assert-BundlePlainPath (Join-Path $runtimeNotices $name)
        if (!(Test-Path -LiteralPath (Join-Path $runtimeNotices $name) -PathType Leaf)) { throw "Missing Rust runtime notice: $name" }
    }
    $lockPath = Join-Path $runtimeNotices 'Cargo.lock'
    if ((Get-FileHash -LiteralPath $lockPath -Algorithm SHA256).Hash -cne '3F2C5396EAAAD4C64258AE724E012FDAA0C7B3E71165BCF0EF2C6B0641D18D27') {
        throw 'Runtime notice lockfile does not match the bundled runtime source revision.'
    }
    $lock = [IO.File]::ReadAllText($lockPath)
    foreach ($component in (Get-Content -LiteralPath (Join-Path $runtimeNotices 'components.json') -Raw | ConvertFrom-Json)) {
        if ($component.license -ne 'MPL-2.0') { continue }
        if ($component.source_archive -cnotmatch '^sources/[A-Za-z0-9_.-]+\.crate$' -or $component.checksum -cnotmatch '^[a-f0-9]{64}$') { throw 'Invalid MPL source record.' }
        $pattern = '(?m)^name = "' + [regex]::Escape($component.name) + '"\r?\nversion = "' + [regex]::Escape($component.version) +
            '"\r?\nsource = "registry[^"\r\n]*"\r?\nchecksum = "' + [regex]::Escape($component.checksum) + '"'
        $source = Join-Path $runtimeNotices $component.source_archive
        Assert-BundlePlainPath $source
        if ($lock -cnotmatch $pattern -or (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant() -cne $component.checksum) {
            throw "MPL source archive differs from the runtime lockfile: $($component.name)"
        }
    }
    foreach ($notice in Get-BundleFiles $runtimeNotices) {
        $relative = 'licenses/pocket-tts/' + $notice.FullName.Substring($runtimeNotices.Length + 1).Replace('\', '/')
        $destination = Join-Path $payload $relative
        $null = New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force
        Copy-Item -LiteralPath $notice.FullName -Destination $destination
        Add-Row $relative $false $false $false
    }
}
function Remove-PreviousInstallerStages([string]$CurrentStage) {
    $stageParent = [IO.Path]::GetFullPath((Join-Path $repository 'generated/production-installer'))
    $current = [IO.Path]::GetFullPath($CurrentStage).TrimEnd('\')
    if (!(Test-Path -LiteralPath $stageParent -PathType Container)) { return }
    foreach ($candidate in Get-ChildItem -LiteralPath $stageParent -Directory) {
        $target = [IO.Path]::GetFullPath($candidate.FullName).TrimEnd('\')
        if ($candidate.Name -cnotmatch '^[a-f0-9]{32}$' -or $target -eq $current) { continue }
        # Never remove a caller-supplied dependency used by this or future builds.
        if ($VcRedistributable -and [IO.Path]::GetFullPath($VcRedistributable).StartsWith($target + '\', [StringComparison]::OrdinalIgnoreCase)) { continue }
        try {
            if ([IO.Path]::GetDirectoryName($target) -ne $stageParent) { throw 'Stage escaped the installer build directory.' }
            Assert-BundlePlainPath $target
            $marker = Join-Path $target 'build-complete.json'
            if (!(Test-Path -LiteralPath $marker -PathType Leaf)) { continue }
            Assert-BundlePlainPath $marker
            if ((Get-Item -LiteralPath $marker).Length -gt 4096) { throw 'Invalid build completion record.' }
            $record = Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json
            if ($record.schema -ne 1 -or $record.kind -cne 'OutLoud.InstallerBuild' -or
                $record.stage -ine $target -or $record.installer_sha256 -cnotmatch '^[a-f0-9]{64}$') { throw 'Unverified build completion record.' }
            # Check the whole tree before recursive removal; never traverse links.
            $pending = [Collections.Generic.Stack[string]]::new(); $pending.Push($target)
            while ($pending.Count) {
                foreach ($entry in Get-ChildItem -LiteralPath ($pending.Pop()) -Force) {
                    if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Linked stage content was preserved.' }
                    if ($entry.PSIsContainer) { $pending.Push($entry.FullName) }
                }
            }
            Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction Stop
            Write-Host "Removed previous completed installer stage: $target"
        } catch { Write-Warning "Previous installer stage was preserved: $target. $($_.Exception.Message)" }
    }
}
$null = New-Item -ItemType Directory -Path $StageRoot
$payload = Join-Path $StageRoot 'payload'
$native = Join-Path $repository 'build-winui-native'
if (!$SkipComponentBuild) {
    if (Test-Path -LiteralPath (Join-Path $native 'CMakeCache.txt')) {
        Invoke-Checked cmake @('-S',$repository,'-B',$native)
    } else {
        Invoke-Checked cmake @('-S',$repository,'-B',$native,'-A','x64','-DBUILD_TESTING=OFF')
    }
    Invoke-Checked cmake @('--build',$native,'--config','Release','--target','OutLoudPocketSAPI','OutLoudVoiceManagerNative','OutLoudVoiceImportHelper','outloud-sapi-voices')
}
Invoke-Checked dotnet @('publish',(Join-Path $repository 'ui/OutLoud.WinUI/OutLoud.WinUI.csproj'),'-c','Release','-p:Platform=x64','-r','win-x64','--self-contained','true',
    '-p:WindowsAppSDKSelfContained=true','-p:PublishTrimmed=false','-p:PublishReadyToRun=false',
    "-p:NativeBuildDirectory=$native", "-p:OutputPath=$StageRoot/build/", "-p:MSBuildProjectExtensionsPath=$StageRoot/restore/", "-p:IntermediateOutputPath=$StageRoot/obj/",'-o',$payload,'-v:minimal')
$layout = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'payload-layout.json') -Raw | ConvertFrom-Json
foreach($entry in $layout.files) {
    if ($entry.PSObject.Properties.Name -contains 'source' -and $entry.source -eq 'winui-build') {
        Copy-Item -LiteralPath (Join-Path $StageRoot ('build/'+$entry.path)) -Destination (Join-Path $payload $entry.path)
    }
}
$expected = @($layout.files.path)
$actual = @(Get-ChildItem -LiteralPath $payload -File -Recurse | Where-Object Extension -ne '.pdb' | ForEach-Object { $_.FullName.Substring($payload.Length+1).Replace('\','/') })
if (Compare-Object ($expected|Sort-Object) ($actual|Sort-Object)) { throw 'WinUI publish layout differs from the reviewed payload allowlist.' }
$runtimeConfig = Get-Content -LiteralPath (Join-Path $payload 'OutLoud.WinUI.runtimeconfig.json') -Raw | ConvertFrom-Json
if ($runtimeConfig.runtimeOptions.PSObject.Properties.Name -contains 'framework' -or $runtimeConfig.runtimeOptions.PSObject.Properties.Name -contains 'frameworks' -or
    @($runtimeConfig.runtimeOptions.includedFrameworks | Where-Object { $_.name -eq 'Microsoft.NETCore.App' -and $_.version -like '10.*' }).Count -ne 1) { throw 'Publish is not self-contained .NET 10.' }
foreach($name in 'coreclr.dll','hostfxr.dll','Microsoft.UI.Xaml.dll','Microsoft.WindowsAppRuntime.dll','OutLoud.WinUI.pri') {
    if (!(Test-Path -LiteralPath (Join-Path $payload $name) -PathType Leaf)) { throw "Missing self-contained dependency: $name" }
}
$rows = [Collections.Generic.List[object]]::new()
function Add-Row([string]$Path,[bool]$Pe,[bool]$Product,[bool]$Sign,[bool]$Pinned=$true) {
    if ($Path -cnotmatch '^[A-Za-z0-9_./-]+$' -or $Path -match '(^|/)\.\.?(/|$)') { throw "Unsafe payload path: $Path" }
    $file=Get-Item -LiteralPath (Join-Path $payload $Path)
    if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Linked payload file: $Path" }
    $rows.Add([ordered]@{path=$Path;pe=$Pe;product_version=$Product;sign=$Sign;bytes=if($Pinned){$file.Length}else{0};sha256=if($Pinned){(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}else{''}})
}
foreach($entry in $layout.files) {
    $path=Join-Path $payload $entry.path
    Assert-BundlePlainPath $path
    if ($entry.PSObject.Properties.Name -contains 'machine') {
        if ($entry.machine -eq 'aa64-sdk-auxiliary') {
            # Preserve this exact Microsoft SDK companion unchanged; it is not an x64 application/runtime entry point.
            if ($entry.path -cne 'Microsoft.Windows.Workloads.Resources_ec.dll' -or $entry.publisher -ne 'Microsoft' -or
                (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $entry.sha256) { throw 'Unexpected Windows App SDK auxiliary image.' }
        } else { Assert-BundleImage $path $entry.machine }
    }
    if ($entry.publisher -eq 'Microsoft') {
        $signature=Get-AuthenticodeSignature -LiteralPath $path
        if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') { throw "Invalid original Microsoft signature: $($entry.path)" }
    }
    if ($entry.publisher -eq 'Kyutai-pinned' -and (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -cne '813CABFC2F8FC7A4E5310DEDA6025426B3164610DD9F3C3098A6078E5CC1895A') { throw 'Official Rust runtime identity mismatch.' }
    $project=$entry.publisher -eq 'OutLoud'
    if ($project -and (Get-AuthenticodeSignature -LiteralPath $path).Status -ne 'NotSigned') { throw "Build unsigned project inputs; installer owns signing: $($entry.path)" }
    Add-Row $entry.path ($project -and $entry.path -ne 'OutLoud.WinUI.dll') $project $project
}
$null=New-Item -ItemType Directory -Path (Join-Path $payload 'bin'),(Join-Path $payload 'licenses'),(Join-Path $StageRoot 'bootstrap'),(Join-Path $StageRoot 'dependencies')
foreach($name in 'OutLoudPocketSAPI.dll','outloud-sapi-voices.exe') { Copy-Item -LiteralPath (Join-Path $native "Release/$name") -Destination (Join-Path $payload "bin/$name"); Add-Row "bin/$name" $true $true $true }
foreach($name in 'LICENSE','THIRD_PARTY_NOTICES.md') { Copy-Item -LiteralPath (Join-Path $repository $name) -Destination (Join-Path $payload $name); Add-Row $name $false $false $false }
foreach($name in 'CC-BY-4.0.txt','Inno-Setup-license.txt','Pocket-Model-Attribution.md','Pocket-Prohibited-Use-Policy.txt','Pocket-Tokenizer-Attribution.md','Sonic-Apache-2.0.txt') {
    Copy-Item -LiteralPath (Join-Path $repository "licenses/$name") -Destination (Join-Path $payload "licenses/$name"); Add-Row "licenses/$name" $false $false $false
}
$nuget=Join-Path $env:USERPROFILE '.nuget/packages'
$coreVersion=[string]$runtimeConfig.runtimeOptions.includedFrameworks[0].version
foreach($pair in @(@("microsoft.netcore.app.runtime.win-x64/$coreVersion/LICENSE.TXT",'DotNet-Runtime-License.txt'),@("microsoft.netcore.app.runtime.win-x64/$coreVersion/THIRD-PARTY-NOTICES.TXT",'DotNet-Runtime-Notices.txt'),@('microsoft.windowsappsdk/2.5.1/license.txt','WindowsAppSDK-License.txt'),@('microsoft.windowsappsdk/2.5.1/NOTICE.txt','WindowsAppSDK-Notices.txt'))) {
    Copy-Item -LiteralPath (Join-Path $nuget $pair[0]) -Destination (Join-Path $payload ('licenses/'+$pair[1])); Add-Row ('licenses/'+$pair[1]) $false $false $false
}
Add-DependencyNotices $nuget (Join-Path $StageRoot 'restore/project.assets.json')
# Bootstrap/helper are compiled from this source after their exact payload contract is generated.
# Their Out Loud version/architecture are verified natively; self-hashes cannot be compiled into themselves.
foreach($name in 'OutLoudInstallSigning.exe','OutLoudPrerequisites.exe') { $rows.Add([ordered]@{path="bin/$name";pe=$true;product_version=$true;sign=$true;bytes=0;sha256=''}) }
$manifest=Join-Path $StageRoot 'package.json'
[IO.File]::WriteAllText($manifest,([ordered]@{schema=1;version=$version;files=@($rows.ToArray()|Sort-Object path)}|ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
$installerBuild=Join-Path $repository 'build-production-installer'
Invoke-Checked cmake @('-S',(Join-Path $repository 'installer/native'),'-B',$installerBuild,'-A','x64',"-DOUTLOUD_INSTALLER_PACKAGE=$manifest")
Invoke-Checked cmake @('--build',$installerBuild,'--config','Release','--target','OutLoudProductionMaintenance','OutLoudPrerequisites','outloud-production-maintenance-check','outloud-production-uninstall-check','outloud-production-registration-check','outloud-production-path-check','outloud-production-signing-check')
foreach($name in 'OutLoudInstallSigning.exe','OutLoudPrerequisites.exe') { Copy-Item -LiteralPath (Join-Path $installerBuild "production/Release/$name") -Destination (Join-Path $payload "bin/$name") }
Copy-Item -LiteralPath (Join-Path $payload 'bin/OutLoudPrerequisites.exe') -Destination (Join-Path $StageRoot 'bootstrap/OutLoudPrerequisites.exe')
Write-Ascii (Join-Path $StageRoot 'prerequisites.ini') "[requirements]`nschema=1`nvc_minimum=14.51.36247.0`n"
if (!$PrepareOnly) {
    Copy-Item -LiteralPath $VcRedistributable -Destination (Join-Path $StageRoot 'dependencies/vc_redist.x64.exe')
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'runtime-packages.ini') -Destination (Join-Path $StageRoot 'runtime-packages.ini')
    Invoke-Checked (Join-Path $StageRoot 'bootstrap/OutLoudPrerequisites.exe') @('--verify-package',(Join-Path $StageRoot 'runtime-packages.ini'),'vc',(Join-Path $StageRoot 'dependencies/vc_redist.x64.exe'),'--report',(Join-Path $StageRoot 'vc-verification.ini'))
}
$maintenanceRequest="[maintenance]`nschema=1`napp_id=OutLoud`nversion=$version`nclsid={F38C3398-3552-43B9-8350-7DD301327D70}`n"
$files=""; $copy=""; $index=0
foreach($row in ($rows.ToArray()|Sort-Object path)) {
    $file=Get-Item -LiteralPath (Join-Path $payload $row.path)
    Assert-BundlePlainPath $file.FullName
    $hash=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($row.sha256 -and ($hash -cne $row.sha256 -or $file.Length -ne $row.bytes)) { throw "Staged payload changed: $($row.path)" }
    if ($row.product_version -and ($file.VersionInfo.ProductName -cne 'Out Loud' -or $file.VersionInfo.FileVersion -cne "$version.0")) { throw "Unexpected product/version identity: $($row.path)" }
    if ($row.pe) { Assert-X64Image $file.FullName }
    if ($row.sign -and (Get-AuthenticodeSignature -LiteralPath $file.FullName).Status -ne 'NotSigned') { throw "Project input must be unsigned: $($row.path)" }
    $maintenanceRequest+="[file$index]`npath=$($row.path)`nsha256=$hash`n"
    $temporary='payload{0:D4}.bin' -f $index
    $files+='Source: "{#StageRoot}\payload\'+$row.path.Replace('/','\')+'"; DestName: "'+$temporary+'"; Flags: dontcopy' + "`n"
    $copy+="    MaterializeMaintenanceFile('$temporary', '$($row.path.Replace('/','\'))');`n"
    $index++
}
$maintenanceRequest+="[bootstrap]`ndetector_sha256=$((Get-FileHash -LiteralPath (Join-Path $StageRoot 'bootstrap/OutLoudPrerequisites.exe') -Algorithm SHA256).Hash.ToLowerInvariant())`nrequirements_sha256=$((Get-FileHash -LiteralPath (Join-Path $StageRoot 'prerequisites.ini') -Algorithm SHA256).Hash.ToLowerInvariant())`n"
Write-Ascii (Join-Path $StageRoot 'maintenance-input.ini') $maintenanceRequest
Write-Ascii (Join-Path $StageRoot 'maintenance-files.iss') $files
Write-Ascii (Join-Path $StageRoot 'maintenance-copy.iss') $copy
Write-Ascii (Join-Path $StageRoot 'installer-version.iss') "#define ProductVersion `"$version`"`n#define ProductFileVersion `"$version.0`"`n"
if ($PrepareOnly) {
    Write-Host "Verified unsigned maintenance package prepared: $StageRoot"
    Write-Host 'No installer compilation, installation, signing, certificate provisioning or registration was executed.'
    return
}
$releaseDirectory = Join-Path $repository 'release'
Invoke-Checked $Compiler @('/Q',"/O$releaseDirectory","/DStageRoot=$StageRoot",(Join-Path $PSScriptRoot 'OutLoud.iss'))
$installer = Join-Path $releaseDirectory 'OutLoud-Setup.exe'
$installerHash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Ascii ($installer + '.sha256') ($installerHash + "  OutLoud-Setup.exe`r`n")
$completedBuild = [ordered]@{ schema=1; kind='OutLoud.InstallerBuild'; stage=$StageRoot; version=$version; installer_sha256=$installerHash }
[IO.File]::WriteAllText((Join-Path $StageRoot 'build-complete.json'), ($completedBuild | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
Remove-PreviousInstallerStages $StageRoot
Write-Host "Installer compiled: $installer"
Write-Host 'No installation, certificate provisioning, signing, registration or uninstallation was executed.'
