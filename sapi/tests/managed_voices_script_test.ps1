#Requires -Version 5.1
# Runs the real helper with fake COM objects. No registry access or playback.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$helper = Join-Path $PSScriptRoot '..\scripts\test-managed-voices.ps1'
$script:checks = 0

# Like ISpeechObjectTokens, this collection is enumerable AND has Count/Item.
# An assignment from an if-expression unwraps it and loses the Item method.
if (-not ('ManagedVoiceScriptTestTokens' -as [type])) {
    Add-Type -TypeDefinition @'
using System.Collections;
public sealed class ManagedVoiceScriptTestTokens : IEnumerable {
    private readonly object[] tokens;
    public ManagedVoiceScriptTestTokens(object[] tokens) { this.tokens = tokens; }
    public int Count { get { return tokens.Length; } }
    public object Item(int index) { return tokens[index]; }
    public IEnumerator GetEnumerator() { return tokens.GetEnumerator(); }
}
'@
}

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "FAIL: $Message" }
    $script:checks++
}

function New-FakeToken([string]$Id, [string]$Description = 'Out Loud - Test', [string]$Scope = 'user') {
    $key = [pscustomobject]@{ Values = @{
        LibraryId = $Id
        OutLoudManagedOwner = 'OutLoud.ManagedVoice.1'
        CLSID = '{F38C3398-3552-43B9-8350-7DD301327D70}'
        '' = $Description
    } }
    Add-Member -InputObject $key -MemberType ScriptMethod -Name GetStringValue -Value {
        param([string]$Name)
        if (-not $this.Values.ContainsKey($Name)) { throw "Missing fake value: $Name" }
        return $this.Values[$Name]
    }
    $hive = if ($Scope -eq 'machine') { 'HKEY_LOCAL_MACHINE' } else { 'HKEY_CURRENT_USER' }
    $token = [pscustomobject]@{
        Id = "$hive\SOFTWARE\Microsoft\Speech\Voices\Tokens\OutLoud_Pocket_$Id"
        Description = $Description
        DataKey = $key
    }
    Add-Member -InputObject $token -MemberType ScriptMethod -Name GetDescription -Value { return $this.Description }
    return $token
}

function Reset-Fakes([object[]]$DefaultTokens = @(), [object[]]$UserTokens = @()) {
    $script:mockState = [pscustomobject]@{
        VoiceCreations = 0
        CategoryCreations = 0
        ForbidCategory = $false
        Voice = $null
        Category = $null
    }
    $script:fakeVoice = [pscustomobject]@{
        Tokens = [ManagedVoiceScriptTestTokens]::new($DefaultTokens)
        GetVoicesCalls = 0
        ForbidGetVoices = $false
        Voice = $null
        Spoken = [System.Collections.Generic.List[object]]::new()
    }
    Add-Member -InputObject $script:fakeVoice -MemberType ScriptMethod -Name GetVoices -Value {
        $this.GetVoicesCalls++
        if ($this.ForbidGetVoices) { throw 'GetVoices must not be called in explicit mode.' }
        return ,$this.Tokens
    }
    Add-Member -InputObject $script:fakeVoice -MemberType ScriptMethod -Name Speak -Value {
        param([string]$Text, [int]$Flags)
        $this.Spoken.Add([pscustomobject]@{ Token = $this.Voice; Text = $Text; Flags = $Flags })
        return 0
    }
    $script:fakeCategory = [pscustomobject]@{
        Tokens = [ManagedVoiceScriptTestTokens]::new($UserTokens)
        SetIdCalls = 0
        CategoryId = $null
        Create = $null
        EnumerateCalls = 0
        ThrowSetId = $false
        ThrowEnumerate = $false
    }
    Add-Member -InputObject $script:fakeCategory -MemberType ScriptMethod -Name SetId -Value {
        param([string]$Id, [bool]$Create)
        $this.SetIdCalls++
        $this.CategoryId = $Id
        $this.Create = $Create
        if ($this.ThrowSetId) { throw 'Fake SetId failure.' }
    }
    Add-Member -InputObject $script:fakeCategory -MemberType ScriptMethod -Name EnumerateTokens -Value {
        param([string]$Required, [string]$Optional)
        $this.EnumerateCalls++
        if ($Required -ne '' -or $Optional -ne '') { throw 'Unexpected enumeration filters.' }
        if ($this.ThrowEnumerate) { throw 'Fake EnumerateTokens failure.' }
        return ,$this.Tokens
    }
    $script:mockState.Voice = $script:fakeVoice
    $script:mockState.Category = $script:fakeCategory
}

