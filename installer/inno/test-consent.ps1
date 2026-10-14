#Requires -Version 5.1
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$StageDirectory, [string]$SourceCard)
. (Join-Path $PSScriptRoot 'production-common.ps1')
[void](Assert-ProductionStage $StageDirectory)
$script:ConsentChecks = 0
function Check([bool]$Passed, [string]$Label) {
    if (-not $Passed) { throw "Consent check failed: $Label" }
    $script:ConsentChecks++
}
$iss = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'OutLoud.iss') -Raw
$consent = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'consent.iss') -Raw
$runtime = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'runtime-install.iss') -Raw
$build = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'build-stage.ps1') -Raw
$metadata = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'consent.json') -Raw | ConvertFrom-Json
$agreement = Get-InstallationAgreementText
Check ($iss -match '(?m)^LicenseFile=\{#StageRoot\}\\payload\\licenses\\OutLoud-Installation-Terms.txt\r?$') 'standard LicenseFile page'
Check ($iss -match '(?m)^LicenseAccepted=I &accept' -and $iss -match '(?m)^LicenseNotAccepted=I &do not accept') 'explicit accept/decline labels'
Check ($consent -match 'LicenseAcceptedRadio.Checked := False;' -and $consent -match 'LicenseNotAcceptedRadio.Checked := True;') 'acceptance never preselected'
Check ($iss -match 'InitializeProductConsent;\s+InitializeRuntimeWizard;') 'initialize acceptance before prerequisites'
Check ($runtime -match "CreateInputOptionPage\(wpLicense," -and $runtime -match 'RuntimeConsentPage.Values\[0\] := False;') 'separate runtime consent follows license page'
Check ($iss -match 'if PageID = wpLicense then begin Result := False; Exit; end;') 'mandatory license page never skipped'
Check ($iss -match 'function InitializeSetup\(\): Boolean;[\s\S]*?if WizardSilent then begin[\s\S]*?Exit;' -and
    $iss -match 'function NextButtonClick[\s\S]*?Result := not WizardSilent;') 'silent initialization and simulated Next rejected'
Check ($iss -match 'function NextButtonClick[\s\S]*?CurPageID <> wpWelcome[\s\S]*?Result := ProductConsentSatisfied;') 'all post-welcome Next transitions require consent'
Check ($iss -match 'function PrepareToInstall[\s\S]*?if not CheckProductConsent[\s\S]*?Exit;[\s\S]*?PrepareRuntimePrerequisites') 'application preparation rechecks acceptance'
Check ($runtime -match 'function EnsureRuntimeComponent[\s\S]*?if not CheckProductConsent[\s\S]*?Exit;[\s\S]*?ReadOutLoudPrerequisites[\s\S]*?RuntimeDownloadPage.Download') 'acceptance guard before each download and vendor execution'
Check ($runtime -match 'function PrepareRuntimePrerequisites[\s\S]*?if not CheckProductConsent[\s\S]*?EnsureRuntimeComponent') 'prerequisite coordinator rechecks acceptance'
Check ($consent -notmatch 'RuntimeConsentPage|SetPreviousData|GetPreviousData|GetIniString|ParamStr|SaveStringToFile') 'no cached, command-line or conflated acceptance'
Check ($iss -match '#ifndef StageRoot' -and $iss -match 'StartSharedMaintenance' -and
    $runtime -match 'function FoundationLifecycleReady\(\): Boolean;[\s\S]*?Result := IsAdmin;') 'validated stage and shared elevated lifecycle required'
# Evaluate the production's small pure Boolean predicate, rather than a second policy.
# This checks consent decisions; it does not execute Pascal or claim a live wizard test.
$match = [regex]::Match($consent, 'function ProductConsentSatisfied\(\): Boolean;\s*begin\s*Result := ([^;]+);\s*end;')
Check ($match.Success) 'single stateless production consent predicate'
$expression = $match.Groups[1].Value.Trim()
Check ($expression -ceq 'not WizardSilent and WizardForm.LicenseAcceptedRadio.Checked') 'bounded predicate evaluation syntax'
$evaluate = [scriptblock]::Create('param([bool]$Silent,[bool]$Accepted) ' +
    $expression.Replace('not ', '-not ').Replace(' and ', ' -and ').Replace('WizardSilent', '$Silent').Replace('WizardForm.LicenseAcceptedRadio.Checked', '$Accepted'))
