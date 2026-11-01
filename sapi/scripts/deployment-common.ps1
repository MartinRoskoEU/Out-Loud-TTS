#Requires -Version 5.1
# Function definitions only; no registry, certificate or signing mutations.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-BundlePath([string]$Root, [string]$Relative) {
    $base = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    if ([IO.Path]::IsPathRooted($Relative)) { throw "Expected install-relative path: $Relative" }
    $path = [IO.Path]::GetFullPath((Join-Path $base $Relative))
    if (-not $path.StartsWith($base + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path escapes deployment: $Relative"
    }
    return $path
}

function Assert-X64Image([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $reader = New-Object IO.BinaryReader($stream)
    try {
        if ($stream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5a4d) { throw "Invalid PE: $Path" }
        $stream.Position = 60
        $offset = $reader.ReadInt32()
        if ($offset -lt 64 -or $offset -gt $stream.Length - 6) { throw "Invalid PE offset: $Path" }
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664) { throw "Expected x64 PE: $Path" }
    } finally { $reader.Dispose() }
}

function Assert-BundlePlainPath([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    if ($full -notmatch '^[A-Za-z]:\\' -or $full.Substring(2).Contains(':')) { throw "Expected a local path without streams: $Path" }
    for ($current = $full; $current; $current = [IO.Path]::GetDirectoryName($current)) {
        if (Test-Path -LiteralPath $current) {
            if ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse point refused: $current" }
        }
    }
}

function Get-BundleFiles([string]$Root) {
    Assert-BundlePlainPath $Root
    foreach ($item in Get-ChildItem -LiteralPath $Root -Force) {
        Assert-BundlePlainPath $item.FullName
        if ($item.PSIsContainer) { Get-BundleFiles $item.FullName }
        else { $item }
    }
}

function Assert-BundleImage([string]$Path, [string]$Machine) {
    if ($Machine -eq '8664') { Assert-X64Image $Path; return }
    # WinUI's Microsoft projection assemblies are AnyCPU, not native x86 DLLs.
    if ($Machine -ne 'msil' -or [Reflection.AssemblyName]::GetAssemblyName($Path).ProcessorArchitecture -ne 'MSIL') {
        throw "Expected an architecture-neutral managed assembly: $Path"
    }
}

function Assert-BundlePinnedFile([string]$Path, $Entry) {
    if ($Entry.PSObject.Properties.Name -contains 'sha256') {
        if ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ne $Entry.sha256) { throw "Pinned component differs: $Path" }
    }
}

function Read-BundleInventory([string]$Root, [switch]$AllowPreviousInventory) {
    $inventoryPath = Join-Path $Root 'deployment-inventory.json'
    Assert-BundlePlainPath $inventoryPath
    $inventory = Get-Content -LiteralPath $inventoryPath -Raw | ConvertFrom-Json
    if ($inventory.schema -eq 3) {
        # Fixed reviewed payload, not a caller-extensible manifest. Update this
        # pin only with a reviewed SDK/component inventory change.
        $hash = [Security.Cryptography.SHA256]::Create()
        try { $inventoryHash = [BitConverter]::ToString($hash.ComputeHash([Text.Encoding]::UTF8.GetBytes([IO.File]::ReadAllText($inventoryPath).Replace("`r`n", "`n")))).Replace('-', '').ToLowerInvariant() }
        finally { $hash.Dispose() }
        # The previous reviewed WinUI inventory differs only by RNG_NOTICES.md.
        # Accept it only when inspecting the source of an in-place update.
        $previous = $AllowPreviousInventory -and $inventoryHash -eq '184129c8a4384694881bb2d86d6db194bad0a47cb0ec9e460d0020fa1650fbcd'
        if ($inventoryHash -ne 'eeecccee890abe42ff0baae4a3b19216f9fd2cd018cedee18409658d8b28ce88' -and -not $previous) {
            throw 'Unrecognized WinUI deployment inventory.'
        }
        $inventory | Add-Member NoteProperty binaries @($inventory.files | Where-Object { $_.PSObject.Properties.Name -contains 'signing' })
        $inventory | Add-Member NoteProperty assets @($inventory.files | Where-Object { $_.PSObject.Properties.Name -notcontains 'signing' } | ForEach-Object path)
        return $inventory
    }
    throw 'Unrecognized deployment inventory; only the fixed native WinUI payload is supported.'
}

function Assert-MicrosoftSignature([string]$Path) {
    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation(?:,|$)') {
        throw "Expected intact Microsoft signature: $Path ($($signature.Status))"
    }
}

