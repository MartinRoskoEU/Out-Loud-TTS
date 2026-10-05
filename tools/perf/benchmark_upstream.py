"""Offline resident Pocket benchmarks. Profilers/hooks are absent from timed runs."""
from __future__ import annotations

import copy
import hashlib
import importlib.metadata
import json
import logging
import os
from pathlib import Path
import platform
import statistics
import sys
import threading
import time

os.environ['HF_HUB_OFFLINE'] = '1'
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/assets'))
from asset_format import MODEL_INPUT, MODEL_SHA, TOKENIZER_SHA, VOICE_SHAS, sha_file
import torch
from pocket_tts import TTSModel
from pocket_tts.modules.stateful_module import init_states, increment_steps

DEST = ROOT / 'generated/perf'
BASE = json.loads((ROOT / 'reference/generation-segment1-baseline.json').read_text())
MIMI = json.loads((ROOT / 'reference/mimi-decoder-baseline.json').read_text())
TEXT = BASE['segment_text']


def require(ok, message):
    if not ok:
        raise ValueError(message)


def summary(values):
    return {'minimum': min(values), 'median': statistics.median(values), 'maximum': max(values)}


def environment():
    config = torch.__config__.show()
    return {'python': platform.python_version(), 'pocket_tts': importlib.metadata.version('pocket-tts'),
            'torch': torch.__version__, 'threads': torch.get_num_threads(),
            'interop_threads': torch.get_num_interop_threads(), 'mkldnn_enabled': torch.backends.mkldnn.enabled,
            'mkl_available': torch.backends.mkl.is_available(), 'mkldnn_available': torch.backends.mkldnn.is_available(),
            'openmp_available': torch.backends.openmp.is_available(), 'torch_config': config,
            'thread_environment': {k: v for k, v in os.environ.items() if any(x in k.upper() for x in
                                   ['OMP_', 'MKL_', 'OPENBLAS_', 'NUMEXPR_', 'VECLIB_', 'BLIS_'])},
            'platform': platform.platform(), 'machine': platform.machine()}


def load():
    require(importlib.metadata.version('pocket-tts') == '3.3.0', 'Wrong Pocket version')
    require(sha_file(MODEL_INPUT) == MODEL_SHA, 'Wrong pinned local model')
    tokenizer = (Path.home() / '.cache/huggingface/hub/models--kyutai--pocket-tts-without-voice-cloning'
                 / 'snapshots/00eac05ed3d16bdc3f6b5d598874019c34a89214/languages/english/tokenizer.json')
    require(sha_file(tokenizer) == TOKENIZER_SHA, 'Wrong local tokenizer')
    voice_path = ROOT / 'voices/gnome_male.safetensors'
    require(sha_file(voice_path) == VOICE_SHAS['gnome_male'], 'Wrong local voice')
    # Keep the original import-established thread/backend settings and model modes.
    model = TTSModel.load_model(language='english', temp=0.3, sampler_decode_steps=1, quantize=False)
    voice = model.get_state_for_audio_prompt(str(voice_path))
    prepared = model.flow_lm.conditioner.prepare(TEXT)
    require(prepared.tolist() == [BASE['token_ids']], 'Canonical tokens changed')
    require(all(s['offset'].item() == 112 for s in voice.values()), 'Voice prefix changed')
    return model, voice, prepared


