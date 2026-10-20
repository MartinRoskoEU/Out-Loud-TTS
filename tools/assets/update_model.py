"""Developer-only CHECK/UPDATE of the official stable English Pocket model.

No target model, revision, hash or version overrides. Installed software is never
discovered or touched. Candidate builds remain unregistered in isolated staging.
"""
from __future__ import annotations
import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import uuid
import xml.etree.ElementTree as ET

import compare_model as diagnostic

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / 'generated/model-updates'
PENDING = WORK / 'pending.json'
# Old journals may contain retired classification files. Model rollback must
# neither depend on them nor resurrect removed voice-tracking functionality.
RETIRED_VOICE_TRACKING = {'reference/voice-provenance.json', 'engine/include/outloud/voice_provenance.hpp'}
require = diagnostic.require
sha = diagnostic.sha


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, sort_keys=True)+'\n', encoding='utf-8')


def durable_journal(data):
    temporary = PENDING.with_suffix('.tmp')
    with temporary.open('w', encoding='utf-8') as stream:
        json.dump(data, stream, indent=2); stream.write('\n'); stream.flush(); os.fsync(stream.fileno())
    os.replace(temporary, PENDING)


def version(root):
    return ET.parse(root / 'ProductVersion.props').findtext('./PropertyGroup/OutLoudProductVersion')


def next_version(current):
    require(bool(re.fullmatch(r'(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)', current)), 'Invalid authoritative SemVer')
    major, minor, patch = map(int, current.split('.'))
    require(max(major, minor+1, patch) <= 65534, 'Automatic MINOR increment exceeds Windows version bounds')
    return f'{major}.{minor+1}.0'


def pe_file_version(path):
    """Read the native PE fixed file version without loading/executing a DLL."""
    import ctypes
    from ctypes import wintypes
    library = ctypes.WinDLL('version', use_last_error=True)
    library.GetFileVersionInfoSizeW.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(wintypes.DWORD)]
    library.GetFileVersionInfoSizeW.restype = wintypes.DWORD
    library.GetFileVersionInfoW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
    library.VerQueryValueW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(wintypes.UINT)]
    ignored = wintypes.DWORD()
    size = library.GetFileVersionInfoSizeW(str(path), ctypes.byref(ignored))
    require(size > 0, 'Missing PE version resource: '+str(path))
    buffer = ctypes.create_string_buffer(size)
    require(library.GetFileVersionInfoW(str(path), 0, size, buffer), 'Cannot read PE version: '+str(path))
    pointer, length = ctypes.c_void_p(), wintypes.UINT()
    require(library.VerQueryValueW(buffer, '\\', ctypes.byref(pointer), ctypes.byref(length)) and length.value >= 52, 'Invalid PE version resource')
    values = ctypes.cast(pointer, ctypes.POINTER(ctypes.c_uint32))
    require(values[0] == 0xFEEF04BD, 'Invalid PE version signature')
    return '.'.join(str(item) for item in (values[2] >> 16, values[2] & 65535, values[3] >> 16, values[3] & 65535))


def run(command, cwd, log):
    # No shell interpolation; no registration, deployment, installer or release command.
    env = dict(os.environ, PYTHONUTF8='1', PYTHONDONTWRITEBYTECODE='1',
               HF_HUB_OFFLINE='1', HF_HUB_DISABLE_TELEMETRY='1')
    with log.open('wb') as stream:
        result = subprocess.run([str(item) for item in command], cwd=cwd, env=env,
                                stdout=stream, stderr=subprocess.STDOUT, check=False)
    require(result.returncode == 0, f'Validation command failed ({result.returncode}); see {log}')


@contextmanager
def update_lock():
    WORK.mkdir(parents=True, exist_ok=True)
    # OS-held byte-range lock is released even after termination; no stale PID guessing.
    import msvcrt
    with (WORK / 'update.lock').open('a+b') as stream:
        if stream.tell() == 0:
            stream.write(b'0'); stream.flush()
        stream.seek(0)
        try:
            msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError as error:
            raise ValueError('Another developer model operation is running') from error
        try:
            yield
        finally:
            stream.seek(0); msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)


