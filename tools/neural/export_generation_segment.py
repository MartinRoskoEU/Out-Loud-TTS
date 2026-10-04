"""Offline Segment 1 capture using the untouched Pocket AR loop, without Mimi or threads."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import threading

os.environ['HF_HUB_OFFLINE'] = '1'
from export_flowlm_prefill import (ROOT, VOICE, COMPATIBILITY_ID, MODEL_SHA, TOKENIZER_SHA,
                                 VOICE_SHAS, build_model, require, sha_file)
import torch
from pocket_tts.models.model_state import _import_model_state
from pocket_tts.models.text_chunking import prepare_text_prompt
from pocket_tts.models.tts_model import TTSModel
from pocket_tts.utils.config import CONFIGS_DIR, load_config

DESTINATION = ROOT / 'generated/neural'


def capture(flow, segment):
    wrapper = TTSModel(flow, 0.3, 1, None, -4.0, load_config(CONFIGS_DIR / 'english.yaml'))
    prepared = flow.conditioner.prepare(segment['text'])
    require(prepared.reshape(-1).tolist() == segment['token_ids'], 'Canonical tokens changed')
    _, tail_guess = prepare_text_prompt(segment['text'], False, False, True, True, {})
    tail = tail_guess + 2
    maximum = wrapper._estimate_max_gen_len(prepared.shape[1])
    require(maximum == segment['max_gen_len'] == 109 and tail == segment['frames_after_eos'] == 3,
            'Canonical generation options changed')
    require(wrapper._MIN_FRAMES_BEFORE_EOS == 6, 'Source minimum EOS index changed')
    state = _import_model_state(VOICE, torch.device('cpu'))
    require(all(s['offset'].item() == 112 and s['pad'].item() == 0 for s in state.values()), 'Voice state changed')
    wrapper._expand_kv_cache(state, 112 + prepared.shape[1] + maximum)
    require(all(s['cache'].shape[2] == 241 for s in state.values()), 'Source capacity changed')
    saved, records, emitted = {}, [], []
    active = ['prefill']

    def save(name, value):
        value = value.detach().cpu().float().contiguous()
        saved[name] = value.reshape(-1).clone()

    def network_input(_, args):
        save(active[0] + '.hidden', args[0])
        save(active[0] + '.noise', args[-1])

    hooks = [flow.flow_net.register_forward_pre_hook(network_input),
             flow.flow_net.register_forward_hook(lambda _, args, output: save(active[0] + '.velocity', output)),
             flow.out_eos.register_forward_hook(lambda _, args, output: save(active[0] + '.eos', output))]
    original = wrapper._run_flow_lm_and_increment_step

    def observe_run(*args, **kwargs):
        prefill = 'text_tokens' in kwargs
        index = len(records)
        name = 'prefill' if prefill else f'candidate{index:02}'
        active[0] = name
        before = wrapper._flow_lm_current_end(state)
        if not prefill:
            input_ = kwargs['backbone_input_latents'].reshape(-1)
            if index == 0:
                require(torch.isnan(input_).all().item(), 'Source first candidate is not BOS')
                save(name + '.input', flow.bos_emb)
            else:
                require(torch.equal(input_.view(torch.int32), saved[f'candidate{index-1:02}.latent'].view(torch.int32)),
                        'Source feedback is not previous normalized latent')
                save(name + '.input', input_)
        latent, eos = original(*args, **kwargs)  # Source forward AND state increment, not a reimplemented loop.
        save(name + '.latent', latent)
        after = wrapper._flow_lm_current_end(state)
        require(all(s['offset'].item() == after for s in state.values()), 'Source layer offsets disagree')
        require(after - before == (20 if prefill else 1), 'Source increment changed')
        require(bool(eos.item()) == bool(saved[name + '.eos'].item() > -4.0), 'Source EOS comparison changed')
        require(torch.equal((saved[name + '.noise'] + saved[name + '.velocity']).view(torch.int32),
                            saved[name + '.latent'].view(torch.int32)), 'Source one-step LSD changed')
        record = {'index': index, 'offset_before': before, 'offset_after': after,
                  'input_kind': 'BOS' if index == 0 else 'previous_normalized_latent',
                  'eos_logit': saved[name + '.eos'].item(), 'eos': bool(eos.item()), 'emitted': False}
        if not prefill:
            records.append(record)
        return latent, eos

    # Instance wrapper observes the installed method; no site-packages files are edited.
    wrapper._run_flow_lm_and_increment_step = observe_run

    class Sink:
        def put(self, latent):
            if latent is None:
                return
            index = len(records) - 1
            require(torch.equal(latent.reshape(-1).view(torch.int32),
                                saved[f'candidate{index:02}.latent'].view(torch.int32)), 'Source emitted latent changed')
            records[index]['emitted'] = True
            emitted.append(latent.reshape(-1).clone())

    torch.manual_seed(73)  # Reset after loading assets, once for the whole segment.
    try:
        wrapper._run_flow_lm_and_increment_step(model_state=state, text_tokens=prepared)
        require(wrapper._flow_lm_current_end(state) == 132, 'Source prefill offset changed')
        # Execute the real installed AR scheduling sequentially; never start its worker/decoder threads.
        wrapper._autoregressive_generation(state, maximum, tail, Sink(), threading.Event())
    finally:
        for handle in hooks:
            handle.remove()
    latched = None
    for record in records:
        if record['eos'] and latched is None and record['index'] >= 6:
            latched = record['index']
        record['latched_eos_index'] = latched
        require(record['emitted'] == (latched is None or record['index'] < latched + tail), 'Source stop/emission order changed')
    require(latched == segment['first_accepted_eos_frame'] == 43 and len(records) == segment['candidate_calls'] == 47
            and len(emitted) == segment['emitted_frames'] == 46 and wrapper._flow_lm_current_end(state) == 179,
            'Canonical scheduling changed; do not replace the baseline')
    saved['noise.stream'] = torch.stack([saved['prefill.noise']] + [saved[f'candidate{i:02}.noise'] for i in range(len(records))])
    saved['emitted.latents'] = torch.stack(emitted)
    for layer in range(6):
        cache = state[f'transformer.layers.{layer}.self_attn']['cache']
        saved[f'final.layer{layer}.keys'] = cache[0, 0, 112:179].contiguous().clone()
        saved[f'final.layer{layer}.values'] = cache[1, 0, 112:179].contiguous().clone()
    return saved, records


def main():
    torch.set_num_threads(1)
    flow = build_model()
    mapping = json.loads((ROOT / 'reference/pipeline-baseline.json').read_text())
    segment = mapping['oracle']['segments'][0]
    with torch.inference_mode():
        saved, candidates = capture(flow, segment)
        repeated, repeat_candidates = capture(flow, segment)
    require(candidates == repeat_candidates and saved.keys() == repeated.keys() and
            all(torch.equal(v.reshape(-1).view(torch.int32), repeated[k].reshape(-1).view(torch.int32)) for k, v in saved.items()),
            'Generation reference is not bit-repeatable')
    prior = json.loads((ROOT / 'reference/flow-sampler-baseline.json').read_text())
    prior_hashes = {c['name']: c['sha256'] for c in prior['checkpoints']}
    for name, previous in [('prefill', 'prefill'), ('candidate00', 'bos')]:
        for field, old_field in [('hidden', 'input.hidden'), ('noise', 'input.noise'), ('velocity', 'velocity'), ('latent', 'latent')]:
            raw = saved[name + '.' + field].numpy().astype('<f4', copy=False).tobytes()
            require(hashlib.sha256(raw).hexdigest() == prior_hashes[previous + '.' + old_field], 'Frozen Step 2E case changed')
    DESTINATION.mkdir(parents=True, exist_ok=True)
    temporary = DESTINATION / 'generation-segment1-f32.bin.tmp'
    checkpoints = []
    with temporary.open('wb') as stream:
        for name, value in saved.items():
            require(torch.isfinite(value).all().item(), 'Nonfinite reference checkpoint')
            raw = value.numpy().astype('<f4', copy=False).tobytes()
            checkpoints.append({'name': name, 'shape': list(value.shape), 'dtype': 'float32',
                                'offset': stream.tell(), 'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()})
            stream.write(raw)
    metadata = {'schema_version': 1, 'pocket_tts_version': '3.3.0', 'torch_version': torch.__version__,
                'compatibility_id': COMPATIBILITY_ID, 'model_source_sha256': MODEL_SHA,
                'tokenizer_source_sha256': TOKENIZER_SHA, 'canonical_voice': 'gnome_male',
                'voice_source_sha256': VOICE_SHAS['gnome_male'], 'seed': 73, 'torch_threads': 1,
                'segment_text': segment['text'], 'token_ids': segment['token_ids'], 'voice_prefix': 112,
                'prefill_offset': 132, 'prefill_emitted': False, 'max_gen_len': 109, 'frames_after_eos': 3,
                'minimum_eos_index': 6, 'eos_threshold': -4.0, 'temperature': 0.3,
                'noise_clamp': None, 'sampler': 'lsd', 'decode_steps': 1, 'noise_draw_count': 48,
                'candidate_count': 47, 'emitted_count': 46, 'first_eos_index': 43, 'final_offset': 179,
                'logical_suffix_positions': 67, 'suffix_capacity': 129, 'repeat_bit_exact': True,
                'termination': 'eos_tail', 'candidates': candidates,
                'fixture': {'file': 'generation-segment1-f32.bin', 'bytes': temporary.stat().st_size, 'sha256': sha_file(temporary)},
                'checkpoints': checkpoints}
    baseline = ROOT / 'reference/generation-segment1-baseline.json'
    if baseline.exists() and json.loads(baseline.read_text()) != metadata:
        temporary.unlink()
        raise ValueError('Frozen generation metadata changed; refusing to overwrite baseline/fixture')
    if not baseline.exists():
        baseline.write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    temporary.replace(DESTINATION / metadata['fixture']['file'])
    lines = ['// Offline untouched Pocket AR sequence, externally supplied noise only.', '#pragma once',
             '#include <array>', '#include <cstdint>', '#include <string_view>', 'namespace outloud::test {',
             'struct GenerationCheckpoint { std::string_view name, sha; std::uint64_t offset, elements; };',
             'struct GenerationCandidate { std::uint32_t before, after; bool eos, emitted; int latched; };',
             f'inline constexpr std::uint64_t generation_fixture_bytes = {metadata["fixture"]["bytes"]};',
             f'inline constexpr std::string_view generation_fixture_sha = "{metadata["fixture"]["sha256"]}";',
             f'inline constexpr std::array<GenerationCheckpoint, {len(checkpoints)}> generation_checkpoints = {{{{']
    lines += ['    {"' + c['name'] + '", "' + c['sha256'] + '", ' + str(c['offset']) + ', ' + str(c['bytes'] // 4) + '},'
              for c in checkpoints]
    lines += ['}};', 'inline constexpr std::array<GenerationCandidate, 47> generation_candidates = {{']
    lines += [f'    {{{c["offset_before"]}, {c["offset_after"]}, {str(c["eos"]).lower()}, {str(c["emitted"]).lower()}, '
              + str(-1 if c['latched_eos_index'] is None else c['latched_eos_index']) + '},' for c in candidates]
    lines += ['}};', '} // namespace outloud::test']
    (DESTINATION / 'generation_segment_fixtures.hpp').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print(f'Offline generation: {len(checkpoints)} checkpoints, {metadata["fixture"]["bytes"]} bytes; repeat bits PASS')
    print('Noise 48 (1 prefill + 47 candidates); EOS 43; emitted 46; discarded candidate 46; final offset 179')
    print('Frozen Step 2D/2E inputs/results unchanged; source AR loop executed without Mimi/threads')


if __name__ == '__main__':
    main()
