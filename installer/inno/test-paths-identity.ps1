#Requires -Version 5.1
# Focused read-only path/identity checks. Writes only new workspace diagnostics.
[CmdletBinding()]
param([string]$HelperPath=(Join-Path $PSScriptRoot '../../build-installer-prerequisites/Release/OutLoudInstallSigning.exe'))
. (Join-Path $PSScriptRoot 'production-common.ps1')
$script:checks=0
function Check([bool]$ok,[string]$label){if(-not $ok){throw "Path/identity check failed: $label"};$script:checks++}
$clsid=Get-OutLoudEngineClsid
# Deliberate immutable compatibility fixture, not an operational identity source.
Check ($clsid -ceq '{F38C3398-3552-43B9-8350-7DD301327D70}') 'existing COM identity unchanged'
$header=[IO.File]::ReadAllText((Join-Path $ProductionRepository 'sapi/src/identity.hpp'))
$initializer=[regex]::Match($header,'engine_clsid\{([^;]+)\};').Groups[1].Value
$numbers=@([regex]::Matches($initializer,'0x([0-9a-f]+)') | ForEach-Object {[Convert]::ToUInt32($_.Groups[1].Value,16)})
Check ($numbers.Count -eq 11) 'one native GUID initializer'
$guid=[guid]::new([BitConverter]::ToInt32([BitConverter]::GetBytes([uint32]$numbers[0]),0),[int16]$numbers[1],[int16]$numbers[2],[byte[]]$numbers[3..10])
Check ($guid.ToString('B').ToUpperInvariant() -ceq $clsid) 'binary and text representations agree'
Check ((Get-ProductionPolicy).engine_clsid -ceq $clsid) 'installer package identity agrees'
$inventory=Get-Content -LiteralPath (Join-Path $ProductionRepository 'sapi/deployment-inventory.json') -Raw | ConvertFrom-Json
Check ($inventory.engine_clsid -ceq $clsid) 'self-contained development inventory agrees'
foreach($relative in @('sapi/scripts/registry-common.ps1','sapi/scripts/deployment-common.ps1','sapi/scripts/test-managed-voices.ps1','sapi/tests/managed_voices_script_test.ps1','ui/OutLoud.VoiceManager.Tests/VoiceTesterTests.cs')){
    $text=[IO.File]::ReadAllText((Join-Path $ProductionRepository $relative))
    $ids=@([regex]::Matches($text,'\{[0-9A-Fa-f-]{36}\}'))
    Check ($ids.Count -gt 0) "documented self-contained/test identity $relative"
    foreach($id in $ids){Check ($id.Value -ceq $clsid) "fixed exception agrees: $relative"}
}
$version=Get-OutLoudInstallerVersionText (Get-OutLoudProductMetadata)
Check ($version.Contains('#define EngineClsid "'+$clsid+'"')) 'Inno identity generated from header'
$generated=Join-Path $ProductionRepository 'ui/OutLoud.VoiceManager/obj/x64/Release/net10.0-windows/SapiIdentity.g.cs'
if(-not(Test-Path -LiteralPath $generated)){$generated=Join-Path $ProductionRepository 'ui/OutLoud.VoiceManager/obj/Release/net10.0-windows/SapiIdentity.g.cs'}
Check ((Test-Path -LiteralPath $generated) -and [IO.File]::ReadAllText($generated).Contains($clsid)) 'built managed identity generated from header'
$setup=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'OutLoud.iss'))
$directory=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'installation.iss'))
$native=[IO.File]::ReadAllText((Join-Path $ProductionRepository 'installer/native/signing.cpp'))
$registration=[IO.File]::ReadAllText((Join-Path $ProductionRepository 'installer/native/registration.inc'))
Check ($setup -match '(?m)^DefaultDirName=\{autopf\}\\OutLoud\r?$' -and $setup -match '(?m)^DisableDirPage=auto\r?$' -and $setup -match '(?m)^UsePreviousAppDir=yes\r?$') 'default is selectable; previous directory reused'
Check ($setup -match '(?m)^AppId=OutLoud\r?$' -and $directory -match 'OutLoud_is1' -and $directory -match 'InstallLocation' -and $directory -match 'HKLM64') 'stable installer metadata discovery'
Check ($directory -match 'SameText\(Directory, ExistingInstallationDirectory\)' -and $directory -match 'DirEdit.Enabled := False') 'update directory cannot relocate or duplicate instance'
Check ($setup.IndexOf('CheckOutLoudInstallationDirectory(Result)') -lt $setup.IndexOf('PrepareRuntimePrerequisites(Result)')) 'security checked before prerequisite or file mutation'
Check ($native -notmatch 'FOLDERID_ProgramFiles|unexpected_installation_root' -and $registration -notmatch 'FOLDERID_ProgramFiles|unexpected_installation_root') 'native operations have no fixed product root'
foreach($file in Get-ChildItem -LiteralPath (Join-Path $ProductionRepository 'installer/native') -File){
    if($file.Extension -in @('.hpp','.cpp','.inc')) {
        Check ([IO.File]::ReadAllText($file.FullName) -notmatch 'migration_required|OutLoud-Pocket-development-01') "no obsolete runtime logic $($file.Name)"
    }
}
Check ($native -match 'FILE_DELETE_CHILD' -and $native -match 'INHERIT_ONLY_ACE' -and $native -match 'installation_inheritance_writable_by_user') 'ACL inheritance and replacement checks present'
Check ($native -match 'require_installation_location\(root\)' -and $native -match 'unsafe_reparse_path' -and $native -match 'installation_requires_ntfs') 'directory security reused by signing'
Check ($setup -match '^; A03.12' -and $setup -match '#ifndef StageRoot' -and $native -match 'foundation_signing_blocked|foundation_registration_blocked') 'staged shared lifecycle; standalone mutation paths remain blocked'
$scratch=Resolve-ProductionPath (Join-Path $ProductionRepository 'generated/installer/path-independence') ([guid]::NewGuid().ToString('N'))
Assert-ProductionPlainPath $scratch
[void](New-Item -ItemType Directory -Path $scratch)
function Probe([string]$mode,[string]$path,[string]$name){
    $report=Join-Path $scratch ($name+'.ini')
    $start=[Diagnostics.ProcessStartInfo]::new()
    $start.FileName=[IO.Path]::GetFullPath($HelperPath)
    $start.Arguments=(@($mode,$path,$report) | ForEach-Object {'"'+$_+'"'}) -join ' '
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $process=[Diagnostics.Process]::Start($start)
    try{$out=$process.StandardOutput.ReadToEndAsync();$err=$process.StandardError.ReadToEndAsync();$process.WaitForExit()
        $result=[pscustomobject]@{code=$process.ExitCode;error=$err.Result;output=$out.Result;report=$report}
        $result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $scratch ($name+'-probe.json')) -Encoding UTF8
        return $result}
    finally{$process.Dispose()}
}
# The selected destination is never created. Existing secure Program Files is
# only a test parent; no default application path is assumed by the helper.
$secure=Join-Path ([Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFiles)) ('Custom Out Loud '+[char]0x017d+'-'+[guid]::NewGuid().ToString('N'))
$probe=Probe '--installation-inspect' $secure 'secure'
Check ($probe.code -eq 0 -and (Get-Content -LiteralPath $probe.report -Raw) -match 'location=secure_candidate') 'secure custom destination, spaces and Unicode'
Check (-not(Test-Path -LiteralPath $secure)) 'read-only inspection never creates destination'
$probe=Probe '--installation-inspect' $scratch 'insecure'
Check ($probe.code -eq 2 -and $probe.error -match 'state_owner_invalid|state_acl_writable_by_user') 'user-writable destination refused'
$junction=Join-Path $scratch 'junction'
$target=Join-Path $scratch 'target'
[void](New-Item -ItemType Directory -Path $target)
[void](New-Item -ItemType Junction -Path $junction -Target $target)
$probe=Probe '--installation-inspect' (Join-Path $junction 'child') 'reparse'
Check ($probe.code -eq 2 -and $probe.error -match 'unsafe_reparse_path') 'junction ancestor refused'
# Remove only the exact test-owned junction, never traverse its target.
Check ([IO.Path]::GetDirectoryName($junction) -ceq $scratch) 'junction cleanup containment'
[IO.Directory]::Delete($junction,$false)
Check (Test-Path -LiteralPath $target) 'junction target preserved'
$probe=Probe '--registration-inspect' (Join-Path $scratch ('Custom Out Loud '+[char]0x017d)) 'registration'
Check ($probe.code -eq 0 -and (Get-Content -LiteralPath $probe.report -Raw) -match 'registration=absent') 'custom Unicode registration inspection, clean system'
Write-Output "PASS: $script:checks focused path/identity checks; no installed directories, registry, certificates or voices changed. Diagnostics: $scratch"
