#Requires -Version 5.1
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$StageDirectory)
. (Join-Path $PSScriptRoot 'production-common.ps1')
# Mutate only a unique copy of the validated stage inside ignored workspace output.
[void](Assert-ProductionStage $StageDirectory)
$testParent = Join-Path $ProductionRepository 'generated/installer/tests'
$scratch = Resolve-ProductionPath $testParent ([Guid]::NewGuid().ToString('N'))
Assert-ProductionPlainPath $scratch
[void](New-Item -ItemType Directory -Path $scratch)
Copy-Item -LiteralPath (Join-Path $StageDirectory 'payload') -Destination $scratch -Recurse
Copy-Item -LiteralPath (Join-Path $StageDirectory 'bootstrap') -Destination $scratch -Recurse
Copy-Item -LiteralPath (Join-Path $StageDirectory 'prerequisites.ini') -Destination $scratch
Copy-Item -LiteralPath (Join-Path $StageDirectory 'runtime-packages.ini') -Destination $scratch
Copy-Item -LiteralPath (Join-Path $StageDirectory 'signing-policy.ini') -Destination $scratch
Copy-Item -LiteralPath (Join-Path $StageDirectory 'registration-policy.ini') -Destination $scratch
Copy-Item -LiteralPath (Join-Path $StageDirectory 'installer-version.iss') -Destination $scratch
foreach ($name in @('maintenance-input.ini', 'maintenance-files.iss', 'maintenance-copy.iss')) {
    Copy-Item -LiteralPath (Join-Path $StageDirectory $name) -Destination $scratch
}
Copy-Item -LiteralPath (Join-Path $StageDirectory 'package-manifest.json'), (Join-Path $StageDirectory 'build-record.json') -Destination $scratch
$manifestPath = Join-Path $scratch 'package-manifest.json'
$originalManifest = [IO.File]::ReadAllBytes($manifestPath)
$maintenancePath = Join-Path $scratch 'maintenance-input.ini'
$originalMaintenance = [IO.File]::ReadAllBytes($maintenancePath)
$script:StageChecks = 0
function Assert-Rejected([string]$Label, [string]$MessagePattern, [scriptblock]$Action) {
    $rejected = $false
    try { & $Action } catch {
        if ($_.Exception.Message -notmatch $MessagePattern) { throw "$Label failed for the wrong reason: $($_.Exception.Message)" }
        $rejected = $true
    }
    if (-not $rejected) { throw "Unsafe stage accepted: $Label" }
    $script:StageChecks++
}
function Update-TestRecord([string]$Relative) {
    $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $entry = $manifest.files | Where-Object { $_.path -eq $Relative }
    $path = Resolve-ProductionPath (Join-Path $scratch 'payload') $Relative
    $entry.sha256 = Get-ProductionHash $path
    $entry.bytes = (Get-Item -LiteralPath $path).Length
    Update-TestMaintenance $manifest
    $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
}
function Update-TestMaintenance($Manifest) {
    # Rebind the private fixture's request so deeper content checks are reached.
    [IO.File]::WriteAllText($maintenancePath,
        (Get-OutLoudMaintenanceIni $Manifest.files $Manifest.detector_sha256 $Manifest.prerequisites_sha256), [Text.Encoding]::ASCII)
    $Manifest.maintenance_ini_sha256 = Get-ProductionHash $maintenancePath
}
try {
    [void](Assert-ProductionStage $scratch); $script:StageChecks++
    foreach ($name in @('test.dll', 'test.exe', 'voice.olv', 'voice.safetensors', 'recording.wav', 'state.tmp', 'certificate.pfx', 'certificate.cer', 'private.key', 'unexpected.json')) {
        $extra = Resolve-ProductionPath (Join-Path $scratch 'payload') $name
        [IO.File]::WriteAllText($extra, 'test-only')
        try { Assert-Rejected $name 'Unexpected package file' { Assert-ProductionStage $scratch } }
        finally { Remove-Item -LiteralPath $extra }
    }
    $extraDirectory = Join-Path $scratch 'payload/development'
    [void](New-Item -ItemType Directory -Path $extraDirectory)
    try { Assert-Rejected 'empty development directory' 'Unexpected package directory' { Assert-ProductionStage $scratch } }
    finally { Remove-Item -LiteralPath $extraDirectory }
    $missing = Join-Path $scratch 'payload/bin/OutLoudVoiceImportHelper.exe'
    $saved = [IO.File]::ReadAllBytes($missing); Remove-Item -LiteralPath $missing
    try { Assert-Rejected 'missing helper' 'Missing package file' { Assert-ProductionStage $scratch } }
    finally { [IO.File]::WriteAllBytes($missing, $saved) }
    $binary = Join-Path $scratch 'payload/bin/OutLoudVoiceManagerNative.dll'
    $saved = [IO.File]::ReadAllBytes($binary); $damaged = [byte[]]$saved.Clone()
    $pe = [BitConverter]::ToUInt32($damaged, 60)
    $damaged[$pe + 4] = 0x4c; $damaged[$pe + 5] = 0x01
    [IO.File]::WriteAllBytes($binary, $damaged); Update-TestRecord 'bin/OutLoudVoiceManagerNative.dll'
    try { Assert-Rejected 'x86 binary with refreshed hash' 'Expected AMD64 PE' { Assert-ProductionStage $scratch } }
    finally { [IO.File]::WriteAllBytes($binary, $saved); [IO.File]::WriteAllBytes($manifestPath, $originalManifest); [IO.File]::WriteAllBytes($maintenancePath, $originalMaintenance) }
    foreach ($relative in @('runtime/pocket-english.olp', 'runtime/pocket-english-tokenizer.bin')) {
        $asset = Resolve-ProductionPath (Join-Path $scratch 'payload') $relative
        $stream = [IO.File]::Open($asset, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        try { $first = $stream.ReadByte(); $stream.Position = 0; $stream.WriteByte($first -bxor 1) } finally { $stream.Dispose() }
        Update-TestRecord $relative
        try { Assert-Rejected $relative 'Incorrect pinned asset' { Assert-ProductionStage $scratch } }
        finally {
            $stream = [IO.File]::OpenWrite($asset)
            try { $stream.WriteByte($first) } finally { $stream.Dispose() }
            [IO.File]::WriteAllBytes($manifestPath, $originalManifest)
            [IO.File]::WriteAllBytes($maintenancePath, $originalMaintenance)
        }
    }
    $license = Join-Path $scratch 'payload/LICENSE'; $saved = [IO.File]::ReadAllBytes($license)
    [IO.File]::WriteAllText($license, 'incorrect license'); Update-TestRecord 'LICENSE'
    try { Assert-Rejected 'altered license with refreshed hash' 'License material differs' { Assert-ProductionStage $scratch } }
    finally { [IO.File]::WriteAllBytes($license, $saved); [IO.File]::WriteAllBytes($manifestPath, $originalManifest); [IO.File]::WriteAllBytes($maintenancePath, $originalMaintenance) }
    foreach ($entry in (Get-ProductionPolicy).files | Where-Object { $_.source -in @('repository', 'consent') }) {
        $legalPath = Resolve-ProductionPath (Join-Path $scratch 'payload') $entry.path
        $savedLegal = [IO.File]::ReadAllBytes($legalPath)
        Remove-Item -LiteralPath $legalPath
        try { Assert-Rejected "missing $($entry.path)" 'Missing package file' { Assert-ProductionStage $scratch } }
        finally { [IO.File]::WriteAllBytes($legalPath, $savedLegal) }
    }
    $terms = Join-Path $scratch 'payload/licenses/OutLoud-Installation-Terms.txt'
    $savedTerms = [IO.File]::ReadAllBytes($terms)
    [IO.File]::WriteAllText($terms, 'truncated legal presentation')
    Update-TestRecord 'licenses/OutLoud-Installation-Terms.txt'
    try { Assert-Rejected 'altered agreement with refreshed hash' 'Installation agreement differs' { Assert-ProductionStage $scratch } }
    finally { [IO.File]::WriteAllBytes($terms, $savedTerms); [IO.File]::WriteAllBytes($manifestPath, $originalManifest); [IO.File]::WriteAllBytes($maintenancePath, $originalMaintenance) }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $manifest.files[0].sha256 = '0' * 64
    Update-TestMaintenance $manifest
    $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    try { Assert-Rejected 'tampered manifest hash' 'Package content differs' { Assert-ProductionStage $scratch } }
    finally { [IO.File]::WriteAllBytes($manifestPath, $originalManifest); [IO.File]::WriteAllBytes($maintenancePath, $originalMaintenance) }
    foreach ($relative in @('prerequisites.ini', 'bootstrap/OutLoudPrerequisites.exe')) {
        $inputPath = Join-Path $scratch $relative
        $savedInput = [IO.File]::ReadAllBytes($inputPath)
        [IO.File]::WriteAllText($inputPath, 'altered bootstrap input')
        try { Assert-Rejected $relative 'Prerequisite metadata or detector changed' { Assert-ProductionStage $scratch } }
        finally { [IO.File]::WriteAllBytes($inputPath, $savedInput) }
    }
    $packagesPath = Join-Path $scratch 'runtime-packages.ini'
    $savedPackages = [IO.File]::ReadAllBytes($packagesPath)
    [IO.File]::WriteAllText($packagesPath, 'unapproved runtime package')
    try { Assert-Rejected 'changed runtime package manifest' 'Runtime acquisition policy or staged metadata changed' { Assert-ProductionStage $scratch } }
    finally { [IO.File]::WriteAllBytes($packagesPath, $savedPackages) }
    $signingPolicyPath = Join-Path $scratch 'signing-policy.ini'
    $savedSigningPolicy = [IO.File]::ReadAllBytes($signingPolicyPath)
    [IO.File]::WriteAllText($signingPolicyPath, 'unapproved signing target')
    try { Assert-Rejected 'changed signing policy' 'Signing policy metadata changed' { Assert-ProductionStage $scratch } }
    finally { [IO.File]::WriteAllBytes($signingPolicyPath, $savedSigningPolicy) }
    $registrationPath = Join-Path $scratch 'registration-policy.ini'
    $savedRegistration = [IO.File]::ReadAllBytes($registrationPath)
    [IO.File]::WriteAllText($registrationPath, 'unapproved registration')
    try { Assert-Rejected 'changed registration inputs' 'Registration policy differs' { Assert-ProductionStage $scratch } }
    finally { [IO.File]::WriteAllBytes($registrationPath, $savedRegistration) }
    [IO.File]::WriteAllText($registrationPath, 'unapproved registration')
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $manifest.registration_ini_sha256 = Get-ProductionHash $registrationPath
    $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    try { Assert-Rejected 'changed registration with refreshed hash' 'Registration policy differs' { Assert-ProductionStage $scratch } }
    finally { [IO.File]::WriteAllBytes($registrationPath, $savedRegistration); [IO.File]::WriteAllBytes($manifestPath, $originalManifest) }
    $installerVersionPath=Join-Path $scratch 'installer-version.iss'
    $savedInstallerVersion=[IO.File]::ReadAllBytes($installerVersionPath)
    [IO.File]::WriteAllText($installerVersionPath,'#define ProductVersion "9.8.7"')
    $manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $manifest.installer_version_sha256=Get-ProductionHash $installerVersionPath
    $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    try { Assert-Rejected 'changed installer version with refreshed hash' 'Production product/installer version metadata differs' { Assert-ProductionStage $scratch } }
    finally { [IO.File]::WriteAllBytes($installerVersionPath,$savedInstallerVersion); [IO.File]::WriteAllBytes($manifestPath,$originalManifest) }
    foreach($case in @('version','file_version','app_id','output_filename','version_source_sha256')) {
        $manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        switch($case){
            version {$manifest.version='9.8.7'}
            file_version {$manifest.file_version='9.8.7.0'}
            app_id {$manifest.installer.app_id='different-product'}
            output_filename {$manifest.installer.output_filename='wrong.exe'}
            version_source_sha256 {$manifest.version_source_sha256='0'*64}
        }
        $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
        try { Assert-Rejected "changed product metadata $case" 'Unexpected public release manifest/build identity|Production product/installer version metadata differs' { Assert-ProductionStage $scratch } }
        finally { [IO.File]::WriteAllBytes($manifestPath,$originalManifest) }
    }
    $recordPath=Join-Path $scratch 'build-record.json';$saved=[IO.File]::ReadAllBytes($recordPath)
    $record=Get-Content -LiteralPath $recordPath -Raw | ConvertFrom-Json
    $manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $record.version='9.8.7';$manifest.version='9.8.7'
    $record | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $recordPath -Encoding UTF8
    $manifest.build_record_sha256=Get-ProductionHash $recordPath
    $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    try { Assert-Rejected 'coordinated wrong release and refreshed hash' 'Unexpected public release manifest/build identity|Production product/installer version metadata differs' { Assert-ProductionStage $scratch } }
    finally { [IO.File]::WriteAllBytes($recordPath,$saved); [IO.File]::WriteAllBytes($manifestPath,$originalManifest) }
    $recordPath = Join-Path $scratch 'build-record.json'; $saved = [IO.File]::ReadAllBytes($recordPath)
    $record = Get-Content -LiteralPath $recordPath -Raw | ConvertFrom-Json
    $record.supported_platform = 'Windows 10 x64'
    $record | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $recordPath -Encoding UTF8
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $manifest.build_record_sha256 = Get-ProductionHash $recordPath
    $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    try { Assert-Rejected 'old Windows policy with refreshed hash' 'Unexpected Windows or runtime prerequisite policy' { Assert-ProductionStage $scratch } }
    finally { [IO.File]::WriteAllBytes($recordPath, $saved); [IO.File]::WriteAllBytes($manifestPath, $originalManifest) }
    $record = Get-Content -LiteralPath $recordPath -Raw | ConvertFrom-Json
    $record.native_matches.'OutLoudVoiceImportHelper.exe' = '0' * 64
    $record | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $recordPath -Encoding UTF8
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $manifest.build_record_sha256 = Get-ProductionHash $recordPath
    $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    try { Assert-Rejected 'mismatched frontend/native pairing' 'Native/frontend build pairing differs' { Assert-ProductionStage $scratch } }
    finally { [IO.File]::WriteAllBytes($recordPath, $saved); [IO.File]::WriteAllBytes($manifestPath, $originalManifest) }
    $icon = Join-Path $ProductionRepository 'ui/OutLoud.VoiceManager/Assets/OutLoud.ico'
    $exe = Get-ProductionPE (Join-Path $scratch 'payload/bin/OutLoud.VoiceManager.exe')
    $key = @($exe.resources.Keys | Where-Object { $_.StartsWith('3/') })[0]
    $exe.resources[$key][0] = $exe.resources[$key][0] -bxor 1
    Assert-Rejected 'incorrect executable icon' 'Executable icon does not match' { Assert-ProductionIcon $exe $icon }
    Assert-Rejected 'escaping output path' 'Path escapes root' { Resolve-ProductionPath $testParent '../escape' }
    Assert-Rejected 'build output outside stage root' 'StageDirectory must be a new child' {
        & (Join-Path $PSScriptRoot 'build-stage.ps1') -StageDirectory (Join-Path $testParent 'not-a-stage')
    }
    Assert-Rejected 'build replacing an existing stage' 'StageDirectory already exists' {
        & (Join-Path $PSScriptRoot 'build-stage.ps1') -StageDirectory $StageDirectory
    }
    $junction = Join-Path $scratch 'payload/bin/linked-runtime'
    [void](New-Item -ItemType Junction -Path $junction -Target (Join-Path $scratch 'payload/runtime'))
    try { Assert-Rejected 'package junction' 'Reparse point in package' { Assert-ProductionStage $scratch } }
    finally { [IO.Directory]::Delete($junction) }
    $iss = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'OutLoud.iss') -Raw
    if ($iss -notmatch '#ifndef StageRoot' -or $iss -notmatch 'StartSharedMaintenance' -or
        $iss -match '(?im)^\[(Registry|Run|UninstallRun|UninstallDelete)\]') { throw 'Installer stage/shared lifecycle contract differs or unchecked mutation sections exist.' }
    $issFiles = @([regex]::Matches((Get-OutLoudMaintenanceFilesText), '(?m)^Source: "\{#StageRoot\}\\payload\\([^"]+)"[^\r\n]*') | ForEach-Object { $_.Groups[1].Value.Replace('\', '/') } | Sort-Object)
    if (($issFiles -join '|') -ne (@((Get-ProductionPolicy).files.path | Sort-Object) -join '|')) { throw 'Inno file list and production policy disagree.' }
    $script:StageChecks++
    [void](Assert-ProductionStage $scratch); $script:StageChecks++
    Write-Output "$script:StageChecks production staging checks PASS; no production storage or registry accessed."
} finally {
    # Verify the exact resolved test-owned target before recursive cleanup.
    $verifiedScratch = Resolve-ProductionPath $testParent ([IO.Path]::GetFileName($scratch))
    if ($verifiedScratch -ne $scratch) { throw 'Scratch containment changed; cleanup refused.' }
    Assert-ProductionPlainPath $scratch
    Remove-Item -LiteralPath $scratch -Recurse -Force
}
