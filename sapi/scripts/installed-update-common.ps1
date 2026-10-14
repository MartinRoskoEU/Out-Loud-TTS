#Requires -Version 5.1
# Discovery/planning only. These observations never authorize installed-file writes.
. (Join-Path $PSScriptRoot '../../installer/inno/production-common.ps1')
. (Join-Path $PSScriptRoot '../../installer/inno/build-components.ps1')

function Read-OutLoudUninstallRecord {
    $machine = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, [Microsoft.Win32.RegistryView]::Registry64)
    try {
        $key = $machine.OpenSubKey('Software\Microsoft\Windows\CurrentVersion\Uninstall\OutLoud_is1', $false)
        if ($null -eq $key) { return $null }
        try {
            $record = @{}
            foreach ($name in @('InstallLocation', 'DisplayName', 'DisplayVersion', 'Publisher')) {
                if ($name -notin $key.GetValueNames() -or $key.GetValueKind($name) -ne [Microsoft.Win32.RegistryValueKind]::String) {
                    throw "Invalid Out Loud installer record: $name must be REG_SZ. Checked installer repair is required."
                }
                $record[$name] = $key.GetValue($name, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
            }
            return $record
        } finally { $key.Dispose() }
    } finally { $machine.Dispose() }
}

function Resolve-OutLoudInstalledRoot($Record) {
    if ($null -eq $Record) { throw 'Out Loud is not installed. Install Out Loud with its production installer first; no standalone development fallback is available.' }
    $product = Get-OutLoudProductMetadata
    if ($Record.Publisher -cne 'Nendis' -or $Record.DisplayVersion -cne $product.version -or
        $Record.DisplayName -cnotin @($product.product_name, "$($product.product_name) version $($product.version)")) {
        throw 'Out Loud installer identity/version differs from this source. Use the matching source or checked production upgrade/repair.'
    }
    $path = [string]$Record.InstallLocation
    if ($path -notmatch '^[A-Za-z]:[\\/]' -or $path -match '["\r\n]' -or $path.Substring(2).Contains(':')) {
        throw 'Invalid Out Loud InstallLocation; a local absolute directory is required.'
    }
    $root = [IO.Path]::GetFullPath($path).TrimEnd('\', '/')
    if ($root.Length -le 3 -or -not (Test-Path -LiteralPath $root -PathType Container)) { throw 'Out Loud InstallLocation is missing or invalid; checked installer repair is required.' }
    Assert-ProductionPlainPath $root
    return $root
}

function Read-OutLoudUpdateIni([string]$Path) {
    Assert-ProductionPlainPath $Path
    if ((Get-Item -LiteralPath $Path).Length -gt 262144) { throw 'Oversized installation metadata.' }
    $sections = @{}; $current = $null
    foreach ($line in Get-Content -LiteralPath $Path -Encoding UTF8) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        if ($line -match '^\[([a-z_]+)\]$') {
            $name = $Matches[1]
            if ($sections.ContainsKey($name)) { throw 'Duplicate installation metadata section.' }
            $current = @{}; $sections[$name] = $current
        } elseif ($line -match '^([a-z_][a-z0-9_]*)=(.*)$' -and $null -ne $current) {
            $name = $Matches[1]
            if ($current.ContainsKey($name)) { throw 'Duplicate installation metadata field.' }
            $current[$name] = $Matches[2]
        } else { throw 'Malformed installation metadata.' }
    }
    return $sections
}

