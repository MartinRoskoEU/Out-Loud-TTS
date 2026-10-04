"""Offline JSON-to-C++ fixture conversion; native tests never invoke Python or parse JSON."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'generated/tokenizer/native_text_fixtures.hpp')
    args = parser.parse_args()
    baseline = json.loads((ROOT / 'reference/pipeline-baseline.json').read_text(encoding='utf-8'))
    cases = json.loads((ROOT / 'reference/text-pipeline-cases.json').read_text(encoding='utf-8'))
    assert cases['tokenizer_sha256'] == baseline['tokenizer']['sha256']
    oracle = baseline['oracle']
    corpus = [dict(name='canonical', text=oracle['text'], prepared_text=oracle['text'],
        raw_token_ids=oracle['token_ids'], decoded_raw_text=oracle['text'],
        full_token_ids=oracle['token_ids'], segments=oracle['segments'])] + cases['cases']
    quote = lambda s: json.dumps(s, ensure_ascii=False)
    ids = lambda values: '{' + ','.join(map(str, values)) + '}'
    lines = ['// Generated offline from tracked JSON fixtures; do not edit.', '#pragma once',
             'namespace outloud::test {', 'inline const std::vector<Case> cases = {']
    for case in corpus:
        lines.append('{' + ','.join(quote(case[k]) for k in ('name', 'text', 'prepared_text')) + ',')
        lines.append(ids(case['raw_token_ids']) + ',' + quote(case['decoded_raw_text']) + ',' + ids(case['full_token_ids']) + ',{')
        for s in case['segments']:
            lines.append('{' + quote(s['text']) + ',' + ids(s['token_ids']) + ',' + str(s['frames_after_eos']) + '},')
        lines.append('}},')
    lines.extend(['};', '}'])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print(f'Exported {len(corpus)} native fixture records to {args.output}')


if __name__ == '__main__':
    main()
