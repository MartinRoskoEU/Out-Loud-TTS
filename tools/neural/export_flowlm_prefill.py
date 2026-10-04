"""Offline checkpoints from untouched Pocket 3.3.0; no sampler, RNG or decoder calls."""
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
from asset_format import (COMPATIBILITY_ID, MODEL_INPUT, MODEL_SHA, TOKENIZER_SHA,
                          VOICE_SHAS, SafeTensorSource, sha_file)
from export_voice import read_voice

import torch
from safetensors.torch import load_file
from pocket_tts.models.flow_lm import FlowLMModel
from pocket_tts.models.model_state import _import_model_state
from pocket_tts.modules.stateful_module import StatefulModule, increment_steps
from pocket_tts.utils.config import CONFIGS_DIR, load_config

TOKENIZER = (Path.home() / '.cache/huggingface/hub/models--kyutai--pocket-tts-without-voice-cloning'
             / 'snapshots/00eac05ed3d16bdc3f6b5d598874019c34a89214/languages/english/tokenizer.json')
VOICE = ROOT / 'voices/gnome_male.safetensors'
DESTINATION = ROOT / 'generated/neural'


def require(condition, message):
    if not condition:
        raise ValueError(message)


def build_model():
    require(importlib.metadata.version('pocket-tts') == '3.3.0', 'Wrong Pocket version')
    require(sha_file(MODEL_INPUT) == MODEL_SHA, 'Wrong local model identity')
    require(sha_file(TOKENIZER) == TOKENIZER_SHA, 'Wrong local tokenizer identity')
    require(sha_file(VOICE) == VOICE_SHAS['gnome_male'], 'Wrong local voice identity')
    config = load_config(CONFIGS_DIR / 'english.yaml')
    config.flow_lm.lookup_table.tokenizer_path = str(TOKENIZER)
    # Meta construction avoids consuming CPU RNG for parameters overwritten by weights.
    with torch.device('meta'):
        model = FlowLMModel.from_pydantic_config(
            config.flow_lm, latent_dim=32, insert_bos_before_voice=True)
    source = load_file(str(MODEL_INPUT), device='cpu')
    expected = set(model.state_dict())
    actual = {name.removeprefix('flow_lm.'): value.float() for name, value in source.items()
              if name.startswith('flow_lm.') and name != 'flow_lm.speaker_proj_weight'}
    require(set(actual) == expected, 'Upstream FlowLM parameter schema changed')
    model.load_state_dict(actual, strict=True, assign=True)
    model.eval()
    for name, module in model.named_modules():
        if isinstance(module, StatefulModule):
            module._module_absolute_name = name
    return model