function Assert-OutLoudUpdateOwnership($Ownership, $Receipt, $Signature, $Registration) {
    if ($Ownership.schema -cne '1' -or $Ownership.product -cne 'OutLoud' -or $Ownership.scope -cne 'LocalMachine' -or
        $Ownership.stores -cne 'My,Root' -or $Ownership.provider -cne 'Microsoft Software Key Storage Provider' -or
        $Ownership.phase -cne 'verified' -or $Ownership.owner_sid -cne 'S-1-5-32-544' -or
        $Ownership.installation_id -cnotmatch '^[0-9a-f]{32}$' -or
        $Ownership.key_name -cne ('OutLoud.Install.' + $Ownership.installation_id) -or
        $Ownership.thumbprint -cnotmatch '^[0-9a-f]{40}$' -or $Ownership.certificate_sha256 -cnotmatch '^[0-9a-f]{64}$' -or
        $Ownership.signed_sha256 -cnotmatch '^[0-9a-f]{64}$') { throw 'Installation signing ownership is invalid.' }
    if ($Signature.signature -cne 'valid' -or $Signature.sha256 -cne $Ownership.signed_sha256 -or
        $Signature.thumbprint -cne $Ownership.thumbprint -or $Signature.certificate_sha256 -cne $Ownership.certificate_sha256) {
        throw 'Installed engine signature/hash does not match installation ownership.'
    }
    $clsid = Get-OutLoudEngineClsid
    if ($Registration.schema -cne '1' -or $Registration.registration -cne 'owned_candidate' -or
        $Registration.hive -cne 'HKLM' -or $Registration.view -cne '64' -or $Registration.clsid -cne $clsid -or
        $Receipt.schema -cne '1' -or $Receipt.phase -cne 'before_registry_transaction' -or
        $Receipt.hive -cne 'HKLM' -or $Receipt.view -cne '64' -or $Receipt.clsid -cne $clsid -or
        $Receipt.installation_id -cne $Ownership.installation_id -or $Receipt.certificate_sha256 -cne $Ownership.certificate_sha256 -or
        $Receipt.before_encoding -cne 'utf16le-typed-tree-v1' -or $Registration.before_encoding -cne $Receipt.before_encoding -or
        $Receipt.desired -cnotmatch '^[0-9a-f]+$' -or $Registration.before -cne $Receipt.desired) {
        throw 'Installed COM identity/registration receipt differs; checked recovery is required.'
    }
}

function Invoke-OutLoudUpdateInspection([string]$Root, [string]$Reports) {
    # Execute only the trusted workspace helper, never an executable selected by registry metadata.
    $helper = Join-Path $ProductionRepository 'build-installer-prerequisites/Release/OutLoudInstallSigning.exe'
    Assert-ProductionPlainPath $helper
    if (-not (Test-Path -LiteralPath $helper -PathType Leaf)) { throw 'Build the existing OutLoudInstallSigning workspace target before planning an update.' }
    foreach ($name in @('maintenance.pending', 'signing.pending.ini', 'signing.next.ini', 'signing.previous.ini', 'registration.pending.ini')) {
        if (Test-Path -LiteralPath (Join-Path $Root "configuration/$name")) { throw "Installation recovery required: configuration/$name. No update is permitted." }
    }
    $locationReport = Join-Path $Reports 'location.ini'
    & $helper --installation-inspect $Root $locationReport | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'Installed location/security/ownership inspection failed.' }
    $location = (Read-OutLoudUpdateIni $locationReport).results
    if ($location.schema -cne '1' -or $location.location -cne 'secure_candidate') { throw 'Invalid location inspection result.' }
    $registrationReport = Join-Path $Reports 'registration.ini'
    & $helper --registration-inspect $Root $registrationReport | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'Read-only installed registration inspection failed.' }
    $ownership = (Read-OutLoudUpdateIni (Join-Path $Root 'configuration/signing.ini')).ownership
    $receipt = (Read-OutLoudUpdateIni (Join-Path $Root 'configuration/registration.ini')).registration
    $signature = Get-ProductionSignatureDescription (Join-Path $Root 'bin/OutLoudPocketSAPI.dll')
    Assert-OutLoudUpdateOwnership $ownership $receipt $signature (Read-OutLoudUpdateIni $registrationReport).results
    & $helper --dependencies (Join-Path $Root 'bin/OutLoudPocketSAPI.dll') --report (Join-Path $Reports 'dependencies.ini') | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'Installed engine vendor dependency/signature check failed.' }
    return $ownership
}

function Get-OutLoudUpdateFiles {
    param([ValidateSet('Engine', 'VoiceManager', 'Both')][string]$Components)
    $wanted = @()
    if ($Components -in @('Engine', 'Both')) { $wanted += 'bin/OutLoudPocketSAPI.dll' }
    if ($Components -in @('VoiceManager', 'Both')) {
        $wanted += @('bin/OutLoudVoiceManagerNative.dll', 'bin/OutLoudVoiceImportHelper.exe', 'bin/OutLoud.VoiceManager.exe',
            'bin/OutLoud.VoiceManager.dll', 'bin/OutLoud.VoiceManager.deps.json', 'bin/OutLoud.VoiceManager.runtimeconfig.json')
    }
    $policy = Get-ProductionPolicy
    foreach ($path in $wanted) {
        $entry = @($policy.files | Where-Object { $_.path -ceq $path })
        $source = if ($path -like 'bin/OutLoud.VoiceManager.*') { 'managed' } else { 'native' }
        if ($entry.Count -ne 1 -or $entry[0].source -cne $source -or $entry[0].name -cne $path.Substring(4)) { throw "Update component differs from production package policy: $path" }
        $entry[0]
    }
}