Check (-not (& $evaluate $false $false)) 'decline blocks progression'
Check ([bool](& $evaluate $false $true)) 'affirmative interactive acceptance allows later pages subject to foundation gate'
Check (-not (& $evaluate $true $false)) 'silent decline rejected'
Check (-not (& $evaluate $true $true)) 'silent acceptance cannot bypass'
Check (-not (& $evaluate $false $false)) 'returning and declining revokes progression without cached state'
Check ($agreement.Contains('Copyright (c) 2026 Nendis') -and $agreement.Contains('Creative Commons Attribution 4.0 International')) 'MIT and CC identities'
Check ($agreement.Contains('SEPARATE FROM CC BY 4.0') -and $agreement.Contains('not restrictions contained in CC BY 4.0')) 'separate upstream policy contract'
foreach ($phrase in @('voice impersonation or cloning without explicit and lawful consent', 'misinformation, disinformation, or deception',
    'fake news, fraudulent calls', 'presenting generated content as genuine recordings of real people or events',
    'unlawful, harmful, libelous, abusive, harassing, discriminatory, hateful, or privacy-invasive content', 'We disclaim all liability')) {
    Check ($agreement.Contains($phrase)) "complete upstream scope: $phrase"
}
foreach ($document in $metadata.documents) {
    $original = [IO.File]::ReadAllText((Resolve-ProductionPath $ProductionRepository $document), [Text.Encoding]::UTF8)
    Check ($agreement.Contains($original) -and $document -cin @((Get-ProductionPolicy).files.path)) "full presented and installed document: $document"
}
$staged = Join-Path $StageDirectory ('payload/' + $metadata.agreement_path)
Check ([IO.File]::ReadAllText($staged, [Text.Encoding]::UTF8) -ceq $agreement) 'staged presentation matches all current sources'
Check ($build.IndexOf('$agreement = Get-InstallationAgreementText') -lt $build.IndexOf('& cmake -S')) 'missing legal files fail before builds'
if ($SourceCard) {
    Assert-ProductionPlainPath $SourceCard
    $card = [IO.File]::ReadAllText($SourceCard, [Text.Encoding]::UTF8)
    $upstream = [regex]::Match($card, '(?s)## Prohibited use\r?\n\r?\n(.+?)(?=\r?\n\r?\n## Authors)').Groups[1].Value.Trim()
    Check ((Get-ConsentTextHash $upstream) -eq $metadata.policy_text_sha256) 'verbatim paragraph matches authoritative pinned card'
}
# All negative-input writes target a new, contained copy of the legal inputs.
$parent = Join-Path $ProductionRepository 'generated/installer/consent-tests'
$scratch = Resolve-ProductionPath $parent ([Guid]::NewGuid().ToString('N'))
Assert-ProductionPlainPath $scratch
[void](New-Item -ItemType Directory -Path $scratch -Force)
try {
    foreach ($relative in @($metadata.documents) + @($metadata.introduction)) {
        $target = Resolve-ProductionPath $scratch $relative
        [void](New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($target)) -Force)
        Copy-Item -LiteralPath (Resolve-ProductionPath $ProductionRepository $relative) -Destination $target
    }
    Check ((Get-InstallationAgreementText $scratch) -ceq $agreement) 'private legal source copy matches'
    foreach ($relative in @($metadata.documents) + @($metadata.introduction)) {
        $path = Resolve-ProductionPath $scratch $relative
        $saved = [IO.File]::ReadAllBytes($path)
        Remove-Item -LiteralPath $path
        $rejected = $false
        try { [void](Get-InstallationAgreementText $scratch) } catch { if ($_.Exception.Message -notmatch 'Missing legal document') { throw }; $rejected = $true }
        finally { [IO.File]::WriteAllBytes($path, $saved) }
        Check $rejected "missing source refuses packaging: $relative"
    }
    $path = Resolve-ProductionPath $scratch $metadata.policy_path
    $text = [IO.File]::ReadAllText($path)
    [IO.File]::WriteAllText($path, $text.Replace('explicit and lawful consent', 'consent'))
    $rejected = $false
    try { [void](Get-InstallationAgreementText $scratch) } catch { if ($_.Exception.Message -notmatch 'Pinned Kyutai policy differs') { throw }; $rejected = $true }
    Check $rejected 'changed upstream meaning/hash rejected'
    Write-Output "$script:ConsentChecks controlled consent checks PASS; no installer compiled or executed."
} finally {
    $verified = Resolve-ProductionPath $parent ([IO.Path]::GetFileName($scratch))
    if ($verified -ne $scratch) { throw 'Consent scratch containment changed; cleanup refused.' }
    Assert-ProductionPlainPath $scratch
    Remove-Item -LiteralPath $scratch -Recurse -Force
}
