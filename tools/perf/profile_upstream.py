"""Separate CPU operator profile and production concurrency observation; never benchmark timing."""
from __future__ import annotations

import json
import logging
import threading
import time

from benchmark_upstream import DEST, load, production, sequential, validate
import torch


def main():
    logging.disable(logging.CRITICAL)
    model, voice, prepared = load()
    model.max_decoder_frames_per_call = 1
    _, chunks = sequential(model, voice, prepared)  # One unprofiled warm-up.
    validate(chunks, True)
    handles = []
    contexts = {}
    def tag(module, name):
        def pre(_, args):
            ctx = torch.profiler.record_function(name)
            ctx.__enter__()
            contexts[name] = ctx
        def post(_, args, output):
            contexts.pop(name).__exit__(None, None, None)
        handles.append(module.register_forward_pre_hook(pre))
        handles.append(module.register_forward_hook(post))
    tag(model.flow_lm.transformer, 'group.flowlm_transformer')
    tag(model.flow_lm.flow_net, 'group.sampler')
    tag(model.mimi.decoder_transformer, 'group.mimi_transformer')
    tag(model.mimi.decoder, 'group.seanet')
    try:
        with torch.profiler.profile(activities=[torch.profiler.ProfilerActivity.CPU], record_shapes=True) as profiler:
            _, chunks = sequential(model, voice, prepared)
    finally:
        for h in handles:
            h.remove()
    validate(chunks, True)
    DEST.mkdir(parents=True, exist_ok=True)
    averages = profiler.key_averages(group_by_input_shape=True)
    operators = [{'name': e.key, 'self_cpu_us': e.self_cpu_time_total, 'total_cpu_us': e.cpu_time_total,
                  'calls': e.count, 'shapes': e.input_shapes} for e in averages]
    operators.sort(key=lambda e:e['self_cpu_us'], reverse=True)
    (DEST / 'upstream-profiler.json').write_text(json.dumps({'mode': 'U3', 'instrumented_not_benchmark': True,
        'operators': operators, 'observed_profiler_thread_ids': sorted(set(e.thread for e in profiler.events()))}, indent=2)+'\n')
    (DEST / 'upstream-profiler.txt').write_text(averages.table(sort_by='self_cpu_time_total', row_limit=60)+'\n', encoding='utf-8')

    # torch.profiler's CPU collection is thread-local here. Use a distinct production
    # observation to prove overlap, instead of treating missing worker events as no work.
    model.max_decoder_frames_per_call = 0
    intervals = []
    original_flow = model._run_flow_lm_and_increment_step
    original_decode = model.mimi.decode_from_latent
    def observed(function, group):
        def call(*args, **kwargs):
            thread = threading.get_native_id()
            start = time.perf_counter_ns()
            result = function(*args, **kwargs)
            end = time.perf_counter_ns()
            intervals.append({'group':group, 'thread':thread, 'start_ns':start, 'end_ns':end})
            return result
        return call
    model._run_flow_lm_and_increment_step = observed(original_flow, 'flowlm_and_sampler')
    model.mimi.decode_from_latent = observed(original_decode, 'mimi')
    try:
        observation, chunks = production(model, voice)
    finally:
        model._run_flow_lm_and_increment_step = original_flow
        model.mimi.decode_from_latent = original_decode
    output = validate(chunks)
    flow = [x for x in intervals if x['group']=='flowlm_and_sampler']
    decode = [x for x in intervals if x['group']=='mimi']
    overlap_ns = sum(max(0,min(a['end_ns'],b['end_ns'])-max(a['start_ns'],b['start_ns'])) for a in flow for b in decode)
    (DEST / 'upstream-concurrency.json').write_text(json.dumps({'instrumented_not_benchmark':True,
        'observation':observation, 'output':output, 'intervals':intervals, 'overlap_ms':overlap_ns/1e6,
        'flow_thread_ids':sorted(set(x['thread'] for x in flow)), 'mimi_thread_ids':sorted(set(x['thread'] for x in decode))},indent=2)+'\n')
    print('One U3 CPU profile and one U1 concurrency observation complete; overlap ms:',overlap_ns/1e6)


if __name__ == '__main__':
    main()
