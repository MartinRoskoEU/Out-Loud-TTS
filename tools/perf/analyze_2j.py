"""Analyze already captured native reports; no synthesis or upstream imports."""
from __future__ import annotations

import json
from pathlib import Path
from statistics import median

DEST = Path(__file__).resolve().parents[2] / 'generated' / 'perf'
IDEAL = 906.354
OLD_SEQUENTIAL = 1493.116


def summary(values):
    return dict(minimum=min(values), median=median(values), maximum=max(values))


def replay(run):
    producer, consumer = run['prefill_ms'], 0.0
    for candidate, frame in zip(run['candidates_ms'][:46], run['mimi_frames_ms']):
        producer += candidate
        consumer = max(producer, consumer) + frame
    producer += run['candidates_ms'][46]
    return max(producer, consumer)


def main():
    raw = json.loads((DEST / '2j-final-benchmark.json').read_text())
    assert raw['warmups_per_mode'] == 1 and raw['timed_repetitions'] == 5
    assert raw['same_process'] and raw['trace_and_profiler_compiled_out']
    modes = {}
    for mode in ('sequential', 'worker'):
        runs = raw[mode]
        assert len(runs) == 5
        for r in runs:
            assert [r[k] for k in ('candidates', 'emitted', 'noise_draws', 'samples', 'first_eos', 'hot_allocations')] == [47, 46, 48, 88320, 43, 0]
        modes[mode] = {k: summary([r[k] for r in runs]) for k in
                      ('first_pcm_ms', 'total_ms', 'rtf', 'process_cpu_ms', 'cpu_wall_ratio')}
        modes[mode]['total_range_percent_of_median'] = 100 * (
            modes[mode]['total_ms']['maximum'] - modes[mode]['total_ms']['minimum']) / modes[mode]['total_ms']['median']
    s = modes['sequential']['total_ms']['median']
    w = modes['worker']['total_ms']['median']
    ratios = {
        'worker_over_sequential': w/s, 'sequential_over_worker_speedup': s/w,
        'wall_time_reduction_percent': 100*(1-w/s),
        'U1_over_worker_speedup': 1424.587/w, 'worker_over_U1': w/1424.587,
        'U3_over_worker_speedup': 1733.871/w, 'worker_over_U3': w/1733.871,
        'real_over_2i_ideal': w/IDEAL, 'ideal_over_real_efficiency_percent': 100*IDEAL/w,
        # Incremental speedup above 1x, using the same historical sequential
        # anchor as the Step 2I theoretical speedup. Wall savings is separate.
        '2i_incremental_speedup_captured_percent': 100*(OLD_SEQUENTIAL/w-1)/(OLD_SEQUENTIAL/IDEAL-1),
        '2i_theoretical_wall_savings_captured_percent': 100*(OLD_SEQUENTIAL-w)/(OLD_SEQUENTIAL-IDEAL),
    }
    diagnostic = json.loads((DEST / '2j-pipeline-trace.json').read_text())
    t, result = diagnostic['trace'], diagnostic['result']
    cs, fs = t['candidates'], t['frames']
    assert len(cs) == 47 and len(fs) == 46 and len(t['queue_occupancy']) == 92
    assert cs[-1]['queue_push'] == cs[-1]['latent_ready'] == 0
    producer_intervals = [(t['prefill_start'], t['prefill_end'])] + [(c['start'], c['end']) for c in cs]
    overlap = sum(max(0, min(end, f['end']) - max(begin, f['start']))
                  for begin, end in producer_intervals for f in fs)
    occupancy_ms = {str(n): 0.0 for n in range(3)}
    previous_time, occupancy = 0.0, 0
    for time, next_occupancy in t['queue_occupancy'] + [[result['total_ms'], 0]]:
        assert time >= previous_time and next_occupancy in range(3)
        occupancy_ms[str(occupancy)] += time - previous_time
        previous_time, occupancy = time, next_occupancy
    sequential_ar = median([r['ar_including_sampler_ms'] for r in raw['sequential']])
    sequential_mimi = median([r['mimi_ms'] for r in raw['sequential']])
    producer_ar = sum(c['end']-c['start'] for c in cs)
    consumer_mimi = sum(f['end']-f['start'] for f in fs)
    push_residual = sum(c['queue_push'] - c['latent_ready'] - c['capacity_wait'] for c in cs[:46])
    consumer_dispatch = sum(f['start'] - f['queue_pop'] for f in fs)
    trace = {
        'diagnostic_total_ms': result['total_ms'], 'diagnostic_over_worker_median': result['total_ms']/w,
        'diagnostic_cpu_ms': result['process_cpu_ms'], 'diagnostic_cpu_wall_ratio': result['cpu_wall_ratio'],
        'prefill_ms': t['prefill_end']-t['prefill_start'], 'ar_active_call_ms': producer_ar,
        'mimi_active_call_ms': consumer_mimi, 'active_call_overlap_ms': overlap,
        'producer_capacity_wait_ms': sum(c['capacity_wait'] for c in cs),
        'consumer_latent_wait_ms': sum(f['latent_wait'] for f in fs)+t['terminal_consumer_wait'],
        'consumer_wait_after_first_latent_ms': sum(f['latent_wait'] for f in fs[1:])+t['terminal_consumer_wait'],
        'occupancy_ms': occupancy_ms, 'maximum_occupancy': max(e[1] for e in t['queue_occupancy']),
        'push_nonblocking_residual_ms': push_residual, 'consumer_pop_to_decode_ms': consumer_dispatch,
        'synchronization_nonblocking_partial_ms': push_residual + consumer_dispatch,
        'producer_completion_ms': t['producer_completion'], 'consumer_completion_ms': t['consumer_completion'],
        'final_pcm_ready_ms': fs[-1]['pcm_ready'],
        'final_pcm_tail_after_producer_ms': fs[-1]['pcm_ready'] - t['producer_completion'],
        'completion_tail_after_producer_ms': t['consumer_completion'] - t['producer_completion'],
        'ar_over_sequential_ar': producer_ar/sequential_ar,
        'mimi_over_sequential_mimi': consumer_mimi/sequential_mimi,
        'sequential_ar_median_ms': sequential_ar, 'sequential_mimi_median_ms': sequential_mimi,
    }
    report = dict(modes=modes, ratios=ratios, trace=trace, memory=raw['memory'],
                  fresh_sequential_ideal_ms=summary([replay(r) for r in raw['sequential']]),
                  historical_2i_ideal_ms=IDEAL, historical_2i_sequential_ms=OLD_SEQUENTIAL,
                  upstream_reference_ms=dict(U1=1424.587, U3=1733.871), upstream_rerun=False)
    (DEST / '2j-comparison.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
