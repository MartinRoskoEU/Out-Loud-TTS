#Requires -Version 5.1
# Read-only helpers. No deployment, registry, certificate or signing operations.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProductionRepository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $ProductionRepository 'sapi/identity-common.ps1')
. (Join-Path $PSScriptRoot 'prerequisites-common.ps1')
. (Join-Path $PSScriptRoot 'consent-common.ps1')
. (Join-Path $PSScriptRoot 'signing-common.ps1')
. (Join-Path $PSScriptRoot 'registration-common.ps1')
. (Join-Path $PSScriptRoot 'version-common.ps1')
. (Join-Path $PSScriptRoot 'maintenance-common.ps1')

function Get-ProductionPolicy {
    $policy = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'package.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($policy.engine_clsid -cne (Get-OutLoudEngineClsid)) { throw 'Package CLSID differs from sapi/src/identity.hpp.' }
    return $policy
}

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

function Get-ProductionSourceIdentity {
    $head = (& git -C $ProductionRepository rev-parse HEAD | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Cannot record source revision.' }
    $paths = @(& git -C $ProductionRepository ls-files -co --exclude-standard | Sort-Object -Unique)
    if ($LASTEXITCODE -ne 0) { throw 'Cannot inventory source inputs.' }
    $rows = foreach ($path in $paths) { "$path`0$(Get-ProductionHash (Resolve-ProductionPath $ProductionRepository $path))" }
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $digest = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes(($rows -join "`n")))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
    return @{ head = $head; tree_sha256 = $digest; files = $paths.Count }
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

function Assert-ProductionIcon([hashtable]$PE, [string]$IconPath) {
    $icon = [IO.File]::ReadAllBytes($IconPath)
    if ($icon.Length -lt 6 -or [BitConverter]::ToUInt32($icon, 0) -ne 0x10000) { throw 'Invalid application ICO.' }
    $count = [BitConverter]::ToUInt16($icon, 4)
    $groups = @($PE.resources.Keys | Where-Object { $_.StartsWith('14/') })
    if ($count -lt 1 -or $groups.Count -ne 1) { throw 'Missing application icon group.' }
    $group = $PE.resources[$groups[0]]
    if ($group.Length -ne 6 + 14 * $count -or [BitConverter]::ToUInt16($group, 4) -ne $count) { throw 'Application icon frames differ.' }
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        for ($i = 0; $i -lt $count; $i++) {
            $offset = 6 + 16 * $i
            $length = [BitConverter]::ToUInt32($icon, $offset + 8)
            $start = [BitConverter]::ToUInt32($icon, $offset + 12)
            $id = [BitConverter]::ToUInt16($group, 6 + 14 * $i + 12)
            $keys = @($PE.resources.Keys | Where-Object { $_.StartsWith("3/$id/") })
            if ($start + $length -gt $icon.Length -or $keys.Count -ne 1) { throw 'Invalid embedded icon image.' }
            $expected = [BitConverter]::ToString($sha.ComputeHash($icon, $start, $length))
            $actual = [BitConverter]::ToString($sha.ComputeHash([byte[]]$PE.resources[$keys[0]]))
            if ($actual -ne $expected) { throw 'Executable icon does not match Assets/OutLoud.ico.' }
        }
    } finally { $sha.Dispose() }
}

