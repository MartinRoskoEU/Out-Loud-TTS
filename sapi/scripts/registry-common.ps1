#Requires -Version 5.1
# Definitions only. Register/unregister are separate, explicitly MANUAL entry points.
. (Join-Path $PSScriptRoot 'deployment-common.ps1')
$PocketClsid = '{F38C3398-3552-43B9-8350-7DD301327D70}'
$PocketClassPath = "SOFTWARE\Classes\CLSID\$PocketClsid"
$PocketVoiceRoot = 'SOFTWARE\Microsoft\Speech\Voices\Tokens'
# Existing shared-engine ownership marker; independent of managed voice tokens.
$PocketOwner = 'OutLoudPocketSAPI-2M'

function Assert-RegistryAdministrator {
    if (-not [Environment]::Is64BitProcess) { throw 'Use elevated x64 PowerShell (Registry64).' }
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    try {
        $principal = New-Object Security.Principal.WindowsPrincipal($identity)
        if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'HKLM registration requires an elevated PowerShell.' }
    } finally { $identity.Dispose() }
}
function Assert-GameClosed {
    if (Get-Process -Name Wow, WowB, WowClassic -ErrorAction SilentlyContinue) {
        throw 'Close WoW manually before changing registration. This script never stops processes.'
    }
}
function Assert-OwnedClass($Machine, [string]$ExpectedDll = '') {
    $key = $Machine.OpenSubKey($PocketClassPath)
    if ($null -eq $key) { return }
    try {
        if ($key.GetValue('OutLoudPocketOwner') -ne $PocketOwner) { throw 'Engine CLSID exists without Pocket ownership; refusing to change it.' }
        $server = $key.OpenSubKey('InprocServer32')
        if ($null -eq $server) { throw 'Existing Pocket COM registration is incomplete.' }
        try {
            if ($server.GetValue('ThreadingModel') -ne 'Both' -or -not $server.GetValue('')) { throw 'Unexpected Pocket COM server registration.' }
            if ($ExpectedDll -and $server.GetValue('') -ne $ExpectedDll) {
                throw 'This CLSID uses another deployment. Select the existing shared deployment; replacing engine registration requires separate manual maintenance.'
            }
        } finally { $server.Dispose() }
    } finally { $key.Dispose() }
}
function Assert-NoUserOverride {
    $user = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser, [Microsoft.Win32.RegistryView]::Registry64)
    try {
        $key = $user.OpenSubKey($PocketClassPath)
        if ($null -ne $key) { $key.Dispose(); throw 'An HKCU override for the shared Pocket CLSID exists; inspect it manually. No HKCU cleanup is performed.' }
    } finally { $user.Dispose() }
}
function Read-RegistryTree($Base, [string]$Path) {
    $key = $Base.OpenSubKey($Path)
    if ($null -eq $key) { return $null }
    try {
        $values = @()
        foreach ($name in $key.GetValueNames()) {
            $values += @{ name = $name; value = $key.GetValue($name, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames); kind = [int]$key.GetValueKind($name) }
        }
        $children = @{}
        foreach ($name in $key.GetSubKeyNames()) { $children[$name] = Read-RegistryTree $key $name }
        return @{ values = $values; children = $children }
    } finally { $key.Dispose() }
}
function Restore-RegistryTree($Base, [string]$Path, $Tree) {
    # Caller has already verified this EXACT Pocket key's ownership. No wildcards.
    $Base.DeleteSubKeyTree($Path, $false)
    if ($null -eq $Tree) { return }
    $key = $Base.CreateSubKey($Path)
    try {
        foreach ($value in $Tree.values) { $key.SetValue($value.name, $value.value, [Microsoft.Win32.RegistryValueKind]$value.kind) }
        foreach ($name in $Tree.children.Keys) { Restore-RegistryTree $key $name $Tree.children[$name] }
    } finally { $key.Dispose() }
}
function Save-RegistrySnapshot([string]$Directory, $Snapshot) {
    $Directory = [IO.Path]::GetFullPath($Directory)
    if (Test-Path -LiteralPath $Directory) { throw 'Use a fresh ReportDirectory to preserve rollback evidence.' }
    New-Item -ItemType Directory -Path $Directory | Out-Null
    $Snapshot | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath (Join-Path $Directory 'registry-before.json') -Encoding UTF8
}
function Set-StringValues($Base, [string]$Path, $Values) {
    $key = $Base.CreateSubKey($Path)
    try {
        foreach ($name in $Values.Keys) { $key.SetValue($name, [string]$Values[$name], [Microsoft.Win32.RegistryValueKind]::String) }
    } finally { $key.Dispose() }
}
