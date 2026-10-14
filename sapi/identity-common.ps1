# Shared read-only identity for repository tooling. Deployed development bundles
# remain self-contained and their fixed copies are checked by test-paths-identity.ps1.
function Get-OutLoudEngineClsid {
    $header = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'src/identity.hpp'))
    $matches = [regex]::Matches($header, 'inline constexpr wchar_t clsid_text\[\]\s*=\s*L"(\{[0-9A-Fa-f-]{36}\})";')
    if ($matches.Count -ne 1) { throw 'identity.hpp must contain one authoritative SAPI CLSID.' }
    return ([guid]$matches[0].Groups[1].Value).ToString('B').ToUpperInvariant()
}
