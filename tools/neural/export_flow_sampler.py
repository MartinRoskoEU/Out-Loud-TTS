"""Capture two real upstream seed-73 noise draws and sampler checkpoints, offline only."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path

os.environ['HF_HUB_OFFLINE'] = '1'
from export_flowlm_prefill import (ROOT, VOICE, COMPATIBILITY_ID, MODEL_SHA, VOICE_SHAS,
                                 build_model, require, sha_file)
import torch
from pocket_tts.models.model_state import _import_model_state
from pocket_tts.modules.stateful_module import increment_steps

DESTINATION = ROOT / 'generated/neural'
CASES = ('prefill', 'bos')


def capture(model, tokens):
    saved = {}
    handles = []
    case = ['prefill']

    def save(name, value):
        value = value.detach().cpu().float().contiguous().reshape(-1).clone()
        saved[case[0] + '.' + name] = value

    def pre(module, name):
        handles.append(module.register_forward_pre_hook(lambda _, args: save(name, args[0])))

    def post(module, name):
        handles.append(module.register_forward_hook(lambda _, args, output: save(name, output)))

    def sampler_input(_, args):
        save('input.hidden', args[0])
        save('input.noise', args[-1])  # The actual noise allocated/drawn by FlowLM.forward.

    net = model.flow_net
    handles.append(net.register_forward_pre_hook(sampler_input))
    post(net.input_proj, 'input.projected')
    post(net.cond_embed, 'condition.projected')
    for i, label in enumerate(('s', 't')):
        p = f'time.{label}.'
        pre(net.time_embed[i], p + 'scalar')
        pre(net.time_embed[i].mlp, p + 'features')
        post(net.time_embed[i].mlp[0], p + 'linear1')
        post(net.time_embed[i].mlp[1], p + 'silu')
        post(net.time_embed[i].mlp[2], p + 'mlp')
        post(net.time_embed[i].mlp[3], p + 'normalized')

    def block_input(_, args, p):
        save(p + 'input', args[0])
        if p == 'block0.':
            save('condition.combined', args[1])

    def block_parameters(_, args, output, p):
        for name, value in zip(('shift', 'scale', 'gate'), output.chunk(3, dim=-1)):
            save(p + name, value)

    def block_output(_, args, output, p):
        prefix = case[0] + '.' + p
        gate, update = saved[prefix + 'gate'], saved[prefix + 'linear2']
        gated = gate * update  # Safe reconstruction of the exact source multiply.
        save(p + 'gated_update', gated)
        require(torch.equal(output.reshape(-1).view(torch.int32),
                            (args[0].reshape(-1) + gated).view(torch.int32)), 'Residual reconstruction differs')
        save(p + 'output', output)

    for i, block in enumerate(net.res_blocks):
        p = f'block{i}.'
        handles.append(block.register_forward_pre_hook(lambda m, args, p=p: block_input(m, args, p)))
        post(block.in_ln, p + 'norm')
        post(block.adaLN_modulation[0], p + 'condition_silu')
        handles.append(block.adaLN_modulation[1].register_forward_hook(
            lambda m, args, output, p=p: block_parameters(m, args, output, p)))
        pre(block.mlp[0], p + 'modulated')
        post(block.mlp[0], p + 'linear1')
        post(block.mlp[1], p + 'silu')
        post(block.mlp[2], p + 'linear2')
        handles.append(block.register_forward_hook(lambda m, args, output, p=p: block_output(m, args, output, p)))
    post(net.final_layer.norm_final, 'final.norm')
    post(net.final_layer.adaLN_modulation[0], 'final.condition_silu')
    def final_parameters(_, args, output):
        shift, scale = output.chunk(2, dim=-1)
        save('final.shift', shift)
        save('final.scale', scale)

    handles.append(net.final_layer.adaLN_modulation[1].register_forward_hook(final_parameters))
    pre(net.final_layer.linear, 'final.modulated')
    post(net, 'velocity')
    state = _import_model_state(VOICE, torch.device('cpu'))
    for s in state.values():
        require(s['offset'].item() == 112 and s['pad'].item() == 0, 'Wrong source voice prefix')
        s['cache'] = torch.cat((s['cache'], torch.zeros(2, 1, 129, 16, 64)), dim=2)  # Canonical capacity 241.
    torch.manual_seed(73)  # After model/voice load, as in the frozen oracle.
    rng_states = [hashlib.sha256(torch.get_rng_state().numpy().tobytes()).hexdigest()]
    try:
        embedding = model.conditioner(torch.tensor(tokens).view(1, -1))
        latent, _ = model(torch.empty(1, 0, 32), embedding, state, 1, 0.3, None, -4.0)
        save('latent', latent)
        increment_steps(model, state, 20)
        require(all(s['offset'].item() == 132 for s in state.values()), 'Wrong post-prefill offset')
        rng_states.append(hashlib.sha256(torch.get_rng_state().numpy().tobytes()).hexdigest())
        case[0] = 'bos'
        latent, _ = model(torch.full((1, 1, 32), float('nan')), torch.empty(1, 0, 1024), state,
                          1, 0.3, None, -4.0)
        save('latent', latent)
        increment_steps(model, state, 1)
        require(all(s['offset'].item() == 133 for s in state.values()), 'Wrong BOS offset')
        rng_states.append(hashlib.sha256(torch.get_rng_state().numpy().tobytes()).hexdigest())
    finally:
        for handle in handles:
            handle.remove()
    return saved, rng_states


def main():
    torch.set_num_threads(1)
    model = build_model()  # Reuse pinned local upstream construction, not its fixture writer.
    previous = json.loads((ROOT / 'reference/flowlm-prefill-baseline.json').read_text(encoding='utf-8'))
    with torch.inference_mode():
        saved, rng_states = capture(model, previous['token_ids'])
        repeated, repeated_rng = capture(model, previous['token_ids'])
    require(saved.keys() == repeated.keys() and rng_states == repeated_rng and
            all(torch.equal(v.view(torch.int32), repeated[k].view(torch.int32)) for k, v in saved.items()),
            'Sampler capture is not bit-repeatable')
    previous_hashes = {c['name']: c['sha256'] for c in previous['checkpoints']}
    for case, old_name in zip(CASES, ('final.last_hidden', 'single.last_hidden')):
        raw = saved[case + '.input.hidden'].numpy().astype('<f4', copy=False).tobytes()
        require(hashlib.sha256(raw).hexdigest() == previous_hashes[old_name], 'Frozen Step 2D hidden changed')
        noise, velocity, latent = (saved[case + '.' + name] for name in ('input.noise', 'velocity', 'latent'))
        require(torch.equal((noise + velocity).view(torch.int32), latent.view(torch.int32)), 'One-step LSD differs')
        y = saved[case + '.condition.projected']
        s, t = saved[case + '.time.s.normalized'], saved[case + '.time.t.normalized']
        require(torch.equal((y + (s + t) / 2).view(torch.int32),
                            saved[case + '.condition.combined'].view(torch.int32)), 'Time averaging differs')
    DESTINATION.mkdir(parents=True, exist_ok=True)
    temporary = DESTINATION / 'flow-sampler-f32.bin.tmp'
    checkpoints = []
    with temporary.open('wb') as stream:
        for name, value in saved.items():
            require(torch.isfinite(value).all().item(), 'Nonfinite sampler checkpoint')
            raw = value.numpy().astype('<f4', copy=False).tobytes()
            checkpoints.append({'name': name, 'shape': list(value.shape), 'dtype': 'float32',
                'offset': stream.tell(), 'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()})
            stream.write(raw)
    checkpoint_by_name = {c['name']: c for c in checkpoints}
    metadata = {'schema_version': 1, 'pocket_tts_version': '3.3.0', 'torch_version': torch.__version__,
        'compatibility_id': COMPATIBILITY_ID, 'model_source_sha256': MODEL_SHA,
        'canonical_voice': 'gnome_male', 'voice_source_sha256': VOICE_SHAS['gnome_male'], 'segment_text': previous['segment_text'],
        'token_ids': previous['token_ids'], 'seed': 73, 'temperature': 0.3, 'noise_std': 0.3**0.5,
        'noise_clamp': None, 'sampler_type': model.flow_type, 'decode_steps': 1, 's': 0.0, 't': 1.0,
        'dtype': 'float32', 'device': 'cpu', 'torch_threads': 1, 'repeat_bit_exact': True,
        'rng_state_hashes_before_after_draws': rng_states,
        'architecture': {'input': 32, 'condition': 1024, 'width': 512, 'blocks': 6,
                         'time_features': 256, 'time_norm_correction': 1, 'time_norm_eps': 1e-5,
                         'block_norm_correction': 0, 'block_norm_eps': 1e-6},
        'cases': [{'name': case, 'noise_draw_index': i, 'offset': 132 + i,
                   'hidden_sha256': checkpoint_by_name[case + '.input.hidden']['sha256'],
                   'noise_sha256': checkpoint_by_name[case + '.input.noise']['sha256'],
                   'velocity_sha256': checkpoint_by_name[case + '.velocity']['sha256'],
                   'latent_sha256': checkpoint_by_name[case + '.latent']['sha256']} for i, case in enumerate(CASES)],
        'fixture': {'file': 'flow-sampler-f32.bin', 'bytes': temporary.stat().st_size, 'sha256': sha_file(temporary)},
        'checkpoints': checkpoints}
    baseline = ROOT / 'reference/flow-sampler-baseline.json'
    if baseline.exists() and json.loads(baseline.read_text(encoding='utf-8')) != metadata:
        temporary.unlink()
        raise ValueError('Frozen sampler reference changed; refusing to overwrite baseline/fixtures')
    if not baseline.exists():
        baseline.write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    temporary.replace(DESTINATION / 'flow-sampler-f32.bin')
    lines = ['// Offline untouched-upstream sampler fixtures, fixed noise; no native RNG.', '#pragma once',
        '#include <array>', '#include <cstdint>', '#include <string_view>', 'namespace outloud::test {',
        'struct SamplerCheckpoint { std::string_view name, sha; std::uint64_t offset, elements; };',
        f'inline constexpr std::uint64_t sampler_fixture_bytes = {metadata["fixture"]["bytes"]};',
        f'inline constexpr std::string_view sampler_fixture_sha = "{metadata["fixture"]["sha256"]}";',
        f'inline constexpr std::array<SamplerCheckpoint, {len(checkpoints)}> sampler_checkpoints = {{{{']
    lines += ['    {"' + c['name'] + '", "' + c['sha256'] + '", ' + str(c['offset']) + ', '
              + str(c['bytes'] // 4) + '},' for c in checkpoints]
    lines += ['}};', '} // namespace outloud::test']
    (DESTINATION / 'flow_sampler_fixtures.hpp').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print(f'Offline sampler: {len(checkpoints)} checkpoints, {metadata["fixture"]["bytes"]} bytes; two actual seed-73 draws')
    print('Hidden hashes match frozen Step 2D; repeat bits: PASS; one-step LSD identity: PASS; offsets 132/133')


if __name__ == '__main__':
    main()
