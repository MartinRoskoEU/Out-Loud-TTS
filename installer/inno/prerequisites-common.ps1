#Requires -Version 5.1
# Build-time metadata only. No runtime acquisition, registry or certificate writes.
function Get-NativePrerequisiteRequirements([string]$NativeBuild) {
    $compilerFiles = @(Get-ChildItem -LiteralPath (Join-Path $NativeBuild 'CMakeFiles') -Filter CMakeCXXCompiler.cmake -File -Recurse)
    if ($compilerFiles.Count -ne 1) { throw 'Cannot identify the configured compiler metadata uniquely.' }
    $compiler = Get-Content -LiteralPath $compilerFiles[0].FullName -Raw
    if ($compiler -notmatch 'set\(CMAKE_CXX_COMPILER_VERSION "(19\.(\d+)\.[^"]+)"\)') { throw 'Expected an identified MSVC toolchain.' }
    $compilerVersion = $Matches[1]; $minor = [int]$Matches[2]
    if ($compiler -notmatch 'set\(CMAKE_CXX_COMPILER "(.+/VC)/Tools/MSVC/([^/]+)/bin/Hostx64/x64/cl.exe"\)') { throw 'Cannot resolve the selected x64 MSVC toolset.' }
    $vc = $Matches[1]; $toolsVersion = $Matches[2]
    $redistVersion = (Get-Content -LiteralPath (Join-Path $vc 'Auxiliary/Build/Microsoft.VCRedistVersion.default.txt') -Raw).Trim()
    $redist = [version]$redistVersion
    if ($redist.Major -ne 14 -or $redist.Minor -ne $minor) { throw 'Selected compiler and default redistributable family differ; select/review the matched pack explicitly.' }
    $floor = [version]'14.0.0.0'
    foreach ($name in @('msvcp140.dll', 'msvcp140_atomic_wait.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')) {
        $path = Join-Path $vc "Redist/MSVC/$redistVersion/x64/Microsoft.VC143.CRT/$name"
        Assert-ProductionPlainPath $path
        [void](Get-ProductionPE $path)
        $info = (Get-Item -LiteralPath $path).VersionInfo
        $value = [version]("{0}.{1}.{2}.{3}" -f $info.FileMajorPart,$info.FileMinorPart,$info.FileBuildPart,$info.FilePrivatePart)
        if ($value -gt $floor) { $floor = $value }
    }
    return @{ compiler = $compilerVersion; tools = $toolsVersion; redist_pack = $redistVersion; vc_minimum = $floor.ToString() }
}

function Get-RuntimePackagePolicy {
    $policy = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'runtime-packages.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($policy.schema -ne 1 -or @($policy.packages).Count -ne 2 -or
        (@($policy.packages.id | Sort-Object) -join '|') -ne 'dotnet|vc') { throw 'Invalid runtime package inventory.' }
    foreach ($package in $policy.packages) {
        $uri = [Uri]$package.url
        $hostName = if ($package.id -eq 'vc') { 'download.visualstudio.microsoft.com' } else { 'builds.dotnet.microsoft.com' }
        if ($uri.Scheme -ne 'https' -or $uri.Host -ne $hostName -or -not $uri.IsDefaultPort -or $uri.UserInfo -or $uri.Query -or $uri.Fragment -or
            $package.architecture -ne 'x64' -or $package.bootstrap_machine -ne '014c' -or
            $package.sha256 -notmatch '^[a-f0-9]{64}$' -or $package.bytes -le 0 -or $package.bytes -gt 268435456 -or
            $package.product_name -notmatch '\(x64\)' -or $package.signer_organization -ne 'Microsoft Corporation' -or
            $package.signer_cn -notin @('Microsoft Corporation', '.NET') -or
            $package.filename -notmatch '^[A-Za-z0-9._-]+\.exe$' -or [IO.Path]::GetFileName($uri.AbsolutePath) -ine $package.filename) {
            throw "Unsafe runtime package metadata: $($package.id)"
        }
        if (($package.id -eq 'vc' -and ([version]$package.version).Major -ne 14) -or
            ($package.id -eq 'dotnet' -and ([version]$package.version).Major -ne 10) -or
            ([version]$package.file_version).Major -ne ([version]$package.version).Major) { throw 'Wrong runtime package family.' }
    }
    return $policy
}

function Get-RuntimePackageIni {
    $lines = @('[packages]', 'schema=1')
    foreach ($package in (Get-RuntimePackagePolicy).packages) {
        $lines += "[$($package.id)]"
        foreach ($field in @('name','version','file_version','architecture','bootstrap_machine','product_name','signer_cn','signer_organization','filename','url','bytes','sha256')) {
            $value = [string]$package.$field
            if ($value -match '[\r\n]') { throw 'Multiline package field refused.' }
            $lines += "$field=$value"
        }
    }
    return ($lines -join "`r`n") + "`r`n"
}
