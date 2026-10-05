#Requires -Version 5.1
# MANUAL validation only. No registry writes, registration, deployment copies,
# signing or process changes. -Speak explicitly enables audio playback.
[CmdletBinding()]
param(
    [ValidatePattern('^[0-9a-f]{32}$')][string[]]$LibraryId = @(),
    [switch]$Speak,
    [switch]$ExplicitUserCategory,
    [ValidateSet('machine','user')][string]$Scope = 'machine'
)
$ErrorActionPreference = 'Stop'
if (-not [Environment]::Is64BitProcess) { throw 'Use x64 PowerShell.' }
if ($Speak -and $LibraryId.Count -eq 0) { throw '-Speak requires exact managed LibraryId values.' }
if ($ExplicitUserCategory.IsPresent -and $PSBoundParameters.ContainsKey('Scope') -and $Scope -ne 'user') {
    throw '-ExplicitUserCategory requires user scope; it cannot select a machine token.'
}
if ($ExplicitUserCategory.IsPresent) { $Scope = 'user' }
$voice = New-Object -ComObject SAPI.SpVoice
$category = $null
try {
    # Assign directly inside each branch: an if-expression's output pipeline
    # enumerates the COM collection, losing its Count/Item interface.
    if ($ExplicitUserCategory.IsPresent) {
        $category = New-Object -ComObject SAPI.SpObjectTokenCategory
        $category.SetId('HKEY_CURRENT_USER\SOFTWARE\Microsoft\Speech\Voices', $false)
        $tokens = $category.EnumerateTokens('', '')
        Write-Output 'Explicit HKCU SAPI category enumeration:'
    } else {
        $tokens = $voice.GetVoices()
        Write-Output 'Default SAPI.SpVoice.GetVoices() enumeration:'
    }
    for ($i = 0; $i -lt $tokens.Count; $i++) {
        $token = $tokens.Item($i)
        Write-Output ([pscustomobject]@{ Id = $token.Id; Description = $token.GetDescription() })
    }
    foreach ($id in $LibraryId) {
        $hive = if ($Scope -eq 'machine') { 'HKEY_LOCAL_MACHINE' } else { 'HKEY_CURRENT_USER' }
        $expectedId = "$hive\SOFTWARE\Microsoft\Speech\Voices\Tokens\OutLoud_Pocket_$id"
        $selected = $null
        for ($i = 0; $i -lt $tokens.Count; $i++) {
            $candidate = $tokens.Item($i)
            # Exact identity disambiguates duplicate library names.
            if ($candidate.Id -ieq $expectedId) {
                if ($null -ne $selected) { throw "Duplicate exact token: $id" }
                $selected = $candidate
            }
        }
        if ($null -eq $selected) {
            throw "Managed voice absent from selected enumeration: $id. No fallback is performed."
        }
        # Read the selected token's data key only; never create or modify it.
        $dataKey = $selected.DataKey
        if ($dataKey.GetStringValue('LibraryId') -cne $id -or
            $dataKey.GetStringValue('OutLoudManagedOwner') -cne 'OutLoud.ManagedVoice.1' -or
            $dataKey.GetStringValue('CLSID') -cne '{F38C3398-3552-43B9-8350-7DD301327D70}') {
            throw "Managed token ownership differs: $id"
        }
        $description = $dataKey.GetStringValue('')
        if ($selected.GetDescription() -cne $description) { throw "Managed token description differs: $id" }
        Write-Output "FOUND: $description [$expectedId]"
        if ($Speak) {
            $voice.Voice = $selected
            $null = $voice.Speak('Hello traveler. This is the managed Out Loud voice test.', 0)
        }
    }
} finally {
    if ($null -ne $category -and [Runtime.InteropServices.Marshal]::IsComObject($category)) {
        $null = [Runtime.InteropServices.Marshal]::FinalReleaseComObject($category)
    }
    if ([Runtime.InteropServices.Marshal]::IsComObject($voice)) {
        $null = [Runtime.InteropServices.Marshal]::FinalReleaseComObject($voice)
    }
}
