#Requires -Version 5.1
# WinUI deployment helpers. Installer updates delegate ownership/signing to native maintenance.
$ProductionRepository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $ProductionRepository 'sapi/identity-common.ps1')
. (Join-Path $PSScriptRoot 'registry-common.ps1')

function Resolve-ProductionPath([string]$Root, [string]$Relative) {
    $base = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    if ([IO.Path]::IsPathRooted($Relative)) { throw "Expected a relative path: $Relative" }
    $path = [IO.Path]::GetFullPath((Join-Path $base $Relative))
    if (-not $path.StartsWith($base + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "Path escapes root: $Relative" }
    return $path
}

function Assert-ProductionPlainPath([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    if ($full.StartsWith('\\') -or $full.Substring(2).Contains(':')) { throw "Expected a local path without streams: $full" }
    $current = $full
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse point refused: $current" }
        }
        $parent = [IO.Path]::GetDirectoryName($current)
        if ($parent -eq $current) { break }
        $current = $parent
    }
}

function Get-ProductionHash([string]$Path) {
    Assert-ProductionPlainPath $Path
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-ProductionPE([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    $stream = New-Object IO.MemoryStream(,$bytes)
    $reader = New-Object IO.BinaryReader($stream)
    try {
        if ($bytes.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5a4d) { throw "Invalid PE: $Path" }
        $stream.Position = 60; $pe = $reader.ReadUInt32()
        if ($pe -lt 64 -or $pe -gt $bytes.Length - 24) { throw "Invalid PE offset: $Path" }
        $stream.Position = $pe
        if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664) { throw "Expected AMD64 PE: $Path" }
        $sectionsCount = $reader.ReadUInt16()
        $stream.Position = $pe + 20; $optionalSize = $reader.ReadUInt16()
        $optional = $pe + 24; $stream.Position = $optional
        if ($reader.ReadUInt16() -ne 0x20b -or $optionalSize -lt 240) { throw "Expected PE32+ image: $Path" }
        $directories = $optional + 112
        $sections = @()
        for ($i = 0; $i -lt $sectionsCount; $i++) {
            $stream.Position = $optional + $optionalSize + $i * 40 + 8
            $virtual = $reader.ReadUInt32(); $rva = $reader.ReadUInt32()
            $size = $reader.ReadUInt32(); $raw = $reader.ReadUInt32()
            $sections += @{ rva = $rva; size = [Math]::Max($virtual, $size); raw = $raw }
        }
        function Convert-Rva([long]$Rva) {
            foreach ($section in $sections) {
                if ($Rva -ge $section.rva -and $Rva -lt $section.rva + $section.size) {
                    $offset = $section.raw + $Rva - $section.rva
                    if ($offset -ge $bytes.Length) { break }
                    return $offset
                }
            }
            throw "PE RVA outside file: $Path"
        }
        $imports = @()
        $stream.Position = $directories + 8; $importRva = $reader.ReadUInt32()
        if ($importRva) {
            $descriptor = Convert-Rva $importRva
            while ($true) {
                $stream.Position = $descriptor + 12; $name = $reader.ReadUInt32()
                if (-not $name) { break }
                $stream.Position = Convert-Rva $name
                $text = New-Object Text.StringBuilder
                while (($character = $reader.ReadByte()) -ne 0) { [void]$text.Append([char]$character) }
                $imports += $text.ToString(); $descriptor += 20
            }
        }
        $resources = @{}
        $stream.Position = $directories + 16; $resourceRva = $reader.ReadUInt32()
        if ($resourceRva) {
            $resourceBase = Convert-Rva $resourceRva
            function Read-Resources([long]$Offset, [string]$Prefix, [int]$Depth) {
                if ($Depth -gt 3) { throw "Malformed resource tree: $Path" }
                $stream.Position = $resourceBase + $Offset + 12
                $named = $reader.ReadUInt16(); $ids = $reader.ReadUInt16()
                for ($j = 0; $j -lt $named + $ids; $j++) {
                    $stream.Position = $resourceBase + $Offset + 16 + $j * 8
                    $id = $reader.ReadUInt32(); $target = $reader.ReadUInt32()
                    if ($id -band 0x80000000L) { $id = 'named' }
                    $key = "$Prefix$id/"
                    if ($target -band 0x80000000L) { Read-Resources ($target -band 0x7fffffff) $key ($Depth + 1) }
                    else {
                        $stream.Position = $resourceBase + $target
                        $rva = $reader.ReadUInt32(); $length = $reader.ReadUInt32()
                        $stream.Position = Convert-Rva $rva
                        $data = $reader.ReadBytes($length)
                        if ($data.Length -ne $length) { throw "Truncated resource: $Path" }
                        $resources[$key] = $data
                    }
                }
            }
            Read-Resources 0 '' 0
        }
        return @{ machine = '8664'; imports = @($imports | Sort-Object -Unique); resources = $resources }
    } finally { $reader.Dispose(); $stream.Dispose() }
}

function ConvertTo-OutLoudProductVersion([string]$Text) {
    if ($Text -cnotmatch '\A(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\z') {
        throw 'Out Loud requires canonical MAJOR.MINOR.PATCH versioning.'
    }
    foreach ($part in $Text.Split('.')) {
        if ($part.Length -gt 5 -or [int]$part -gt 65534) { throw 'Out Loud version components must fit Windows and .NET assembly metadata (0..65534).' }
    }
    return [version]($Text + '.0')
}

function Get-OutLoudProductMetadata([string]$Path = (Join-Path $PSScriptRoot '../../ProductVersion.props')) {
    $info=Get-Item -LiteralPath $Path
    if ($info.Length -gt 16384) { throw 'ProductVersion.props is unexpectedly large.' }
    $settings=New-Object Xml.XmlReaderSettings
    $settings.DtdProcessing=[Xml.DtdProcessing]::Prohibit
    $settings.XmlResolver=$null
    $reader=[Xml.XmlReader]::Create($info.FullName,$settings)
    try { $xml=New-Object Xml.XmlDocument; $xml.XmlResolver=$null; $xml.Load($reader) } finally { $reader.Dispose() }
    $fields=@{}
    foreach ($name in @('OutLoudProductVersion','OutLoudProductName','OutLoudProductCopyright')) {
        $nodes=$xml.SelectNodes("/Project/PropertyGroup/$name")
        if ($nodes.Count -ne 1) { throw "ProductVersion.props requires one $name." }
        $fields[$name]=$nodes[0].InnerText
    }
    $numeric=ConvertTo-OutLoudProductVersion $fields.OutLoudProductVersion
    if ($fields.OutLoudProductName -cne 'Out Loud' -or $fields.OutLoudProductCopyright -cne 'Copyright (c) 2026 Nendis') {
        throw 'Unexpected Out Loud product identity/copyright.'
    }
    return [pscustomobject]@{
        version=$fields.OutLoudProductVersion; file_version=$numeric.ToString(); product_name=$fields.OutLoudProductName
        copyright=$fields.OutLoudProductCopyright; app_id='OutLoud'
        source_sha256=(Get-FileHash -LiteralPath $info.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

function Assert-OutLoudBinaryVersion([string]$Path,$Metadata) {
    $info=[Diagnostics.FileVersionInfo]::GetVersionInfo([IO.Path]::GetFullPath($Path))
    $file='{0}.{1}.{2}.{3}' -f $info.FileMajorPart,$info.FileMinorPart,$info.FileBuildPart,$info.FilePrivatePart
    $product='{0}.{1}.{2}.{3}' -f $info.ProductMajorPart,$info.ProductMinorPart,$info.ProductBuildPart,$info.ProductPrivatePart
    if ($file -cne $Metadata.file_version -or $product -cne $Metadata.file_version -or
        $info.FileVersion -cne $Metadata.file_version -or $info.ProductVersion -cne $Metadata.version -or
        $info.ProductName -cne $Metadata.product_name -or $info.LegalCopyright -cne $Metadata.copyright) {
        throw "Product version metadata differs from ProductVersion.props: $Path"
    }
}

function Get-OutLoudMaintenanceHelper {
    $helper = Join-Path $ProductionRepository 'build-installer-prerequisites/Release/OutLoudInstallSigning.exe'
    Assert-ProductionPlainPath $helper
    if (-not (Test-Path -LiteralPath $helper -PathType Leaf)) { throw 'Build the existing native installation maintenance helper first.' }
    [void](Get-ProductionPE $helper)
    Assert-OutLoudBinaryVersion $helper (Get-OutLoudProductMetadata)
    return $helper
}

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

function Read-OutLoudUninstallRecord {
    $machine = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, [Microsoft.Win32.RegistryView]::Registry64)
    try {
        $key = $machine.OpenSubKey('Software\Microsoft\Windows\CurrentVersion\Uninstall\OutLoud_is1', $false)
        if ($null -eq $key) { return $null }
        try {
            $record = @{}
            foreach ($name in @('InstallLocation', 'DisplayName', 'DisplayVersion', 'Publisher')) {
                if ($name -notin $key.GetValueNames() -or $key.GetValueKind($name) -ne [Microsoft.Win32.RegistryValueKind]::String) {
                    throw "Invalid Out Loud installer record: $name must be REG_SZ. Manual review is required; no installer repair or migration is performed."
                }
                $record[$name] = $key.GetValue($name, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
            }
            return $record
        } finally { $key.Dispose() }
    } finally { $machine.Dispose() }
}

function Get-OutLoudInstalledApplication([string]$DeploymentRoot, $Record) {
    if ($null -eq $Record) { throw 'A current WinUI installer record is required; no installation will be adopted.' }
    $root = [IO.Path]::GetFullPath($DeploymentRoot).TrimEnd('\', '/')
    Assert-ProductionPlainPath $root
    $installed = [IO.Path]::GetFullPath([string]$Record.InstallLocation).TrimEnd('\', '/')
    if (-not [string]::Equals($root, $installed, [StringComparison]::OrdinalIgnoreCase) -or
        $Record.Publisher -cne 'Nendis' -or $Record.DisplayName -cnotin @('Out Loud', ('Out Loud ' + $Record.DisplayVersion), ('Out Loud version ' + $Record.DisplayVersion))) {
        throw 'The requested directory does not match the Out Loud installer identity.'
    }
    $receipt = Resolve-ProductionPath $root 'configuration/inventory.dat'
    Assert-ProductionPlainPath $receipt
    $stream = [IO.File]::OpenRead($receipt)
    try {
        $magic = New-Object byte[] 20
        if ($stream.Read($magic, 0, $magic.Length) -ne 20 -or
            [Text.Encoding]::ASCII.GetString($magic) -cne "OutLoud.Inventory.3`n") {
            throw 'Only the current WinUI production inventory is supported; legacy installations and development bundles are not adopted.'
        }
    } finally { $stream.Dispose() }
    $metadata = Get-OutLoudProductMetadata
    if ($Record.DisplayVersion -cne $metadata.version) {
        throw 'Installed application development updates require the same product version. Use the production installer for a release version change.'
    }
    # Discovery only. Native preflight authenticates the complete receipt,
    # certificate/key, installed files, COM registration and candidates later.
    return $root
}

function Invoke-OutLoudInstalledMaintenance([string]$Helper, [string]$Command, [string]$Root, [string]$InputDirectory, [string]$Report, [string]$Expected) {
    if (Test-Path -LiteralPath $Report) { throw "Maintenance report already exists: $Report" }
    & $Helper $Command development Package $Root $InputDirectory (Join-Path $InputDirectory 'maintenance-input.ini') --consented $Report | Out-Host
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) { throw "Native maintenance failed (exit $exitCode). Preserve the report and protected recovery records: $Report" }
    $result = Get-Content -LiteralPath $Report -Raw -ErrorAction Stop
    if ($result -notmatch '(?m)^schema=1\r?$' -or $result -notmatch ('(?m)^maintenance=' + [regex]::Escape($Expected) + '\r?$')) {
        throw "Native maintenance did not verify '$Expected'. Preserve the report: $Report"
    }
    if ($result -match '(?m)^archive_cleanup=pending\r?$') {
        Write-Warning "Application maintenance reached '$Expected', but archive cleanup remains pending and will be retried. Report: $Report"
    }
}

function Invoke-OutLoudInstalledApplicationUpdate {
    param([ValidateSet('Plan', 'Prepare', 'Deploy')][string]$Action, [string]$DeploymentRoot, $Record)
    $ErrorActionPreference = 'Stop'
    $root = Get-OutLoudInstalledApplication $DeploymentRoot $Record
    if ($Action -eq 'Plan') {
        Write-Output "PLAN: same-version full-package development update of installer-managed Out Loud at $root. Reuse its certificate/key, preserve COM/SAPI, uninstall host and application data. No build or publication performed; complete native verification occurs during Prepare/Deploy."
        return
    }
    Assert-RegistryAdministrator
    Assert-OutLoudDevelopmentClientsClosed $root
    $lock = Enter-OutLoudArtifactLock
    $report = $null
    try {
        $work = Join-Path $ProductionRepository ('generated/installed-updates/' + [Guid]::NewGuid().ToString('N'))
        [void][IO.Directory]::CreateDirectory($work)
        $report = Join-Path $work 'installed-application-update.json'
        $stage = Join-Path $work 'package'
        $inputs = Join-Path $work 'maintenance-input'
        $state = [ordered]@{ root = $root; stage = $stage; result = 'preparing'; registration = 'unchanged'; application_data = 'excluded' }
        $state | ConvertTo-Json | Set-Content -LiteralPath $report -Encoding UTF8
        # Reuse the complete reviewed production payload, including XBF/PRI,
        # runtime dependencies, registration CLI and inventory-aware helpers.
        & (Join-Path $ProductionRepository 'installer/inno/build-installer.ps1') -PrepareOnly -StageRoot $stage | Out-Host
        $package = Get-Content -LiteralPath (Join-Path $stage 'package.json') -Raw | ConvertFrom-Json
        [void][IO.Directory]::CreateDirectory($inputs)
        foreach ($row in $package.files) {
            $source = Resolve-ProductionPath (Join-Path $stage 'payload') $row.path
            $destination = Resolve-ProductionPath $inputs $row.path
            Assert-ProductionPlainPath $source
            [void][IO.Directory]::CreateDirectory((Split-Path $destination -Parent))
            Copy-Item -LiteralPath $source -Destination $destination
        }
        [void][IO.Directory]::CreateDirectory((Join-Path $inputs 'bootstrap'))
        Copy-Item -LiteralPath (Join-Path $stage 'bootstrap/OutLoudPrerequisites.exe') -Destination (Join-Path $inputs 'bootstrap/OutLoudPrerequisites.exe')
        foreach ($name in @('maintenance-input.ini', 'prerequisites.ini')) {
            Copy-Item -LiteralPath (Join-Path $stage $name) -Destination (Join-Path $inputs $name)
        }
        $helper = Join-Path $stage 'payload/bin/OutLoudInstallSigning.exe'
        [void](Get-OutLoudInstalledApplication $root (Read-OutLoudUninstallRecord))
        Assert-OutLoudDevelopmentClientsClosed $root
        Invoke-OutLoudInstalledMaintenance $helper '--maintenance-check' $root $inputs (Join-Path $work 'preflight.ini') 'ready'
        if ($Action -eq 'Prepare') {
            $state.result = 'prepared_verified'
            $state | ConvertTo-Json | Set-Content -LiteralPath $report -Encoding UTF8
            Write-Output "PREPARED: $inputs. Native preflight passed; installed files unchanged and candidates unsigned. Report: $report"
            return
        }
        $state.result = 'publishing'
        $state | ConvertTo-Json | Set-Content -LiteralPath $report -Encoding UTF8
        Invoke-OutLoudInstalledMaintenance $helper '--maintain' $root $inputs (Join-Path $work 'deployment.ini') 'committed'
        $state.result = 'verified'
        $state | ConvertTo-Json | Set-Content -LiteralPath $report -Encoding UTF8
        Write-Output "DEPLOY verified: $root. Installation-owned signing, complete inventory and uninstall support retained; COM/SAPI and application data preserved. Report: $report"
    } catch {
        if ($report) {
            $state.result = 'failed'; $state['error'] = $_.Exception.Message
            $state | ConvertTo-Json | Set-Content -LiteralPath $report -Encoding UTF8
        }
        throw
    } finally { Exit-OutLoudArtifactLock $lock }
}

function Get-OutLoudDevelopmentRegistration([string]$Root) {
    Assert-NoUserOverride
    $machine = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, [Microsoft.Win32.RegistryView]::Registry64)
    try {
        Assert-OwnedClass $machine (Resolve-BundlePath $Root 'bin/OutLoudPocketSAPI.dll')
        $class = Read-RegistryTree $machine $PocketClassPath
        if ($null -eq $class) { throw 'The existing development engine registration is missing; update will not create or repair registration.' }
        return (@{ engine = $class; voices = (Read-RegistryTree $machine $PocketVoiceRoot) } | ConvertTo-Json -Depth 64 -Compress)
    } finally { $machine.Dispose() }
}

function Assert-OutLoudDevelopmentClientsClosed([string]$Root) {
    Assert-GameClosed
    $prefix = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    foreach ($process in Get-Process) {
        $image = $null
        try { $image = $process.Path } catch { }
        if ($image -and $image.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Deployment process is active: $($process.ProcessName) (PID $($process.Id)). Close it manually and retry."
        }
    }
    # Exclusive handles below also catch loaded SAPI DLLs and other open files.
}

function Get-OutLoudHeldHash([IO.Stream]$Stream) {
    $Stream.Position = 0
    $digest = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($digest.ComputeHash($Stream)).Replace('-', '').ToLowerInvariant() }
    finally { $digest.Dispose(); $Stream.Position = 0 }
}

# Called only after both fixed inventories, signatures and protected locations
# have been verified. No arbitrary files, user data, backup or rollback paths.
function Publish-OutLoudDevelopmentFiles([string]$Root, [string]$Stage, $Before, $After) {
    $sources = @{}; $targets = @{}; $old = @{}; $next = @{}
    foreach ($record in $Before.files) { $old[$record.path] = $record }
    foreach ($record in $After.files) { $next[$record.path] = $record }
    $old['deployment.json'] = @{ sha256 = (Get-FileHash -LiteralPath (Join-Path $Root 'deployment.json')).Hash.ToLowerInvariant() }
    $next['deployment.json'] = @{ sha256 = (Get-FileHash -LiteralPath (Join-Path $Stage 'deployment.json')).Hash.ToLowerInvariant() }
    $paths = @(@($old.Keys) + @($next.Keys) | Sort-Object -Unique)
    $changed = 0
    try {
        foreach ($relative in $next.Keys) {
            $path = Resolve-BundlePath $Stage $relative
            $sources[$relative] = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
            if ((Get-OutLoudHeldHash $sources[$relative]) -cne $next[$relative].sha256) { throw "Staged bytes changed: $relative" }
        }
        foreach ($relative in $paths) {
            $path = Resolve-BundlePath $Root $relative
            Assert-BundlePlainPath $path
            if ($old.ContainsKey($relative)) {
                try { $targets[$relative] = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None) }
                catch { throw "Deployment file is locked or not writable: $relative. Close active clients and retry. $($_.Exception.Message)" }
                if ((Get-OutLoudHeldHash $targets[$relative]) -cne $old[$relative].sha256) { throw "Deployed bytes changed: $relative" }
            } elseif (Test-Path -LiteralPath $path) { throw "New deployment path already exists: $relative" }
        }
        # Keep every existing file pinned, including unchanged DLLs. Preserve its
        # owner and ACL by writing through the existing handle. Metadata goes last.
        foreach ($relative in @($next.Keys | Where-Object { $_ -ne 'deployment.json' } | Sort-Object) + @('deployment.json')) {
            if ($relative -eq 'deployment.json') {
                foreach ($obsolete in @($old.Keys | Where-Object { -not $next.ContainsKey($_) })) {
                    $targets[$obsolete].Dispose(); $targets.Remove($obsolete)
                    Remove-Item -LiteralPath (Resolve-BundlePath $Root $obsolete) -ErrorAction Stop
                    $changed++
                }
            }
            if ($old.ContainsKey($relative) -and $old[$relative].sha256 -ceq $next[$relative].sha256) { continue }
            if (-not $targets.ContainsKey($relative)) {
                $path = Resolve-BundlePath $Root $relative
                [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
                $targets[$relative] = [IO.File]::Open($path, [IO.FileMode]::CreateNew, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
            }
            $changed++
            $target = $targets[$relative]; $source = $sources[$relative]
            $source.Position = 0; $target.Position = 0; $target.SetLength(0)
            $source.CopyTo($target); $target.Flush($true)
            if ((Get-OutLoudHeldHash $target) -cne $next[$relative].sha256) { throw "Published bytes differ: $relative" }
        }
        return $changed
    } catch {
        throw "Development publication failed after $changed file change(s). No rollback was performed; do not treat this deployment as verified. $($_.Exception.Message)"
    } finally {
        foreach ($stream in $targets.Values) { $stream.Dispose() }
        foreach ($stream in $sources.Values) { $stream.Dispose() }
    }
}

function Build-OutLoudWinUIUpdate([string]$BuildDirectory, [string]$PublishDirectory) {
    if (-not [IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory = Join-Path $ProductionRepository $BuildDirectory }
    $BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
    if ((Split-Path $BuildDirectory -Leaf) -cne 'Release') { throw 'Use the existing multi-configuration native Release output directory.' }
    $native = Split-Path $BuildDirectory -Parent
    Assert-ProductionPlainPath $native
    if (Test-Path -LiteralPath (Join-Path $native 'CMakeCache.txt')) {
        $cache = Get-Content -LiteralPath (Join-Path $native 'CMakeCache.txt') -Raw
        if ($cache -notmatch '(?m)^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$' -or [IO.Path]::GetFullPath($Matches[1].Trim()) -ne $ProductionRepository) { throw 'Native cache belongs to another source tree.' }
        & cmake -S $ProductionRepository -B $native -DBUILD_TESTING=OFF -DOUTLOUD_BUILD_DEVELOPMENT_TOOLS=OFF | Out-Host
    } else {
        & cmake -S $ProductionRepository -B $native -G 'Visual Studio 17 2022' -A x64 -DBUILD_TESTING=OFF -DOUTLOUD_BUILD_DEVELOPMENT_TOOLS=OFF | Out-Host
    }
    if ($LASTEXITCODE -ne 0) { throw 'WinUI native configuration failed.' }
    & cmake --build $native --config Release --parallel --target OutLoudPocketSAPI OutLoudVoiceManagerNative OutLoudVoiceImportHelper outloud-sapi-voices | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'WinUI native Release build failed.' }
    & cmake --build (Join-Path $ProductionRepository 'build-installer-prerequisites') --config Release --target OutLoudInstallSigning | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'Existing security inspector Release build failed.' }
    & dotnet publish (Join-Path $ProductionRepository 'ui/OutLoud.WinUI/OutLoud.WinUI.csproj') -c Release -p:Platform=x64 --no-restore --self-contained false "-p:NativeBuildDirectory=$native" -o $PublishDirectory | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'WinUI Release publish failed.' }
}

function Invoke-OutLoudDevelopmentBundleUpdate {
    param([ValidateSet('Plan', 'Prepare', 'Deploy')][string]$Action, [string]$DeploymentRoot, [string]$BuildDirectory,
        [string]$PublishDirectory, [string]$WinUIBuildDirectory, [string]$CertificateThumbprint, [string]$CertificateStoreLocation,
        [string]$SignToolPath, [string]$TimestampUrl)
    if ($PSVersionTable.PSEdition -ne 'Desktop') { throw 'Use x64 Windows PowerShell 5.1 (powershell.exe); the existing managed-image verification uses .NET Framework metadata.' }
    $root = [IO.Path]::GetFullPath($DeploymentRoot).TrimEnd('\', '/')
    Assert-ProductionPlainPath $root
    if ($null -ne (Read-OutLoudUninstallRecord)) { throw 'Legacy Inno installations are retired; no installer maintenance or migration is performed.' }
    $inspector = Get-OutLoudMaintenanceHelper
    Assert-DevelopmentBundleSecurity $root $inspector
    $before = Assert-Bundle $root $true -AllowPreviousInventory
    $registration = Get-OutLoudDevelopmentRegistration $root
    Assert-OutLoudDevelopmentClientsClosed $root
    if ($Action -eq 'Plan') {
        Write-Output "PLAN: update verified development bundle in place at $root; preserve COM/SAPI registration and application data. No publication performed."
        return
    }
    Assert-RegistryAdministrator
    if ($Action -eq 'Deploy' -and $CertificateThumbprint -notmatch '^[0-9a-fA-F]{40}$') { throw 'Supply the existing trusted signing certificate thumbprint.' }
    $lock = Enter-OutLoudArtifactLock
    $report = $null
    try {
        $work = Join-Path $ProductionRepository ('generated/installed-updates/' + [Guid]::NewGuid().ToString('N'))
        [void][IO.Directory]::CreateDirectory($work)
        $report = Join-Path $work 'development-update.json'
        $stage = Join-Path (Split-Path $root -Parent) ('.outloud-stage-' + [Guid]::NewGuid().ToString('N'))
        $state = [ordered]@{ root = $root; stage = $stage; result = 'preparing'; registration = 'unchanged'; application_data = 'excluded' }
        $state | ConvertTo-Json | Set-Content -LiteralPath $report -Encoding UTF8
        if (-not $PublishDirectory) {
            $PublishDirectory = Join-Path $work 'publish'
            Build-OutLoudWinUIUpdate $BuildDirectory $PublishDirectory
        }
        Assert-DevelopmentBundleSecurity $stage $inspector
        & (Join-Path $ProductionRepository 'sapi/scripts/prepare-deployment.ps1') -ProjectRoot $ProductionRepository `
            -BuildDirectory $BuildDirectory -DeploymentRoot $stage -PublishDirectory $PublishDirectory `
            -WinUIBuildDirectory $WinUIBuildDirectory -SecurityHelperPath $inspector | Out-Host
        Assert-DevelopmentBundleSecurity $stage $inspector
        if ($Action -eq 'Prepare') { $state.result = 'prepared_unsigned'; $state | ConvertTo-Json | Set-Content -LiteralPath $report -Encoding UTF8; Write-Output "PREPARED: $stage. Deployed files unchanged."; return }
        $sign = @{ DeploymentRoot = $stage; CertificateThumbprint = $CertificateThumbprint; CertificateStoreLocation = $CertificateStoreLocation }
        if ($SignToolPath) { $sign.SignToolPath = $SignToolPath }
        if ($TimestampUrl) { $sign.TimestampUrl = $TimestampUrl }
        & (Join-Path $ProductionRepository 'sapi/scripts/sign-deployment.ps1') @sign | Out-Host
        $after = Assert-Bundle $stage $true
        foreach ($base in @($root, $stage)) {
            foreach ($entry in (Read-BundleInventory $base -AllowPreviousInventory).binaries | Where-Object signing -eq 'project-sign') {
                $signature = Get-AuthenticodeSignature -LiteralPath (Resolve-BundlePath $base $entry.path)
                if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Thumbprint -ne $CertificateThumbprint) { throw 'Project signer differs from the existing selected trusted certificate.' }
            }
        }
        if ($null -ne (Read-OutLoudUninstallRecord)) { throw 'Installation state changed before publication.' }
        Assert-OutLoudDevelopmentClientsClosed $root
        Assert-DevelopmentBundleSecurity $root $inspector
        $before = Assert-Bundle $root $true -AllowPreviousInventory
        if ((Get-OutLoudDevelopmentRegistration $root) -cne $registration) { throw 'Registration changed during preparation; no publication performed.' }
        $state.result = 'publishing'; $state | ConvertTo-Json | Set-Content -LiteralPath $report -Encoding UTF8
        $state['changed_files'] = Publish-OutLoudDevelopmentFiles $root $stage $before $after
        [void](Assert-Bundle $root $true)
        Assert-DevelopmentBundleSecurity $root $inspector
        if ((Get-OutLoudDevelopmentRegistration $root) -cne $registration) { throw 'Registration changed during publication; no registry writes were performed by this updater.' }
        & (Resolve-BundlePath $root 'bin/outloud-sapi-voices.exe') verify-engine --runtime-root $root | Out-Host
        if ($LASTEXITCODE -ne 0) { throw 'Deployed engine registration verification failed.' }
        $state.result = 'verified'; $state | ConvertTo-Json | Set-Content -LiteralPath $report -Encoding UTF8
        Write-Output "DEPLOY verified: $root. Existing COM/SAPI registration and application data preserved. Report: $report"
    } catch {
        if ($report) { $state.result = 'failed'; $state['error'] = $_.Exception.Message; $state | ConvertTo-Json | Set-Content -LiteralPath $report -Encoding UTF8 }
        throw
    } finally { Exit-OutLoudArtifactLock $lock }
}

# The explicit clean path composes the existing manual bundle operations. It
# never creates an Inno uninstall entry or installer signing/ownership receipts.
function Invoke-OutLoudCleanDevelopmentDeployment {
    param([ValidateSet('Plan', 'Prepare', 'Deploy')][string]$Action, [string]$DeploymentRoot, [string]$BuildDirectory, [string]$PublishDirectory,
        [string]$WinUIBuildDirectory, [string]$CertificateThumbprint, [string]$CertificateStoreLocation,
        [string]$SignToolPath, [string]$TimestampUrl)
    if ($null -ne (Read-OutLoudUninstallRecord)) { throw 'Legacy Inno installations are retired; this workflow only manages explicit WinUI development bundles.' }
    $root = [IO.Path]::GetFullPath($DeploymentRoot).TrimEnd('\', '/')
    Assert-BundlePlainPath $root
    if (Test-Path -LiteralPath $root) { throw 'Clean deployment requires a fresh versioned DeploymentRoot; no existing files are replaced.' }
    $inspector = Get-OutLoudMaintenanceHelper
    Assert-DevelopmentBundleSecurity $root $inspector
    if ($Action -eq 'Plan') {
        Write-Output "PLAN: fresh WinUI development bundle at $root. Existing prepare/sign/verify/register operations; no installer record, voice or model mutation."
        return
    }
    if (-not $PublishDirectory) { throw 'Supply the explicit current Release WinUI PublishDirectory.' }
    if ($Action -eq 'Deploy') {
        Assert-RegistryAdministrator
        Assert-GameClosed
        Assert-NoUserOverride
        if ($CertificateThumbprint -notmatch '^[0-9a-fA-F]{40}$') { throw 'Deploy requires an explicit existing signing CertificateThumbprint.' }
        $machine = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, [Microsoft.Win32.RegistryView]::Registry64)
        try {
            $existing = $machine.OpenSubKey($PocketClassPath, $false)
            if ($null -ne $existing) { $existing.Dispose(); throw 'A shared engine registration already exists. Use the in-place WinUI update at its registered deployment; clean deployment never replaces it.' }
        } finally { $machine.Dispose() }
    }
    & (Join-Path $ProductionRepository 'sapi/scripts/prepare-deployment.ps1') -ProjectRoot $ProductionRepository `
        -BundleType WinUI -BuildDirectory $BuildDirectory -DeploymentRoot $root -PublishDirectory $PublishDirectory `
        -WinUIBuildDirectory $WinUIBuildDirectory -SecurityHelperPath $inspector | Out-Host
    Assert-DevelopmentBundleSecurity $root $inspector
    if ($Action -eq 'Prepare') { Write-Output "PREPARE complete, unsigned and unregistered: $root"; return }
    if ($null -ne (Read-OutLoudUninstallRecord)) { throw 'Installation state changed; the staged bundle is retained and deployment is stopped. Installer migration is not supported.' }
    $sign = @{ DeploymentRoot = $root; CertificateThumbprint = $CertificateThumbprint; CertificateStoreLocation = $CertificateStoreLocation }
    if ($SignToolPath) { $sign.SignToolPath = $SignToolPath }
    if ($TimestampUrl) { $sign.TimestampUrl = $TimestampUrl }
    & (Join-Path $ProductionRepository 'sapi/scripts/sign-deployment.ps1') @sign | Out-Host
    & (Join-Path $ProductionRepository 'sapi/scripts/verify-deployment.ps1') -DeploymentRoot $root -RequireSignatures | Out-Host
    & (Join-Path $ProductionRepository 'sapi/scripts/register.ps1') -DeploymentRoot $root | Out-Host
    Write-Output "DEPLOY complete: signed WinUI development bundle and owned shared COM registration at $root. Managed voice registrations remain explicit CLI operations; no application data changed."
}
