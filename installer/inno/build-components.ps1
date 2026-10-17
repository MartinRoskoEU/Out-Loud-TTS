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

# Artifact retention belongs to the workspace build workflow. Nothing here opens
# an installed application, registry, certificate store, voice library or release/.
function Enter-OutLoudArtifactLock {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $id = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($ProductionRepository.ToLowerInvariant()))).Replace('-', '') }
    finally { $sha.Dispose() }
    $mutex = New-Object Threading.Mutex($false, ('Global\OutLoud.BuildArtifacts.' + $id))
    try {
        try { $acquired = $mutex.WaitOne(0) }
        catch [Threading.AbandonedMutexException] { $acquired = $true }
        if (-not $acquired) { throw 'Another Out Loud build/publication/cleanup owns the workspace. Retry after it finishes.' }
        return $mutex
    } catch { $mutex.Dispose(); throw }
}

function Exit-OutLoudArtifactLock($Mutex) {
    try { $Mutex.ReleaseMutex() } finally { $Mutex.Dispose() }
}

function Assert-OutLoudArtifactDirectory([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $allowed = @('stages', 'compiler-output', 'builds') | ForEach-Object {
        [IO.Path]::GetFullPath((Join-Path $ProductionRepository "generated/installer/$_")).TrimEnd('\')
    }
    if ([IO.Path]::GetDirectoryName($full) -notin $allowed) { throw "Not a direct workspace artifact directory: $full" }
    Assert-ProductionPlainPath $full
    return $full
}

function Get-OutLoudArtifactInventory([string]$Path, [string[]]$Exclude = @()) {
    Assert-ProductionPlainPath $Path
    $root = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    if (-not (Test-Path -LiteralPath $root -PathType Container)) { throw "Missing artifact directory: $root" }
    $pending = New-Object 'Collections.Generic.Stack[string]'
    $pending.Push($root)
    $files = @(); $directories = @()
    while ($pending.Count) {
        $directory = $pending.Pop()
        Assert-ProductionPlainPath $directory
        foreach ($entry in Get-ChildItem -LiteralPath $directory -Force) {
            Assert-ProductionPlainPath $entry.FullName
            $relative = $entry.FullName.Substring($root.Length + 1)
            if ($entry.PSIsContainer) { $directories += $relative; $pending.Push($entry.FullName) }
            elseif ($relative -notin $Exclude) {
                $files += [pscustomobject]@{ path = $relative; bytes = $entry.Length; sha256 = Get-ProductionHash $entry.FullName }
            }
        }
    }
    return [pscustomobject]@{ files = @($files | Sort-Object path); directories = @($directories | Sort-Object) }
}

function Assert-OutLoudArtifactInventory([string]$Path, $Inventory, [string[]]$Exclude = @()) {
    $actual = Get-OutLoudArtifactInventory $Path $Exclude
    $expectedRows = @($Inventory.files | ForEach-Object { "$($_.path)|$($_.bytes)|$($_.sha256)" })
    $actualRows = @($actual.files | ForEach-Object { "$($_.path)|$($_.bytes)|$($_.sha256)" })
    if (($expectedRows -join "`n") -cne ($actualRows -join "`n") -or
        (@($Inventory.directories) -join "`n") -cne (@($actual.directories) -join "`n")) {
        throw "Artifact changed or contains unrecorded files; preserve for diagnosis: $Path"
    }
}

function Remove-OutLoudOwnedArtifact([string]$Path, $Inventory, [string]$Reason, [string[]]$Exclude = @()) {
    $root = Assert-OutLoudArtifactDirectory $Path
    Assert-OutLoudArtifactInventory $root $Inventory $Exclude
    # All files must be available before the first deletion. A sharing violation
    # preserves the tree. Never use recursive deletion or traverse a reparse point.
    $handles = @()
    try {
        foreach ($relative in @($Inventory.files | ForEach-Object path) + $Exclude) {
            $file = Resolve-ProductionPath $root $relative
            Assert-ProductionPlainPath $file
            $handles += [IO.File]::Open($file, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
        }
    } finally { foreach ($handle in $handles) { $handle.Dispose() } }
    $receipts = Join-Path $ProductionRepository 'generated/installer/cleanup-receipts'
    Assert-ProductionPlainPath $receipts
    [void](New-Item -ItemType Directory -Path $receipts -Force)
    $receipt = Join-Path $receipts (([guid]::NewGuid().ToString('N')) + '.json')
    $record = [ordered]@{ schema = 1; artifact = $root; reason = $Reason; utc = [DateTime]::UtcNow.ToString('o'); status = 'started'; inventory = $Inventory }
    $record | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath $receipt -Encoding UTF8
    foreach ($relative in @($Inventory.files | ForEach-Object path) + $Exclude) {
        $file = Resolve-ProductionPath $root $relative
        Assert-ProductionPlainPath $file
        [IO.File]::Delete($file)
    }
    foreach ($relative in @($Inventory.directories | Sort-Object Length -Descending)) {
        $directory = Resolve-ProductionPath $root $relative
        Assert-ProductionPlainPath $directory
        [IO.Directory]::Delete($directory, $false)
    }
    Assert-ProductionPlainPath $root
    [IO.Directory]::Delete($root, $false)
    $record.status = 'removed'
    $record | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath $receipt -Encoding UTF8
    Write-Host "CLEANED $root"
}

function Get-OutLoudArtifactReferences {
    # Historical diagnostic code and publication evidence can still consume old
    # stages. Receipts are deletion inventories, not live publication records.
    $generated = Join-Path $ProductionRepository 'generated'
    $texts = @()
    $pending = New-Object 'Collections.Generic.Stack[string]'
    $pending.Push($generated)
    $excluded = @('stages', 'compiler-output', 'builds', 'cleanup-receipts', 'artifact-cleanup-evidence') | ForEach-Object {
        Join-Path $generated "installer/$_"
    }
    while ($pending.Count) {
        $directory = $pending.Pop()
        Assert-ProductionPlainPath $directory
        foreach ($entry in Get-ChildItem -LiteralPath $directory -Force) {
            if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse point refused: $($entry.FullName)" }
            if ($entry.PSIsContainer) {
                if ($entry.FullName -notin $excluded) { $pending.Push($entry.FullName) }
            } elseif ($entry.Extension -in @('.json', '.ini', '.txt', '.ps1', '.py', '.md', '.cmake', '.cpp', '.cs', '.hpp')) {
                $texts += [regex]::Replace([IO.File]::ReadAllText($entry.FullName), '[\\/]+', '\').ToLowerInvariant()
            }
        }
    }
    return ($texts -join "`n")
}

function Test-OutLoudArtifactReference([string]$References, [string]$Path) {
    $relative = [IO.Path]::GetFullPath($Path).Substring($ProductionRepository.TrimEnd('\').Length + 1).Replace('/', '\').ToLowerInvariant()
    return $References.Contains($relative)
}

function Write-OutLoudPublicationRecord([string]$Output, [string]$Stage, [string[]]$OwnedPublicationFiles = @()) {
    $outputRoot = Assert-OutLoudArtifactDirectory $Output
    $stageRoot = Assert-OutLoudArtifactDirectory $Stage
    if ([IO.Path]::GetDirectoryName($outputRoot) -ne (Join-Path $ProductionRepository 'generated/installer/compiler-output') -or
        [IO.Path]::GetDirectoryName($stageRoot) -ne (Join-Path $ProductionRepository 'generated/installer/stages')) { throw 'Unexpected artifact roles.' }
    $build = Get-Content -LiteralPath (Join-Path $outputRoot 'installer-build.json') -Raw | ConvertFrom-Json
    if ($build.status -cne 'public-release' -or $build.stage -cne $stageRoot -or
        [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($build.installer)) -cne $outputRoot -or
        (Get-ProductionHash (Join-Path $stageRoot 'package-manifest.json')) -cne $build.manifest_sha256 -or
        (Get-ProductionHash $build.installer) -cne $build.sha256 -or
        (Get-Item -LiteralPath $build.installer).Length -ne $build.bytes) { throw 'Publication ownership/provenance differs.' }
    $recordPath = Join-Path $outputRoot 'publication-record.json'
    Assert-ProductionPlainPath $recordPath
    # Backup ownership comes from files actually produced by this publication or
    # a previously verified inventory, never from a filename pattern alone.
    foreach ($file in Get-ChildItem -LiteralPath $outputRoot -Force) {
        if ($file.PSIsContainer -or ($file.Name -cne [IO.Path]::GetFileName($build.installer) -and
            $file.Name -notin @('installer-build.json', 'publication-record.json', 'build-input.json') -and
            $file.FullName -notin $OwnedPublicationFiles)) {
            throw 'Unowned compiler output; automatic retention disabled for this build.'
        }
    }
    [ordered]@{
        schema = 1; owner = 'OutLoud installer workflow'; status = 'published'; utc = [DateTime]::UtcNow.ToString('o')
        stage = $stageRoot; output = $outputRoot
        stage_inventory = Get-OutLoudArtifactInventory $stageRoot
        output_inventory = Get-OutLoudArtifactInventory $outputRoot @('publication-record.json')
    } | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath $recordPath -Encoding UTF8
}

function Remove-OutLoudPublicationBackups([string]$Output, [string[]]$OwnedFiles, [string]$CurrentHash) {
    $root = Assert-OutLoudArtifactDirectory $Output
    $references = Get-OutLoudArtifactReferences
    $backups = @()
    foreach ($file in $OwnedFiles) {
        if ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($file)) -cne $root) { throw 'Backup outside compiler output.' }
        if ([IO.Path]::GetFileName($file) -cmatch '^release-(previous-[0-9a-f]{32}|obsolete-[0-9a-f]{32}-OutLoud-Setup-\d+\.\d+\.\d+)\.exe$') {
            Assert-ProductionPlainPath $file
            $backups += [pscustomobject]@{ path = $file; hash = Get-ProductionHash $file }
        }
    }
    # The caller supplies previously inventoried files followed by newly created
    # backups in publication order. Keep one distinct rollback binary per output.
    $distinct = @($backups | Where-Object hash -ne $CurrentHash)
    $keep = if ($distinct.Count) { $distinct[-1].path } else { '' }
    $retired = @($backups | Where-Object { $_.path -ne $keep -and -not (Test-OutLoudArtifactReference $references $_.path) })
    $handles = @()
    try {
        foreach ($backup in $retired) { $handles += [IO.File]::Open($backup.path, 'Open', 'Read', 'None') }
    } finally { foreach ($handle in $handles) { $handle.Dispose() } }
    foreach ($backup in $retired) {
        Assert-ProductionPlainPath $backup.path
        [IO.File]::Delete($backup.path)
    }
}

function Invoke-OutLoudArtifactRetention([string]$CurrentOutput, [ValidateRange(2, 10)][int]$KeepSuccessful = 2) {
    $mutex = Enter-OutLoudArtifactLock
    try {
        $current = Assert-OutLoudArtifactDirectory $CurrentOutput
        $parent = Join-Path $ProductionRepository 'generated/installer/compiler-output'
        $references = Get-OutLoudArtifactReferences
        $published = @(); $allRecords = @()
        foreach ($directory in Get-ChildItem -LiteralPath $parent -Directory -Force) {
            Assert-ProductionPlainPath $directory.FullName
            foreach ($name in @('installer-build.json', 'build-input.json', 'publication-record.json')) {
                $buildPath = Join-Path $directory.FullName $name
                if (Test-Path -LiteralPath $buildPath) {
                    Assert-ProductionPlainPath $buildPath
                    $build = Get-Content -LiteralPath $buildPath -Raw | ConvertFrom-Json
                    $allRecords += [pscustomobject]@{ output = $directory.FullName; stage = [IO.Path]::GetFullPath($build.stage) }
                }
            }
            $path = Join-Path $directory.FullName 'publication-record.json'
            if (-not (Test-Path -LiteralPath $path)) { continue } # Legacy/incomplete: preserve.
            Assert-ProductionPlainPath $path
            $record = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
            if ($record.schema -ne 1 -or $record.owner -cne 'OutLoud installer workflow' -or
                $record.status -cne 'published' -or $record.output -cne $directory.FullName) { throw 'Unknown publication ownership; cleanup refused.' }
            [void](Assert-OutLoudArtifactDirectory $record.stage)
            if ([IO.Path]::GetDirectoryName($record.stage) -ne (Join-Path $ProductionRepository 'generated/installer/stages')) { throw 'Publication stage outside stages.' }
            Assert-OutLoudArtifactInventory $record.stage $record.stage_inventory
            Assert-OutLoudArtifactInventory $record.output $record.output_inventory @('publication-record.json')
            $published += $record
        }
        if ($current -notin @($published.output)) { throw 'Current publication is not complete; cleanup refused.' }
        $keep = @($current)
        $keep += @($published | Where-Object output -ne $current | Sort-Object { [DateTime]$_.utc } -Descending |
            Select-Object -First ($KeepSuccessful - 1) | ForEach-Object output)
        foreach ($record in $published) {
            if ($record.output -in $keep -or (Test-OutLoudArtifactReference $references $record.output) -or
                (Test-OutLoudArtifactReference $references $record.stage)) { continue }
            # Output records disappear before their stage can be retired. Any
            # retained installer record (including legacy or diagnostic) pins it.
            Remove-OutLoudOwnedArtifact $record.output $record.output_inventory 'obsolete successful publication' @('publication-record.json')
            $allRecords = @($allRecords | Where-Object output -ne $record.output)
            if ($record.stage -notin @($allRecords.stage)) {
                Remove-OutLoudOwnedArtifact $record.stage $record.stage_inventory 'unreferenced successful publication stage'
            }
        }
    } finally { Exit-OutLoudArtifactLock $mutex }
}
