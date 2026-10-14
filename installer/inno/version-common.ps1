#Requires -Version 5.1
# Product metadata only. Dependency, ABI, protocol and asset versions are independent.
function ConvertTo-OutLoudProductVersion([string]$Text) {
    if ($Text -cnotmatch '\A(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\z') {
        throw 'Out Loud requires canonical MAJOR.MINOR.PATCH versioning.'
    }
    foreach ($part in $Text.Split('.')) {
        if ($part.Length -gt 5 -or [int]$part -gt 65534) { throw 'Out Loud version components must fit Windows and .NET assembly metadata (0..65534).' }
    }
    return [version]($Text + '.0')
}
function Compare-OutLoudProductVersion([string]$Left, [string]$Right) {
    return (ConvertTo-OutLoudProductVersion $Left).CompareTo((ConvertTo-OutLoudProductVersion $Right))
}
function Get-OutLoudProductMetadata([string]$Path = (Join-Path $PSScriptRoot '../../ProductVersion.props')) {
    $info=Get-Item -LiteralPath $Path
    if ($info.Length -gt 16384) { throw 'ProductVersion.props is unexpectedly large.' }
    $settings=New-Object Xml.XmlReaderSettings
    $settings.DtdProcessing=[Xml.DtdProcessing]::Prohibit
    $settings.XmlResolver=$null
    $reader=[Xml.XmlReader]::Create($info.FullName,$settings)
    try { $xml=New-Object Xml.XmlDocument; $xml.XmlResolver=$null; $xml.Load($reader) } finally { $reader.Dispose() }
    $fields=@{}
    foreach ($name in @('OutLoudProductVersion','OutLoudProductName','OutLoudProductCopyright')) {
        $nodes=$xml.SelectNodes("/Project/PropertyGroup/$name")
        if ($nodes.Count -ne 1) { throw "ProductVersion.props requires one $name." }
        $fields[$name]=$nodes[0].InnerText
    }
    $numeric=ConvertTo-OutLoudProductVersion $fields.OutLoudProductVersion
    if ($fields.OutLoudProductName -cne 'Out Loud' -or $fields.OutLoudProductCopyright -cne 'Copyright (c) 2026 Nendis') {
        throw 'Unexpected Out Loud product identity/copyright.'
    }
    return [pscustomobject]@{
        version=$fields.OutLoudProductVersion; file_version=$numeric.ToString(); product_name=$fields.OutLoudProductName
        copyright=$fields.OutLoudProductCopyright; app_id='OutLoud'
        installer_filename="OutLoud-Setup-$($fields.OutLoudProductVersion).exe"
        source_sha256=(Get-FileHash -LiteralPath $info.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}
function Get-OutLoudInstallerVersionText($Metadata) {
    . (Join-Path $PSScriptRoot '../../sapi/identity-common.ps1')
    return (@('// Generated from ProductVersion.props; do not edit.',
        "#define ProductVersion `"$($Metadata.version)`"", "#define ProductFileVersion `"$($Metadata.file_version)`"",
        "#define ProductName `"$($Metadata.product_name)`"", "#define ProductCopyright `"$($Metadata.copyright)`"",
        "#define InstallerBaseFilename `"$([IO.Path]::GetFileNameWithoutExtension($Metadata.installer_filename))`"",
        "#define EngineClsid `"$(Get-OutLoudEngineClsid)`"") -join "`n") + "`n"
}
function Assert-OutLoudBinaryVersion([string]$Path,$Metadata) {
    $info=[Diagnostics.FileVersionInfo]::GetVersionInfo([IO.Path]::GetFullPath($Path))
    $file='{0}.{1}.{2}.{3}' -f $info.FileMajorPart,$info.FileMinorPart,$info.FileBuildPart,$info.FilePrivatePart
    $product='{0}.{1}.{2}.{3}' -f $info.ProductMajorPart,$info.ProductMinorPart,$info.ProductBuildPart,$info.ProductPrivatePart
    if ($file -cne $Metadata.file_version -or $product -cne $Metadata.file_version -or
        $info.FileVersion -cne $Metadata.file_version -or $info.ProductVersion -cne $Metadata.version -or
        $info.ProductName -cne $Metadata.product_name -or $info.LegalCopyright -cne $Metadata.copyright) {
        throw "Product version metadata differs from ProductVersion.props: $Path"
    }
}
function Assert-OutLoudInstallerVersionSource {
    $text=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'OutLoud.iss'))
    foreach($line in @('AppId=OutLoud','AppName={#ProductName}','AppVersion={#ProductVersion}',
        'VersionInfoVersion={#ProductFileVersion}','VersionInfoProductVersion={#ProductFileVersion}',
        'VersionInfoProductName={#ProductName}','VersionInfoTextVersion={#ProductFileVersion}',
        'VersionInfoProductTextVersion={#ProductVersion}','VersionInfoCopyright={#ProductCopyright}',
        'OutputBaseFilename={#InstallerBaseFilename}')) {
        if ($text -notmatch ('(?m)^'+[regex]::Escape($line)+'\r?$')) { throw "Installer version configuration differs: $line" }
    }
    if ($text -notmatch '(?m)^#include StageRoot \+ "\\installer-version\.iss"\r?$' -or
        $text -notmatch '#ifndef StageRoot' -or $text -notmatch 'EncodeVer\(7,0,2\)') {
        throw 'Installer centralized version, verified-stage requirement or compiler floor differs.'
    }
}
