"""Offline full-utterance reference; reuse frozen metadata, never replace baselines."""
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
import numpy as np
import torch
from pocket_tts import TTSModel
from pocket_tts.models.text_chunking import prepare_text_prompt, split_into_best_sentences
from pocket_tts.modules.text_conditioner import JsonTokenizer

DEST = ROOT / 'generated/orchestration'


def require(value, message):
    if not value:
        raise ValueError(message)


def main():
    baseline = json.loads((ROOT / 'reference/pipeline-baseline.json').read_text())
    oracle = baseline['oracle']
    sources = ['models/tts_model.py','models/text_chunking.py','modules/text_conditioner.py',
               'models/flow_lm.py','modules/stateful_module.py','config/english.yaml']
    package = ROOT/'.venv/Lib/site-packages/pocket_tts'
    source_hashes = {str(package/p):sha_file(package/p) for p in sources}
    require(torch.__version__ == '2.14.1+cpu', 'Wrong Torch reference')
    require(sha_file(MODEL_INPUT) == MODEL_SHA, 'Wrong local model')
    DEST.mkdir(parents=True, exist_ok=True)
    # Existing protected metadata already records the canonical per-segment
    # schedule. Capture one normal correctness run only when its full FP32 PCM
    # is unavailable. This is never an upstream performance campaign.
    audio_path = DEST / 'upstream-full-canonical.f32'
    if not audio_path.exists() or sha_file(audio_path) != oracle['expected_float32_sha256']:
        existing = ROOT / 'generated/pocket_oracle_gnome.f32'
        if existing.exists() and sha_file(existing) == oracle['expected_float32_sha256']:
            audio_path.write_bytes(existing.read_bytes())
        else:
            model = TTSModel.load_model(language='english', temp=.3, sampler_decode_steps=1, quantize=False)
            model.max_decoder_frames_per_call = 1
            voice = model.get_state_for_audio_prompt(str(ROOT / 'voices/gnome_male.safetensors'))
            noise, schedules = [], []
            calls = []
            original = model._generate_audio_stream_short_text
            def observe_flow(module, args, kwargs, output):
                calls.append(bool(output[1].item()))
            def observe_noise(module, args):
                noise.append(args[-1].detach().cpu().contiguous().numpy().astype('<f4', copy=False).tobytes())
            def observe_segment(**kwargs):
                start = len(noise)
                calls.clear()
                frames = 0
                for chunk in original(**kwargs):
                    frames += 1
                    yield chunk
                first_eos = next(i for i, eos in enumerate(calls[1:]) if i >= 6 and eos)
                schedules.append(dict(first_draw=start, draws=len(noise)-start, candidates=len(calls)-1,
                                      eos=first_eos, frames=frames, samples=frames*1920))
            hooks = [model.flow_lm.register_forward_hook(observe_flow, with_kwargs=True),
                     model.flow_lm.flow_net.register_forward_pre_hook(observe_noise)]
            model._generate_audio_stream_short_text = observe_segment
            torch.manual_seed(73) # exactly once after model/voice loading
            try:
                chunks = list(model.generate_audio_stream(voice, oracle['text']))
            finally:
                for hook in hooks:
                    hook.remove()
                model._generate_audio_stream_short_text = original
            audio = torch.cat(chunks).numpy().astype('<f4', copy=False).tobytes()
            require(len(chunks) == 170 and len(audio) == 326400*4 and
                    hashlib.sha256(audio).hexdigest() == oracle['expected_float32_sha256'], 'Full oracle changed')
            rng = (ROOT / 'generated/rng/rng-reference.bin').read_bytes()
            require(len(noise) == 176 and b''.join(noise) == rng[:176*128], 'Utterance RNG progression changed')
            for s, expected in zip(schedules, oracle['segments'], strict=True):
                require(s['candidates'] == expected['candidate_calls'] and s['eos'] == expected['first_accepted_eos_frame']
                        and s['frames'] == expected['emitted_frames'], 'Upstream segment schedule changed')
            audio_path.write_bytes(audio)
            (DEST / 'upstream-observed-schedule.json').write_text(json.dumps(schedules, indent=2)+'\n', encoding='utf-8')
            del model, voice
    # Validate preparation edge behavior without any neural generation.
    from huggingface_hub import hf_hub_download
    tokenizer_path = hf_hub_download('kyutai/pocket-tts-without-voice-cloning', 'languages/english/tokenizer.json',
        revision=baseline['tokenizer']['revision'], local_files_only=True)
    tokenizer = JsonTokenizer(4000, str(tokenizer_path))
    options = (False, False, True, True, {})
    chunks = split_into_best_sentences(tokenizer, oracle['text'], 50, *options)
    require(chunks == [s['text'] for s in oracle['segments']], 'Protected segments changed')
    cases = []
    for label, text in [('empty',''),('whitespace',' \t\n\u00a0'),('short','Hello there.'),
                        ('at_limit',' '.join(['The']*49)+'.'),('over_limit',' '.join(['The']*50)+'.')]:
        try:
            prepared, _ = prepare_text_prompt(text, *options)
            split = split_into_best_sentences(tokenizer, text, 50, *options)
            parts = []
            for chunk in split:
                p, tail = prepare_text_prompt(chunk, *options)
                parts.append(dict(text=p, ids=tokenizer(p)[0].tolist(), tail=tail+2))
            cases.append(dict(label=label,text=text,prepared=prepared,ids=tokenizer(prepared)[0].tolist(),segments=parts,error=False))
        except ValueError:
            cases.append(dict(label=label,text=text,error=True))
    require(len(cases[-2]['ids']) == 50 and len(cases[-1]['ids']) == 51, 'Limit probes must be exact')
    metadata = dict(text=oracle['text'],input_tokens=75,seed=73,segments=[],total_draws=176,
                    frames=170,samples=326400,pcm_sha256=oracle['expected_float32_sha256'],edge_cases=cases)
    draw = 0
    for s in oracle['segments']:
        ids = tokenizer(s['text'])[0].tolist()
        require(ids == s['token_ids'], 'Protected token IDs changed')
        metadata['segments'].append(dict(text=s['text'],ids=ids,first_draw=draw,draws=s['candidate_calls']+1,
            candidates=s['candidate_calls'],eos=s['first_accepted_eos_frame'],frames=s['emitted_frames'],
            samples=s['emitted_frames']*1920,tail=s['frames_after_eos']))
        draw += s['candidate_calls']+1
    require(draw == 176, 'Frozen draw accounting changed')
    metadata['source_sha256'] = source_hashes
    require(all(sha_file(Path(p)) == h for p,h in metadata['source_sha256'].items()), 'Installed source changed')
    (DEST/'orchestration-reference.json').write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf-8')
    lines = ['// Development only, derived from unchanged frozen pipeline metadata.', '#pragma once',
             '#include <array>','#include <string_view>','#include <cstdint>','namespace outloud::test {',
             'inline constexpr std::string_view full_text = '+json.dumps(oracle['text'])+';',
             'inline constexpr std::string_view at_limit_text = '+json.dumps(cases[-2]['text'])+';',
             'inline constexpr std::string_view over_limit_text = '+json.dumps(cases[-1]['text'])+';',
             'struct SegmentOracle { std::string_view text; std::uint32_t tokens, first_draw, draws, candidates, eos, frames, samples, tail; };',
             'inline constexpr std::array<SegmentOracle,3> segment_oracles = {{']
    for s in metadata['segments']:
        values = [len(s['ids'])]+[s[k] for k in ('first_draw','draws','candidates','eos','frames','samples','tail')]
        lines.append('    {'+json.dumps(s['text'])+','+','.join(map(str,values))+'},')
    lines += ['}};']
    for i,s in enumerate(metadata['segments']):
        lines.append(f'inline constexpr std::array<std::uint32_t,{len(s["ids"])}> segment_ids_{i} = {{'+','.join(map(str,s['ids']))+'};')
    lines.append('inline constexpr std::array<std::uint32_t,75> full_ids = {'+','.join(map(str,oracle['token_ids']))+'};')
    lines += ['} // namespace outloud::test','']
    (DEST/'orchestration_reference.hpp').write_text('\n'.join(lines),encoding='utf-8')
    # Existing shared listening conversion: float32 is authoritative.
    import wave
    pcm = np.frombuffer(audio_path.read_bytes(),dtype='<f4')
    encoded = np.rint(np.clip(pcm.astype(np.float64),-1,1)*32767).astype('<i2')
    wav = ROOT/'generated/audio/upstream-full-canonical.wav'
    wav.parent.mkdir(parents=True,exist_ok=True)
    with wave.open(str(wav),'wb') as f:
        f.setnchannels(1); f.setsampwidth(2); f.setframerate(24000); f.writeframes(encoded.tobytes())
    print('PASS: frozen full oracle and schedule; continuous 176-draw stream; 170 frames / 326400 samples.')


if __name__ == '__main__':
    main()
