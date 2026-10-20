"""Shared fixed Pocket asset constants and local-only offline file helpers."""
from __future__ import annotations

import hashlib
import json
import mmap
import os
from pathlib import Path
import struct

os.environ['HF_HUB_OFFLINE'] = '1'
ROOT = Path(__file__).resolve().parents[2]
_MODEL = json.loads((ROOT / 'reference/pocket-model-schema.json').read_text(encoding='utf-8'))
MODEL_SHA = _MODEL['source_sha256']
TOKENIZER_SHA = 'f498428e1eafee50492f7be13dc9bfafcfc12e508cd0eb1b01c92ecd5d8c6687'
MODEL_REVISION = _MODEL['source_revision']
MODEL_INPUT = (Path.home() / '.cache/huggingface/hub/models--kyutai--pocket-tts/snapshots'
               / MODEL_REVISION / 'languages/english/model.safetensors')
COMPATIBILITY_ID = hashlib.sha256(b'OutLoud/Pocket/English/3.3.0/synthesis-v1\0'
                                + bytes.fromhex(MODEL_SHA) + bytes.fromhex(TOKENIZER_SHA)).hexdigest()
VOICE_SHAS = {
    'gnome_male': '2118ea60dfe87bd515b27e051e1d677a7fe15663875740599147bde37a9c4cea',
    # Recomputed locally: UPSTREAM.md's historical entry is missing one hex digit.
    'undead_male': 'dce632cddb3a33203e140c73b2fa3970325bfbef3984813ddfe3e43e328037b6',
}
HEADER_SIZE = 256
ALIGNMENT = 64
RECORD_SIZE = 64


def align(n):
    return (n + ALIGNMENT - 1) // ALIGNMENT * ALIGNMENT


def sha_file(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(8 * 1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


class SafeTensorSource:
    """Read exact source bytes offline; supports only the three dtypes used in this step."""
    def __init__(self, path: Path, expected_sha: str):
        self.path = path
        if sha_file(path) != expected_sha:
            raise ValueError(f'Source SHA256 mismatch: {path}')
        self.file = path.open('rb')
        try:
            self.mapping = mmap.mmap(self.file.fileno(), 0, access=mmap.ACCESS_READ)
            header_size = struct.unpack_from('<Q', self.mapping)[0]
            self.start = 8 + header_size
            if self.start > len(self.mapping):
                raise ValueError('Truncated safetensors header')
            self.records = json.loads(self.mapping[8:self.start])
            self.records.pop('__metadata__', None)
            sizes = {'BF16': 2, 'F32': 4, 'I64': 8}
            import math
            intervals = []
            for name, record in self.records.items():
                first, last = record['data_offsets']
                shape = record['shape']
                if record['dtype'] not in sizes or any(not isinstance(d, int) or d <= 0 for d in shape):
                    raise ValueError(f'Unsupported source tensor: {name}')
                if first < 0 or last < first or last > len(self.mapping) - self.start:
                    raise ValueError(f'Invalid source offsets: {name}')
                if last - first != math.prod(shape) * sizes[record['dtype']]:
                    raise ValueError(f'Invalid source tensor size: {name}')
                intervals.append((first, last))
            end = 0
            for first, last in sorted(intervals):
                if first != end:
                    raise ValueError('Overlapping or non-contiguous source tensors')
                end = last
            if end != len(self.mapping) - self.start:
                raise ValueError('Trailing source bytes')
        except Exception:
            if hasattr(self, 'mapping'):
                self.mapping.close()
            self.file.close()
            raise

    def bytes(self, name):
        first, last = self.records[name]['data_offsets']
        return self.mapping[self.start + first:self.start + last]

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.mapping.close()
        self.file.close()


def schema():
    result = json.loads((ROOT / 'reference/pocket-model-schema.json').read_text(encoding='utf-8'))
    if result['source_sha256'] != MODEL_SHA or result['compatibility_id'] != COMPATIBILITY_ID:
        raise ValueError('Schema identity mismatch')
    return result


def header(*, voice, source_sha, count, record_or_prefix, payload_size, payload_sha,
           total_elements, file_size, payload_offset):
    result = bytearray(HEADER_SIZE)
    struct.pack_into('<8s6I', result, 0, b'OLVPKT01' if voice else b'OLPMOD01',
                     1, HEADER_SIZE, 0x01020304, 1, 1, 2 if voice else 1)
    result[32:64] = bytes.fromhex(COMPATIBILITY_ID)
    result[64:96] = bytes.fromhex(source_sha)
    # 96..127: whole-container SHA256, computed with those bytes zeroed.
    result[128:160] = bytes.fromhex(payload_sha)
    struct.pack_into('<4Q8IQI', result, 160, file_size, HEADER_SIZE, payload_offset, payload_size,
                     count, record_or_prefix, 16 if voice else 1024, 64 if voice else 6,
                     1 if voice else 32, 24000, 1920, 1 if voice else 0, total_elements, 1 if voice else 0)
    return result


def finish_asset(stream, asset_header):
    stream.seek(0)
    stream.write(asset_header)
    stream.flush()
    stream.seek(0)
    h = hashlib.sha256()
    for block in iter(lambda: stream.read(8 * 1024 * 1024), b''):
        h.update(block)  # Integrity field is still zero.
    stream.seek(96)
    stream.write(h.digest())


def output_path(path: Path, source: Path):
    path = path.resolve()
    if not path.is_relative_to((ROOT / 'generated/assets').resolve()) or path == source.resolve():
        raise ValueError('Converters write only under generated/assets, never over a source asset')
    path.parent.mkdir(parents=True, exist_ok=True)
    return path
