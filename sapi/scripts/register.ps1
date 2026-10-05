#Requires -Version 5.1
# MANUAL HKLM x64 registration ONLY. BUILD -> STAGE -> SIGN -> VERIFY -> REGISTER.
# No signing/certificate setup, regsvr32, HKCU changes, voice enumeration or WoW launch.
[CmdletBinding()]
param(
    [string]$DeploymentRoot = (Split-Path $PSScriptRoot -Parent),
    [string]$TokenName = 'OutLoud_Pocket_GnomeMale_Development',
    [string]$DisplayName = 'Out Loud - Gnome Male (Development)',
    [string]$VoicePath = 'voices/gnome_male.olv',
    [uint64]$Seed = 73,
    [ValidateRange(1, 128)][uint32]$MaximumSegmentTokens = 128,
    [ValidateSet('Auto', 'Portable', 'AVX2')][string]$KernelPath = 'Auto',
    [ValidateSet('Male', 'Female', 'Neutral')][string]$Gender = 'Male',
    [ValidatePattern('^[0-9a-fA-F]+$')][string]$Language = '409',
    [string]$ReportDirectory
)
. (Join-Path $PSScriptRoot 'registry-common.ps1')
Assert-RegistryAdministrator
Assert-GameClosed
$DeploymentRoot = (Resolve-Path -LiteralPath $DeploymentRoot).Path
$null = Assert-Bundle $DeploymentRoot $true
$dll = Resolve-BundlePath $DeploymentRoot 'bin/OutLoudPocketSAPI.dll'
$voice = if ([IO.Path]::IsPathRooted($VoicePath)) { [IO.Path]::GetFullPath($VoicePath) } else { Resolve-BundlePath $DeploymentRoot $VoicePath }
if (-not (Test-Path -LiteralPath $voice -PathType Leaf) -or [IO.Path]::GetExtension($voice) -ne '.olv') { throw 'Selected .olv voice file is missing.' }
if (-not $DisplayName.Trim()) { throw 'DisplayName is required.' }
$tokenPath = Get-PocketTokenPath $TokenName
if (-not $ReportDirectory) { $ReportDirectory = Join-Path $DeploymentRoot ('reports/register-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')) }
Assert-NoUserOverride
$machine = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, [Microsoft.Win32.RegistryView]::Registry64)
try {
    Assert-OwnedClass $machine $dll
    Assert-OwnedToken $machine $tokenPath
    $before = @{ engine_class = (Read-RegistryTree $machine $PocketClassPath); voice_token = (Read-RegistryTree $machine $tokenPath) }
    if ($null -ne $before.voice_token) { throw 'This token already exists. Unregister it explicitly before replacing it, or choose a new Pocket TokenName.' }
    Save-RegistrySnapshot $ReportDirectory $before
    Assert-GameClosed
    try {
        if ($null -eq $before.engine_class) {
            Set-StringValues $machine $PocketClassPath @{ '' = 'Out Loud Pocket SAPI Engine'; OutLoudPocketOwner = $PocketOwner }
            Set-StringValues $machine "$PocketClassPath\InprocServer32" @{ '' = $dll; ThreadingModel = 'Both' }
        }
        Set-StringValues $machine $tokenPath @{
            '' = $DisplayName; '409' = $DisplayName; CLSID = $PocketClsid; OutLoudPocketOwner = $PocketOwner
            AssetRoot = $DeploymentRoot; ModelPath = 'runtime/pocket-english.olp'; TokenizerPath = 'runtime/pocket-english-tokenizer.bin'
            VoicePath = $VoicePath; Seed = $Seed.ToString([Globalization.CultureInfo]::InvariantCulture)
            MaximumSegmentTokens = $MaximumSegmentTokens.ToString([Globalization.CultureInfo]::InvariantCulture); KernelPath = $KernelPath
        }
        Set-StringValues $machine "$tokenPath\Attributes" @{
            Name = $DisplayName; Gender = $Gender; Age = 'Adult'; Language = $Language; Vendor = 'Out Loud'; Version = '0.1'
        }
        Assert-OwnedClass $machine $dll
        Assert-OwnedToken $machine $tokenPath
        $installed = $machine.OpenSubKey($tokenPath)
        try {
            foreach ($name in @('AssetRoot', 'VoicePath', 'Seed', 'MaximumSegmentTokens', 'KernelPath')) {
                $expected = switch ($name) {
                    AssetRoot { $DeploymentRoot }; VoicePath { $VoicePath }; Seed { $Seed.ToString([Globalization.CultureInfo]::InvariantCulture) }
                    MaximumSegmentTokens { $MaximumSegmentTokens.ToString([Globalization.CultureInfo]::InvariantCulture) }; KernelPath { $KernelPath }
                }
                if ($installed.GetValue($name) -ne $expected) { throw "Registry readback differs: $name" }
            }
        } finally { $installed.Dispose() }
    } catch {
        $original = $_
        $rollbackErrors = @()
        try { Restore-RegistryTree $machine $tokenPath $before.voice_token } catch { $rollbackErrors += $_.Exception.Message }
        if ($null -eq $before.engine_class) {
            try { Restore-RegistryTree $machine $PocketClassPath $null } catch { $rollbackErrors += $_.Exception.Message }
        }
        if ($rollbackErrors.Count) { throw "Registration failed: $original. Rollback failed: $($rollbackErrors -join '; '). See $ReportDirectory." }
        throw $original
    }
    Write-Output "REGISTERED HKLM Registry64: $PocketClassPath; $tokenPath. DLL: $dll. Snapshot: $ReportDirectory"
} finally { $machine.Dispose() }
