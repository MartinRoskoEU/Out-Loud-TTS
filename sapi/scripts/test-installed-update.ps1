#Requires -Version 5.1
# In-memory metadata and test-owned workspace files only. No production mutation.
[CmdletBinding()]
param([switch]$BuildComponents, [string]$BuildRecord)
. (Join-Path $PSScriptRoot 'installed-update-common.ps1')
if ($BuildComponents -and $BuildRecord) { throw 'Choose a build or reuse its workspace record, not both.' }
$script:checks = 0
function Check([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "FAIL: $Message" }
    $script:checks++
}
function Reject([scriptblock]$Call, [string]$Expected) {
    $message = ''
    try { & $Call | Out-Null } catch { $message = $_.Exception.Message }
    Check ($message -like "*$Expected*") "refuses: $Expected (observed: $message)"
}
$scratch = Join-Path $ProductionRepository ('generated/installed-updates/tests-' + [Guid]::NewGuid().ToString('N'))
Assert-ProductionPlainPath $scratch
[void](New-Item -ItemType Directory -Path $scratch)
$custom = Join-Path $scratch ('Out Loud ' + [char]0x017d + ' hlasy')
[void](New-Item -ItemType Directory -Path $custom)
$record = @{ InstallLocation = $custom + '\'; DisplayName = 'Out Loud version 0.1.0'; DisplayVersion = '0.1.0'; Publisher = 'Nendis' }
Check ((Resolve-OutLoudInstalledRoot $record) -ceq $custom) 'Unicode/space path comes from record, trailing slash normalized'
Reject { Resolve-OutLoudInstalledRoot $null } 'production installer first'
foreach ($field in @('Publisher', 'DisplayVersion', 'DisplayName')) {
    $bad = $record.Clone(); $bad[$field] = 'other'
    Reject { Resolve-OutLoudInstalledRoot $bad } 'identity/version differs'
}
foreach ($path in @('relative', '\\server\share\OutLoud', 'C:\root:stream', '"C:\OutLoud"', 'C:\missing-outloud-fixture')) {
    $bad = $record.Clone(); $bad.InstallLocation = $path
    Reject { Resolve-OutLoudInstalledRoot $bad } 'invalid'
}
$ownership = @{ schema = '1'; product = 'OutLoud'; scope = 'LocalMachine'; stores = 'My,Root'; provider = 'Microsoft Software Key Storage Provider'
    phase = 'verified'; owner_sid = 'S-1-5-32-544'; installation_id = ('a' * 32); key_name = ('OutLoud.Install.' + ('a' * 32))
    thumbprint = ('b' * 40); certificate_sha256 = ('c' * 64); signed_sha256 = ('d' * 64) }
$signature = @{ signature = 'valid'; sha256 = $ownership.signed_sha256; thumbprint = $ownership.thumbprint; certificate_sha256 = $ownership.certificate_sha256 }
$registration = @{ schema = '1'; registration = 'owned_candidate'; hive = 'HKLM'; view = '64'; clsid = Get-OutLoudEngineClsid
    before_encoding = 'utf16le-typed-tree-v1'; before = 'abcd' }
$receipt = @{ schema = '1'; phase = 'before_registry_transaction'; hive = 'HKLM'; view = '64'; clsid = Get-OutLoudEngineClsid
    installation_id = $ownership.installation_id; certificate_sha256 = $ownership.certificate_sha256
    before_encoding = 'utf16le-typed-tree-v1'; before = '00'; desired = 'abcd' }
Assert-OutLoudUpdateOwnership $ownership $receipt $signature $registration
Check $true 'matching exact installation/certificate/engine/COM/receipt identity accepted for observation'
foreach ($field in @('scope', 'product', 'phase', 'key_name', 'owner_sid', 'installation_id', 'certificate_sha256', 'thumbprint', 'signed_sha256')) {
    $bad = $ownership.Clone(); $bad[$field] = 'other'
    Reject { Assert-OutLoudUpdateOwnership $bad $receipt $signature $registration } 'ownership is invalid'
}
foreach ($field in @('signature', 'sha256', 'thumbprint', 'certificate_sha256')) {
    $bad = $signature.Clone(); $bad[$field] = 'other'
    Reject { Assert-OutLoudUpdateOwnership $ownership $receipt $bad $registration } 'signature/hash'
}
foreach ($field in @('registration', 'before', 'clsid', 'hive', 'view')) {
    $bad = $registration.Clone(); $bad[$field] = 'other'
    Reject { Assert-OutLoudUpdateOwnership $ownership $receipt $signature $bad } 'receipt differs'
}
foreach ($field in @('installation_id', 'certificate_sha256', 'desired', 'phase', 'view')) {
    $bad = $receipt.Clone(); $bad[$field] = 'other'
    Reject { Assert-OutLoudUpdateOwnership $ownership $bad $signature $registration } 'receipt differs'
}
$ini = Join-Path $scratch 'signing.ini'
[IO.File]::WriteAllText($ini, "[ownership]`ncertificate_sha256=$($ownership.certificate_sha256)`n", [Text.Encoding]::UTF8)
Check ((Read-OutLoudUpdateIni $ini).ownership.certificate_sha256 -ceq $ownership.certificate_sha256) 'INI hash field is preserved'
[IO.File]::WriteAllText($ini, "[ownership]`nproduct=OutLoud`nproduct=other`n", [Text.Encoding]::UTF8)
Reject { Read-OutLoudUpdateIni $ini } 'Duplicate'
[IO.File]::WriteAllText($ini, "[ownership]`n[ownership]`n", [Text.Encoding]::UTF8)
Reject { Read-OutLoudUpdateIni $ini } 'Duplicate'
[IO.File]::WriteAllText($ini, 'invalid', [Text.Encoding]::UTF8)
Reject { Read-OutLoudUpdateIni $ini } 'Malformed'
$all = @(Get-OutLoudUpdateFiles Both)
foreach ($file in $all) {
    $path = Resolve-ProductionPath $custom $file.path
    [void](New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($path)) -Force)
    [IO.File]::WriteAllText($path, 'fixture', [Text.Encoding]::ASCII)
}
$before = @($all | ForEach-Object { Get-ProductionHash (Resolve-ProductionPath $custom $_.path) }) -join '|'
foreach ($mode in @('Engine', 'VoiceManager', 'Both')) {
    $plan = New-OutLoudUpdatePlan $custom $mode $ownership
    $count = @{ Engine = 1; VoiceManager = 6; Both = 7 }[$mode]
    Check ($plan.files.Count -eq $count) "$mode exact file count"
    Check ($plan.installation_root -ceq $custom -and $plan.clsid -ceq (Get-OutLoudEngineClsid)) "$mode recorded root/shared identity"
    Check ($plan.deployment -ceq 'blocked' -and $plan.recovery -ceq 'native_transaction_pending') "$mode cannot report deployment success"
    Check (@($plan.files | Where-Object { $_.path -match 'runtime/|\.olv$|configuration/|outloud-sapi-voices|OutLoudInstallSigning' }).Count -eq 0) "$mode excludes persistent data, assets and maintenance tools"
    Check (($plan.files.path -contains 'bin/OutLoudPocketSAPI.dll') -eq ($mode -ne 'VoiceManager')) "$mode engine selection"
}
$locked = Resolve-ProductionPath $custom $all[-1].path
$pin = [IO.File]::Open($locked, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
try { Reject { New-OutLoudUpdatePlan $custom Both $ownership } 'unavailable/in use' } finally { $pin.Dispose() }
Check ((New-OutLoudUpdatePlan $custom Both $ownership).files.Count -eq 7) 'failed probe releases all earlier pins'
Assert-OutLoudDevelopmentPublication
Check $true 'shared native lifecycle available; status check does not publish'
Check ((@($all | ForEach-Object { Get-ProductionHash (Resolve-ProductionPath $custom $_.path) }) -join '|') -ceq $before) 'all rejection/planning paths preserve fixture files'
# A pending journal is rejected before even the native read-only inspectors run.
[void](New-Item -ItemType Directory -Path (Join-Path $custom 'configuration'))
[IO.File]::WriteAllText((Join-Path $custom 'configuration/signing.pending.ini'), 'incomplete')
Reject { Invoke-OutLoudUpdateInspection $custom $scratch } 'recovery required'
# Real inspector rejects the user-writable fixture; it never creates installed state.
$helper = Join-Path $ProductionRepository 'build-installer-prerequisites/Release/OutLoudInstallSigning.exe'
$start = New-Object Diagnostics.ProcessStartInfo
$start.FileName = $helper
$start.Arguments = '--installation-inspect "' + $custom + '" "' + (Join-Path $scratch 'unsafe.ini') + '"'
$start.UseShellExecute = $false; $start.CreateNoWindow = $true; $start.RedirectStandardOutput = $true; $start.RedirectStandardError = $true
$process = [Diagnostics.Process]::Start($start)
try {
    $output = $process.StandardOutput.ReadToEnd(); $errorText = $process.StandardError.ReadToEnd(); $process.WaitForExit()
    Check ($process.ExitCode -ne 0 -and -not (Test-Path -LiteralPath (Join-Path $scratch 'unsafe.ini'))) 'existing native security check refuses unsafe fixture'
    [IO.File]::WriteAllText((Join-Path $scratch 'native-refusal.txt'), $output + $errorText)
} finally { $process.Dispose() }
Check ((Get-ProductionPolicy).engine_clsid -ceq (Get-OutLoudEngineClsid)) 'production policy retains authoritative identity'
foreach ($relative in @('sapi/scripts/registry-common.ps1', 'sapi/scripts/deployment-common.ps1')) {
    $ids = @([regex]::Matches([IO.File]::ReadAllText((Join-Path $ProductionRepository $relative)), '\{[0-9A-Fa-f-]{36}\}'))
    Check ($ids.Count -gt 0) "portable script identity exists: $relative"
    foreach ($id in $ids) { Check ($id.Value -ceq (Get-OutLoudEngineClsid)) "portable script identity preserved: $relative" }
}
if ($BuildComponents -or $BuildRecord) {
    $built = if ($BuildComponents) {
        Build-OutLoudComponents -Components Both -WorkDirectory (Join-Path $ProductionRepository ('generated/installer/builds/' + [Guid]::NewGuid().ToString('N')))
    } else { Get-Content -LiteralPath $BuildRecord -Raw -Encoding UTF8 | ConvertFrom-Json }
    foreach ($file in $all) {
        $base = if ($file.source -eq 'native') { Join-Path $built.native 'Release' } else { $built.publish }
        $path = Join-Path $base $file.name
        Check (Test-Path -LiteralPath $path -PathType Leaf) "actual selective build: $($file.name)"
        if ($file.pe) { [void](Get-ProductionPE $path); Assert-OutLoudBinaryVersion $path (Get-OutLoudProductMetadata) }
    }
    $plan = New-OutLoudUpdatePlan $custom Both $ownership
    Copy-OutLoudUpdateCandidates $plan $scratch $built
    foreach ($file in $plan.files) {
        Check ((Get-ProductionHash (Resolve-ProductionPath $plan.candidates $file.path)) -ceq $file.candidate_sha256) "checked candidate copy: $($file.path)"
    }
    Check ($plan.preparation -ceq 'built_and_copied_unsigned_not_deployable' -and $plan.deployment -ceq 'blocked') 'preparation retains blocked publication status'
    Reject { Copy-OutLoudUpdateCandidates $plan $scratch $built } 'already exists'
    Check ((@($all | ForEach-Object { Get-ProductionHash (Resolve-ProductionPath $custom $_.path) }) -join '|') -ceq $before) 'actual candidate preparation preserves simulated installed files'
    $built | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $scratch 'build.json') -Encoding UTF8
}
[ordered]@{ checks = $script:checks; build_performed = $BuildComponents.IsPresent; live_deployment = $false } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $scratch 'results.json') -Encoding UTF8
Write-Output "PASS: $script:checks focused checks. Evidence: $scratch. No live deployment, registration, certificate or voice operation."