def local_identity():
    contract = read(ROOT / 'reference/pocket-model-schema.json')
    manifest = read(ROOT / 'installer/inno/package.json')
    model_entry = next(row for row in manifest['files'] if row['source'] == 'model')
    model, info = diagnostic.inspect_olp(ROOT / 'generated/assets/pocket-english.olp', contract, model_entry['sha256'])
    model.close()
    cpp = (ROOT / 'engine/include/outloud/pocket_model_schema.hpp').read_text()
    for key in ('source_sha256', 'payload_sha256', 'compatibility_id'):
        require(f'pocket_model_{key} = "{contract[key]}"' in cpp, 'Native/JSON identity mismatch: '+key)
    return contract, info


def prevent_downgrade(selected, contract):
    from huggingface_hub import list_repo_commits
    commits = list_repo_commits(diagnostic.REPO, revision=selected['revision'])
    ancestry = [commit.commit_id for commit in commits]
    require(contract['source_revision'] in ancestry,
            'Selected weight revision is not a descendant of the current pin; refusing downgrade/divergent history')
    history_path = ROOT / 'reference/model-history'
    for record_path in history_path.glob('*.json'):
        record = read(record_path)
        require(record.get('model', {}).get('sha256') != selected['digest'],
                'Official reference points at previously retired weights; automatic downgrade refused')
    def last_change(revision):
        rows = diagnostic.remote_json(f'https://huggingface.co/api/models/{diagnostic.REPO}/tree/{revision}/languages/english?expand=true')
        return next(row['lastCommit']['id'] for row in rows if row['path'] == selected['filename'])
    old, new = last_change(contract['source_revision']), last_change(selected['revision'])
    require(old in ancestry and new in ancestry and ancestry.index(new) < ancestry.index(old),
            'Weight-file history does not establish a forward update; refusing alias-only rewind')


def check(no_download=False):
    import yaml
    require(not PENDING.exists(), 'Incomplete publication exists; run UPDATE to recover before CHECK')
    contract, olp_info = local_identity()
    selected = diagnostic.discover_official()
    report = dict(checked_utc=datetime.now(timezone.utc).isoformat(), previous_engine_version=version(ROOT),
                  current=dict(revision=contract['source_revision'], sha256=contract['source_sha256'], olp=olp_info),
                  official=dict(repository=selected['repo'], filename=selected['filename'],
                                revision=selected['revision'], sha256=selected['digest'],
                                config_revision=selected['git_revision'], config_url=selected['config_url'],
                                config_sha256=hashlib.sha256(selected['config_bytes']).hexdigest(),
                                stable_aliases=selected['aliases']),
                  status='already-current', explanation='Verified source weights are identical; no files, versions or builds changed.')
    # Actual source-file identity must be verified even on the no-op branch.
    source, verification = diagnostic.acquire(selected['repo'], selected['filename'], selected['revision'], selected['pinned'], no_download)
    report['source_verification'] = verification
    selected['source_path'] = source
    current_config = yaml.safe_load((ROOT / 'reference/model-config.yaml').read_bytes())
    before, after = diagnostic.flatten(current_config), diagnostic.flatten(selected['config'])
    differences = {key: dict(current=before.get(key), official=after.get(key)) for key in before.keys() | after.keys() if before.get(key) != after.get(key)}
    report['configuration_differences'] = differences
    require(not differences, 'Unsupported configuration changes: '+json.dumps(differences, sort_keys=True))
    token_repo, token_file, token_revision = diagnostic.hf_uri(selected['config']['flow_lm']['lookup_table']['tokenizer_path'])
    tokenizer, token_info = diagnostic.acquire(token_repo, token_file, token_revision, diagnostic.metadata(token_repo, token_revision), no_download)
    baseline = read(ROOT / 'reference/pipeline-baseline.json')
    require(token_info['sha256'] == baseline['tokenizer']['sha256'], 'Tokenizer changed; native text pipeline review required')
    selected['tokenizer_path'] = tokenizer
    selected['tokenizer_info'] = token_info
    latest = diagnostic.Tensors(source)
    old, _ = diagnostic.inspect_olp(ROOT / 'generated/assets/pocket-english.olp', contract, olp_info['sha256'])
    try:
        comparison = diagnostic.compare(old, latest, contract, False)
    finally:
        latest.close(); old.close()
    report['comparison'] = comparison
    require(comparison['schema_equal'], 'Unsupported tensor changes: '+json.dumps({key: comparison[key] for key in ('added', 'removed', 'schema_changed')}))
    if selected['digest'] != contract['source_sha256']:
        prevent_downgrade(selected, contract)
        report.update(status='update-available', proposed_engine_version=next_version(version(ROOT)),
                      explanation=f"Official stable English default ({', '.join(selected['aliases'])}); {comparison['changed_retained']} synthesis tensors changed. Same native topology/configuration and tokenizer. New weight identity requires MINOR increment; Voice playback depends on supported structure and integrity.")
    return selected, contract, report


