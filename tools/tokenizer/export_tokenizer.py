"""Offline export of the single pinned Pocket English tokenizer; standard library only."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import unicodedata

os.environ['HF_HUB_OFFLINE'] = '1'
ROOT = Path(__file__).resolve().parents[2]
SHA = 'f498428e1eafee50492f7be13dc9bfafcfc12e508cd0eb1b01c92ecd5d8c6687'
REVISION = '00eac05ed3d16bdc3f6b5d598874019c34a89214'
DEFAULT_INPUT = (Path.home() / '.cache/huggingface/hub/models--kyutai--pocket-tts-without-voice-cloning'
                 / 'snapshots' / REVISION / 'languages/english/tokenizer.json')


def ranges(predicate):
    result = []
    start = None
    for cp in range(0x110001):
        matches = cp < 0x110000 and predicate(chr(cp))
        if matches and start is None:
            start = cp
        elif not matches and start is not None:
            result.append((start, cp - 1))
            start = None
    return result


def export(source: Path, output: Path):
    data = source.read_bytes()
    if hashlib.sha256(data).hexdigest() != SHA:
        raise ValueError('Input is not the pinned Pocket English tokenizer JSON')
    if unicodedata.unidata_version != '15.0.0':
        raise ValueError('Use the reference Python 3.12 / Unicode 15.0.0 for preparation tables')
    doc = json.loads(data)
    assert doc['model']['type'] == 'Unigram' and doc['model']['unk_id'] == 0
    assert doc['model']['byte_fallback'] is True and len(doc['model']['vocab']) == 4000
    assert doc['normalizer'] == {'type': 'Prepend', 'prepend': '\u2581'}
    assert doc['pre_tokenizer'] == {'type': 'Metaspace', 'replacement': '\u2581', 'prepend_scheme': 'always', 'split': True}
    assert doc['decoder']['decoders'] == [doc['pre_tokenizer'], {'type': 'ByteFallback'}, {'type': 'Fuse'}]
    assert doc['post_processor'] is None and doc['padding'] is None and doc['truncation'] is None
    expected_specials = ['<unk>', '<s>', '</s>', '<pad>']
    assert doc['added_tokens'] == [dict(id=i, content=s, single_word=False, lstrip=False,
        rstrip=False, normalized=False, special=True) for i, s in enumerate(expected_specials)]
    payload = bytearray()
    def u32(n):
        payload.extend(struct.pack('<I', n))
    def string(s):
        encoded = s.encode('utf-8')
        u32(len(encoded))
        payload.extend(encoded)
    for i, (piece, score) in enumerate(doc['model']['vocab']):
        if not math.isfinite(score) or not piece:
            raise ValueError('Invalid vocabulary entry')
        if i < 4:
            assert piece == expected_specials[i]
        if 4 <= i < 260:
            assert piece == f'<0x{i - 4:02X}>'
        string(piece)
        payload.extend(struct.pack('<d', score))
    string(unicodedata.unidata_version)
    for predicate in (str.isspace, str.isupper, str.isdigit):
        table = ranges(predicate)
        u32(len(table))
        for start, end in table:
            u32(start)
            u32(end)
    upper = [(cp, chr(cp).upper()) for cp in range(0x110000) if chr(cp).upper() != chr(cp)]
    u32(len(upper))
    for cp, replacement in upper:
        u32(cp)
        string(replacement)
    # Header: magic, version, vocab count, unk ID, payload length, source SHA, payload SHA.
    header = struct.pack('<8sIIII', b'OLPTOK01', 1, 4000, 0, len(payload))
    header += bytes.fromhex(SHA) + hashlib.sha256(payload).digest()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(header + payload)
    print(f'Exported {output}: {len(header) + len(payload)} bytes; tokenizer SHA256 {SHA}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, default=DEFAULT_INPUT)
    parser.add_argument('--output', type=Path, default=ROOT / 'generated/tokenizer/pocket-english-tokenizer.bin')
    args = parser.parse_args()
    export(args.input, args.output)


if __name__ == '__main__':
    main()
