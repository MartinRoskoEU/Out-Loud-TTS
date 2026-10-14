#Requires -Version 5.1
# Workspace builds only. Shared by production staging and development preparation.
function Build-OutLoudComponents {
    param(
        [Parameter(Mandatory = $true)][ValidateSet('Engine', 'VoiceManager', 'Both')][string]$Components,
        [Parameter(Mandatory = $true)][string]$WorkDirectory,
        [switch]$IncludeManagementCli
    )
    $native = Join-Path $ProductionRepository 'build-installer-production'
    Assert-ProductionPlainPath $native
    $work = [IO.Path]::GetFullPath($WorkDirectory)
    $parent = [IO.Path]::GetFullPath((Join-Path $ProductionRepository 'generated/installer/builds')).TrimEnd('\')
    if (-not $work.StartsWith($parent + '\', [StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $work)) {
        throw 'Build work must be a fresh child of generated/installer/builds.'
    }
    Assert-ProductionPlainPath $work
    if (Test-Path -LiteralPath (Join-Path $native 'CMakeCache.txt')) {
        $cache = Get-Content -LiteralPath (Join-Path $native 'CMakeCache.txt') -Raw
        if ($cache -notmatch '(?m)^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$' -or
            [IO.Path]::GetFullPath($Matches[1].Trim()) -ne $ProductionRepository) { throw 'Native cache belongs to a different source directory.' }
    }
    & cmake -S $ProductionRepository -B $native -G 'Visual Studio 17 2022' -A x64 -DBUILD_TESTING=OFF -DOUTLOUD_BUILD_DEVELOPMENT_TOOLS=OFF -DOUTLOUD_ENABLE_AVX2=ON | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'Production CMake configuration failed.' }
    $cache = Get-Content -LiteralPath (Join-Path $native 'CMakeCache.txt') -Raw
    foreach ($setting in @('BUILD_TESTING:BOOL=OFF', 'OUTLOUD_BUILD_DEVELOPMENT_TOOLS:BOOL=OFF', 'OUTLOUD_ENABLE_AVX2:BOOL=ON', 'CMAKE_GENERATOR_PLATFORM:INTERNAL=x64')) {
        if ($cache -notmatch ('(?m)^' + [regex]::Escape($setting) + '\r?$')) { throw "Unexpected production cache: $setting" }
    }
    $targets = @()
    if ($Components -in @('Engine', 'Both')) { $targets += 'OutLoudPocketSAPI' }
    if ($Components -in @('VoiceManager', 'Both')) { $targets += @('OutLoudVoiceManagerNative', 'OutLoudVoiceImportHelper') }
    if ($IncludeManagementCli) { $targets += 'outloud-sapi-voices' }
    & cmake --build $native --config Release --parallel --target @targets | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'Production native build failed.' }
    [void](New-Item -ItemType Directory -Path $work)
    $publish = Join-Path $work 'publish'
    $apphost = ''; $apphostHash = ''
    if ($Components -in @('VoiceManager', 'Both')) {
        $policy = Get-ProductionPolicy
        $project = Join-Path $ProductionRepository 'ui/OutLoud.VoiceManager/OutLoud.VoiceManager.csproj'
        $feed = Join-Path $work 'empty-feed'
        [void](New-Item -ItemType Directory -Path $feed)
        $previousCertificateSetting = $env:DOTNET_GENERATE_ASPNET_CERTIFICATE
        try {
            $env:DOTNET_GENERATE_ASPNET_CERTIFICATE = 'false'
            $sdk = (& dotnet --version | Out-String).Trim()
            if ($LASTEXITCODE -ne 0 -or $sdk -notmatch '^10\.') { throw 'An installed .NET 10 SDK is required.' }
            & dotnet restore $project --source $feed -p:Platform=x64 | Out-Host
            if ($LASTEXITCODE -ne 0) { throw 'Offline frontend restore failed; install the required SDK packs separately.' }
            $apphost = (& dotnet msbuild $project -t:ResolveFrameworkReferences -getProperty:AppHostSourcePath -p:Platform=x64 -p:SelfContained=false | Out-String).Trim()
            if ($LASTEXITCODE -ne 0 -or $apphost -notmatch '\\Microsoft\.NETCore\.App\.Host\.win-x64\\([^\\]+)\\' -or
                $Matches[1] -ne $policy.apphost_pack_version) { throw 'Apphost pack differs from the reviewed package policy.' }
            $apphostHash = Get-ProductionHash $apphost
            & dotnet publish $project -c Release --no-restore -p:Platform=x64 "-p:NativeBuildDirectory=$native" `
                "-p:OutputPath=$work/ui-bin/" "-p:PublishDir=$publish/" -p:SelfContained=false -p:PublishSingleFile=false -p:PublishReadyToRun=false -p:DebugType=None -p:DebugSymbols=false | Out-Host
            if ($LASTEXITCODE -ne 0) { throw 'Release x64 frontend publish failed.' }
        } finally { $env:DOTNET_GENERATE_ASPNET_CERTIFICATE = $previousCertificateSetting }
        if ((Get-ProductionHash $apphost) -ne $apphostHash) { throw 'SDK apphost changed during publishing.' }
        $publishedNames = @($policy.files | Where-Object source -eq 'managed' | ForEach-Object name) + @('OutLoudVoiceManagerNative.dll', 'OutLoudVoiceImportHelper.exe')
        foreach ($item in Get-ChildItem -LiteralPath $publish -Force) {
            if ($item.PSIsContainer -or $item.Name -cnotin $publishedNames -or $item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Unexpected frontend publish artifact: $($item.Name)"
            }
        }
        foreach ($name in @('OutLoudVoiceManagerNative.dll', 'OutLoudVoiceImportHelper.exe')) {
            if ((Get-ProductionHash (Join-Path $native "Release/$name")) -ne (Get-ProductionHash (Join-Path $publish $name))) {
                throw "Frontend contains a mismatched native component: $name"
            }
        }
    }
    # Capture build output separately; do not mix its stdout into this result object.
    return [pscustomobject]@{ native = $native; publish = $publish; work = $work; apphost = $apphost; apphost_hash = $apphostHash }
}
