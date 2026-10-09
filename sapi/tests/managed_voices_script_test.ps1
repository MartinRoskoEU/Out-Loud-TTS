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

function New-FakeToken([string]$Id, [string]$Description = 'Test voice') {
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
    $token = [pscustomobject]@{
        Id = "HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Speech\Voices\Tokens\OutLoud_Pocket_$Id"
        Description = $Description
        DataKey = $key
    }
    Add-Member -InputObject $token -MemberType ScriptMethod -Name GetDescription -Value { return $this.Description }
    return $token
}

function Reset-Fakes([object[]]$Tokens = @()) {
    $script:mockState = [pscustomobject]@{ VoiceCreations = 0; Voice = $null }
    $script:fakeVoice = [pscustomobject]@{
        Tokens = [ManagedVoiceScriptTestTokens]::new($Tokens)
        GetVoicesCalls = 0
        Voice = $null
        Spoken = [System.Collections.Generic.List[object]]::new()
    }
    Add-Member -InputObject $script:fakeVoice -MemberType ScriptMethod -Name GetVoices -Value {
        $this.GetVoicesCalls++
        return ,$this.Tokens
    }
    Add-Member -InputObject $script:fakeVoice -MemberType ScriptMethod -Name Speak -Value {
        param([string]$Text, [int]$Flags)
        $this.Spoken.Add([pscustomobject]@{ Token = $this.Voice; Text = $Text; Flags = $Flags })
        return 0
    }
    $script:mockState.Voice = $script:fakeVoice
}

# Shadow COM construction for the invoked helper; unknown requests fail closed.
function New-Object {
    param([string]$ComObject)
    if ($ComObject -ne 'SAPI.SpVoice') { throw "Unexpected object request: $ComObject" }
    $mockState.VoiceCreations++
    return $mockState.Voice
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
$heading = 'Default SAPI.SpVoice.GetVoices() enumeration:'
$tokenA = New-FakeToken $a
Reset-Fakes -Tokens @($tokenA)
$output = @(& $helper -LibraryId $a -Speak)
Assert-True ($output -contains $heading) 'Default enumeration heading.'
Assert-True ($script:fakeVoice.GetVoicesCalls -eq 1 -and $script:mockState.VoiceCreations -eq 1) 'One default enumeration and voice object.'
Assert-True ($script:fakeVoice.Spoken.Count -eq 1) 'One requested utterance.'
$spoken = $script:fakeVoice.Spoken[0]
Assert-True ([object]::ReferenceEquals($spoken.Token, $tokenA)) 'Speak uses the exact enumerated token.'
Assert-True ($spoken.Text -ceq $phrase -and $spoken.Flags -eq 0) 'Exact phrase and synchronous flags.'

# Equal descriptions and a prefix decoy cannot substitute for exact token IDs.
$tokenB = New-FakeToken $b
$prefix = New-FakeToken $a
$prefix.Id += '_extra'
Reset-Fakes -Tokens @($prefix, $tokenB, $tokenA)
$null = & $helper -LibraryId $a,$b -Speak
Assert-True ($script:fakeVoice.Spoken.Count -eq 2) 'Two requested identities spoken.'
Assert-True ([object]::ReferenceEquals($script:fakeVoice.Spoken[0].Token, $tokenA)) 'First exact identity.'
Assert-True ([object]::ReferenceEquals($script:fakeVoice.Spoken[1].Token, $tokenB)) 'Second exact identity.'

Reset-Fakes -Tokens @($tokenA)
$output = @(& $helper -LibraryId $a)
Assert-True ($output -contains "FOUND: Test voice [$($tokenA.Id)]") 'Lookup without Speak succeeds.'
Assert-True ($script:fakeVoice.Spoken.Count -eq 0) 'Omitted Speak suppresses playback.'
Reset-Fakes
Assert-Failure @{ LibraryId = $a; Speak = $true } 'Managed voice absent from selected enumeration'
Reset-Fakes -Tokens @($tokenA, $tokenA)
Assert-Failure @{ LibraryId = $a; Speak = $true } 'Duplicate exact token'
foreach ($field in @('LibraryId', 'OutLoudManagedOwner', 'CLSID')) {
    $bad = New-FakeToken $a
    $bad.DataKey.Values[$field] = 'wrong'
    Reset-Fakes -Tokens @($bad)
    Assert-Failure @{ LibraryId = $a; Speak = $true } 'Managed token ownership differs'
}
$bad = New-FakeToken $a
$bad.Description = 'Different description'
Reset-Fakes -Tokens @($bad)
Assert-Failure @{ LibraryId = $a; Speak = $true } 'Managed token description differs'
Reset-Fakes
$output = @(& $helper)
Assert-True ($output.Count -eq 1 -and $output[0] -ceq $heading) 'Empty enumeration is valid without requested IDs.'
Reset-Fakes
Assert-Failure @{ Speak = $true } '-Speak requires exact managed LibraryId values.'
Assert-True ($script:mockState.VoiceCreations -eq 0) 'Speak without IDs fails before COM creation.'

Write-Output "PASS: $script:checks managed-voice script assertions; fake COM only, no registry access or playback."
