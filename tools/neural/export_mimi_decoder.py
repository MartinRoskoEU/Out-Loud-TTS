"""Offline decoder-only capture from unchanged installed Pocket TTS 3.3.0."""
from __future__ import annotations

import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import sys

os.environ['HF_HUB_OFFLINE'] = '1'
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/assets'))
from asset_format import COMPATIBILITY_ID, MODEL_INPUT, MODEL_SHA, sha_file
import numpy as np
import torch
from safetensors import safe_open
from pocket_tts.models.mimi import build_mimi
from pocket_tts.modules.conv import StreamingConv1d, StreamingConvTranspose1d
from pocket_tts.modules.stateful_module import StatefulModule, init_states, increment_steps
from pocket_tts.utils.config import CONFIGS_DIR, load_config

DEST = ROOT / 'generated/neural'
FRAMES = (0, 1, 2, 23, 45)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def build_decoder():
    require(importlib.metadata.version('pocket-tts') == '3.3.0', 'Wrong Pocket version')
    require(sha_file(MODEL_INPUT) == MODEL_SHA, 'Wrong pinned local model')
    config = load_config(CONFIGS_DIR / 'english.yaml')
    with torch.device('meta'):
        mimi = build_mimi(config.mimi)
    # These unused meta modules never allocate CPU weights or execute.
    del mimi.encoder, mimi.encoder_transformer, mimi.downsample
    with safe_open(MODEL_INPUT, framework='pt', device='cpu') as source:
        weights = {name: source.get_tensor('mimi.' + name).float() for name in mimi.state_dict()}
        mean = source.get_tensor('flow_lm.emb_mean').float()
        std = source.get_tensor('flow_lm.emb_std').float()
    require(len(weights) == 44, 'Decoder tensor subset changed')
    mimi.load_state_dict(weights, strict=True, assign=True)
    mimi.eval()
    for name, module in mimi.named_modules():
        if isinstance(module, StatefulModule):
            module._module_absolute_name = name
    return mimi, mean, std


def read_latents():
    meta = json.loads((ROOT / 'reference/generation-segment1-baseline.json').read_text())
    path = DEST / meta['fixture']['file']
    require(sha_file(path) == meta['fixture']['sha256'], 'Frozen generation blob changed')
    c = next(c for c in meta['checkpoints'] if c['name'] == 'emitted.latents')
    with path.open('rb') as stream:
        stream.seek(c['offset'])
        raw = stream.read(c['bytes'])
    require(hashlib.sha256(raw).hexdigest() == c['sha256'] and c['shape'] == [46, 32], 'Frozen latents changed')
    return torch.from_numpy(np.frombuffer(raw, dtype='<f4').copy()).reshape(46, 32), c['sha256']


