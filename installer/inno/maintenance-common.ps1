#Requires -Version 5.1
# Candidate manifests only. Publication and recovery belong to the native helper.
function Get-OutLoudMaintenanceFilesText {
    $lines = @('; Generated from package.json. Native transaction owns payload publication.')
    $index = 0
    foreach ($entry in (Get-ProductionPolicy).files) {
        $relative = $entry.path.Replace('/', '\')
        $lines += "Source: `"{#StageRoot}\payload\$relative`"; DestName: `"maintenance-$index.bin`"; Flags: dontcopy"
        $index++
    }
    return ($lines -join "`n") + "`n"
}

function Get-OutLoudMaintenanceCopyText {
    $lines = @('{ Generated from package.json. Only materialize private temporary candidates. }')
    $index = 0
    foreach ($entry in (Get-ProductionPolicy).files) {
        $relative = $entry.path.Replace('/', '\')
        $lines += "  MaterializeMaintenanceFile('maintenance-$index.bin', '$relative');"
        $index++
    }
    return ($lines -join "`n") + "`n"
}
function Get-OutLoudMaintenanceIni($Files, [string]$DetectorHash, [string]$RequirementsHash) {
    foreach ($hash in @($DetectorHash, $RequirementsHash)) {
        if ($hash -cnotmatch '^[0-9a-f]{64}$') { throw 'Invalid maintenance bootstrap hash.' }
    }
    $policy = Get-ProductionPolicy
    $entries = @($Files | Sort-Object { $_.path })
    $paths = @($entries | ForEach-Object { $_.path })
    if ($paths.Count -eq 0 -or @($paths | Select-Object -Unique).Count -ne $paths.Count) { throw 'Empty or duplicate maintenance selection.' }
    $lines = @('[maintenance]', 'schema=1', 'app_id=OutLoud', "version=$((Get-OutLoudProductMetadata).version)",
        "clsid=$(Get-OutLoudEngineClsid)", '[bootstrap]', "detector_sha256=$DetectorHash", "requirements_sha256=$RequirementsHash")
    for ($i=0; $i -lt $entries.Count; $i++) {
        $entry = $entries[$i]
        if ($entry.path -cnotin @($policy.files.path) -or $entry.sha256 -cnotmatch '^[0-9a-f]{64}$') { throw 'Maintenance file is not in the production allowlist or has an invalid hash.' }
        $lines += @("[file$i]", "path=$($entry.path)", "sha256=$($entry.sha256)")
    }
    return ($lines -join "`n") + "`n"
}

function Add-OutLoudMaintenanceBootstrap($Plan, $Built) {
    $probeRoot = Join-Path $ProductionRepository 'build-installer-prerequisites'
    $detector = Join-Path $probeRoot 'Release/OutLoudPrerequisites.exe'
    Assert-ProductionPlainPath $detector
    if (-not (Test-Path -LiteralPath $detector -PathType Leaf)) { throw 'Build the existing OutLoudPrerequisites target before preparing maintenance candidates.' }
    [void](Get-ProductionPE $detector)
    Assert-OutLoudBinaryVersion $detector (Get-OutLoudProductMetadata)
    $requirements = Get-NativePrerequisiteRequirements $Built.native
    $destination = Join-Path $Plan.candidates 'bootstrap'
    [void](New-Item -ItemType Directory -Path $destination)
    $hash = Get-ProductionHash $detector
    Copy-Item -LiteralPath $detector -Destination (Join-Path $destination 'OutLoudPrerequisites.exe')
    if ((Get-ProductionHash $detector) -cne $hash -or (Get-ProductionHash (Join-Path $destination 'OutLoudPrerequisites.exe')) -cne $hash) { throw 'Maintenance detector changed during copying.' }
    $ini = Join-Path $Plan.candidates 'prerequisites.ini'
    [IO.File]::WriteAllText($ini, "[requirements]`nvc_minimum=$($requirements.vc_minimum)`ndesktop_minimum=10.0.0`ncore_minimum=10.0.0`n", [Text.Encoding]::ASCII)
    $files = @($Plan.files | ForEach-Object { @{ path=$_.path; sha256=$_.candidate_sha256 } })
    [IO.File]::WriteAllText((Join-Path $Plan.candidates 'maintenance-input.ini'),
        (Get-OutLoudMaintenanceIni $files $hash (Get-ProductionHash $ini)), [Text.Encoding]::ASCII)
}

function Get-OutLoudMaintenanceHelper {
    $helper = Join-Path $ProductionRepository 'build-installer-prerequisites/Release/OutLoudInstallSigning.exe'
    Assert-ProductionPlainPath $helper
    if (-not (Test-Path -LiteralPath $helper -PathType Leaf)) { throw 'Build the existing native installation maintenance helper first.' }
    [void](Get-ProductionPE $helper)
    Assert-OutLoudBinaryVersion $helper (Get-OutLoudProductMetadata)
    return $helper
}

function Assert-OutLoudMaintenanceEnabled {
    $helper = Get-OutLoudMaintenanceHelper
    $report = Join-Path ([IO.Path]::GetTempPath()) ('outloud-maintenance-status-' + [Guid]::NewGuid().ToString('N') + '.ini')
    & $helper --maintenance-status $report | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'Native maintenance availability could not be verified.' }
    $status = Read-OutLoudUpdateIni $report
    if ($status['results']['schema'] -cne '1' -or $status['results']['maintenance'] -cne 'enabled') {
        throw 'Shared native maintenance is blocked pending live lifecycle validation and A03.11 uninstall. No installed files or metadata changed.'
    }
}

function Invoke-OutLoudDevelopmentMaintenance($Plan, [string]$Reports) {
    Assert-OutLoudMaintenanceEnabled
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Use an elevated x64 PowerShell session for machine-wide maintenance.' }
    $helper = Get-OutLoudMaintenanceHelper
    $report = Join-Path $Reports 'maintenance-result.ini'
    & $helper --maintain development $Plan.components $Plan.installation_root $Plan.candidates (Join-Path $Plan.candidates 'maintenance-input.ini') --consented $report | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'Native maintenance failed. Preserve its protected journal; checked recovery is required if indicated. No success is assumed.' }
    $status = Read-OutLoudUpdateIni $report
    if ($status['results']['schema'] -cne '1' -or $status['results']['maintenance'] -cne 'committed' -or
        $status['results']['installation_id'] -cne $Plan.installation_id) { throw 'Native maintenance did not verify a matching committed installation.' }
    $Plan['deployment'] = 'committed'
    $Plan['replacement'] = 'native_verified'
    $Plan['recovery'] = 'native_transaction'
}
