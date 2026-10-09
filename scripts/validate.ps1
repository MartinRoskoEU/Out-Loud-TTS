# Correctness execution only: no builds, downloads, elevation or benchmarks.
[CmdletBinding()]
param(
    [ValidateSet('Fast','Correctness')][string]$Mode = 'Fast',
    [ValidateSet('Release','Debug')][string]$Configuration = 'Release',
    [string]$BuildDirectory,
    [switch]$List
)
$ErrorActionPreference = 'Stop'
try {
    if (-not $PSBoundParameters.ContainsKey('BuildDirectory')) {
        $BuildDirectory = Join-Path $PSScriptRoot '..\build'
    }
    $directory = [IO.Path]::GetFullPath($BuildDirectory)
    if (-not (Test-Path -LiteralPath (Join-Path $directory 'CTestTestfile.cmake') -PathType Leaf)) {
        throw "No configured development CTests in $directory. Configure with BUILD_TESTING=ON first."
    }
    $label = if ($Mode -eq 'Fast') { '^fast$' } else { '^correctness$' }
    Write-Output "$Mode validation ($Configuration): $directory"
    $ctestArguments = @('--test-dir', $directory, '-C', $Configuration, '-L', $label, '--no-tests=error')
    if ($List) {
        $ctestArguments += @('-N', '-V')
        Write-Output 'Selection only; no tests executed. Missing executables are reported by CTest.'
    } else {
        # Stable serial execution avoids overlapping inference and output artifacts.
        $ctestArguments += @('--parallel', '1', '--output-on-failure')
    }
    & ctest @ctestArguments
    exit $LASTEXITCODE
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