function Assert-ProductionStage([string]$StageDirectory) {
    $root = [IO.Path]::GetFullPath($StageDirectory).TrimEnd('\', '/')
    Assert-ProductionPlainPath $root
    $policy = Get-ProductionPolicy
    $expected = @('package-manifest.json', 'build-record.json', 'prerequisites.ini', 'runtime-packages.ini', 'signing-policy.ini', 'registration-policy.ini', 'maintenance-input.ini', 'maintenance-files.iss', 'maintenance-copy.iss', 'installer-version.iss', 'bootstrap/OutLoudPrerequisites.exe') + @($policy.files | ForEach-Object { 'payload/' + $_.path })
    $allowedDirectories = @('payload', 'payload/bin', 'payload/runtime', 'payload/licenses', 'bootstrap')
    function Read-StageTree([string]$Directory) {
        foreach ($item in Get-ChildItem -LiteralPath $Directory -Force) {
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse point in package: $($item.FullName)" }
            $relative = $item.FullName.Substring($root.Length + 1).Replace('\', '/')
            if ($item.PSIsContainer) {
                if ($relative -cnotin $allowedDirectories) { throw "Unexpected package directory: $relative" }
                Read-StageTree $item.FullName
            } elseif ($relative -cnotin $expected) { throw "Unexpected package file: $relative" }
        }
    }
    Read-StageTree $root
    foreach ($path in $expected) {
        if (-not (Test-Path -LiteralPath (Resolve-ProductionPath $root $path) -PathType Leaf)) { throw "Missing package file: $path" }
    }
    $manifest = Get-Content -LiteralPath (Join-Path $root 'package-manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $record = Get-Content -LiteralPath (Join-Path $root 'build-record.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $product = Get-OutLoudProductMetadata
    Assert-OutLoudInstallerVersionSource
    if ($policy.supported_platform.name -ne 'Windows 11 x64' -or $policy.supported_platform.minimum_build -ne 22000 -or
        $policy.supported_platform.nt_major -ne 10 -or $policy.supported_platform.nt_minor -ne 0 -or
        $policy.supported_platform.product_type -ne 1 -or $policy.supported_platform.machine -ne '8664' -or
        $record.supported_platform -ne 'Windows 11 x64' -or $record.prerequisites.vc_minimum -notmatch '^14\.\d+\.\d+\.\d+$') {
        throw 'Unexpected Windows or runtime prerequisite policy.'
    }
    if ($manifest.schema -ne 1 -or $manifest.product_id -ne $policy.product_id -or
        $manifest.status -ne 'public-release' -or $manifest.version -notmatch '^\d+\.\d+\.\d+$' -or
        $record.version -ne $manifest.version -or $record.build_testing -ne 'OFF' -or $record.development_tools -ne 'OFF' -or
        $record.configuration -ne 'Release' -or $record.architecture -ne 'x64' -or $record.dotnet_sdk -notmatch '^10\.' -or
        $record.apphost_pack_version -ne $policy.apphost_pack_version -or $record.apphost_sha256 -notmatch '^[0-9a-f]{64}$') {
        throw 'Unexpected public release manifest/build identity. Rebuild historical stages.'
    }
    if ($manifest.version -cne $product.version -or $record.version -cne $product.version -or
        $manifest.product_name -cne $product.product_name -or $record.product_name -cne $product.product_name -or
        $manifest.copyright -cne $product.copyright -or $record.copyright -cne $product.copyright -or
        $manifest.file_version -cne $product.file_version -or $record.file_version -cne $product.file_version -or
        $manifest.version_source_sha256 -cne $product.source_sha256 -or $record.version_source_sha256 -cne $product.source_sha256 -or
        $manifest.installer.app_id -cne $product.app_id -or $manifest.installer.app_version -cne $product.version -or
        $manifest.installer.file_version -cne $product.file_version -or $manifest.installer.output_filename -cne $product.installer_filename -or
        $record.installer.app_id -cne $product.app_id -or $record.installer.app_version -cne $product.version -or
        $record.installer.file_version -cne $product.file_version -or $record.installer.output_filename -cne $product.installer_filename -or
        (Get-ProductionHash (Join-Path $root 'installer-version.iss')) -cne $manifest.installer_version_sha256 -or
        [IO.File]::ReadAllText((Join-Path $root 'installer-version.iss')) -cne (Get-OutLoudInstallerVersionText $product)) {
        throw 'Production product/installer version metadata differs from ProductVersion.props.'
    }
    if ((Get-ProductionHash (Join-Path $root 'build-record.json')) -ne $manifest.build_record_sha256 -or
        (Get-ProductionHash (Join-Path $PSScriptRoot 'package.json')) -ne $manifest.policy_sha256 -or
        (Get-ProductionHash (Join-Path $ProductionRepository 'ui/OutLoud.VoiceManager/Assets/OutLoud.ico')) -ne $manifest.icon_sha256) {
        throw 'Build record, package policy or source icon changed.'
    }
    if ((Get-ProductionHash (Join-Path $root 'prerequisites.ini')) -ne $manifest.prerequisites_sha256 -or
        (Get-ProductionHash (Join-Path $root 'bootstrap/OutLoudPrerequisites.exe')) -ne $manifest.detector_sha256) { throw 'Prerequisite metadata or detector changed.' }
    $bootstrap = Get-ProductionPE (Join-Path $root 'bootstrap/OutLoudPrerequisites.exe')
    Assert-OutLoudBinaryVersion (Join-Path $root 'bootstrap/OutLoudPrerequisites.exe') $product
    if ((Get-ProductionHash (Join-Path $root 'runtime-packages.ini')) -ne $manifest.runtime_packages_sha256 -or
        (Get-ProductionHash (Join-Path $PSScriptRoot 'runtime-packages.json')) -ne $manifest.runtime_policy_sha256 -or
        [IO.File]::ReadAllText((Join-Path $root 'runtime-packages.ini')) -cne (Get-RuntimePackageIni)) {
        throw 'Runtime acquisition policy or staged metadata changed.'
    }
    foreach ($dependency in $bootstrap.imports) {
        if ($dependency -notmatch '^(?i:KERNEL32|ADVAPI32|VERSION|SHELL32|ole32|bcrypt|WINTRUST|CRYPT32)\.dll$') { throw "Detector must run without VC or .NET prerequisites: $dependency" }
    }
    if ((Get-ProductionHash (Join-Path $root 'signing-policy.ini')) -ne $manifest.signing_ini_sha256 -or
        (Get-ProductionHash (Join-Path $PSScriptRoot 'signing-policy.json')) -ne $manifest.signing_policy_sha256) { throw 'Signing policy metadata changed.' }
    $signature = Get-ProductionSignatureDescription (Join-Path $root 'payload/bin/OutLoudPocketSAPI.dll')
    $maintenance = Get-OutLoudMaintenanceIni $manifest.files $manifest.detector_sha256 $manifest.prerequisites_sha256
    if ((Get-ProductionHash (Join-Path $root 'maintenance-files.iss')) -cne $manifest.maintenance_files_sha256 -or
        (Get-ProductionHash (Join-Path $root 'maintenance-copy.iss')) -cne $manifest.maintenance_copy_sha256 -or
        [IO.File]::ReadAllText((Join-Path $root 'maintenance-files.iss')) -cne (Get-OutLoudMaintenanceFilesText) -or
        [IO.File]::ReadAllText((Join-Path $root 'maintenance-copy.iss')) -cne (Get-OutLoudMaintenanceCopyText)) { throw 'Generated Inno payload differs from package.json.' }
    if ((Get-ProductionHash (Join-Path $root 'maintenance-input.ini')) -cne $manifest.maintenance_ini_sha256 -or
        [IO.File]::ReadAllText((Join-Path $root 'maintenance-input.ini')) -cne $maintenance) { throw 'Maintenance inputs differ from the explicit production package.' }
    if ($signature.signature -ne 'unsigned' -or [IO.File]::ReadAllText((Join-Path $root 'signing-policy.ini')) -cne
        (Get-ProductionSigningIni $signature.sha256 $signature.sip_sha256)) { throw 'Signing inputs differ from the reviewed unsigned engine.' }
    $requirements = Get-Content -LiteralPath (Join-Path $root 'prerequisites.ini') -Raw
    if ($requirements -notmatch ('(?m)^vc_minimum=' + [regex]::Escape($record.prerequisites.vc_minimum) + '\r?$') -or
        $requirements -notmatch '(?m)^desktop_minimum=10\.0\.0\r?$' -or $requirements -notmatch '(?m)^core_minimum=10\.0\.0\r?$') {
        throw 'Prerequisite requirements disagree with build metadata.'
    }
    $records = @{}
    foreach ($file in $manifest.files) {
        if ($file.path -cnotin @($policy.files.path) -or $records.ContainsKey($file.path)) { throw 'Unexpected or duplicate manifest file.' }
        $records[$file.path] = $file
    }
    if ($records.Count -ne @($policy.files).Count) { throw 'Incomplete package manifest.' }
    $imports = @{}
    foreach ($file in $policy.files) {
        $path = Resolve-ProductionPath (Join-Path $root 'payload') $file.path
        $hash = Get-ProductionHash $path
        if ($hash -ne $records[$file.path].sha256 -or (Get-Item -LiteralPath $path).Length -ne $records[$file.path].bytes) {
            throw "Package content differs: $($file.path)"
        }
        if ($file.source -in @('model', 'tokenizer')) {
            if ($hash -ne $file.sha256 -or (Get-Item -LiteralPath $path).Length -ne $file.bytes) { throw "Incorrect pinned asset: $($file.path)" }
        } elseif ($file.source -eq 'repository') {
            if ($hash -ne (Get-ProductionHash (Resolve-ProductionPath $ProductionRepository $file.name))) { throw "License material differs: $($file.path)" }
        } elseif ($file.source -eq 'consent') {
            if ($file.path -ne 'licenses/OutLoud-Installation-Terms.txt' -or
                $hash -ne (Get-ConsentTextHash (Get-InstallationAgreementText))) { throw 'Installation agreement differs from complete legal sources.' }
        }
        if ($file.pe) {
            $pe = Get-ProductionPE $path
            Assert-OutLoudBinaryVersion $path $product
            $imports[$file.path] = $pe.imports
            foreach ($dependency in $pe.imports) {
                if ($file.source -eq 'installer-native') {
                    if ($dependency -notmatch '^(?i:KERNEL32|ADVAPI32|CRYPT32|NCRYPT|bcrypt|WINTRUST|SHELL32|ole32|ktmw32|RstrtMgr|VERSION)\.dll$') { throw "Signer requires an unreviewed dependency: $dependency" }
                    continue
                }
                if ($dependency -notmatch '^(?i:KERNEL32|ADVAPI32|USER32|bcrypt|ole32|OLEAUT32|SHELL32|ktmw32|MSVCP140|MSVCP140_ATOMIC_WAIT|VCRUNTIME140|VCRUNTIME140_1)\.dll$' -and
                    $dependency -notmatch '^(?i:api-ms-win-crt-[a-z0-9-]+)\.dll$') { throw "Unreviewed runtime import: $dependency ($($file.path))" }
            }
            if ($file.path -eq 'bin/OutLoud.VoiceManager.exe') {
                Assert-ProductionIcon $pe (Join-Path $ProductionRepository 'ui/OutLoud.VoiceManager/Assets/OutLoud.ico')
            }
        }
    }
    $assembly=[Reflection.AssemblyName]::GetAssemblyName((Join-Path $root 'payload/bin/OutLoud.VoiceManager.dll'))
    if ($assembly.Name -cne 'OutLoud.VoiceManager' -or $assembly.Version.ToString() -cne $product.file_version) {
        throw 'Voice Manager assembly version/identity differs from ProductVersion.props.'
    }
    foreach ($name in @('OutLoudVoiceManagerNative.dll', 'OutLoudVoiceImportHelper.exe')) {
        if ($records["bin/$name"].sha256 -ne $record.native_matches.$name) { throw "Native/frontend build pairing differs: $name" }
    }
    $runtime = Get-Content -LiteralPath (Join-Path $root 'payload/bin/OutLoud.VoiceManager.runtimeconfig.json') -Raw | ConvertFrom-Json
    $frameworks = @($runtime.runtimeOptions.frameworks | ForEach-Object { "$($_.name)/$($_.version)" } | Sort-Object)
    if ($runtime.runtimeOptions.tfm -ne 'net10.0' -or ($frameworks -join '|') -ne 'Microsoft.NETCore.App/10.0.0|Microsoft.WindowsDesktop.App/10.0.0') {
        throw 'Expected framework-dependent .NET 10 Desktop application.'
    }
    $deps = Get-Content -LiteralPath (Join-Path $root 'payload/bin/OutLoud.VoiceManager.deps.json') -Raw | ConvertFrom-Json
    $libraries = @($deps.libraries.PSObject.Properties)
    if ($libraries.Count -ne 1 -or $libraries[0].Name -ne "OutLoud.VoiceManager/$($manifest.version)" -or $libraries[0].Value.type -ne 'project') {
        throw 'Unexpected managed dependency closure.'
    }
    if ((Get-ProductionHash (Join-Path $root 'registration-policy.ini')) -ne $manifest.registration_ini_sha256 -or
        [IO.File]::ReadAllText((Join-Path $root 'registration-policy.ini')) -cne
        (Get-ProductionRegistrationIni $manifest.files $signature.sip_sha256 $manifest.detector_sha256 $manifest.prerequisites_sha256)) {
        throw 'Registration policy differs from the complete verified payload.'
    }
    return [pscustomobject]@{ version = $manifest.version; files = $records.Count; imports = $imports; status = $manifest.status }
}
