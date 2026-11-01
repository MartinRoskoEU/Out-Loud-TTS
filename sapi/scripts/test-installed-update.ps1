#Requires -Version 5.1
# In-memory metadata and test-owned workspace files only. No production mutation.
[CmdletBinding()]
param([switch]$DevelopmentBundleOnly, [switch]$InstalledApplicationOnly)
. (Join-Path $PSScriptRoot 'installed-update-common.ps1')
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
if (-not $InstalledApplicationOnly) { & {
    # Only this new branch: private files and metadata, no native execution,
    # certificates, deployment directory, registry access or broad test suite.
    function Fixture([string]$Root, [hashtable]$Contents) {
        [void][IO.Directory]::CreateDirectory($Root)
        $records = foreach ($relative in $Contents.Keys) {
            $path = Resolve-BundlePath $Root $relative
            [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
            [IO.File]::WriteAllText($path, $Contents[$relative], [Text.Encoding]::UTF8)
            @{ path = $relative; sha256 = (Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() }
        }
        $manifest = @{ schema = 1; files = @($records) }
        $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $Root 'deployment.json') -Encoding UTF8
        return $manifest
    }
    $root = Join-Path $scratch 'existing bundle'
    $stage = Join-Path $scratch 'signed-stage-fixture'
    $before = Fixture $root @{ 'bin/engine.dll' = 'old'; 'RNG_NOTICES.md' = 'obsolete owned file'; 'resource.xbf' = 'old resource' }
    $after = Fixture $stage @{ 'bin/engine.dll' = 'new'; 'resource.xbf' = 'new resource'; 'assets/added.txt' = 'new asset' }
    $acl = (Get-Acl -LiteralPath (Join-Path $root 'bin/engine.dll')).Sddl
    $data = Join-Path $scratch 'user-data.txt'; [IO.File]::WriteAllText($data, 'preserved')
    $held = [IO.File]::Open((Join-Path $root 'resource.xbf'), [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try { Reject { Publish-OutLoudDevelopmentFiles $root $stage $before $after } 'locked or not writable' }
    finally { $held.Dispose() }
    Check ([IO.File]::ReadAllText((Join-Path $root 'bin/engine.dll')) -eq 'old') 'locked preflight performs no earlier writes'
    Check (Test-Path -LiteralPath (Join-Path $root 'RNG_NOTICES.md')) 'locked preflight performs no obsolete deletion'
    $good = $after.files[0].sha256; $after.files[0].sha256 = '0' * 64
    Reject { Publish-OutLoudDevelopmentFiles $root $stage $before $after } 'Staged bytes changed'
    $after.files[0].sha256 = $good
    Check ([IO.File]::ReadAllText((Join-Path $root 'bin/engine.dll')) -eq 'old') 'changed staged bytes do not alter deployment'
    $changed = Publish-OutLoudDevelopmentFiles $root $stage $before $after
    Check ($changed -eq 5) 'replace resources, add asset, retire owned file and publish metadata'
    Check (-not (Test-Path -LiteralPath (Join-Path $root 'RNG_NOTICES.md'))) 'only obsolete inventory file removed'
    foreach ($record in $after.files) { Check ((Get-FileHash -LiteralPath (Resolve-BundlePath $root $record.path)).Hash -eq $record.sha256) "published hash: $($record.path)" }
    Check ((Get-Acl -LiteralPath (Join-Path $root 'bin/engine.dll')).Sddl -ceq $acl) 'existing file ACL and owner preserved'
    Check ((Get-FileHash -LiteralPath (Join-Path $root 'deployment.json')).Hash -eq (Get-FileHash -LiteralPath (Join-Path $stage 'deployment.json')).Hash) 'manifest matches staged complete bundle'
    $repeat = Publish-OutLoudDevelopmentFiles $root $stage $after $after
    Check ($repeat -eq 0) 'verified identical bundle can be deployed repeatedly without rewriting files'
    $stage2 = Join-Path $scratch 'second-stage'
    $next = Fixture $stage2 @{ 'bin/engine.dll' = 'next source build'; 'resource.xbf' = 'second resource'; 'assets/added.txt' = 'new asset' }
    [void](Publish-OutLoudDevelopmentFiles $root $stage2 $after $next)
    Check ([IO.File]::ReadAllText((Join-Path $root 'bin/engine.dll')) -eq 'next source build') 'subsequent source update uses same directory'
    Check ([IO.File]::ReadAllText($data) -eq 'preserved') 'application data outside bundle remains untouched'
    Reject { Resolve-BundlePath $root '../user-data.txt' } 'escapes deployment'
    $inventoryRoot = Join-Path $scratch 'previous-inventory'
    [void][IO.Directory]::CreateDirectory($inventoryRoot)
    $currentInventory = [IO.File]::ReadAllText((Join-Path $ProductionRepository 'sapi/deployment-inventory.json'))
    [IO.File]::WriteAllText((Join-Path $inventoryRoot 'deployment-inventory.json'), $currentInventory, (New-Object Text.UTF8Encoding($false)))
    Check ((Read-BundleInventory $inventoryRoot).files.Count -eq 80) 'current fixed inventory accepted'
    [IO.File]::WriteAllText((Join-Path $inventoryRoot 'deployment-inventory.json'), $currentInventory.Replace('OutLoud.WinUI.exe', 'unverified.exe'), (New-Object Text.UTF8Encoding($false)))
    Reject { Read-BundleInventory $inventoryRoot -AllowPreviousInventory } 'Unrecognized WinUI'
    Write-Output "PASS: $script:checks focused development-bundle update checks; private files only."
    return
} }
if (-not $DevelopmentBundleOnly) { & {
    # Discovery/Plan checks use private receipts and in-memory installer records.
    # No registry, certificate, native helper, signing or installed files are used.
    $root = Join-Path $scratch 'installed application'
    [void][IO.Directory]::CreateDirectory((Join-Path $root 'configuration'))
    $receipt = Join-Path $root 'configuration/inventory.dat'
    [IO.File]::WriteAllText($receipt, "OutLoud.Inventory.3`nfixture only", [Text.Encoding]::ASCII)
    $version = (Get-OutLoudProductMetadata).version
    $record = @{ InstallLocation=$root; Publisher='Nendis'; DisplayVersion=$version; DisplayName=('Out Loud ' + $version) }
    $before = Get-FileHash -LiteralPath $receipt
    function Assert-RegistryAdministrator { throw 'Plan must not request mutation authority.' }
    Check ((Get-OutLoudInstalledApplication $root $record) -ceq $root) 'current WinUI production receipt selected'
    $plan = Invoke-OutLoudInstalledApplicationUpdate -Action Plan -DeploymentRoot $root -Record $record
    Check ($plan -like '*No build or publication*' -and $plan -like '*complete native verification occurs*') 'Plan distinguishes discovery from full preflight'
    Check ((Get-FileHash -LiteralPath $receipt).Hash -ceq $before.Hash) 'Plan preserves receipt bytes'
    Reject { Get-OutLoudInstalledApplication $root $null } 'record is required'
    Reject { Get-OutLoudInstalledApplication (Join-Path $scratch 'different') $record } 'does not match'
    $record.Publisher='Unknown'
    Reject { Get-OutLoudInstalledApplication $root $record } 'does not match'
    $record.Publisher='Nendis';$record.DisplayName='Other application'
    Reject { Get-OutLoudInstalledApplication $root $record } 'does not match'
    foreach ($name in @('Out Loud', ('Out Loud version ' + $version))) {
        $record.DisplayName=$name
        Check ((Get-OutLoudInstalledApplication $root $record) -ceq $root) 'existing exact Inno display-name form accepted'
    }
    $record.DisplayVersion='99.0.0';$record.DisplayName='Out Loud'
    Reject { Get-OutLoudInstalledApplication $root $record } 'same product version'
    $record.DisplayVersion=$version
    foreach ($magic in @("OutLoud.Inventory.2`n", 'incomplete')) {
        [IO.File]::WriteAllText($receipt, $magic, [Text.Encoding]::ASCII)
        Reject { Get-OutLoudInstalledApplication $root $record } 'current WinUI production inventory'
    }
    function Invoke-ReadyFixtureHelper {
        [IO.File]::WriteAllText($args[-1], "[results]`nschema=1`nmaintenance=ready`n", [Text.Encoding]::ASCII)
        $global:LASTEXITCODE=0
    }
    function Invoke-IncompleteFixtureHelper {
        [IO.File]::WriteAllText($args[-1], "[results]`nschema=1`nmaintenance=awaiting_host`n", [Text.Encoding]::ASCII)
        $global:LASTEXITCODE=0
    }
    function Invoke-FailedFixtureHelper { $global:LASTEXITCODE=2 }
    $ready = Join-Path $scratch 'ready.ini'
    Invoke-OutLoudInstalledMaintenance 'Invoke-ReadyFixtureHelper' '--maintenance-check' $root $scratch $ready 'ready'
    Check (Test-Path -LiteralPath $ready) 'native success requires the requested completion report'
    Reject { Invoke-OutLoudInstalledMaintenance 'Invoke-ReadyFixtureHelper' '--maintenance-check' $root $scratch $ready 'ready' } 'already exists'
    Reject { Invoke-OutLoudInstalledMaintenance 'Invoke-IncompleteFixtureHelper' '--maintain' $root $scratch (Join-Path $scratch 'incomplete.ini') 'committed' } "did not verify 'committed'"
    Reject { Invoke-OutLoudInstalledMaintenance 'Invoke-FailedFixtureHelper' '--maintain' $root $scratch (Join-Path $scratch 'failed.ini') 'committed' } 'Native maintenance failed'
    function Invoke-CleanupPendingFixtureHelper {
        [IO.File]::WriteAllText($args[-1], "[results]`nschema=1`nmaintenance=committed`narchive_cleanup=pending`n", [Text.Encoding]::ASCII)
        $global:LASTEXITCODE = 0
    }
    $warnings=@(Invoke-OutLoudInstalledMaintenance 'Invoke-CleanupPendingFixtureHelper' '--maintain' $root $scratch (Join-Path $scratch 'cleanup-pending.ini') 'committed' 3>&1)
    Check (($warnings -join ' ') -match 'archive cleanup remains pending') 'verified deployment reports pending cleanup explicitly'
    Write-Output "PASS: $script:checks focused update checks; private files/in-memory records only."
} }