def capture(model, tokens):
    saved = {}
    handles = []
    active_layer = ['']

    def save(name, value):
        value = value.detach().cpu().float().contiguous()
        if value.ndim > 1 and value.shape[0] == 1:
            value = value.squeeze(0)
        saved[name] = value.clone()

    def pre(module, name):
        handles.append(module.register_forward_pre_hook(lambda _, args: save(name, args[0])))

    def post(module, name):
        handles.append(module.register_forward_hook(lambda _, args, output: save(name, output)))

    def rotated(_, args, output):
        save(active_layer[0] + 'q_rope', output[0])
        save(active_layer[0] + 'k_suffix', output[1])

    # All layers share one RotaryEmbedding object; hook it once and label its caller.
    handles.append(model.transformer.rope.register_forward_hook(rotated))

    for i, layer in enumerate(model.transformer.layers):
        p = f'layer{i}.'
        handles.append(layer.register_forward_pre_hook(
            lambda _, args, p=p: active_layer.__setitem__(0, p)))
        pre(layer, p + 'input')
        post(layer.norm1, p + 'norm1')
        post(layer.self_attn.in_proj, p + 'qkv')
        # Original V is packed after Q and K; no precision/layout transformation.
        handles.append(layer.self_attn.in_proj.register_forward_hook(
            lambda _, args, output, p=p: save(p + 'v_suffix', output.view(1, 20, 3, 16, 64)[:, :, 2])))
        pre(layer.self_attn.out_proj, p + 'attention_context')
        post(layer.self_attn.out_proj, p + 'attention_update')
        pre(layer.norm2, p + 'attention_residual')
        post(layer.norm2, p + 'norm2')
        post(layer.linear1, p + 'linear1')
        pre(layer.linear2, p + 'gelu')
        post(layer.linear2, p + 'ffn_update')
        post(layer, p + 'output')
    state = _import_model_state(VOICE, torch.device('cpu'))
    for layer_state in state.values():
        require(layer_state['offset'].item() == 112 and layer_state['pad'].item() == 0,
                'Invalid reference voice state')
        # One extra slot permits a separate deterministic single-token backbone probe.
        cache = layer_state['cache']
        layer_state['cache'] = torch.cat((cache, torch.zeros(2, 1, 21, 16, 64)), dim=2)
    try:
        embedding = model.conditioner(torch.tensor(tokens, dtype=torch.int64).view(1, -1))
        save('text.embedding', embedding)
        empty_sequence = torch.empty(1, 0, 32)
        result = model.backbone(torch.empty(1, 0, 1024), embedding, empty_sequence, state)
        save('final.norm', result)
        save('final.last_hidden', result[:, -1])
        save('final.eos', model.out_eos(result[:, -1]))
        for i in range(6):
            cache = state[f'transformer.layers.{i}.self_attn']['cache']
            require(torch.equal(cache[0, :, 112:132].squeeze(0), saved[f'layer{i}.k_suffix']),
                    'Hook K differs from actual upstream cache append')
            require(torch.equal(cache[1, :, 112:132].squeeze(0), saved[f'layer{i}.v_suffix']),
                    'Hook V differs from actual upstream cache append')
        increment_steps(model, state, 20)
        require(all(s['offset'].item() == 132 for s in state.values()), 'Wrong prefill offsets')
    finally:
        for handle in handles:
            handle.remove()
    # Real learned BOS input, without running FlowLM.forward or sampling a latent.
    bos = model.bos_emb.view(1, 1, 32)
    projected = model.input_linear(bos)
    save('kernel.input_linear.input', bos)
    save('kernel.input_linear.output', projected)
    single = model.backbone(projected, torch.empty(1, 0, 1024), bos, state)
    save('single.last_hidden', single[:, -1])
    save('single.eos', model.out_eos(single[:, -1]))
    return saved