function Assert-Bundle([string]$Root, [bool]$RequireSignatures, [switch]$AllowPreviousInventory) {
    Assert-BundlePlainPath $Root
    $inventory = Read-BundleInventory $Root -AllowPreviousInventory:$AllowPreviousInventory
    $manifest = Get-Content -LiteralPath (Join-Path $Root 'deployment.json') -Raw | ConvertFrom-Json
    if ($manifest.schema -ne 1) { throw 'Unexpected deployment manifest.' }
    $records = @{}
    foreach ($record in $manifest.files) {
        if ($records.ContainsKey($record.path)) { throw "Duplicate manifest record: $($record.path)" }
        if ($inventory.schema -eq 3 -and $record.path -cnotin @($inventory.files.path)) { throw "Uninventoried record: $($record.path)" }
        $records[$record.path] = $record
        $path = Resolve-BundlePath $Root $record.path
        Assert-BundlePlainPath $path
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing deployment file: $path" }
        if ($inventory.schema -eq 3 -and (Get-Item -LiteralPath $path).Length -ne $record.size_bytes) { throw "Deployment size differs: $path" }
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $record.sha256) { throw "Deployment hash differs: $path" }
    }
    foreach ($name in @($inventory.assets) + @('deployment-inventory.json', 'scripts/deployment-common.ps1', 'scripts/register.ps1',
        'scripts/unregister.ps1', 'scripts/registry-common.ps1', 'scripts/sign-deployment.ps1', 'scripts/verify-deployment.ps1',
        'docs/SAPI_ADAPTER.md', 'docs/MULTI_VOICE_SAPI.md', 'docs/SAPI_CANCELLATION.md')) {
        if (-not $records.ContainsKey($name)) { throw "Missing required manifest record: $name" }
    }
    foreach ($entry in $inventory.binaries) {
        if (-not $records.ContainsKey($entry.path)) { throw "Missing binary manifest record: $($entry.path)" }
        $path = Resolve-BundlePath $Root $entry.path
        Assert-BundleImage $path $entry.machine
        Assert-BundlePinnedFile $path $entry
        if ($entry.signing -eq 'preserve-pinned') { continue }
        $signature = Get-AuthenticodeSignature -LiteralPath $path
        if ($entry.signing -eq 'preserve-microsoft') { Assert-MicrosoftSignature $path }
        elseif ($signature.Status -ne 'Valid' -and ($RequireSignatures -or $signature.Status -ne 'NotSigned')) {
            throw "Invalid project signature: $path ($($signature.Status))"
        }
    }
    # Reject extra PE files: the inventory covers ALL binaries shipped by this bundle.
    foreach ($file in Get-BundleFiles $Root) {
        Assert-BundlePlainPath $file.FullName
        if ($inventory.schema -eq 3) {
            $relative = $file.FullName.Substring([IO.Path]::GetFullPath($Root).TrimEnd('\', '/').Length + 1).Replace('\', '/')
            if ($relative -cnotin @($inventory.files.path) -and $relative -cne 'deployment.json' -and -not ($relative -clike 'reports/*' -and $file.Extension -in @('.json', '.ini'))) {
                throw "Uninventoried file: $relative"
            }
        }
        if ($file.Extension -in @('.dll', '.exe')) {
            $relative = $file.FullName.Substring([IO.Path]::GetFullPath($Root).TrimEnd('\', '/').Length + 1).Replace('\', '/')
            if ($relative -notin @($inventory.binaries.path)) { throw "Uninventoried binary: $relative" }
        }
    }
    if ($inventory.schema -eq 3) {
        foreach ($entry in $inventory.files) { Assert-BundlePinnedFile (Resolve-BundlePath $Root $entry.path) $entry }
    }
    return $manifest
}

function Assert-DevelopmentBundleSecurity([string]$Root, [string]$InspectorPath, [string]$ReportDirectory) {
    Assert-BundlePlainPath $Root
    Assert-BundlePlainPath $InspectorPath
    Assert-X64Image $InspectorPath
    if (-not $ReportDirectory) { $ReportDirectory = [IO.Path]::GetTempPath() }
    Assert-BundlePlainPath $ReportDirectory
    $report = Join-Path $ReportDirectory ('outloud-location-' + [Guid]::NewGuid().ToString('N') + '.ini')
    & $InspectorPath --installation-inspect $Root $report --development-bundle | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Deployment location/ACL/independent-file inspection failed. See $report" }
    $text = Get-Content -LiteralPath $report -Raw
    if ($text -notmatch '(?m)^location=secure_candidate\r?$') { throw 'Native security inspection did not verify the deployment.' }
}