# Shadow COM construction for the invoked helper; unknown requests fail closed.
function New-Object {
    param([string]$ComObject)
    switch ($ComObject) {
        'SAPI.SpVoice' { $mockState.VoiceCreations++; return $mockState.Voice }
        'SAPI.SpObjectTokenCategory' {
            $mockState.CategoryCreations++
            if ($mockState.ForbidCategory) { throw 'Explicit category must not be created in default mode.' }
            return $mockState.Category
        }
        default { throw "Unexpected object request: $ComObject" }
    }
}

function Assert-Failure([hashtable]$Parameters, [string]$Expected) {
    $message = $null
    try { $null = & $helper @Parameters } catch { $message = $_.Exception.Message }
    Assert-True ($null -ne $message -and $message.Contains($Expected)) "Expected failure '$Expected', got '$message'."
    Assert-True ($script:fakeVoice.Spoken.Count -eq 0) 'Failure must suppress playback.'
}

$a = 'a4fb27ae6305b2c563711fdb512bcbb2'
$b = '0123456789abcdef0123456789abcdef'
$phrase = 'Hello traveler. This is the managed Out Loud voice test.'
$explicitHeading = 'Explicit HKCU SAPI category enumeration:'
$defaultHeading = 'Default SAPI.SpVoice.GetVoices() enumeration:'

# The user's exact switch combination, with a single enumerable token.
$tokenA = New-FakeToken $a
Reset-Fakes -UserTokens @($tokenA)
$script:fakeVoice.ForbidGetVoices = $true
$output = @(& $helper -LibraryId $a -ExplicitUserCategory -Speak)
Assert-True ($output -contains $explicitHeading -and $output -notcontains $defaultHeading) 'Explicit heading only.'
Assert-True ($script:fakeVoice.GetVoicesCalls -eq 0) 'Explicit mode never calls GetVoices.'
Assert-True ($script:mockState.CategoryCreations -eq 1 -and $script:fakeCategory.SetIdCalls -eq 1) 'Explicit category created and set once.'
Assert-True ($script:fakeCategory.CategoryId -ceq 'HKEY_CURRENT_USER\SOFTWARE\Microsoft\Speech\Voices') 'Exact HKCU category.'
Assert-True ($script:fakeCategory.Create -ceq $false) 'SetId uses create=false.'
Assert-True ($script:fakeCategory.EnumerateCalls -eq 1) 'Explicit mode enumerates once.'
Assert-True ($script:fakeVoice.Spoken.Count -eq 1) 'One requested utterance.'
$spoken = $script:fakeVoice.Spoken[0]
Assert-True ([object]::ReferenceEquals($spoken.Token, $tokenA)) 'Speak uses the exact enumerated token.'
Assert-True ($spoken.Text -ceq $phrase -and $spoken.Flags -eq 0) 'Exact phrase and synchronous flags.'

# Multiple tokens with equal descriptions: identity selects each requested one.
$tokenB = New-FakeToken $b
$decoy = New-FakeToken $a
$decoy.Id = "HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Speech\Voices\Tokens\OutLoud_Pocket_$a"
$prefix = New-FakeToken $a
$prefix.Id += '_extra'
Reset-Fakes -UserTokens @($decoy, $prefix, $tokenB, $tokenA)
$script:fakeVoice.ForbidGetVoices = $true
$null = & $helper -LibraryId $a,$b -ExplicitUserCategory -Speak
Assert-True ($script:fakeVoice.Spoken.Count -eq 2) 'Two requested identities spoken.'
Assert-True ([object]::ReferenceEquals($script:fakeVoice.Spoken[0].Token, $tokenA)) 'First exact identity, not an equal-name decoy.'
Assert-True ([object]::ReferenceEquals($script:fakeVoice.Spoken[1].Token, $tokenB)) 'Second exact identity.'

Reset-Fakes -UserTokens @($tokenA)
$script:fakeVoice.ForbidGetVoices = $true
$output = @(& $helper -LibraryId $a -ExplicitUserCategory)
Assert-True ($output -contains "FOUND: Out Loud - Test [$($tokenA.Id)]") 'Explicit lookup without Speak succeeds.'
Assert-True ($script:fakeVoice.Spoken.Count -eq 0) 'Omitted Speak suppresses playback.'

