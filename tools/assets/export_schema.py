"""Audit the pinned local weights and emit the fixed numeric-ID contract and C++ schema."""
import argparse
import hashlib
import json
import math
from pathlib import Path

from asset_format import (ROOT, MODEL_INPUT, MODEL_SHA, MODEL_REVISION, COMPATIBILITY_ID,
                          SafeTensorSource, align)


def excluded(name):
    return name.startswith(('mimi.encoder.', 'mimi.encoder_transformer.', 'mimi.downsample.')) or name in (
        'flow_lm.speaker_proj_weight', 'flow_lm.bos_before_voice')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, default=MODEL_INPUT)
    args = parser.parse_args()
    with SafeTensorSource(args.input, MODEL_SHA) as source:
        kept, omitted = [], []
        payload_hash = hashlib.sha256()
        payload_size = 0
        for name, item in sorted(source.records.items()):
            if item['dtype'] != 'BF16' or not 1 <= len(item['shape']) <= 4:
                raise ValueError('Unexpected pinned tensor schema')
            record = {'name': name, 'dtype': 'BF16', 'shape': item['shape']}
            if excluded(name):
                omitted.append(record)
            else:
                record = {'id': len(kept), **record}
                kept.append(record)
                raw = source.bytes(name)
                payload_hash.update(raw)
                payload_hash.update(bytes(align(len(raw)) - len(raw)))
                payload_size += align(len(raw))
        total = sum(math.prod(r['shape']) for r in kept + omitted)
        included_elements = sum(math.prod(r['shape']) for r in kept)
        if (len(kept) + len(omitted), total, len(kept), included_elements) != (214, 109502146, 169, 99719330):
            raise ValueError('Reviewed inclusion/exclusion counts changed')
    contract = {'schema_version': 1, 'pocket_version': '3.3.0', 'architecture': 'english-synthesis-v1',
        'source_revision': MODEL_REVISION, 'source_sha256': MODEL_SHA, 'compatibility_id': COMPATIBILITY_ID,
        'source_tensor_count': 214, 'source_element_count': total, 'tensor_count': len(kept),
        'element_count': included_elements, 'payload_bytes': payload_size, 'payload_sha256': payload_hash.hexdigest(),
        'tensors': kept, 'excluded_tensors': omitted}
    text = json.dumps(contract, indent=2)
    import re
    text = re.sub(r'\[\s*((?:\d+,\s*)*\d+)\s*\]',
                  lambda m: '[' + ', '.join(re.findall(r'\d+', m[1])) + ']', text)
    (ROOT / 'reference/pocket-model-schema.json').write_text(text + '\n', encoding='utf-8')
    identifier = lambda name: name.replace('.', '_')
    lines = ['// Generated offline by tools/assets/export_schema.py from the pinned source.',
        '// Numeric IDs are frozen by format v1; do not reorder them.', '#pragma once',
        '#include <array>', '#include <cstdint>', '#include <string_view>', 'namespace outloud {',
        f'inline constexpr std::string_view pocket_model_source_sha256 = "{MODEL_SHA}";',
        f'inline constexpr std::string_view pocket_model_compatibility_id = "{COMPATIBILITY_ID}";',
        f'inline constexpr std::string_view pocket_model_payload_sha256 = "{payload_hash.hexdigest()}";',
        f'inline constexpr std::uint64_t pocket_model_payload_bytes = {payload_size}ULL;',
        'enum class PocketTensorId : std::uint32_t {']
    lines += [f"    {identifier(r['name'])} = {r['id']}," for r in kept]
    lines += ['};', 'struct PocketTensorSpec {', '    PocketTensorId id;', '    std::string_view name;',
        '    std::uint32_t rank;', '    std::array<std::uint32_t, 4> dimensions;',
        '    std::uint64_t elements;', '};',
        'inline constexpr std::array<PocketTensorSpec, 169> pocket_tensor_schema = {{']
    for r in kept:
        dims = ','.join(map(str, r['shape'] + [0] * (4 - len(r['shape']))))
        lines.append(f'    {{PocketTensorId::{identifier(r["name"])}, "{r["name"]}", {len(r["shape"])}, '
                     + '{' + dims + '}, ' + str(math.prod(r['shape'])) + 'ULL},')
    lines += ['}};', '} // namespace outloud']
    (ROOT / 'engine/include/outloud/pocket_model_schema.hpp').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print(f'Reviewed schema: {len(kept)} kept, {len(omitted)} excluded, {included_elements} synthesis elements')
    print(f'Compatibility ID: {COMPATIBILITY_ID}; aligned payload: {payload_size} bytes')


if __name__ == '__main__':
    main()