def capture(mimi, mean, std, latents, hooks_enabled=True):
    saved = {}
    handles = []
    active = [0, '']
    state = init_states(mimi, 1, 1744)

    def save(name, value):
        if active[0] in FRAMES and hooks_enabled:
            saved[f'frame{active[0]:02}.' + name] = value.detach().float().contiguous().clone().squeeze(0)

    def pre(module, name):
        handles.append(module.register_forward_pre_hook(lambda _, args: save(name, args[0])))

    def post(module, name):
        handles.append(module.register_forward_hook(lambda _, args, output: save(name, output)))

    configs = []
    schema = json.loads((ROOT / 'reference/pocket-model-schema.json').read_text())
    ids = {t['name']: t['id'] for t in schema['tensors']}
    for name, module in mimi.named_modules():
        if isinstance(module, (StreamingConv1d, StreamingConvTranspose1d)):
            tr = isinstance(module, StreamingConvTranspose1d)
            op = module.convtr if tr else module.conv
            suffix = '.convtr' if tr else '.conv'
            key = 'partial' if tr else 'previous'
            configs.append({'name': name, 'transpose': tr, 'in_channels': op.in_channels,
                            'out_channels': op.out_channels, 'kernel': op.kernel_size[0],
                            'stride': op.stride[0], 'dilation': op.dilation[0], 'groups': op.groups,
                            'pad_mode': None if tr else module.pad_mode,
                            'weight_id': ids['mimi.' + name + suffix + '.weight'],
                            'bias_id': -1 if op.bias is None else ids['mimi.' + name + suffix + '.bias']})
            pre(module, name + '.input')
            handles.append(module.register_forward_pre_hook(
                lambda _, args, name=name, key=key: save(name + '.state_before', state[name][key])))
            post(module, name + '.output')
            handles.append(module.register_forward_hook(
                lambda _, args, output, name=name, key=key: save(name + '.state_after', state[name][key])))
        elif name == 'quantizer.output_proj':
            pre(module, 'projection.input')
            post(module, 'projection.output')

    transformer = mimi.decoder_transformer.transformer
    pre(transformer, 'transformer.input')
    post(transformer, 'transformer.output')
    def rotated(_, args, output):
        save(active[1] + 'q_rope', output[0])
        save(active[1] + 'k_rope', output[1])
    handles.append(transformer.rope.register_forward_hook(rotated))
    for i, layer in enumerate(transformer.layers):
        p = f'layer{i}.'
        handles.append(layer.register_forward_pre_hook(lambda _, args, p=p: active.__setitem__(1, p)))
        pre(layer, p + 'input')
        for module, field in [(layer.norm1, 'norm1'), (layer.self_attn.in_proj, 'qkv'),
                              (layer.self_attn.out_proj, 'attention_update'), (layer.layer_scale_1, 'attention_scale'),
                              (layer.norm2, 'norm2'), (layer.linear1, 'linear1'), (layer.linear2, 'ffn_update'),
                              (layer.layer_scale_2, 'ffn_scale'), (layer, 'output')]:
            post(module, p + field)
        handles.append(layer.self_attn.in_proj.register_forward_hook(
            lambda _, args, output, p=p: save(p + 'v', output.reshape(1, 16, 3, 8, 64)[:, :, 2])))
        pre(layer.self_attn.out_proj, p + 'attention_context')
        pre(layer.norm2, p + 'attention_residual')
        pre(layer.linear2, p + 'gelu')
    for index in [3, 6, 9]:
        post(mimi.decoder.model[index], f'decoder.model.{index}.output')

    pcm = []
    try:
        for i, latent in enumerate(latents):
            active[0] = i
            save('normalized', latent.reshape(1, 32))
            z = latent.reshape(1, 1, 32) * std + mean
            save('denormalized', z)
            audio = mimi.decode_from_latent(z, state)
            require(audio.shape == (1, 1, 1920), 'One-frame output length changed')
            increment_steps(mimi, state, 16)
            require(all(s['offset'].item() == (i + 1) * 16 for s in state.values() if 'offset' in s), 'Offset changed')
            save('pcm', audio)
            for layer in range(2):
                kv = state[f'decoder_transformer.transformer.layers.{layer}.self_attn']['cache']
                save(f'layer{layer}.cache_k', kv[0, :, :16*(i+1)])
                save(f'layer{layer}.cache_v', kv[1, :, :16*(i+1)])
            pcm.append(audio.reshape(-1).clone())
    finally:
        for handle in handles:
            handle.remove()
    saved['normalized.latents'] = latents.clone()
    saved['pcm.complete'] = torch.cat(pcm)
    return saved, configs