# Omitted switch and explicitly false switch both use only default enumeration.
$machineToken = New-FakeToken $a -Scope machine
foreach ($parameters in @(@{ LibraryId = $a; Speak = $true }, @{ LibraryId = $a; Speak = $true; ExplicitUserCategory = $false })) {
    Reset-Fakes -DefaultTokens @($machineToken, $tokenA)
    $script:mockState.ForbidCategory = $true
    $output = @(& $helper @parameters)
    Assert-True ($output -contains $defaultHeading -and $output -notcontains $explicitHeading) 'Default heading only.'
    Assert-True ($script:fakeVoice.GetVoicesCalls -eq 1 -and $script:mockState.CategoryCreations -eq 0) 'Default branch only.'
    Assert-True ([object]::ReferenceEquals($script:fakeVoice.Spoken[0].Token, $machineToken)) 'Default branch exact HKLM-token playback, ignoring legacy HKCU peer.'
}

# Missing token must not fall back, even if the other category contains it.
Reset-Fakes -DefaultTokens @($tokenA)
$script:fakeVoice.ForbidGetVoices = $true
Assert-Failure @{ LibraryId = $a; ExplicitUserCategory = $true; Speak = $true } 'Managed voice absent from selected enumeration'
Assert-True ($script:fakeVoice.GetVoicesCalls -eq 0) 'Missing explicit token never falls back.'
Reset-Fakes -UserTokens @($tokenA)
$script:mockState.ForbidCategory = $true
Assert-Failure @{ LibraryId = $a; Speak = $true } 'Managed voice absent from selected enumeration'
Assert-True ($script:mockState.CategoryCreations -eq 0) 'Missing default token never falls back.'

Reset-Fakes -UserTokens @($tokenA, $tokenA)
Assert-Failure @{ LibraryId = $a; ExplicitUserCategory = $true; Speak = $true } 'Duplicate exact token'
foreach ($field in @('LibraryId', 'OutLoudManagedOwner', 'CLSID')) {
    $bad = New-FakeToken $a
    $bad.DataKey.Values[$field] = 'wrong'
    Reset-Fakes -UserTokens @($bad)
    Assert-Failure @{ LibraryId = $a; ExplicitUserCategory = $true; Speak = $true } 'Managed token ownership differs'
}
$bad = New-FakeToken $a
$bad.Description = 'Different description'
Reset-Fakes -UserTokens @($bad)
Assert-Failure @{ LibraryId = $a; ExplicitUserCategory = $true; Speak = $true } 'Managed token description differs'

# Category errors cannot lead to default enumeration either.
foreach ($failure in @('ThrowSetId', 'ThrowEnumerate')) {
    Reset-Fakes -UserTokens @($tokenA)
    $script:fakeVoice.ForbidGetVoices = $true
    $script:fakeCategory.$failure = $true
    $expected = if ($failure -eq 'ThrowSetId') { 'Fake SetId failure.' } else { 'Fake EnumerateTokens failure.' }
    Assert-Failure @{ LibraryId = $a; ExplicitUserCategory = $true; Speak = $true } $expected
    Assert-True ($script:fakeVoice.GetVoicesCalls -eq 0) 'Category errors do not fall back.'
}

Reset-Fakes
$script:fakeVoice.ForbidGetVoices = $true
$output = @(& $helper -ExplicitUserCategory)
Assert-True ($output.Count -eq 1 -and $output[0] -ceq $explicitHeading) 'Empty explicit enumeration is valid without requested IDs.'
Reset-Fakes
Assert-Failure @{ Speak = $true; ExplicitUserCategory = $true } '-Speak requires exact managed LibraryId values.'
Assert-True ($script:mockState.VoiceCreations -eq 0 -and $script:mockState.CategoryCreations -eq 0) 'Speak without IDs fails before COM creation.'
Reset-Fakes
Assert-Failure @{ LibraryId = $a; ExplicitUserCategory = $true; Scope = 'machine'; Speak = $true } '-ExplicitUserCategory requires user scope'
Assert-True ($script:mockState.VoiceCreations -eq 0 -and $script:mockState.CategoryCreations -eq 0) 'Conflicting scopes fail before COM creation.'

Write-Output "PASS: $script:checks managed-voice script assertions; fake COM only, no registry access or playback."
