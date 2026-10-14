#Requires -Version 5.1
# Offline presentation only. Never changes the source legal documents.
function Get-ConsentTextHash([string]$Text) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($Text))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
}

function Get-InstallationAgreementText([string]$Repository = $ProductionRepository) {
    $metadata = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'consent.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($metadata.schema -ne 1 -or $metadata.agreement_path -ne 'licenses/OutLoud-Installation-Terms.txt' -or
        $metadata.policy_path -ne 'licenses/Pocket-Prohibited-Use-Policy.txt' -or
        $metadata.policy_revision -ne '00eac05ed3d16bdc3f6b5d598874019c34a89214' -or
        $metadata.policy_source -ne "https://huggingface.co/kyutai/pocket-tts-without-voice-cloning/raw/$($metadata.policy_revision)/README.md") {
        throw 'Unexpected consent document policy.'
    }
    $expected = @('licenses/Pocket-Prohibited-Use-Policy.txt', 'LICENSE', 'licenses/Pocket-Model-Attribution.md',
        'licenses/Pocket-Tokenizer-Attribution.md', 'licenses/CC-BY-4.0.txt', 'THIRD_PARTY_NOTICES.md', 'RNG_NOTICES.md',
        'licenses/Pocket-TTS-MIT.txt', 'licenses/MAR-MIT.txt', 'licenses/DotNet-AppHost-MIT.txt', 'licenses/Unicode-Data.txt',
        'licenses/Sonic-Apache-2.0.txt')
    if (($metadata.documents -join '|') -cne ($expected -join '|') -or $metadata.introduction -ne 'installer/inno/consent-introduction.txt') {
        throw 'Incomplete or unexpected consent document list.'
    }
    $read = {
        param([string]$Relative)
        $path = Resolve-ProductionPath $Repository $Relative
        Assert-ProductionPlainPath $path
        if (-not [IO.File]::Exists($path)) { throw "Missing legal document: $Relative" }
        $text = [IO.File]::ReadAllText($path, [Text.Encoding]::UTF8)
        if ([string]::IsNullOrWhiteSpace($text)) { throw "Empty legal document: $Relative" }
        return $text
    }
    $introduction = & $read $metadata.introduction
    $parts = @($introduction)
    foreach ($document in $metadata.documents) {
        $text = & $read $document
        if ($document -eq $metadata.policy_path) {
            $match = [regex]::Match($text, '(?s)BEGIN UPSTREAM POLICY\r?\n(.+?)\r?\nEND UPSTREAM POLICY')
            if (-not $match.Success -or (Get-ConsentTextHash $match.Groups[1].Value) -ne $metadata.policy_text_sha256 -or
                -not $text.Contains($metadata.policy_source)) { throw 'Pinned Kyutai policy differs.' }
        }
        $parts += "`n`n================================================================`nDOCUMENT: $document`n================================================================`n`n$text"
    }
    return ($parts -join '')
}