def snapshot(destination):
    result = subprocess.run(['git', 'ls-files', '-co', '--exclude-standard', '-z'], cwd=ROOT, check=True, capture_output=True)
    files = set(result.stdout.decode('utf-8').split('\0')) - {''}
    for name in files:
        source = ROOT / name
        require(source.resolve().is_relative_to(ROOT.resolve()) and not source.is_symlink(), 'Unsafe source snapshot path: '+name)
        if source.is_file():
            target = destination / name
            target.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(source, target)


def repin(root, selected, contract, report):
    candidate = json.loads(json.dumps(contract))
    candidate.update(source_sha256=selected['digest'], source_revision=selected['revision'])
    tokenizer_sha = selected['tokenizer_info']['sha256']
    candidate['compatibility_id'] = hashlib.sha256(b'OutLoud/Pocket/English/3.3.0/synthesis-v1\0' + bytes.fromhex(selected['digest']) + bytes.fromhex(tokenizer_sha)).hexdigest()
    source = diagnostic.Tensors(selected['source_path'])
    try:
        digest = hashlib.sha256()
        for record in contract['tensors']:
            raw = source.raw(record['name'])
            digest.update(raw); digest.update(bytes((-len(raw)) % 64))
        candidate['payload_sha256'] = digest.hexdigest()
    finally:
        source.close()
    schema_text = json.dumps(candidate, indent=2)
    schema_text = re.sub(r'\[\s*((?:\d+,\s*)*\d+)\s*\]',
                         lambda match: '['+', '.join(re.findall(r'\d+', match[1]))+']', schema_text)
    (root / 'reference/pocket-model-schema.json').write_text(schema_text+'\n', encoding='utf-8')
    cpp = root / 'engine/include/outloud/pocket_model_schema.hpp'
    content = cpp.read_text()
    for key in ('source_sha256', 'compatibility_id', 'payload_sha256'):
        content = content.replace(contract[key], candidate[key])
    cpp.write_text(content, encoding='utf-8')
    (root / 'reference/model-config.yaml').write_bytes(selected['config_bytes'])
    baseline = read(ROOT / 'reference/pipeline-baseline.json')
    previous_path = ROOT / 'reference/active-model.json'
    previous = read(previous_path) if previous_path.is_file() else dict(model=baseline['model'], tokenizer=baseline['tokenizer'], engine_version=report['previous_engine_version'], config_revision=None)
    write(root / ('reference/model-history/'+contract['source_sha256']+'.json'), previous)
    shutil.copy2(root / 'reference/asset-baseline.json', root / ('reference/model-history/'+contract['source_sha256']+'-asset-baseline.json'))
    active = dict(engine_version=report['proposed_engine_version'], model=dict(repository=selected['repo'], file=selected['filename'], revision=selected['revision'], sha256=selected['digest']),
                  tokenizer=selected['tokenizer_info'], config_revision=selected['git_revision'], config_sha256=report['official']['config_sha256'], configuration=selected['config'],
                  compatibility_id=candidate['compatibility_id'], olv_format_version=1)
    active['tokenizer'] = {key: value for key, value in active['tokenizer'].items() if key not in ('path', 'downloaded')}
    active['tokenizer']['file'] = active['tokenizer'].pop('filename')
    write(root / 'reference/active-model.json', active)
    props = root / 'ProductVersion.props'
    props.write_text(props.read_text().replace(f'<OutLoudProductVersion>{report["previous_engine_version"]}</OutLoudProductVersion>',
                    f'<OutLoudProductVersion>{report["proposed_engine_version"]}</OutLoudProductVersion>'), encoding='utf-8')
    return candidate


def atomic_copy(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(destination.name+'.model-update-tmp')
    require(not temporary.exists(), 'Conflicting temporary publication file: '+str(temporary))
    try:
        shutil.copy2(source, temporary); os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)


