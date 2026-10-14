#Requires -Version 5.1
# Canonical installer-owned registration inputs. No registry or certificate writes.
function Get-ProductionRegistrationIni($Files, [string]$EngineSIP, [string]$DetectorHash, [string]$RequirementsHash) {
    foreach ($hash in @($EngineSIP, $DetectorHash, $RequirementsHash)) {
        if ($hash -cnotmatch '^[0-9a-f]{64}$') { throw 'Invalid registration input hash.' }
    }
    $policy = Get-ProductionPolicy
    # Build records are ordered dictionaries; parsed manifests are PSObjects.
    # An explicit expression sorts both representations by their path value.
    $entries = @($Files | Sort-Object { $_.path })
    if (($entries.path -join '|') -cne (@($policy.files.path | Sort-Object) -join '|')) { throw 'Registration requires the complete explicit payload.' }
    $lines = @('[registration]', 'schema=1', 'hive=HKLM', 'view=64', "clsid=$(Get-OutLoudEngineClsid)",
        'threading_model=Both', "file_count=$($entries.Count)", "detector_sha256=$DetectorHash", "requirements_sha256=$RequirementsHash")
    for ($i=0; $i -lt $entries.Count; $i++) {
        $entry=$entries[$i]
        $hash=$entry.sha256
        # Authenticode changes the engine container; its pinned SIP code identity
        # and exact signed hash/certificate in protected signing.ini both apply.
        if ($entry.path -ceq 'bin/OutLoudPocketSAPI.dll') { $hash=$EngineSIP }
        if ($hash -cnotmatch '^[0-9a-f]{64}$') { throw 'Invalid registration payload hash.' }
        $lines += @("[file$i]", "path=$($entry.path)", "sha256=$hash")
    }
    return ($lines -join "`n") + "`n"
}
