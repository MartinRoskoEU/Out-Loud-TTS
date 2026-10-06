# Supplementary in-memory binding checks against the SDK-built WPF view.
# No App startup, production native read, real registry access or visible UI.
[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration = 'Release',
      [string]$NativeBuildDirectory = (Join-Path $PSScriptRoot '..\..\build'))
$ErrorActionPreference = 'Stop'
$nativeDirectory = [IO.Path]::GetFullPath($NativeBuildDirectory)
$previousCertificateSetting = $env:DOTNET_GENERATE_ASPNET_CERTIFICATE
try {
    $env:DOTNET_GENERATE_ASPNET_CERTIFICATE = 'false'
    & dotnet run --project (Join-Path $PSScriptRoot 'OutLoud.VoiceManager.Validation.csproj') -c $Configuration -p:Platform=x64 "-p:NativeBuildDirectory=$nativeDirectory"
    if ($LASTEXITCODE -ne 0) { throw 'SDK-built in-memory WPF validation failed.' }
} finally { $env:DOTNET_GENERATE_ASPNET_CERTIFICATE = $previousCertificateSetting }
Write-Output 'Supplementary SDK-built view checks passed. Real-machine production P/Invoke/UI behavior still requires the manual app launch.'
