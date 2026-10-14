#Requires -Version 5.1
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$StageDirectory)
. (Join-Path $PSScriptRoot 'production-common.ps1')
[void](Assert-ProductionStage $StageDirectory)
$script:VersionChecks=0
function Check([bool]$Ok,[string]$Label){if(-not $Ok){throw "Version check failed: $Label"};$script:VersionChecks++}
function Reject([scriptblock]$Action,[string]$Label){$rejected=$false;try{& $Action | Out-Null}catch{$rejected=$true};Check $rejected $Label}
$product=Get-OutLoudProductMetadata
$testParent=Join-Path $ProductionRepository 'generated/installer/version-tests'
$scratch=Resolve-ProductionPath $testParent ([Guid]::NewGuid().ToString('N'))
Assert-ProductionPlainPath $scratch
[void](New-Item -ItemType Directory -Path $scratch -Force)
$source=[IO.File]::ReadAllText((Join-Path $ProductionRepository 'ProductVersion.props'))
function Fixture([string]$Name,[string]$Version){
    $directory=Resolve-ProductionPath $scratch $Name
    [void](New-Item -ItemType Directory -Path $directory -Force)
    $text=[regex]::Replace($source,'<OutLoudProductVersion>[^<]*</OutLoudProductVersion>',"<OutLoudProductVersion>$Version</OutLoudProductVersion>")
    [IO.File]::WriteAllText((Join-Path $directory 'ProductVersion.props'),$text)
    [IO.File]::WriteAllText((Join-Path $directory 'probe.proj'),'<Project><Import Project="ProductVersion.props" /></Project>')
    return $directory
}
function Run([string]$Program,[string[]]$Arguments){
    $start=New-Object Diagnostics.ProcessStartInfo
    $start.FileName=$Program;$start.Arguments=($Arguments | ForEach-Object {'"'+$_+'"'}) -join ' '
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $process=[Diagnostics.Process]::Start($start)
    try{$out=$process.StandardOutput.ReadToEndAsync();$err=$process.StandardError.ReadToEndAsync();$process.WaitForExit()
        return [pscustomobject]@{code=$process.ExitCode;output=$out.Result;error=$err.Result}
    }finally{$process.Dispose()}
}
try {
    foreach($text in @('0.0.0',$product.version,'1.2.3','0.10.2','65534.65534.65534')) {
        Check ((ConvertTo-OutLoudProductVersion $text).ToString() -ceq ($text+'.0')) "parse $text"
    }
    foreach($text in @('', '1.2','1.2.3.4','01.2.3','1.02.3','1.2.03','1.2.3-beta','1.2.3+build','1.2.3 ',"1.2.3`n",'-1.2.3','65535.0.0','65536.0.0','999999999999.0.0')) {
        Reject {ConvertTo-OutLoudProductVersion $text} "reject $text"
    }
    Check ((Compare-OutLoudProductVersion '0.10.0' '0.9.0') -gt 0) 'numeric minor comparison'
    Check ((Compare-OutLoudProductVersion '1.0.0' '0.65534.65534') -gt 0) 'numeric major comparison'
    Check ((Compare-OutLoudProductVersion '0.1.0' '0.1.1') -lt 0) 'numeric downgrade direction'
    Check ((Compare-OutLoudProductVersion $product.version $product.version) -eq 0) 'numeric equality'
    $cmake=Join-Path $scratch 'read-version.cmake'
    $module=(Join-Path $ProductionRepository 'cmake/OutLoudVersion.cmake').Replace('\','/')
    [IO.File]::WriteAllText($cmake,('include("'+$module+'")'+"`n"+'outloud_read_product_version("${SOURCE}")'+"`n"+'message("product=${OUTLOUD_PRODUCT_VERSION};file=${OUTLOUD_WINDOWS_VERSION}")'+"`n"))
    $index=0
    foreach($case in @(@{version=$product.version;ok=$true},@{version='1.2.3';ok=$true},@{version='65534.65534.65534';ok=$true},
        @{version='01.2.3';ok=$false},@{version='1.2.3-beta';ok=$false},@{version='65535.0.0';ok=$false},@{version='65536.0.0';ok=$false})) {
        $index++;$directory=Fixture "case$index" $case.version
        if($case.ok){Check ((Get-OutLoudProductMetadata (Join-Path $directory 'ProductVersion.props')).version -ceq $case.version) 'XML source parsing'}
        else{Reject {Get-OutLoudProductMetadata (Join-Path $directory 'ProductVersion.props')} 'XML invalid version'}
        $result=Run 'cmake' @("-DSOURCE=$directory/ProductVersion.props",'-P',$cmake)
        Check (($result.code -eq 0) -eq $case.ok) "CMake parser $($case.version)"
        if($case.ok){Check (($result.output+$result.error).Contains("product=$($case.version);file=$($case.version).0")) 'CMake version propagation'}
        $result=Run 'dotnet' @('msbuild',(Join-Path $directory 'probe.proj'),'-t:ValidateOutLoudVersion','-getProperty:Version,AssemblyVersion,FileVersion,InformationalVersion')
        Check (($result.code -eq 0) -eq $case.ok) "MSBuild parser $($case.version)"
        if($case.ok){$props=($result.output | ConvertFrom-Json).Properties
            Check ($props.Version -ceq $case.version -and $props.InformationalVersion -ceq $case.version -and
                $props.AssemblyVersion -ceq ($case.version+'.0') -and $props.FileVersion -ceq ($case.version+'.0')) 'MSBuild version propagation'}
    }
    $duplicate=Fixture 'duplicate' $product.version
    $path=Join-Path $duplicate 'ProductVersion.props'
    [IO.File]::WriteAllText($path,$source.Replace('</Project>',"<PropertyGroup><OutLoudProductVersion>$($product.version)</OutLoudProductVersion></PropertyGroup></Project>"))
    Reject {Get-OutLoudProductMetadata $path} 'duplicate authoritative version refused'
    $result=Run 'cmake' @("-DSOURCE=$path",'-P',$cmake);Check ($result.code -ne 0) 'CMake duplicate version refused'
    foreach($property in @('Version','AssemblyVersion','FileVersion','InformationalVersion','OutLoudProductVersion','VersionSuffix','IncludeSourceRevisionInInformationalVersion','Product','Copyright','OutLoudProductName','OutLoudProductCopyright')) {
        $result=Run 'dotnet' @('msbuild',(Join-Path $ProductionRepository 'ui/OutLoud.VoiceManager/OutLoud.VoiceManager.csproj'),'-t:ValidateOutLoudVersion',"-p:${property}=9.8.7")
        Check ($result.code -ne 0) "MSBuild $property override refused"
    }
    $files=@((Get-ProductionPolicy).files | Where-Object pe | ForEach-Object {'payload/'+$_.path})+@('bootstrap/OutLoudPrerequisites.exe')
    foreach($relative in $files){Assert-OutLoudBinaryVersion (Join-Path $StageDirectory $relative) $product;Check $true "actual binary $relative"}
    $bad=Get-OutLoudProductMetadata;$bad.version='9.8.7';$bad.file_version='9.8.7.0'
    Reject {Assert-OutLoudBinaryVersion (Join-Path $StageDirectory 'payload/bin/OutLoudPocketSAPI.dll') $bad} 'native version mismatch rejected'
    Reject {Assert-OutLoudBinaryVersion (Join-Path $StageDirectory 'payload/bin/OutLoud.VoiceManager.exe') $bad} 'WPF version mismatch rejected'
    $assembly=[Reflection.AssemblyName]::GetAssemblyName((Join-Path $StageDirectory 'payload/bin/OutLoud.VoiceManager.dll'))
    Check ($assembly.Version.ToString() -ceq $product.file_version) 'actual managed assembly version'
    Check ([IO.File]::ReadAllText((Join-Path $StageDirectory 'installer-version.iss')) -ceq (Get-OutLoudInstallerVersionText $product)) 'actual generated installer versions'
    Assert-OutLoudInstallerVersionSource;Check $true 'stable AppId and versioned installer filename wiring'
    $manifest=Get-Content (Join-Path $StageDirectory 'package-manifest.json') -Raw | ConvertFrom-Json
    Check ($manifest.installer.output_filename -ceq $product.installer_filename -and $manifest.version -ceq $product.version) 'package release and output filename'
    $project=[IO.File]::ReadAllText((Join-Path $ProductionRepository 'ui/OutLoud.VoiceManager/OutLoud.VoiceManager.csproj'))
    Check ($project -match '<Import Project="\.\.\\\.\.\\ProductVersion.props"' -and $project -notmatch '<(Version|AssemblyVersion|FileVersion|InformationalVersion)>') 'WPF has no independently editable versions'
    Write-Output "$script:VersionChecks focused product version checks PASS; only private scratch metadata used."
}finally{
    $verified=Resolve-ProductionPath $testParent ([IO.Path]::GetFileName($scratch))
    if($verified -ne $scratch){throw 'Version test containment changed'}
    Assert-ProductionPlainPath $scratch
    Remove-Item -LiteralPath $scratch -Recurse -Force
}
