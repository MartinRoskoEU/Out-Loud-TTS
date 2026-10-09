[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration = 'Release',
      [string]$NativeBuildDirectory)
$ErrorActionPreference = 'Stop'
if (-not $PSBoundParameters.ContainsKey('NativeBuildDirectory')) {
    $NativeBuildDirectory = Join-Path $PSScriptRoot '..\build-production'
}
$repository = Split-Path $PSScriptRoot -Parent
$nativeDirectory = [IO.Path]::GetFullPath($NativeBuildDirectory)
if (-not (Get-Command dotnet -ErrorAction SilentlyContinue) -or -not (dotnet --list-sdks)) {
    throw 'The net10.0-windows projects require an installed .NET 10 SDK; this script does not install an SDK.'
}
# A supplied existing build retains its cached development options.
$configureArguments = @('-S', $repository, '-B', $nativeDirectory, '-A', 'x64')
if (-not (Test-Path -LiteralPath (Join-Path $nativeDirectory 'CMakeCache.txt'))) {
    $configureArguments += @('-DBUILD_TESTING=OFF', '-DOUTLOUD_BUILD_DEVELOPMENT_TOOLS=OFF')
}
& cmake @configureArguments
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
& cmake --build $nativeDirectory --config $Configuration --target OutLoudVoiceManagerNative OutLoudVoiceImportHelper
if ($LASTEXITCODE -ne 0) { throw 'Native bridge build failed.' }
$previousCertificateSetting = $env:DOTNET_GENERATE_ASPNET_CERTIFICATE
try {
    # A desktop build does not need the SDK's first-run ASP.NET certificate.
    $env:DOTNET_GENERATE_ASPNET_CERTIFICATE = 'false'
    & dotnet build (Join-Path $PSScriptRoot 'OutLoud.VoiceManager.sln') -c $Configuration -p:Platform=x64 "-p:NativeBuildDirectory=$nativeDirectory"
    if ($LASTEXITCODE -ne 0) { throw 'WPF build failed.' }
    & dotnet run --project (Join-Path $PSScriptRoot 'OutLoud.VoiceManager.Tests\OutLoud.VoiceManager.Tests.csproj') -c $Configuration -p:Platform=x64 --no-build
    if ($LASTEXITCODE -ne 0) { throw 'Frontend tests failed.' }
} finally { $env:DOTNET_GENERATE_ASPNET_CERTIFICATE = $previousCertificateSetting }
# Building/copying developer output does not launch the production-reading UI.