def rollback(journal):
    journal = dict(journal, files=[row for row in journal['files'] if row['path'] not in RETIRED_VOICE_TRACKING])
    # Preflight every path/hash before restoring anything. Never overwrite edits.
    backup = Path(journal['backup']).resolve()
    require(backup.is_relative_to((ROOT / 'generated/model-updates').resolve()), 'Recovery backup is outside developer staging')
    for row in journal['files']:
        destination = ROOT / row['path']
        require(destination.resolve().is_relative_to(ROOT.resolve()), 'Unsafe recovery path')
        current = sha(destination) if destination.exists() else None
        require(current in (row['before'], row['after']), 'Recovery blocked by concurrent edit: '+row['path'])
        if row['before']:
            require(sha(Path(journal['backup']) / row['path']) == row['before'], 'Rollback backup integrity failed')
    for row in reversed(journal['files']):
        destination = ROOT / row['path']
        if row['before'] is None:
            destination.unlink(missing_ok=True)
        else:
            atomic_copy(Path(journal['backup']) / row['path'], destination)
    PENDING.unlink(missing_ok=True)


def publish(stage, transaction, paths, original_hashes):
    for name, expected in original_hashes.items():
        path = ROOT / name
        require(path.is_file() and sha(path) == expected, 'Source changed during staging: '+name)
    backup = transaction / 'previous'
    journal = dict(schema_version=1, backup=str(backup), files=[])
    for name in paths:
        destination = ROOT / name
        before = sha(destination) if destination.exists() else None
        require(before == original_hashes.get(name), 'Source changed during staging: '+name)
        if before:
            saved = backup / name; saved.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(destination, saved)
        journal['files'].append(dict(path=name, before=before, after=sha(stage / name)))
    write(transaction / 'rollback.json', journal)
    durable_journal(journal)  # durable recovery recipe precedes the first mutation
    try:
        for row in journal['files']:
            atomic_copy(stage / row['path'], ROOT / row['path'])
        require(all(sha(ROOT / row['path']) == row['after'] for row in journal['files']), 'Published file integrity mismatch')
        local_identity()
        require(version(ROOT) == version(stage), 'Published version mismatch')
        PENDING.unlink()
    except BaseException:
        rollback(journal)
        raise


