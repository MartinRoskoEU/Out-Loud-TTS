"""Offline developer fixture preparation. Prepares only scratch audio for native smoke validation."""
from __future__ import annotations
import argparse
import importlib.metadata
import json
import os
from pathlib import Path

os.environ['HF_HUB_OFFLINE'] = '1'
os.environ['HF_HUB_DISABLE_TELEMETRY'] = '1'

from asset_format import ROOT, MODEL_SHA, sha_file, schema


def prepare(weights, tokenizer, audio, output):
    import yaml
    from export_voice import export
    from pocket_tts import TTSModel, export_model_state
    import torch
    contract = schema()
    expected_tokenizer = json.loads((ROOT / 'reference/pipeline-baseline.json').read_text())['tokenizer']['sha256']
    if sha_file(tokenizer) != expected_tokenizer:
        raise ValueError('Preparation tokenizer differs from the reviewed native tokenizer')
    if importlib.metadata.version('pocket-tts') != contract['pocket_version']:
        raise ValueError('Voice preparation requires the reviewed Pocket TTS implementation version')
    if audio.suffix.lower() not in ('.wav', '.flac') or not audio.is_file():
        raise ValueError('Verified preparation requires original local reference audio, not a prepared voice')
    if sha_file(weights) != MODEL_SHA:
        raise ValueError('Preparation weights do not match active verified source identity')
    if not output.resolve().is_relative_to((ROOT / 'generated/assets').resolve()) or output.exists():
        raise ValueError('Preparation output must be a new developer asset under generated/assets')
    output.parent.mkdir(parents=True, exist_ok=True)
    config = yaml.safe_load((ROOT / 'reference/model-config.yaml').read_text())
    config['weights_path'] = str(weights.resolve())
    config.pop('weights_path_without_voice_cloning', None)  # no fallback weights
    config['flow_lm']['lookup_table']['tokenizer_path'] = str(tokenizer.resolve())
    local_config = output.with_suffix('.yaml')
    local_config.write_text(yaml.safe_dump(config), encoding='utf-8')
    audio_hash = sha_file(audio)
    torch.set_num_threads(2)
    with torch.inference_mode():
        model = TTSModel.load_model(config=local_config)
        state = model.get_state_for_audio_prompt(audio.resolve())
        source = output.with_suffix('.safetensors')
        export_model_state(state, source)
    source_hash = sha_file(source)
    export(source, output, source_hash)
    if sha_file(weights) != MODEL_SHA or sha_file(audio) != audio_hash or sha_file(tokenizer) != expected_tokenizer:
        raise ValueError('Preparation input changed during execution')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--weights', type=Path, required=True)
    parser.add_argument('--tokenizer', type=Path, required=True)
    parser.add_argument('--audio', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    prepare(args.weights, args.tokenizer, args.audio, args.output)