def main():
    torch.set_num_threads(1)
    rng = torch.get_rng_state().clone()
    mimi, mean, std = build_decoder()
    latents, latent_sha = read_latents()
    with torch.inference_mode():
        saved, configs = capture(mimi, mean, std, latents)
        repeated, _ = capture(mimi, mean, std, latents)
    require(torch.equal(rng, torch.get_rng_state()), 'Decoder consumed RNG')
    require(saved.keys() == repeated.keys() and all(torch.equal(v.view(torch.int32), repeated[k].view(torch.int32))
            for k, v in saved.items()), 'Decoder checkpoints not bit-repeatable')
    DEST.mkdir(parents=True, exist_ok=True)
    temporary = DEST / 'mimi-decoder-f32.bin.tmp'
    checkpoints = []
    with temporary.open('wb') as stream:
        for name, value in saved.items():
            require(torch.isfinite(value).all().item(), 'Nonfinite reference')
            raw = value.numpy().astype('<f4', copy=False).tobytes()
            checkpoints.append({'name': name, 'shape': list(value.shape), 'dtype': 'float32',
                                'offset': stream.tell(), 'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()})
            stream.write(raw)
    pcm_sha = checkpoints[-1]['sha256']
    meta = {'schema_version': 1, 'compatibility_id': COMPATIBILITY_ID, 'model_source_sha256': MODEL_SHA,
            'canonical_latents_sha256': latent_sha, 'frame_count': 46, 'sample_rate': 24000,
            'pcm_sample_count': 88320, 'pcm_float32_sha256': pcm_sha,
            'architecture': {'latent_dim': 32, 'dimension': 512, 'layers': 2, 'heads': 8, 'head_dim': 64,
                             'ffn': 2048, 'context': 250, 'steps_per_latent': 16, 'samples_per_latent': 1920,
                             'ratios': [6, 5, 4], 'cache_capacity': 1744, 'convolutions': configs},
            'fixture': {'file': 'mimi-decoder-f32.bin', 'bytes': temporary.stat().st_size, 'sha256': sha_file(temporary)},
            'checkpoints': checkpoints}
    baseline = ROOT / 'reference/mimi-decoder-baseline.json'
    if baseline.exists() and json.loads(baseline.read_text()) != meta:
        temporary.unlink()
        raise ValueError('Frozen Mimi reference changed; refusing replacement')
    if not baseline.exists():
        baseline.write_text(json.dumps(meta, indent=2) + '\n', encoding='utf-8')
    temporary.replace(DEST / meta['fixture']['file'])
    lines = ['#pragma once', '#include <array>', '#include <cstdint>', '#include <string_view>', 'namespace outloud::test {',
             'struct MimiCheckpoint { std::string_view name, sha; std::uint64_t offset, elements; };',
             f'inline constexpr std::uint64_t mimi_fixture_bytes = {meta["fixture"]["bytes"]};',
             f'inline constexpr std::string_view mimi_fixture_sha = "{meta["fixture"]["sha256"]}";',
             f'inline constexpr std::array<MimiCheckpoint, {len(checkpoints)}> mimi_checkpoints = {{{{']
    lines += ['    {"' + c['name'] + '", "' + c['sha256'] + '", ' + str(c['offset']) + ', ' + str(c['bytes']//4) + '},' for c in checkpoints]
    lines += ['}};', 'struct MimiReferenceConvolution { std::string_view name; bool transpose; unsigned input, output, kernel, stride, dilation, groups, weight; int bias; };',
              'inline constexpr std::array<MimiReferenceConvolution, 12> mimi_reference_convolutions = {{']
    lines += ['    {"' + c['name'] + '", ' + str(c['transpose']).lower() + ', ' + ', '.join(str(c[f]) for f in
              ['in_channels', 'out_channels', 'kernel', 'stride', 'dilation', 'groups', 'weight_id', 'bias_id']) + '},' for c in configs]
    lines += ['}};', '} // namespace outloud::test']
    (DEST / 'mimi_decoder_fixtures.hpp').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print(f'Offline Mimi: {len(checkpoints)} checkpoints, {meta["fixture"]["bytes"]} bytes; 46 x 1920 = 88320 samples')
    print('PCM SHA256:', pcm_sha, '; repeat bits / unchanged RNG PASS')
    print('Convolution configurations:', len(configs), '; no encoder, FlowLM, queues or threads executed')


if __name__ == '__main__':
    main()
