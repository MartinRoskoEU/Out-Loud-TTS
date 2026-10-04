"""Regenerate twelve text-only fixtures using untouched, installed Pocket 3.3.0 offline."""
from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path

os.environ['HF_HUB_OFFLINE'] = '1'
ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tokenizer', type=Path, default=Path.home() / '.cache/huggingface/hub'
        / 'models--kyutai--pocket-tts-without-voice-cloning/snapshots'
        / '00eac05ed3d16bdc3f6b5d598874019c34a89214/languages/english/tokenizer.json')
    parser.add_argument('--output', type=Path, default=ROOT / 'reference/text-pipeline-cases.json')
    args = parser.parse_args()
    sha = hashlib.sha256(args.tokenizer.read_bytes()).hexdigest()
    if sha != 'f498428e1eafee50492f7be13dc9bfafcfc12e508cd0eb1b01c92ecd5d8c6687':
        raise ValueError('Incorrect tokenizer identity')
    if importlib.metadata.version('pocket-tts') != '3.3.0':
        raise ValueError('Fixtures require installed Pocket TTS 3.3.0')
    if importlib.metadata.version('tokenizers') != '0.23.2':
        raise ValueError('Fixtures require installed tokenizers 0.23.2')
    from pocket_tts.modules.text_conditioner import JsonTokenizer
    from pocket_tts.models.text_chunking import prepare_text_prompt, split_into_best_sentences
    tokenizer = JsonTokenizer(4000, str(args.tokenizer))  # Explicit local path, no HF resolver call.
    examples = [
        ('simple', 'the guard needs your help'),
        ('question', 'Where is the missing scout?'),
        ('exclamation', 'For the Alliance!'),
        ('grouped_sentences', 'Wait... What?! Follow me. Stay close!'),
        ('whitespace', '\u00a0  meet\r\n   the\t scout    at dawn  \u2003'),
        ('weak_punctuation', 'Bring herbs, ore; and cloth:\u2014\u201d'),
        ('apostrophes', "Don't forget the captain's orders. The scout\u2019s waiting."),
        ('decimal', 'Deliver 3.5 pounds of ore for 12 gold. Bring 2.75 more if you can.'),
        ('long_quest', 'Before the next caravan reaches the abandoned watchtower you must follow the river '
         'past the broken bridge and speak with the wounded scout who waits beside the old gate, '
         'recover his report from the ruined camp and carry it safely through the forest; '
         'then return to the watch captain in the village and tell him everything you have learned: '
         'the road must be secured before nightfall and every traveler depends on your courage.'),
        ('utf8', '\u00e9lan guides the mage\u2014seek the runestone\u2026 \U0001f9d9\U0001f3fd \u6771\u4eac'),
        ('special_tokens', 'Speak <s>now</s>, then <unk> return <pad> home.'),
        ('unicode_upper', '\u00dfharp runes mark the entrance. e\u0301lan guards the doorway.)'),
    ]
    cases = []
    for name, text in examples:
        prepared, _ = prepare_text_prompt(text, False, False)
        segments = []
        for chunk in split_into_best_sentences(tokenizer, text, 50, False, False):
            segment, tail = prepare_text_prompt(chunk, False, False)
            segments.append({'text': segment, 'token_ids': tokenizer.encode(segment), 'frames_after_eos': tail + 2})
        raw_ids = tokenizer.encode(text)
        cases.append({'name': name, 'text': text, 'prepared_text': prepared,
            'raw_token_ids': raw_ids, 'decoded_raw_text': tokenizer.decode(raw_ids),
            'full_token_ids': tokenizer.encode(prepared), 'segments': segments})
    result = {'schema_version': 1, 'pocket_tts_version': '3.3.0',
        'tokenizers_version': importlib.metadata.version('tokenizers'), 'tokenizer_sha256': sha,
        'max_tokens': 50, 'cases': cases}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Compact ID lists keep this fixture readable and small.
    formatted = json.dumps(result, ensure_ascii=False, indent=2)
    import re
    formatted = re.sub(r'\[\s*((?:\d+,\s*)*\d+)\s*\]',
        lambda m: '[' + ', '.join(re.findall(r'\d+', m[1])) + ']', formatted)
    args.output.write_text(formatted + '\n', encoding='utf-8')
    print(f'Wrote {len(cases)} untouched-upstream regression cases to {args.output}')


if __name__ == '__main__':
    main()