def update(selected, contract, report, transaction):
    stage = transaction / 'source'
    snapshot(stage)
    assets = stage / 'generated/assets'; assets.mkdir(parents=True)
    # Capture hashes before any long-running conversion/build.
    original_hashes = {}
    for path in stage.rglob('*'):
        if path.is_file():
            name = path.relative_to(stage).as_posix(); original_hashes[name] = sha(path)
    original_hashes['generated/assets/pocket-english.olp'] = report['current']['olp']['sha256']
    for name in ('generated/assets/asset_test_fixtures.hpp', 'generated/assets/bf16-f32-fixture.bin'):
        if (ROOT / name).exists(): original_hashes[name] = sha(ROOT / name)
    preserved = {str(path): sha(path) for directory, pattern in ((ROOT / 'voices', '*.safetensors'), (ROOT / 'generated/assets', '*.olv')) for path in directory.glob(pattern)}
    audio = ROOT / 'samples/gnome_male.wav'
    require(audio.is_file(), 'No original development reference audio available; speech validation cannot run. Existing development state retained.')
    audio_hash = sha(audio)
    python = Path(sys.executable)
    def prepare(source, name):
        output = assets / (name+'.olv')
        run([python, '-B', stage / 'tools/assets/prepare_test_voice.py', '--weights', source,
             '--tokenizer', selected['tokenizer_path'], '--audio', audio, '--output', output], stage, transaction / (name+'.log'))
    # Fresh scratch fixtures only; no existing voice is regenerated or modified.
    candidate = repin(stage, selected, contract, report)
    run([python, '-B', stage / 'tools/assets/export_model.py', '--input', selected['source_path']], stage, transaction / 'conversion.log')
    run([python, '-B', stage / 'tools/assets/export_test_fixtures.py', '--input', selected['source_path'],
         '--voice-directory', ROOT / 'voices'], stage, transaction / 'asset-fixtures.log')
    model_path = assets / 'pocket-english.olp'
    converted, info = diagnostic.inspect_olp(model_path, candidate, sha(model_path)); converted.close()
    report['olp_validation'] = info
    manifest_path = stage / 'installer/inno/package.json'
    manifest_text = manifest_path.read_text()
    previous_model = next(row for row in read(manifest_path)['files'] if row['source'] == 'model')
    require(previous_model['bytes'] == info['bytes'], 'Same tensor topology produced a different OLP byte count')
    manifest_path.write_text(manifest_text.replace(previous_model['sha256'], info['sha256']), encoding='utf-8')
    prepare(selected['source_path'], 'smoke')
    data = bytearray((assets / 'smoke.olv').read_bytes())
    bad_voice = assets / 'invalid.olv'; damaged = bytearray(data); struct.pack_into('<I', damaged, 200, 8)
    damaged[96:128] = bytes(32); damaged[96:128] = hashlib.sha256(damaged).digest(); bad_voice.write_bytes(damaged)
    bad_model = assets / 'invalid.olp'; shutil.copy2(model_path, bad_model)
    with bad_model.open('r+b') as stream:
        stream.seek(-1, 2); value = stream.read(1); stream.seek(-1, 2); stream.write(bytes([value[0] ^ 1]))
    build = transaction / 'build'
    run(['cmake', '-S', stage, '-B', build, '-G', 'Visual Studio 17 2022', '-A', 'x64', '-DBUILD_TESTING=ON',
         '-DOUTLOUD_TOKENIZER_ASSET='+str(ROOT / 'generated/tokenizer/pocket-english-tokenizer.bin'),
         '-DOUTLOUD_FIXTURE_HEADER='+str(ROOT / 'generated/tokenizer/native_text_fixtures.hpp'),
         '-DOUTLOUD_NEURAL_DIRECTORY='+str(ROOT / 'generated/neural'),
         '-DOUTLOUD_BUILD_MODEL_UPDATE_VALIDATOR=ON'], stage, transaction / 'configure.log')
    run(['cmake', '--build', build, '--config', 'Release', '--parallel', '4', '--target', 'outloud-model-update-check', 'outloud-asset-tests',
         'outloud-voice-import-tests', 'outloud-voice-library-tests', 'OutLoudPocketSAPI', 'OutLoudVoiceManagerNative', 'OutLoudVoiceImportHelper', 'outloud-voice-import', 'outloud-sapi-voices'], stage, transaction / 'build.log')
    run([build / 'Release/outloud-model-update-check.exe', model_path, ROOT / 'generated/tokenizer/pocket-english-tokenizer.bin',
         assets / 'smoke.olv', ROOT / 'generated/assets/gnome_male.olv', ROOT / 'voices/gnome_male.safetensors', bad_voice, bad_model, transaction / 'validation'], stage, transaction / 'native-validation.log')
    for path in (ROOT / 'generated/assets').glob('*.olv'):
        shutil.copy2(path, assets / path.name)  # isolated copies for existing format regression tests
    run([build / 'Release/outloud-asset-tests.exe', assets], stage, transaction / 'asset-regressions.log')
    run([build / 'Release/outloud-voice-import-tests.exe', ROOT / 'voices/gnome_male.safetensors', ROOT / 'generated/assets/gnome_male.olv'], stage, transaction / 'import-regressions.log')
    run([build / 'Release/outloud-voice-library-tests.exe', ROOT / 'voices/gnome_male.safetensors', ROOT / 'generated/assets/gnome_male.olv'], stage, transaction / 'library-regressions.log')
    run(['dotnet', 'build', stage / 'ui/OutLoud.VoiceManager/OutLoud.VoiceManager.csproj', '-c', 'Release', '-p:Platform=x64',
         '-p:NativeBuildDirectory='+str(build)], stage, transaction / 'voice-manager-build.log')
    versioned = [build / 'Release' / name for name in ('OutLoudPocketSAPI.dll', 'OutLoudVoiceManagerNative.dll', 'OutLoudVoiceImportHelper.exe', 'outloud-voice-import.exe', 'outloud-sapi-voices.exe')]
    versioned.append(stage / 'ui/OutLoud.VoiceManager/bin/Release/net10.0-windows/OutLoud.VoiceManager.dll')
    report['binary_versions'] = {path.name: pe_file_version(path) for path in versioned}
    require(all(value == version(stage)+'.0' for value in report['binary_versions'].values()), 'Candidate binary versions disagree with ProductVersion.props')
    require(sha(audio) == audio_hash, 'Reference audio changed')
    require(all(sha(Path(path)) == digest for path, digest in preserved.items()), 'An existing developer voice changed during staging')
    report['existing_developer_voices_unchanged'] = preserved
    report.update(native_build='passed (unregistered isolated Release x64)', speech_validation='passed: prepared OLV, existing OLV and imported Safetensors; finite PCM and EOS',
                  voice_manager_build='passed', new_engine_version=version(stage), rollback_manifest=str(transaction / 'rollback.json'))
    # Authoritative version is staged during validation; finalized with all pins only now.
    paths = ['reference/pocket-model-schema.json', 'engine/include/outloud/pocket_model_schema.hpp',
             'reference/model-config.yaml', 'reference/active-model.json', 'installer/inno/package.json',
             'reference/model-history/'+contract['source_sha256']+'.json', 'generated/assets/pocket-english.olp', 'ProductVersion.props']
    paths += ['reference/asset-baseline.json', 'reference/model-history/'+contract['source_sha256']+'-asset-baseline.json',
              'generated/assets/asset_test_fixtures.hpp', 'generated/assets/bf16-f32-fixture.bin']
    active_path = stage / 'reference/active-model.json'
    active = read(active_path); active.update(development_build=build.relative_to(ROOT).as_posix(), validation_report=(transaction / 'native-validation.log').relative_to(ROOT).as_posix(), release_promoted=False)
    write(active_path, active)
    attribution = stage / 'licenses/Pocket-Model-Attribution.md'
    attribution.write_text(attribution.read_text()+f'\n## Active development checkpoint (engine {version(stage)})\n\nOfficial stable English: {selected["repo"]}@{selected["revision"]}.\nSource SHA-256: `{selected["digest"]}`.\nConfiguration: {report["official"]["config_url"]}.\nOLP SHA-256: `{info["sha256"]}`.\nSame synthesis-only BF16 conversion and license/policy obligations. Historical release artifacts are retained.\n', encoding='utf-8')
    paths.insert(0, 'licenses/Pocket-Model-Attribution.md')
    publish(stage, transaction, paths, original_hashes)
    report['status'] = 'updated'
    report['development_build'] = str(build)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', type=str.lower, choices=('check', 'update'))
    parser.add_argument('--no-download', action='store_true', help='CHECK only: require existing verified cache files')
    parser.add_argument('--rollback', action='store_true', help='UPDATE only: restore the last validated update from its integrity-checked backup')
    args = parser.parse_args()
    require(not args.no_download or args.mode == 'check', '--no-download applies to CHECK only')
    require(not args.rollback or args.mode == 'update', '--rollback applies to UPDATE only')
    transaction = WORK / ('run-'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+uuid.uuid4().hex[:8])
    transaction.mkdir(parents=True)
    report = dict(status='blocked')
    try:
        with update_lock():
            if args.mode == 'update' and PENDING.exists():
                rollback(read(PENDING))
            if args.rollback:
                active = read(ROOT / 'reference/active-model.json')
                build = Path(active['development_build'])
                if not build.is_absolute(): build = ROOT / build
                require(build.resolve().is_relative_to(WORK.resolve()), 'Rollback build is outside developer staging')
                journal_path = build.parent / 'rollback.json'
                rollback(read(journal_path))
                report.update(status='rolled-back', previous_engine_version=active['engine_version'], new_engine_version=version(ROOT), rollback_manifest=str(journal_path))
                write(transaction / 'result.json', report)
                print(f"Rolled back engine {active['engine_version']} -> {version(ROOT)}. Existing working builds were retained.")
                return 0
            selected, contract, report = check(args.no_download)
            write(transaction / 'check.json', report)
            print(report['explanation'], flush=True)
            if args.mode == 'update' and report['status'] == 'update-available':
                print('Staging verified conversion, original-audio fixtures and development builds...', flush=True)
                update(selected, contract, report, transaction)
        write(transaction / 'result.json', report)
        print(f"{report['status']}: engine {report['previous_engine_version']}" + (' -> '+report['new_engine_version'] if 'new_engine_version' in report else ''))
        print('Report:', transaction / 'result.json')
        return 0
    except Exception as error:
        report.update(status='blocked', error=str(error))
        write(transaction / 'result.json', report)
        print('BLOCKED:', error, file=sys.stderr)
        print('Report:', transaction / 'result.json', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