def main():
    torch.set_num_threads(1)  # Fixed reference computation, independent of host core count.
    rng_before = torch.get_rng_state().clone()
    model = build_model()
    baseline = json.loads((ROOT / 'reference/pipeline-baseline.json').read_text(encoding='utf-8'))
    segment = baseline['oracle']['segments'][0]
    tokens = segment['token_ids']
    require(len(tokens) == 20 and model.conditioner.tokenizer.encode(segment['text']) == tokens,
            'Canonical segment token IDs changed')
    with torch.inference_mode():
        saved = capture(model, tokens)
        repeated = capture(model, tokens)
    require(saved.keys() == repeated.keys() and
            all(torch.equal(v.view(torch.int32), repeated[k].view(torch.int32)) for k, v in saved.items()),
            'Deterministic reference checkpoint repeat failed')
    require(torch.equal(torch.get_rng_state(), rng_before), 'Exporter consumed CPU RNG')
    with SafeTensorSource(VOICE, VOICE_SHAS['gnome_male']) as source:
        prefix, raw_voice = read_voice(source)
    voice_asset = (ROOT / 'generated/assets/gnome_male.olv').read_bytes()
    require(voice_asset[256:] == raw_voice, 'Existing .olv differs from original voice K/V')
    DESTINATION.mkdir(parents=True, exist_ok=True)
    checkpoints = []
    temporary_fixture = DESTINATION / 'flowlm-prefill-f32.bin.tmp'
    with temporary_fixture.open('wb') as stream:
        for name, value in saved.items():
            raw = value.numpy().astype('<f4', copy=False).tobytes()
            require(torch.isfinite(value).all().item(), f'Nonfinite checkpoint: {name}')
            checkpoints.append({'name': name, 'shape': list(value.shape), 'dtype': 'float32',
                'offset': stream.tell(), 'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()})
            stream.write(raw)
    fixture = temporary_fixture
    metadata = {'schema_version': 1, 'pocket_tts_version': '3.3.0', 'torch_version': torch.__version__,
        'compatibility_id': COMPATIBILITY_ID, 'model_source_sha256': MODEL_SHA,
        'tokenizer_source_sha256': TOKENIZER_SHA, 'voice_source_sha256': VOICE_SHAS['gnome_male'],
        'voice_prefix_length': prefix, 'voice_payload_sha256': hashlib.sha256(raw_voice).hexdigest(),
        'segment_text': segment['text'], 'token_ids': tokens, 'final_offset': 132,
        'dtype': 'float32', 'device': 'cpu', 'torch_threads': 1,
        'sampler_called': False, 'rng_unchanged': True, 'repeat_bit_exact': True,
        'fixture': {'file': 'flowlm-prefill-f32.bin', 'bytes': fixture.stat().st_size,
                    'sha256': sha_file(fixture)}, 'checkpoints': checkpoints,
        'eos_logit': saved['final.eos'].item(), 'single_bos_eos_logit': saved['single.eos'].item()}
    baseline_path = ROOT / 'reference/flowlm-prefill-baseline.json'
    if baseline_path.exists():
        original_metadata = json.loads(baseline_path.read_text(encoding='utf-8'))
        if metadata != original_metadata:
            temporary_fixture.unlink()
            raise ValueError('Frozen FlowLM reference changed; refusing to overwrite baseline or fixtures')
    else:
        baseline_path.write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    temporary_fixture.replace(DESTINATION / 'flowlm-prefill-f32.bin')
    fixture = DESTINATION / 'flowlm-prefill-f32.bin'
    lines = ['// Generated by offline untouched upstream checkpoint exporter.', '#pragma once',
        '#include <array>', '#include <cstdint>', '#include <string_view>',
        'namespace outloud::test {',
        'struct NeuralCheckpoint { std::string_view name, sha; std::uint64_t offset, bytes; '
        'std::array<std::uint32_t, 3> shape; std::uint32_t rank; };',
        f'inline constexpr std::uint64_t neural_fixture_bytes = {fixture.stat().st_size};',
        f'inline constexpr std::string_view neural_fixture_sha = "{sha_file(fixture)}";',
        f'inline constexpr std::string_view neural_voice_sha = "{VOICE_SHAS["gnome_male"]}";',
        f'inline constexpr std::string_view neural_voice_payload_sha = "{hashlib.sha256(raw_voice).hexdigest()}";',
        f'inline constexpr std::string_view neural_compatibility_id = "{COMPATIBILITY_ID}";',
        f'inline constexpr std::string_view neural_segment_text = "{segment["text"]}";',
        'inline constexpr std::array<std::uint32_t, 20> neural_tokens = {' + ','.join(map(str, tokens)) + '};',
        f'inline constexpr std::array<NeuralCheckpoint, {len(checkpoints)}> neural_checkpoints = {{{{']
    for c in checkpoints:
        shape = c['shape'] + [0] * (3 - len(c['shape']))
        lines.append('    {"' + c['name'] + '", "' + c['sha256'] + '", '
                     + str(c['offset']) + ', ' + str(c['bytes']) + ', {'
                     + ','.join(map(str, shape)) + '}, ' + str(len(c['shape'])) + '},')
    lines += ['}};', '} // namespace outloud::test']
    (DESTINATION / 'flowlm_prefill_fixtures.hpp').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print(f'Offline reference: {len(checkpoints)} checkpoints; {fixture.stat().st_size} bytes; '
          f'prefix 112 + 20 tokens = 132; EOS {metadata["eos_logit"]:.9g}')
    print('Repeat bit equality: PASS; CPU RNG unchanged: PASS; sampler/Mimi not called')


if __name__ == '__main__':
    main()
