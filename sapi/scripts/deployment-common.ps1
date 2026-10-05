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

function Read-BundleInventory([string]$Root) {
    $inventory = Get-Content -LiteralPath (Join-Path $Root 'deployment-inventory.json') -Raw | ConvertFrom-Json
    # Explicit allowlist prevents manifest edits from expanding the signing scope.
    $policies = @{
        'bin/OutLoudPocketSAPI.dll' = 'project-sign'
        'bin/outloud-sapi-check.exe' = 'project-sign'
        'bin/msvcp140.dll' = 'preserve-microsoft'
        'bin/msvcp140_atomic_wait.dll' = 'preserve-microsoft'
        'bin/vcruntime140.dll' = 'preserve-microsoft'
        'bin/vcruntime140_1.dll' = 'preserve-microsoft'
    }
    if ($inventory.schema -ne 1 -or $inventory.engine_clsid -ne '{F38C3398-3552-43B9-8350-7DD301327D70}' -or @($inventory.binaries).Count -ne 6) {
        throw 'Unrecognized deployment inventory.'
    }
    $seen = @{}
    foreach ($entry in $inventory.binaries) {
        if (-not $policies.ContainsKey($entry.path) -or $seen.ContainsKey($entry.path) -or
            $entry.signing -ne $policies[$entry.path] -or $entry.machine -ne '8664') { throw 'Invalid binary inventory.' }
        $seen[$entry.path] = $true
    }
    if ((@($inventory.assets) -join '|') -ne 'runtime/pocket-english.olp|runtime/pocket-english-tokenizer.bin|voices/gnome_male.olv') {
        throw 'Unexpected development asset inventory.'
    }
    return $inventory
}

function Assert-MicrosoftSignature([string]$Path) {
    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation(?:,|$)') {
        throw "Expected intact Microsoft signature: $Path ($($signature.Status))"
    }
}

function Assert-Bundle([string]$Root, [bool]$RequireSignatures) {
    $inventory = Read-BundleInventory $Root
    $manifest = Get-Content -LiteralPath (Join-Path $Root 'deployment.json') -Raw | ConvertFrom-Json
    if ($manifest.schema -ne 1) { throw 'Unexpected deployment manifest.' }
    $records = @{}
    foreach ($record in $manifest.files) {
        if ($records.ContainsKey($record.path)) { throw "Duplicate manifest record: $($record.path)" }
        $records[$record.path] = $record
        $path = Resolve-BundlePath $Root $record.path
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing deployment file: $path" }
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $record.sha256) { throw "Deployment hash differs: $path" }
    }
    foreach ($name in @($inventory.assets) + @('deployment-inventory.json', 'scripts/deployment-common.ps1', 'scripts/register.ps1',
        'scripts/unregister.ps1', 'scripts/registry-common.ps1', 'scripts/sign-deployment.ps1', 'scripts/verify-deployment.ps1',
        'docs/SAPI_ADAPTER.md', 'docs/SAPI_CANCELLATION.md', 'RNG_NOTICES.md')) {
        if (-not $records.ContainsKey($name)) { throw "Missing required manifest record: $name" }
    }
    foreach ($entry in $inventory.binaries) {
        if (-not $records.ContainsKey($entry.path)) { throw "Missing binary manifest record: $($entry.path)" }
        $path = Resolve-BundlePath $Root $entry.path
        Assert-X64Image $path
        $signature = Get-AuthenticodeSignature -LiteralPath $path
        if ($entry.signing -eq 'preserve-microsoft') { Assert-MicrosoftSignature $path }
        elseif ($signature.Status -ne 'Valid' -and ($RequireSignatures -or $signature.Status -ne 'NotSigned')) {
            throw "Invalid project signature: $path ($($signature.Status))"
        }
    }
    # Reject extra PE files: the inventory covers ALL binaries shipped by this bundle.
    foreach ($file in Get-ChildItem -LiteralPath $Root -File -Recurse) {
        if ($file.Extension -in @('.dll', '.exe')) {
            $relative = $file.FullName.Substring([IO.Path]::GetFullPath($Root).TrimEnd('\', '/').Length + 1).Replace('\', '/')
            if ($relative -notin @($inventory.binaries.path)) { throw "Uninventoried binary: $relative" }
        }
    }
    return $manifest
}