def validate(chunks, deterministic=False):
    sizes = [int(c.numel()) for c in chunks]
    require(sum(sizes) == 88320 and all(n > 0 and n % 1920 == 0 for n in sizes), 'Wrong canonical sample/frame count')
    audio = torch.cat([c.reshape(-1) for c in chunks])
    require(audio.dtype == torch.float32 and torch.isfinite(audio).all().item(), 'Invalid output PCM')
    digest = hashlib.sha256(audio.numpy().astype('<f4', copy=False).tobytes()).hexdigest()
    if deterministic:
        require(sizes == [1920] * 46 and digest == MIMI['pcm_float32_sha256'], 'One-frame upstream PCM changed')
    return {'samples': sum(sizes), 'emitted_frames': sum(sizes) // 1920, 'chunks': len(sizes),
            'decoder_frames_per_call': [n // 1920 for n in sizes], 'pcm_sha256': digest}


def production(model, voice):
    torch.manual_seed(73)
    chunks = []
    first = None
    threads_before = threading.active_count()
    observed_threads = threads_before
    cpu = time.process_time_ns()
    start = time.perf_counter_ns()
    for chunk in model.generate_audio_stream(voice, TEXT):
        now = time.perf_counter_ns()
        if first is None:
            first = now
        chunks.append(chunk)
        observed_threads = max(observed_threads, threading.active_count())
    end = time.perf_counter_ns()
    cpu_end = time.process_time_ns()
    return {'first_pcm_ms': (first-start)/1e6, 'total_ms': (end-start)/1e6,
            'process_cpu_ms': (cpu_end-cpu)/1e6, 'cpu_wall_ratio': (cpu_end-cpu)/(end-start),
            'rtf': 3680/((end-start)/1e6), 'python_threads_before': threads_before,
            'python_threads_observed_max': observed_threads}, chunks


class SequentialSink:
    def __init__(self, model, state):
        self.model, self.state = model, state
        self.pcm, self.latents, self.decode_ms = [], [], []
        self.first = None

    def put(self, latent):
        if latent is None:
            return
        start = time.perf_counter_ns()
        z = latent * self.model.flow_lm.emb_std + self.model.flow_lm.emb_mean
        audio = self.model.mimi.decode_from_latent(z, self.state)
        increment_steps(self.model.mimi, self.state, 16)
        end = time.perf_counter_ns()
        if self.first is None:
            self.first = end
        self.decode_ms.append((end-start)/1e6)
        self.pcm.append(audio.reshape(-1))
        self.latents.append(latent)


@torch.no_grad()
def sequential(model, voice, prepared):
    state = copy.deepcopy(voice)
    model._expand_kv_cache(state, sequence_length=241)
    mimi_state = init_states(model.mimi, 1, 1744)
    sink = SequentialSink(model, mimi_state)
    stop = threading.Event()  # Never starts a thread; argument required by installed AR loop.
    torch.manual_seed(73)
    cpu = time.process_time_ns()
    start = time.perf_counter_ns()
    model._run_flow_lm_and_increment_step(model_state=state, text_tokens=prepared)
    prefilled = time.perf_counter_ns()
    # Installed scheduling, including BOS, all candidates, EOS/tail and final discard.
    model._autoregressive_generation(state, 109, 3, sink, stop)
    end = time.perf_counter_ns()
    cpu_end = time.process_time_ns()
    result = {'first_pcm_ms': (sink.first-start)/1e6, 'total_ms': (end-start)/1e6,
              'prefill_ms': (prefilled-start)/1e6, 'ar_including_sampler_ms': (end-prefilled)/1e6-sum(sink.decode_ms),
              'mimi_ms': sum(sink.decode_ms), 'mimi_frames_ms': sink.decode_ms,
              'process_cpu_ms': (cpu_end-cpu)/1e6, 'cpu_wall_ratio': (cpu_end-cpu)/(end-start),
              'rtf': 3680/((end-start)/1e6)}
    require(all(s['offset'].item() == 179 for s in state.values()), 'Wrong final FlowLM offset')
    require(all(s['offset'].item() == 736 for s in mimi_state.values() if 'offset' in s), 'Wrong Mimi offsets')
    latent_bytes = torch.cat(sink.latents, dim=1).reshape(46,32).numpy().astype('<f4',copy=False).tobytes()
    require(hashlib.sha256(latent_bytes).hexdigest() == MIMI['canonical_latents_sha256'], 'Sequential AR latents changed')
    result.update({'candidates': 47, 'noise_draws': 48, 'first_eos': 43, 'final_offset': 179})
    return result, sink.pcm


def verify_production_scheduling(model, voice):
    """One warm-up verification with hooks; remove all hooks before timed repetitions."""
    candidates = []
    def captured(_, args, kwargs, output):
        sequence = kwargs['sequence'] if 'sequence' in kwargs else args[0]
        if sequence.shape[1] == 1:
            candidates.append(bool(output[1].item()))
    hook = model.flow_lm.register_forward_hook(captured, with_kwargs=True)
    try:
        _, chunks = production(model, voice)
    finally:
        hook.remove()
    require(len(candidates) == 47 and next(i for i,e in enumerate(candidates) if i >= 6 and e) == 43,
            'Production scheduling changed')
    return validate(chunks, model.max_decoder_frames_per_call == 1)


def main():
    logging.disable(logging.CRITICAL)
    model, voice, prepared = load()
    env = environment()
    report = {'methodology': {'warmups': 1, 'timed_repetitions': 5, 'primary': 'median',
                             'assets_resident': True, 'no_timed_hooks_or_profiler': True},
              'environment': env, 'canonical': {'text': TEXT, 'voice': 'gnome_male', 'seed': 73,
                                              'samples': 88320, 'duration_seconds': 3.680}, 'modes': {}}
    for mode in ['U1', 'U2', 'U3']:
        model.max_decoder_frames_per_call = 0 if mode == 'U1' else 1
        if mode != 'U3':
            warmup = verify_production_scheduling(model, voice)
        else:
            _, pcm = sequential(model, voice, prepared)
            warmup = validate(pcm, True)
        runs = []
        for i in range(5):
            r, pcm = sequential(model, voice, prepared) if mode == 'U3' else production(model, voice)
            r['output'] = validate(pcm, mode != 'U1')  # Outside measured region.
            runs.append(r)
            print(mode, i+1, 'first/total ms', round(r['first_pcm_ms'],3), round(r['total_ms'],3), flush=True)
        keys = ['first_pcm_ms', 'total_ms', 'rtf', 'process_cpu_ms', 'cpu_wall_ratio']
        if mode == 'U3':
            keys += ['prefill_ms', 'ar_including_sampler_ms', 'mimi_ms']
        report['modes'][mode] = {'warmup_output': warmup, 'runs': runs,
                                'summary': {k: summary([r[k] for r in runs]) for k in keys}}
    DEST.mkdir(parents=True, exist_ok=True)
    (DEST / 'upstream-benchmark.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print('Resident upstream benchmarks complete; reports contain all five runs. No audio files written.')


if __name__ == '__main__':
    main()
