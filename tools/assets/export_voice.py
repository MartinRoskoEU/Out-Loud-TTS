"""Convert a validated local precomputed Pocket voice to ready-to-synthesize .olv."""
import argparse
import hashlib
from pathlib import Path
import struct

import numpy as np

from asset_format import ROOT, VOICE_SHAS, SafeTensorSource, finish_asset, header, output_path


def read_voice(source):
    names = {f'transformer.layers.{layer}.self_attn/{field}'
             for layer in range(6) for field in ('cache', 'offset', 'pad')}
    if set(source.records) != names:
        raise ValueError('Voice must contain exactly six cache/offset/pad groups')
    prefix = None
    payload = bytearray()
    for layer in range(6):
        base = f'transformer.layers.{layer}.self_attn/'
        cache = source.records[base + 'cache']
        shape = cache['shape']
        if len(shape) != 5 or shape[:2] != [2, 1] or shape[3:] != [16, 64] or cache['dtype'] != 'F32':
            raise ValueError('Voice K/V layout must be FP32 [2,1,prefix,16,64]')
        for field in ('offset', 'pad'):
            record = source.records[base + field]
            if record['shape'] != [1] or record['dtype'] != 'I64':
                raise ValueError('Voice offset/pad schema mismatch')
        offset = struct.unpack('<q', source.bytes(base + 'offset'))[0]
        pad = struct.unpack('<q', source.bytes(base + 'pad'))[0]
        if offset != shape[2] or pad != 0 or (prefix is not None and prefix != offset):
            raise ValueError('Voice must have equal valid-prefix offsets and zero padding')
        prefix = offset
        raw = source.bytes(base + 'cache')
        if not np.isfinite(np.frombuffer(raw, dtype='<f4')).all():
            raise ValueError('Voice cache contains non-finite/unused positions')
        payload.extend(raw)
    if not prefix or len(payload) > 1024 * 1024 * 1024:
        raise ValueError('Invalid/oversized voice prefix')
    return prefix, payload


def export(source_path, destination, source_sha):
    destination = output_path(destination, source_path)
    with SafeTensorSource(source_path, source_sha) as source:
        prefix, payload = read_voice(source)
    h = header(voice=True, source_sha=source_sha, count=6, record_or_prefix=prefix,
        payload_size=len(payload), payload_sha=hashlib.sha256(payload).hexdigest(),
        total_elements=len(payload) // 4, file_size=256 + len(payload), payload_offset=256)
    temporary = destination.with_suffix('.olv.tmp')
    try:
        with temporary.open('w+b') as stream:
            stream.write(bytes(256))
            stream.write(payload)
            finish_asset(stream, h)
        temporary.replace(destination)
    finally:
        temporary.unlink(missing_ok=True)
    print(f'Voice: {destination}; {256 + len(payload)} bytes; prefix={prefix}; 6 exact FP32 K/V prefixes')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, default=ROOT / 'voices/gnome_male.safetensors')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--source-sha256', help='Required for a local voice outside the two reference identities')
    args = parser.parse_args()
    expected_sha = args.source_sha256 or VOICE_SHAS.get(args.input.stem)
    if not expected_sha or len(expected_sha) != 64:
        parser.error('Supply the expected SHA256 of an unknown local source voice')
    output = args.output or ROOT / 'generated/assets' / (args.input.stem + '.olv')
    export(args.input, output, expected_sha)
