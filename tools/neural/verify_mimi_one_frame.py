"""Optional offline upstream-only oracle verification; no native orchestration."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import sys

os.environ['HF_HUB_OFFLINE'] = '1'
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/assets'))
from asset_format import MODEL_INPUT, MODEL_SHA, sha_file
import torch
from pocket_tts import TTSModel


def main():
    if sha_file(MODEL_INPUT) != MODEL_SHA:
        raise ValueError('Wrong pinned local model')
    protected = json.loads((ROOT / 'reference/pipeline-baseline.json').read_text())['oracle']
    decoder = json.loads((ROOT / 'reference/mimi-decoder-baseline.json').read_text())
    fixture = ROOT / 'generated/neural' / decoder['fixture']['file']
    if sha_file(fixture) != decoder['fixture']['sha256']:
        raise ValueError('Wrong Mimi fixture identity')
    model = TTSModel.load_model(language='english', temp=0.3, sampler_decode_steps=1, quantize=False)
    model.max_decoder_frames_per_call = 1  # Instance option; no source/oracle edits.
    voice = model.get_state_for_audio_prompt(str(ROOT / 'voices/gnome_male.safetensors'))
    torch.manual_seed(73)
    chunks = list(model.generate_audio_stream(voice, protected['text']))
    audio = torch.cat(chunks).numpy().astype('<f4', copy=False)
    digest = hashlib.sha256(audio.tobytes()).hexdigest()
    if digest != protected['expected_float32_sha256'] or len(chunks) != 170 or audio.size != 326400:
        raise ValueError('Protected one-frame upstream diagnostic changed')
    checkpoint = next(c for c in decoder['checkpoints'] if c['name'] == 'pcm.complete')
    with fixture.open('rb') as stream:
        stream.seek(checkpoint['offset'])
        segment = stream.read(checkpoint['bytes'])
    if audio[:88320].tobytes() != segment:
        raise ValueError('Decoder fixture differs from protected one-frame diagnostic prefix')
    result = {'full_float32_sha256': digest, 'chunks': len(chunks), 'samples': int(audio.size),
              'segment1_sha256': hashlib.sha256(segment).hexdigest(), 'segment1_bit_exact': True}
    (ROOT / 'generated/neural/mimi-one-frame-oracle-verification.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('Protected one-frame upstream oracle and exact Segment 1 prefix PASS:', result)


if __name__ == '__main__':
    main()
