"""Read-only Pocket model comparison. Writes only isolated diagnostic cache/reports."""
from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
import math
import mmap
from pathlib import Path
import re
import struct
import urllib.request
import uuid

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / 'generated/model-comparison'
REPO = 'kyutai/pocket-tts'
GITHUB = 'https://api.github.com/repos/kyutai-labs/pocket-tts/commits/main'
DTYPES = {'BF16': 2, 'F16': 2, 'F32': 4, 'F64': 8, 'I64': 8, 'I32': 4,
          'I16': 2, 'I8': 1, 'U8': 1, 'BOOL': 1, 'U16': 2, 'U32': 4, 'U64': 8}


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(8 * 1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def get(url):
    # No credentials in URLs, reports, or GitHub requests. HF downloads below
    # use huggingface_hub's own authentication and redirect handling.
    request = urllib.request.Request(url, headers={'User-Agent': 'OutLoud-model-comparison'})
    with urllib.request.urlopen(request, timeout=45) as response:
        return response.read()


def remote_json(url):
    return json.loads(get(url))


def metadata(repo, revision):
    return remote_json(f'https://huggingface.co/api/models/{repo}/revision/{revision}?blobs=true')


def hf_uri(uri):
    match = re.fullmatch(r'hf://(kyutai/[^/]+)/(.+)@([0-9a-f]{40})', uri)
    require(match is not None, 'Expected an immutable official Kyutai HF URI')
    return match.groups()


def entry(info, filename):
    return next(row for row in info['siblings'] if row['rfilename'] == filename)


def cached_file(repo, filename, revision, expected=None):
    from huggingface_hub import constants, try_to_load_from_cache
    for cache in (Path(constants.HF_HUB_CACHE), WORK / 'cache'):
        exact = try_to_load_from_cache(repo, filename, revision=revision, cache_dir=cache)
        candidates = [Path(exact)] if isinstance(exact, str) else []
        base = cache / ('models--' + repo.replace('/', '--'))
        if expected:
            candidates.append(base / 'blobs' / expected)
            # Windows caches may contain copies rather than symlinked blobs.
            candidates.extend((base / 'snapshots').glob('*/' + filename))
        for path in candidates:
            if path.is_file() and (expected is None or sha(path) == expected):
                return path
    return None


def acquire(repo, filename, revision, info, no_download):
    row = entry(info, filename)
    expected = row.get('lfs', {}).get('sha256')
    path = cached_file(repo, filename, revision, expected)
    downloaded = False
    if path is None:
        require(not no_download, f'{filename} is not cached; downloading is disabled')
        from huggingface_hub import hf_hub_download
        path = Path(hf_hub_download(repo, filename, revision=revision, cache_dir=WORK / 'cache'))
        downloaded = True
    digest = sha(path)
    require(path.stat().st_size == row['size'], f'Official size mismatch: {filename}')
    if expected:
        require(digest == expected, f'Official LFS SHA256 mismatch: {filename}')
    else:
        data = path.read_bytes()
        git_hash = hashlib.sha1(f'blob {len(data)}\0'.encode() + data).hexdigest()
        require(git_hash == row['blobId'], f'Official Git blob mismatch: {filename}')
    return path, dict(repository=repo, revision=revision, filename=filename,
                      sha256=digest, bytes=path.stat().st_size, path=str(path),
                      downloaded=downloaded, published_hash_verified=True)


def unique_pairs(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f'Duplicate JSON key: {key}')
        result[key] = value
    return result


class Tensors:
    """Read-only mappings; never deserialize pickle or execute model code."""
    def __init__(self, path, records=None):
        self.path = Path(path)
        self.file = self.path.open('rb')
        self.data = mmap.mmap(self.file.fileno(), 0, access=mmap.ACCESS_READ)
        self.start = 0
        self.records = records
        if records is None:
            require(len(self.data) >= 8, 'Truncated Safetensors length')
            size, = struct.unpack_from('<Q', self.data)
            require(0 < size <= 4 * 1024 * 1024 and 8 + size <= len(self.data), 'Invalid Safetensors header')
            self.start = 8 + size
            self.records = json.loads(self.data[8:self.start], object_pairs_hook=unique_pairs)
            extra = self.records.pop('__metadata__', {})
            require(isinstance(extra, dict) and all(isinstance(v, str) for v in extra.values()), 'Invalid metadata')
            end = 0
            intervals = []
            for name, record in self.records.items():
                require(set(record) == {'dtype', 'shape', 'data_offsets'}, f'Invalid tensor record: {name}')
                shape, offsets, dtype = record['shape'], record['data_offsets'], record['dtype']
                require(dtype in DTYPES and isinstance(shape, list) and
                        all(type(d) is int and d > 0 for d in shape), f'Invalid tensor schema: {name}')
                require(len(offsets) == 2 and all(type(d) is int for d in offsets), f'Invalid offsets: {name}')
                first, last = offsets
                require(0 <= first <= last <= len(self.data) - self.start and
                        last - first == math.prod(shape) * DTYPES[dtype], f'Invalid tensor bounds: {name}')
                intervals.append((first, last))
            for first, last in sorted(intervals):
                require(first == end, 'Overlapping or non-contiguous Safetensors data')
                end = last
            require(end == len(self.data) - self.start, 'Trailing Safetensors data')

    def raw(self, name):
        first, last = self.records[name]['data_offsets']
        return self.data[self.start + first:self.start + last]

    def close(self):
        self.data.close()
        self.file.close()


def inspect_olp(path, contract, expected_hash):
    with Path(path).open('rb') as stream:
        header = stream.read(256)
        table = stream.read(64 * contract['tensor_count'])
    u32 = lambda at: struct.unpack_from('<I', header, at)[0]
    u64 = lambda at: struct.unpack_from('<Q', header, at)[0]
    require(len(header) == 256 and header[:8] == b'OLPMOD01', f'Not an OLP model: {path}')
    require([u32(at) for at in (8, 12, 16, 20, 24, 28)] == [1, 256, 0x01020304, 1, 1, 1], 'Unsupported OLP schema')
    require(header[32:64].hex() == contract['compatibility_id'] and
            header[64:96].hex() == contract['source_sha256'], 'OLP identity differs from source contract')
    require([u32(at) for at in (192, 196, 200, 204, 208, 212, 216, 220, 232)] ==
            [169, 64, 1024, 6, 32, 24000, 1920, 0, 0] and not any(header[236:]), 'Wrong OLP architecture')
    records = {}
    offset = 256 + len(table)
    require(u64(168) == 256 and u64(176) == offset and
            u64(184) == contract['payload_bytes'] and u64(224) == contract['element_count'], 'Wrong OLP layout')
    for spec in contract['tensors']:
        row = table[spec['id'] * 64:(spec['id'] + 1) * 64]
        values = struct.unpack('<8I3Q8s', row)
        elements = math.prod(spec['shape'])
        require(values[:4] == (spec['id'], 1, len(spec['shape']), 0) and
                list(values[4:8]) == spec['shape'] + [0] * (4 - len(spec['shape'])) and
                values[8:11] == (offset, elements * 2, elements) and not any(values[11]), 'Wrong OLP tensor record')
        records[spec['name']] = dict(dtype='BF16', shape=spec['shape'], data_offsets=[offset, offset + elements * 2])
        offset = (offset + elements * 2 + 63) // 64 * 64
    model = Tensors(path, records)
    require(len(model.data) == offset == u64(160), 'Wrong OLP file length')
    require(hashlib.sha256(model.data[u64(176):]).hexdigest() == header[128:160].hex() == contract['payload_sha256'], 'Wrong OLP payload hash')
    digest = hashlib.sha256(header[:96] + bytes(32) + header[128:])
    digest.update(model.data[256:])
    require(digest.hexdigest() == header[96:128].hex(), 'Wrong OLP integrity hash')
    actual = sha(path)
    require(actual == expected_hash, 'Actual OLP differs from packaging manifest')
    return model, dict(path=str(path), sha256=actual, bytes=offset, format_version=1,
                       source_sha256=header[64:96].hex(), compatibility_id=header[32:64].hex(),
                       tensor_count=169, elements=contract['element_count'], integrity_verified=True)


def installed_paths(clsid):
    result = dict(models=[], matching_sapi_tokens=0, issues=[])
    try:
        import winreg as reg
        access = reg.KEY_READ | reg.KEY_WOW64_64KEY
        with reg.OpenKey(reg.HKEY_LOCAL_MACHINE, r'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\OutLoud_is1', 0, access) as key:
            root = Path(reg.QueryValueEx(key, 'InstallLocation')[0])
        result['installation_root'] = str(root)
        result['models'].append(str(root / 'runtime/pocket-english.olp'))
        for hive in (reg.HKEY_LOCAL_MACHINE, reg.HKEY_CURRENT_USER):
            try:
                with reg.OpenKey(hive, r'SOFTWARE\Microsoft\Speech\Voices\Tokens', 0, access) as tokens:
                    for index in range(reg.QueryInfoKey(tokens)[0]):
                        with reg.OpenKey(tokens, reg.EnumKey(tokens, index)) as token:
                            values = {reg.EnumValue(token, i)[0]: reg.EnumValue(token, i)[1] for i in range(reg.QueryInfoKey(token)[1])}
                            if str(values.get('CLSID', '')).lower() != clsid.lower():
                                continue
                            result['matching_sapi_tokens'] += 1
                            path = Path(values.get('ModelPath') or 'runtime/pocket-english.olp')
                            if not path.is_absolute():
                                path = Path(values.get('AssetRoot') or root) / path
                            result['models'].append(str(path))
            except FileNotFoundError:
                continue
        result['models'] = sorted(set(result['models']))
    except (ImportError, OSError) as error:
        result['issues'].append('Installed model discovery unavailable: ' + type(error).__name__)
    return result


def flatten(value, prefix=''):
    if isinstance(value, dict):
        return {k: v for name, child in value.items()
                if name not in ('weights_path', 'weights_path_without_voice_cloning', 'tokenizer_path')
                for k, v in flatten(child, prefix + name + '.').items()}
    return {prefix.rstrip('.'): value}


def numeric_difference(old, new, dtype):
    import numpy as np
    def decode(raw):
        if dtype == 'BF16':
            return (np.frombuffer(raw, dtype='<u2').astype('<u4') << 16).view('<f4')
        return np.frombuffer(raw, dtype={'F16': '<f2', 'F32': '<f4', 'F64': '<f8'}[dtype]).astype('float64')
    a, b = decode(old), decode(new)
    require(bool(np.isfinite(a).all() and np.isfinite(b).all()), 'Non-finite model weight')
    difference = b.astype('float64') - a
    return dict(changed_elements=int(np.count_nonzero(a != b)), max_abs=float(np.abs(difference).max()),
                mean_abs=float(np.abs(difference).mean()), rms=float(np.sqrt(np.mean(difference * difference))))


def compare(old, latest, contract, stats):
    expected = {r['name']: r for r in contract['tensors'] + contract['excluded_tensors']}
    actual = latest.records
    result = dict(added=sorted(actual.keys() - expected.keys()), removed=sorted(expected.keys() - actual.keys()),
                  schema_changed=[], changed=[], unchanged=[], unavailable=[], tensor_details=[])
    for name in sorted(expected.keys() & actual.keys()):
        spec, current = expected[name], actual[name]
        if (spec['shape'], spec['dtype']) != (current['shape'], current['dtype']):
            result['schema_changed'].append(name)
            continue
        if name not in old.records:
            result['unavailable'].append(name)
            continue
        before, after = old.raw(name), latest.raw(name)
        changed = before != after
        result['changed' if changed else 'unchanged'].append(name)
        row = dict(name=name, shape=current['shape'], dtype=current['dtype'], changed=changed,
                   old_sha256=hashlib.sha256(before).hexdigest(), new_sha256=hashlib.sha256(after).hexdigest())
        if stats and current['dtype'] in ('BF16', 'F16', 'F32', 'F64'):
            row['numeric'] = numeric_difference(before, after, current['dtype'])
        result['tensor_details'].append(row)
    kept = {r['name'] for r in contract['tensors']}
    result['changed_retained'] = len(kept & set(result['changed']))
    result['changed_excluded'] = len(set(result['changed']) - kept)
    result['changed_groups'] = dict(Counter('.'.join(n.split('.')[:2]) for n in result['changed']))
    result['latest_tensor_count'] = len(actual)
    result['latest_elements'] = sum(math.prod(r['shape']) for r in actual.values())
    result['latest_dtypes'] = dict(Counter(r['dtype'] for r in actual.values()))
    result['schema_equal'] = not any(result[k] for k in ('added', 'removed', 'schema_changed'))
    result['voice_preparation_changes'] = [n for n in result['changed'] if n.startswith(('flow_lm.transformer.', 'mimi.encoder', 'mimi.downsample.'))
                                           or n in ('flow_lm.speaker_proj_weight', 'flow_lm.bos_before_voice')]
    return result


def report_text(report):
    current, official, comparison = report['current'], report['official'], report.get('comparison')
    lines = ['# Pocket TTS model comparison', '', f"Checked: {report['checked_utc']}",
             f"Current original: {REPO}@{current['revision']}", f"Pinned original SHA256: {current['source_sha256']}",
             f"Pinned original bytes verified: {current['original_verified']}; size: {current['original_bytes']}",
             f"Build OLP v1: {current['olp']['sha256']} ({current['olp']['bytes']:,} bytes)",
             'Native architecture: FlowLM 6L, width 1024, 16 heads x 64, FFN 4096; sampler 6 x 512; Mimi 2L/512/8 heads; latent 32; 24 kHz.',
             f"Installed assets: {len(report['installed_assets'])} verified model path(s); {report['installation']['matching_sapi_tokens']} matching SAPI tokens.",
             '', f"Official stable default: {official['stable_aliases']}", f"HF repository HEAD: {official['head_revision']}",
             f"Weights configuration pin: {official['weight_revision']}", f"Weight-file last commit: {official['weight_last_commit']}",
             f"Published original SHA256: {official['published_sha256']} ({official['published_bytes']:,} bytes)",
             f"Official configuration: {official['config_url']}", f"Original bytes verified: {official.get('weights', {}).get('published_hash_verified', False)}",
             f"Non-path configuration differences: {report['configuration_differences']}",
             f"Tokenizer equal to current original: {report.get('tokenizer_equal')}"]
    if comparison:
        lines += ['', f"Tensors: {current['source_tensor_count']} -> {comparison['latest_tensor_count']}; elements: {current['source_elements']:,} -> {comparison['latest_elements']:,}.",
                  f"Dtypes: {comparison['latest_dtypes']}; added {len(comparison['added'])}, removed {len(comparison['removed'])}, schema changes {len(comparison['schema_changed'])}.",
                  f"Changed {len(comparison['changed'])}, unchanged {len(comparison['unchanged'])}, unavailable {len(comparison['unavailable'])}; retained changes {comparison['changed_retained']}/169, excluded changes {comparison['changed_excluded']}/45.",
                  f"Changed groups: {comparison['changed_groups']}"]
        if comparison['added'] or comparison['removed'] or comparison['schema_changed']:
            lines.append(f"Added: {comparison['added']}; removed: {comparison['removed']}; shape/dtype changes: {comparison['schema_changed']}")
        largest = sorted((r for r in comparison['tensor_details'] if r['changed'] and 'numeric' in r),
                         key=lambda r: r['numeric']['max_abs'], reverse=True)[:6]
        for row in largest:
            n = row['numeric']
            lines.append(f"  {row['name']}: max |delta|={n['max_abs']:.6g}, mean={n['mean_abs']:.6g}, RMS={n['rms']:.6g}")
    for category in ('confirmed', 'risks', 'unknowns'):
        lines += ['', category.upper() + ':'] + ['- ' + item for item in report[category]]
    lines += ['', 'No weights converted/replaced; no installed components or voice data modified.',
              'Full tensor names, hashes, schemas and optional numerical statistics: comparison.json.']
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stats', action='store_true', help='Include NumPy per-tensor absolute differences; no inference')
    parser.add_argument('--no-download', action='store_true', help='Query metadata/configs but require weights/tokenizer already cached')
    parser.add_argument('--current-source', type=Path, help='Read-only override for the pinned original Safetensors')
    args = parser.parse_args()
    import yaml
    contract = json.loads((ROOT / 'reference/pocket-model-schema.json').read_text())
    baseline = json.loads((ROOT / 'reference/pipeline-baseline.json').read_text())
    policy = json.loads((ROOT / 'installer/inno/package.json').read_text())
    cpp = (ROOT / 'engine/include/outloud/pocket_model_schema.hpp').read_text()
    for key in ('source_sha256', 'compatibility_id', 'payload_sha256'):
        require(re.search(r'pocket_model_' + key + r' = "' + contract[key] + '"', cpp), 'C++/JSON model identity mismatch')
    require(contract['source_sha256'] == baseline['model']['sha256'] and contract['source_revision'] == baseline['model']['revision'], 'Documented source identities disagree')
    olp_entry = next(r for r in policy['files'] if r['source'] == 'model')
    olp, olp_info = inspect_olp(ROOT / 'generated/assets/pocket-english.olp', contract, olp_entry['sha256'])
    opened = [olp]
    original = args.current_source or cached_file(REPO, baseline['model']['file'], contract['source_revision'], contract['source_sha256'])
    old = olp
    if original:
        require(sha(original) == contract['source_sha256'], 'Pinned original source hash mismatch')
        old = Tensors(original)
        opened.append(old)
        require(set(old.records) == {r['name'] for r in contract['tensors'] + contract['excluded_tensors']}, 'Pinned original key set mismatch')
        for spec in contract['tensors'] + contract['excluded_tensors']:
            row = old.records[spec['name']]
            require((row['shape'], row['dtype']) == (spec['shape'], spec['dtype']), 'Pinned original schema mismatch')
        require(all(olp.raw(r['name']) == old.raw(r['name']) for r in contract['tensors']), 'OLP weights differ from pinned original')
    installation = installed_paths(policy['engine_clsid'])
    installed = []
    for path in installation['models']:
        try:
            mapped, info = inspect_olp(path, contract, olp_entry['sha256'])
            mapped.close()
            installed.append(info)
        except (OSError, ValueError) as error:
            installation['issues'].append(f'{path}: {error}')
    monitored = {str(olp.path): olp_info['sha256'], **{r['path']: r['sha256'] for r in installed}}
    if original:
        monitored[str(original)] = contract['source_sha256']
    hf = metadata(REPO, 'main')
    require(re.fullmatch('[0-9a-f]{40}', hf['sha']) is not None, 'Invalid HF revision')
    git_revision = remote_json(GITHUB)['sha']
    config_url = f'https://raw.githubusercontent.com/kyutai-labs/pocket-tts/{git_revision}/pocket_tts/config/english.yaml'
    config_bytes = get(config_url)
    config = yaml.safe_load(config_bytes)
    require(config['flow_lm']['transformer']['num_layers'] == 6, 'Official default is not a six-layer model; no 24-layer/experimental fallback selected')
    repo, filename, revision = hf_uri(config['weights_path'])
    require(repo == REPO and filename == 'languages/english/model.safetensors', 'Default model selection changed; manual review required')
    pinned = metadata(repo, revision)
    weight_entry = entry(pinned, filename)
    head_entry = entry(hf, filename)
    digest = weight_entry['lfs']['sha256']
    aliases = [r['rfilename'].split('/')[1] for r in hf['siblings'] if
               re.fullmatch(r'languages/english_\d{4}-\d{2}(?:_\d+)?/model.safetensors', r['rfilename']) and r.get('lfs', {}).get('sha256') == digest]
    history = remote_json(f'https://huggingface.co/api/models/{REPO}/tree/{hf["sha"]}/languages/english?expand=true')
    last_commit = next(r['lastCommit'] for r in history if r['path'] == filename)
    local_config = ROOT / '.venv/Lib/site-packages/pocket_tts/config/english.yaml'
    config_old = yaml.safe_load(local_config.read_bytes()) if local_config.is_file() else None
    config_verified = bool(config_old and hf_uri(config_old['weights_path'])[2] == contract['source_revision'])
    differences = None
    if config_verified:
        a, b = flatten(config_old), flatten(config)
        differences = {k: dict(current=a.get(k), latest=b.get(k)) for k in a.keys() | b.keys() if a.get(k) != b.get(k)}
    report = dict(checked_utc=datetime.now(timezone.utc).isoformat(),
                  current=dict(revision=contract['source_revision'], source_sha256=contract['source_sha256'],
                               source_tensor_count=contract['source_tensor_count'], source_elements=contract['source_element_count'],
                               original_verified=bool(original), original_bytes=Path(original).stat().st_size if original else None,
                               original_path=str(original) if original else None, olp=olp_info,
                               reference_config=str(local_config), reference_config_verified=config_verified,
                               reference_config_sha256=sha(local_config) if config_verified else None,
                               aliases_at_official_head=[r['rfilename'] for r in hf['siblings'] if r.get('lfs', {}).get('sha256') == contract['source_sha256']]),
                  official=dict(head_revision=hf['sha'], head_modified=hf.get('lastModified'), weight_revision=revision,
                                filename=filename, published_sha256=digest, published_bytes=weight_entry['size'],
                                stable_aliases=aliases, config_url=config_url, config_sha256=hashlib.sha256(config_bytes).hexdigest(),
                                weight_last_commit=last_commit, head_matches_configured_weights=head_entry['lfs']['sha256'] == digest,
                                selection_basis='Official default English config, confirmed six layers; dated stable aliases match by SHA256. 24l/drifting variants excluded.'),
                  configuration_differences=differences, current_configuration=config_old if config_verified else None,
                  latest_configuration=config, installation=installation, installed_assets=installed,
                  confirmed=[], risks=[], unknowns=list(installation['issues']))
    try:
        latest_path, report['official']['weights'] = acquire(repo, filename, revision, pinned, args.no_download)
        latest = Tensors(latest_path)
        opened.append(latest)
        report['comparison'] = compare(old, latest, contract, args.stats)
        token_repo, token_file, token_revision = hf_uri(config['flow_lm']['lookup_table']['tokenizer_path'])
        _, report['tokenizer'] = acquire(token_repo, token_file, token_revision, metadata(token_repo, token_revision), args.no_download)
        report['tokenizer_equal'] = report['tokenizer']['sha256'] == baseline['tokenizer']['sha256']
    except Exception as error:
        report['unknowns'].append('Latest comparison incomplete: ' + type(error).__name__ + '. No model identity equivalence is assumed; check HF access/cache and diagnostic dependencies.')
    comparison = report.get('comparison')
    report['confirmed'].append(f'Build OLP validates against the pinned schema/manifest; {len(installed)} installed model path(s) match.')
    if digest != contract['source_sha256']:
        report['confirmed'] += ['The existing exporter rejects the new source hash; it cannot convert this checkpoint unchanged. Reviewed source/schema/payload pins must be updated.',
                                'The current native binary rejects new source/payload/compatibility identities; a repin and rebuild is required.']
    else:
        report['confirmed'].append('Official original file hash equals the current pin; repository/package changes alone do not require a model update.')
    if comparison:
        report['confirmed'].append(f"Source schema equal: {comparison['schema_equal']}; non-path configuration equal: {differences == {}}.")
        report['confirmed'].append(f"Actual raw tensor changes: {len(comparison['changed'])}; OLP-retained changes: {comparison['changed_retained']}.")
        if not comparison['schema_equal']:
            report['confirmed'].append('New tensor schema is incompatible with the existing converter/native fixed schema; schema or implementation changes require review.')
        if comparison['schema_equal'] and differences == {} and report.get('tokenizer_equal'):
            report['confirmed'].append('No inference topology/kernel changes indicated by the compared schema/config; exporter algorithm and OLP v1 layout can be reused after updating pins.')
        if comparison['voice_preparation_changes']:
            report['risks'].append(f"{len(comparison['voice_preparation_changes'])} voice-preparation tensors changed. Existing old-weight KV caches are not established compatible; regenerate them with the new model.")
    report['risks'] += ['OLV records a required model compatibility ID and voice-source hash, not authenticated preparation provenance; library metadata explicitly says model_provenance_verified=false.',
                        'OLV contains post-RoPE KV caches, not reference audio/conditioning latents. No supported safe migration to changed voice-preparation weights is available from OLV alone; changing its ID is unsafe.']
    report['unknowns'] += ['No synthesis/quality/EOS benchmark or live process-memory inspection was performed. Registered model paths, not a running process snapshot, were checked.',
                          'Original audio and independently verified preparation provenance for existing user voices were not inspected.',
                          'Counts include all stored parameter/buffer elements. Safetensors does not record requires_grad, so a trainable-only parameter count is not asserted.',
                          'The historical Python config is a local reference tied to the pinned HF revision; its original upstream code-commit identity was not preserved independently.']
    if not config_verified:
        report['unknowns'].append('Historical reference config unavailable or no longer pins this model; semantic configuration equivalence is unverified.')
    if not original:
        report['unknowns'].append('Pinned full original is not cached: only the 169 actual OLP tensors can be byte-compared; excluded tensors are unknown.')
    if head_entry['lfs']['sha256'] != digest:
        report['unknowns'].append('HF HEAD weights differ from the official default config pin; the stable-default selection needs upstream review.')
    for model in opened:
        model.close()
    require(all(sha(Path(p)) == digest for p, digest in monitored.items()), 'Existing asset changed during diagnostics')
    report['existing_assets_unchanged'] = True
    destination = WORK / ('run-' + datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ') + '-' + uuid.uuid4().hex[:8])
    destination.mkdir(parents=True, exist_ok=False)
    (destination / 'comparison.json').write_text(json.dumps(report, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    text = report_text(report)
    (destination / 'comparison.md').write_text(text, encoding='utf-8')
    print(text)
    print('Report directory:', destination)
    return 0 if comparison is not None and not installation['issues'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
