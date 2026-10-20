"""Offline original-byte and PyTorch widening references; no native runtime dependencies."""
import hashlib
import json
import argparse
from pathlib import Path

import numpy as np
import torch

from asset_format import ROOT, MODEL_INPUT, MODEL_SHA, VOICE_SHAS, SafeTensorSource, schema
from export_voice import read_voice


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, default=MODEL_INPUT)
    parser.add_argument('--voice-directory', type=Path, default=ROOT / 'voices')
    args = parser.parse_args()
    destination = ROOT / 'generated/assets'
    destination.mkdir(parents=True, exist_ok=True)
    contract = schema()
    with SafeTensorSource(args.input, MODEL_SHA) as source:
        original_hash = hashlib.sha256()
        widened_hash = hashlib.sha256()
        for r in contract['tensors']:
            raw = source.bytes(r['name'])
            original_hash.update(raw)
            bits = np.frombuffer(raw, dtype='<u2').copy()
            widened = torch.from_numpy(bits).view(torch.bfloat16).float()
            widened_hash.update(widened.numpy().astype('<f4', copy=False).tobytes())
    # Full 16-bit domain, including signed zeros, subnormals, infinities and NaN payloads.
    all_bits = torch.arange(65536, dtype=torch.int32).to(torch.uint16)
    widened = all_bits.view(torch.bfloat16).float().view(torch.int32).numpy().astype('<i4', copy=False).tobytes()
    (destination / 'bf16-f32-fixture.bin').write_bytes(widened)
    voices = []
    for name, sha in VOICE_SHAS.items():
        path = args.voice_directory / (name + '.safetensors')
        if not path.exists():
            continue
        with SafeTensorSource(path, sha) as source:
            prefix, payload = read_voice(source)
        voices.append({'name': name, 'source_sha256': sha, 'prefix_length': prefix,
                       'payload_bytes': len(payload), 'payload_sha256': hashlib.sha256(payload).hexdigest()})
    baseline = {'schema_version': 1, 'model_source_sha256': MODEL_SHA,
        'model_raw_tensor_bytes_sha256': original_hash.hexdigest(),
        'model_widened_tensor_bytes_sha256': widened_hash.hexdigest(),
        'bf16_pattern_count': 65536, 'bf16_fixture_sha256': hashlib.sha256(widened).hexdigest(),
        'torch_version': torch.__version__, 'voices': voices}
    (ROOT / 'reference/asset-baseline.json').write_text(json.dumps(baseline, indent=2) + '\n', encoding='utf-8')
    lines = ['// Generated offline from original source bytes and PyTorch CPU widening.', '#pragma once',
        'namespace outloud::test {',
        f'inline constexpr std::string_view model_raw_sha = "{original_hash.hexdigest()}";',
        f'inline constexpr std::string_view model_f32_sha = "{widened_hash.hexdigest()}";',
        f'inline constexpr std::string_view bf16_fixture_sha = "{hashlib.sha256(widened).hexdigest()}";',
        'struct VoiceExpected { std::string_view name, source_sha, payload_sha; std::uint32_t prefix; };',
        f'inline constexpr std::array<VoiceExpected, {len(voices)}> voice_expected = {{{{']
    lines += [f'    {{"{v["name"]}", "{v["source_sha256"]}", "{v["payload_sha256"]}", {v["prefix_length"]}}},' for v in voices]
    lines += ['}};', '}']
    (destination / 'asset_test_fixtures.hpp').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print('Exported exact original model/voice hashes and 65,536 PyTorch BF16 widening patterns')


if __name__ == '__main__':
    main()
