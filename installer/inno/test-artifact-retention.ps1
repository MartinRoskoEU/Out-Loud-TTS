#Requires -Version 5.1
# Synthetic workspace artifacts only. No compilation, installation or signing.
[CmdletBinding()]
param()
. (Join-Path $PSScriptRoot 'production-common.ps1')
. (Join-Path $PSScriptRoot 'build-components.ps1')
$realRepository = $ProductionRepository
$scratch = Join-Path $realRepository ('generated/installer/artifact-cleanup-evidence/tests-' + [guid]::NewGuid().ToString('N'))
Assert-ProductionPlainPath $scratch
[void](New-Item -ItemType Directory -Path $scratch)
$script:checks = 0
function Check([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "FAIL: $Message" }
    $script:checks++
}
function Refused([scriptblock]$Action, [string]$Message) {
    $refused = $false
    try { & $Action } catch { $refused = $true }
    Check $refused $Message
}
function New-Scenario([string]$Name) {
    $script:ProductionRepository = Join-Path $scratch $Name
    foreach ($relative in @('generated/installer/stages', 'generated/installer/compiler-output', 'generated/installer/builds')) {
        [void](New-Item -ItemType Directory -Path (Join-Path $script:ProductionRepository $relative) -Force)
    }
}
function New-Publication([string]$Name, [int]$Order) {
    $stage = Join-Path $ProductionRepository "generated/installer/stages/$Name"
    $output = Join-Path $ProductionRepository "generated/installer/compiler-output/$Name"
    [void](New-Item -ItemType Directory -Path $stage, $output)
    Set-Content -LiteralPath (Join-Path $stage 'package-manifest.json') -Value '{"files":[]}'
    Set-Content -LiteralPath (Join-Path $stage 'build-record.json') -Value '{}'
    Set-Content -LiteralPath (Join-Path $stage 'payload.bin') -Value "payload-$Name"
    $installer = Join-Path $output 'OutLoud-Setup-0.1.0.exe'
    Set-Content -LiteralPath $installer -Value "fixture-$Name"
    @{ status = 'public-release'; stage = $stage; installer = $installer; bytes = (Get-Item $installer).Length
        manifest_sha256 = Get-ProductionHash (Join-Path $stage 'package-manifest.json'); sha256 = Get-ProductionHash $installer } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'installer-build.json')
    Write-OutLoudPublicationRecord $output $stage
    $path = Join-Path $output 'publication-record.json'
    $record = Get-Content $path -Raw | ConvertFrom-Json
    $record.utc = ([DateTime]'2026-01-01').AddSeconds($Order).ToString('o')
    $record | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath $path
    return [pscustomobject]@{ stage = $stage; output = $output }
}
try {
    New-Scenario 'bounded'
    $a = New-Publication 'a' 1; $b = New-Publication 'b' 2
    $c = New-Publication 'c' 3; $d = New-Publication 'd' 4
    $lock = Enter-OutLoudArtifactLock
    try { Invoke-OutLoudArtifactRetention $b.output } finally { Exit-OutLoudArtifactLock $lock }
    Check ((Test-Path $b.output) -and (Test-Path $b.stage)) 'current historical publication survives newer timestamps'
    Check ((Test-Path $d.output) -and (Test-Path $d.stage)) 'one previous successful pair survives'
    Check ((-not (Test-Path $a.output)) -and (-not (Test-Path $a.stage)) -and (-not (Test-Path $c.output))) 'obsolete successful pairs removed'
    Invoke-OutLoudArtifactRetention $b.output
    Check (@(Get-ChildItem (Join-Path $ProductionRepository 'generated/installer/compiler-output') -Directory).Count -eq 2) 'idempotent bounded retention'

    New-Scenario 'references'
    $a = New-Publication 'a' 1; $b = New-Publication 'b' 2; $c = New-Publication 'c' 3
    $legacy = Join-Path $ProductionRepository 'generated/installer/compiler-output/interrupted'
    [void](New-Item -ItemType Directory $legacy)
    @{stage=$a.stage;status='compiling'} | ConvertTo-Json | Set-Content (Join-Path $legacy 'build-input.json')
    Invoke-OutLoudArtifactRetention $c.output
    Check ((Test-Path $a.stage) -and (Test-Path $legacy) -and (-not (Test-Path $a.output))) 'interrupted compiler pins stage'
    $d = New-Publication 'd' 4
    @{stage=$b.stage} | ConvertTo-Json | Set-Content (Join-Path $ProductionRepository 'generated/publication-evidence.json')
    Invoke-OutLoudArtifactRetention $d.output
    Check ((Test-Path $b.stage) -and (Test-Path $b.output)) 'external publication record pins complete pair'
    $escaped = $b.stage.Replace('\', '\\\\')
    Check (Test-OutLoudArtifactReference ([regex]::Replace($escaped, '[\\/]+', '\').ToLowerInvariant()) $b.stage) 'multiply escaped diagnostic paths preserve dependencies'

    New-Scenario 'tampered'
    $a = New-Publication 'a' 1; $b = New-Publication 'b' 2; $c = New-Publication 'c' 3
    Set-Content (Join-Path $a.stage 'unexpected-diagnostic.txt') 'preserve'
    Refused { Invoke-OutLoudArtifactRetention $c.output } 'unrecorded diagnostics refuse cleanup before deleting anything'
    Check ((Test-Path $a.output) -and (Test-Path $b.output) -and (Test-Path $c.output)) 'all publications retained after failed preflight'

    New-Scenario 'locked'
    $a = New-Publication 'a' 1; $b = New-Publication 'b' 2; $c = New-Publication 'c' 3
    $handle = [IO.File]::Open((Join-Path $a.output 'OutLoud-Setup-0.1.0.exe'), 'Open', 'Read', 'None')
    try { Refused { Invoke-OutLoudArtifactRetention $c.output } 'active artifact sharing violation refuses cleanup' }
    finally { $handle.Dispose() }
    Check (Test-Path $a.stage) 'locked output retains matching stage'

    New-Scenario 'unknown'
    $a = New-Publication 'a' 1; $b = New-Publication 'b' 2
    $partial = Join-Path $ProductionRepository 'generated/installer/stages/.partial-fixture'
    [void](New-Item -ItemType Directory $partial)
    Set-Content (Join-Path $partial 'diagnostic.txt') 'interrupted'
    $unmanaged = Join-Path $ProductionRepository 'generated/installer/builds/unmarked'
    [void](New-Item -ItemType Directory $unmanaged)
    Invoke-OutLoudArtifactRetention $b.output
    Check ((Test-Path $partial) -and (Test-Path $unmanaged)) 'unmarked/partial artifacts never automatically adopted'
    Refused { Invoke-OutLoudArtifactRetention $unmanaged } 'missing current publication cannot authorize cleanup'
    Refused { Remove-OutLoudOwnedArtifact $scratch (Get-OutLoudArtifactInventory $scratch) 'invalid' } 'out-of-scope root refused'

    New-Scenario 'links'
    $a = New-Publication 'a' 1; $b = New-Publication 'b' 2; $c = New-Publication 'c' 3
    $outside = Join-Path $scratch 'protected-target'
    [void](New-Item -ItemType Directory $outside)
    Set-Content (Join-Path $outside 'sentinel.txt') 'untouched'
    $junction = Join-Path $a.stage 'junction'
    [void](New-Item -ItemType Junction -Path $junction -Target $outside)
    $referenceJunction = Join-Path $ProductionRepository 'generated/reference-junction'
    [void](New-Item -ItemType Junction -Path $referenceJunction -Target $outside)
    try {
        Refused { Get-OutLoudArtifactReferences } 'reference discovery refuses junctions before reading targets'
        Refused { Invoke-OutLoudArtifactRetention $c.output } 'junction traversal refused'
        Check ((Get-Content (Join-Path $outside 'sentinel.txt')) -ceq 'untouched') 'junction target untouched'
    } finally {
        [IO.Directory]::Delete($junction, $false)
        [IO.Directory]::Delete($referenceJunction, $false)
    }

    New-Scenario 'intermediates'
    $work = Join-Path $ProductionRepository 'generated/installer/builds/completed'
    [void](New-Item -ItemType Directory (Join-Path $work 'publish'))
    Set-Content (Join-Path $work 'publish/fixture.dll') 'copied'
    Remove-OutLoudOwnedArtifact $work (Get-OutLoudArtifactInventory $work) 'verified stage consumed intermediate'
    Check (-not (Test-Path $work)) 'completed owned intermediate removable'

    New-Scenario 'backups'
    $a = New-Publication 'a' 1
    $duplicate = Join-Path $a.output ('release-previous-' + ('1' * 32) + '.exe')
    $older = Join-Path $a.output ('release-previous-' + ('2' * 32) + '.exe')
    $rollback = Join-Path $a.output ('release-previous-' + ('3' * 32) + '.exe')
    $unowned = Join-Path $a.output ('release-previous-' + ('4' * 32) + '.exe')
    Copy-Item (Join-Path $a.output 'OutLoud-Setup-0.1.0.exe') $duplicate
    Set-Content $older 'older'; Set-Content $rollback 'rollback'; Set-Content $unowned 'unowned'
    Remove-OutLoudPublicationBackups $a.output @($older,$duplicate,$rollback) (Get-ProductionHash (Join-Path $a.output 'OutLoud-Setup-0.1.0.exe'))
    Check ((-not(Test-Path $older)) -and (-not(Test-Path $duplicate)) -and (Test-Path $rollback)) 'bounded backups retain one distinct rollback and remove duplicates'
    Check (Test-Path $unowned) 'filename alone never establishes backup ownership'
    Refused { Write-OutLoudPublicationRecord $a.output $a.stage @($rollback) } 'unowned output cannot be adopted into cleanup inventory'
    $handle = [IO.File]::Open($unowned, 'Open', 'Read', 'None')
    try { Refused { Remove-OutLoudPublicationBackups $a.output @($unowned,$rollback) (Get-ProductionHash $rollback) } 'in-use backup refuses deletion' }
    finally { $handle.Dispose() }
    @{backup=$rollback} | ConvertTo-Json | Set-Content (Join-Path $ProductionRepository 'generated/backup-evidence.json')
    Remove-OutLoudPublicationBackups $a.output @($rollback) (Get-ProductionHash $rollback)
    Check (Test-Path $rollback) 'externally referenced duplicate backup preserved'

    New-Scenario 'concurrency'
    $lock = Enter-OutLoudArtifactLock
    $oldSource = $env:OUTLOUD_RETENTION_TEST_SOURCE
    $oldRoot = $env:OUTLOUD_RETENTION_TEST_ROOT
    try {
        $env:OUTLOUD_RETENTION_TEST_SOURCE = $realRepository
        $env:OUTLOUD_RETENTION_TEST_ROOT = $ProductionRepository
        $child = @'
. (Join-Path $env:OUTLOUD_RETENTION_TEST_SOURCE 'installer/inno/production-common.ps1')
. (Join-Path $env:OUTLOUD_RETENTION_TEST_SOURCE 'installer/inno/build-components.ps1')
$ProductionRepository = $env:OUTLOUD_RETENTION_TEST_ROOT
try { $lock = Enter-OutLoudArtifactLock; Exit-OutLoudArtifactLock $lock; exit 9 }
catch { if ($_.Exception.Message -match 'Another Out Loud') { exit 0 }; exit 8 }
'@
        $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($child))
        & powershell.exe -NoProfile -EncodedCommand $encoded
        Check ($LASTEXITCODE -eq 0) 'another process cannot clean an actively owned workspace'
    } finally {
        $env:OUTLOUD_RETENTION_TEST_SOURCE = $oldSource
        $env:OUTLOUD_RETENTION_TEST_ROOT = $oldRoot
        Exit-OutLoudArtifactLock $lock
    }
    $result = @{status='pass';checks=$script:checks;scope='synthetic cleanup logic only';utc=[DateTime]::UtcNow.ToString('o')}
    $result | ConvertTo-Json | Set-Content (Join-Path $scratch 'result.json')
    Write-Output "PASS: $script:checks focused artifact-retention checks. Evidence: $scratch"
} finally { $ProductionRepository = $realRepository }
