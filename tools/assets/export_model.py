"""Offline export of pinned synthesis-only BF16 weights to Out Loud Pocket .olp."""
import argparse
import hashlib
import math
from pathlib import Path
import struct

from asset_format import (ROOT, MODEL_INPUT, MODEL_SHA, HEADER_SIZE, RECORD_SIZE,
                          SafeTensorSource, align, finish_asset, header, output_path, schema)


def export(source_path, destination):
    contract = schema()
    destination = output_path(destination, source_path)
    with SafeTensorSource(source_path, MODEL_SHA) as source:
        records = contract['tensors'] + contract['excluded_tensors']
        if set(source.records) != {r['name'] for r in records}:
            raise ValueError('Complete source tensor key set differs from the reviewed schema')
        for record in records:
            actual = source.records[record['name']]
            if actual['shape'] != record['shape'] or actual['dtype'] != 'BF16':
                raise ValueError(f"Wrong source tensor schema: {record['name']}")
        included = contract['tensors']
        elements = sum(math.prod(r['shape']) for r in included)
        if len(records) != 214 or len(included) != 169 or elements != 99719330:
            raise ValueError('Reviewed model count/element invariants failed')
        offset = payload_offset = align(HEADER_SIZE + len(included) * RECORD_SIZE)
        table = bytearray()
        for r in included:
            size = math.prod(r['shape']) * 2
            dims = r['shape'] + [0] * (4 - len(r['shape']))
            table.extend(struct.pack('<8I3Q8x', r['id'], 1, len(r['shape']), 0, *dims,
                                     offset, size, size // 2))
            offset = align(offset + size)
        temporary = destination.with_suffix('.olp.tmp')
        try:
            with temporary.open('w+b') as stream:
                stream.write(bytes(HEADER_SIZE))
                stream.write(table)
                stream.write(bytes(payload_offset - stream.tell()))
                payload_hash = hashlib.sha256()
                for r in included:
                    raw = source.bytes(r['name'])
                    stream.write(raw)
                    padding = bytes(align(stream.tell()) - stream.tell())
                    stream.write(padding)
                    payload_hash.update(raw)
                    payload_hash.update(padding)
                if stream.tell() != offset or payload_hash.hexdigest() != contract['payload_sha256']:
                    raise ValueError('Exported BF16 payload differs from exact reviewed source bytes')
                h = header(voice=False, source_sha=MODEL_SHA, count=169, record_or_prefix=64,
                    payload_size=offset - payload_offset, payload_sha=payload_hash.hexdigest(),
                    total_elements=elements, file_size=offset, payload_offset=payload_offset)
                finish_asset(stream, h)
            temporary.replace(destination)
        finally:
            temporary.unlink(missing_ok=True)
    print(f'Model: {destination}; {offset} bytes; 169 included / 45 excluded tensors; exact BF16')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, default=MODEL_INPUT)
    parser.add_argument('--output', type=Path, default=ROOT / 'generated/assets/pocket-english.olp')
    args = parser.parse_args()
    export(args.input, args.output)