function Assert-OutLoudUpdateFilesAvailable([string[]]$Paths) {
    # Preliminary read-only sharing probe, not a loaded-image or future publication guarantee.
    $pins = New-Object 'Collections.Generic.List[IO.FileStream]'
    try {
        foreach ($path in $Paths) {
            Assert-ProductionPlainPath $path
            try { $pins.Add([IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)) }
            catch { throw "Installed file unavailable/in use: $path. Close Voice Manager and any SAPI clients (including WoW) manually, or resolve access errors. No files were changed. $($_.Exception.Message)" }
        }
    } finally { foreach ($pin in $pins) { $pin.Dispose() } }
}

function New-OutLoudUpdatePlan([string]$Root, [string]$Components, $Ownership) {
    $files = @(Get-OutLoudUpdateFiles $Components)
    $paths = @($files | ForEach-Object { Resolve-ProductionPath $Root $_.path })
    Assert-OutLoudUpdateFilesAvailable $paths
    $observed = foreach ($file in $files) {
        $path = Resolve-ProductionPath $Root $file.path
        [ordered]@{ path = $file.path; installed_sha256 = Get-ProductionHash $path; source = $file.source; name = $file.name }
    }
    return [ordered]@{
        schema = 1; purpose = 'development-update-plan'; components = $Components; installation_root = $Root
        app_id = 'OutLoud'; version = (Get-OutLoudProductMetadata).version; clsid = Get-OutLoudEngineClsid
        installation_id = $Ownership.installation_id; certificate_sha256 = $Ownership.certificate_sha256
        scope = 'LocalMachine'; files = @($observed); inspection = 'ownership_candidate'
        deployment = 'blocked'; blocker = 'Shared lifecycle requires live validation and A03.11 uninstall before publication is enabled.'
        signing = 'pending_native_maintenance'; replacement = 'not_attempted'; recovery = 'native_transaction_pending'
    }
}

function Prepare-OutLoudUpdateCandidates($Plan, [string]$Reports) {
    $work = Join-Path $ProductionRepository ('generated/installer/builds/' + [Guid]::NewGuid().ToString('N'))
    $built = Build-OutLoudComponents -Components $Plan.components -WorkDirectory $work
    Copy-OutLoudUpdateCandidates $Plan $Reports $built
    Add-OutLoudMaintenanceBootstrap $Plan $built
}

function Copy-OutLoudUpdateCandidates($Plan, [string]$Reports, $Built) {
    $candidates = Join-Path $Reports 'candidates'
    Assert-ProductionPlainPath $candidates
    if (Test-Path -LiteralPath $candidates) { throw 'Candidate directory already exists; use a fresh workspace report.' }
    foreach ($file in $Plan.files) {
        $source = if ($file.source -eq 'native') { Join-Path $Built.native "Release/$($file.name)" } else { Join-Path $Built.publish $file.name }
        $hash = Get-ProductionHash $source
        if ($file.name -match '\.(exe|dll)$') {
            [void](Get-ProductionPE $source)
            Assert-OutLoudBinaryVersion $source (Get-OutLoudProductMetadata)
        }
        if ($file.path -ceq 'bin/OutLoudPocketSAPI.dll') {
            $signature = Get-ProductionSignatureDescription $source
            if ($signature.signature -cne 'unsigned') { throw 'Engine candidate must remain unsigned until the installation-owned signing transaction.' }
        }
        $target = Resolve-ProductionPath $candidates $file.path
        [void](New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($target)) -Force)
        Copy-Item -LiteralPath $source -Destination $target
        if ((Get-ProductionHash $source) -cne $hash -or (Get-ProductionHash $target) -cne $hash) { throw 'Candidate changed while copying; preparation failed.' }
        $file['candidate_sha256'] = $hash
    }
    $Plan['candidates'] = $candidates
    $Plan['preparation'] = 'built_and_copied_unsigned_not_deployable'
}

function Assert-OutLoudDevelopmentPublication {
    # No override, -Force, certificate fallback or independent replacement implementation.
    Assert-OutLoudMaintenanceEnabled
}
