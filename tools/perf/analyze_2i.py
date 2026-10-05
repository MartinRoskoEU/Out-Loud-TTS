"""Step 2I reports: retained experiments, final five-run results and overlap."""
from __future__ import annotations

import json
import statistics

from analyze import DEST, native_profile, pipeline, summary, table

KEYS = ['first_pcm_ms', 'prefill_ms', 'ar_including_sampler_ms', 'mimi_ms', 'total_ms', 'rtf']


def benchmark(tag, path, expected_runs):
    raw = json.loads((DEST / f'{tag}-native-{path}-benchmark.json').read_text())
    if raw['warmups'] != 1 or raw['timed_repetitions'] != expected_runs or len(raw['runs']) != expected_runs:
        raise ValueError('Wrong repetition methodology: '+tag)
    for r in raw['runs']:
        if (r['samples'], r['candidates'], r['emitted'], r['noise_draws'], r['first_eos'], r['final_offset'], r['hot_allocations']) != (
                88320, 47, 46, 48, 43, 179, 0):
            raise ValueError('Wrong canonical workload: '+tag)
    return raw, {k: summary([r[k] for r in raw['runs']]) for k in KEYS}


def main():
    starting = {}
    final = {}
    traces = {}
    for path in ['portable', 'avx2']:
        _, starting[path] = benchmark('2i-start', path, 3)
        traces[path], final[path] = benchmark('2i-final', path, 5)
    experiments = []
    previous = starting['avx2']
    for tag, technique in [('direct', 'BF16 conversion in AVX2 registers'),
                           ('blocked', 'four output rows / four activation positions'),
                           ('convblocked', 'four convolution time positions')]:
        _, current = benchmark('2i-'+tag, 'avx2', 2)
        experiments.append({'name': tag, 'technique': technique, 'before': previous, 'after': current,
                            'improvement_percent': {k: 100*(1-current[k]['median']/previous[k]['median'])
                                                    for k in KEYS if k != 'rtf'},
                            'retained': True, 'additional_resident_bytes': 0, 'isa': 'optional AVX2 + FMA'})
        previous = current
    raw_profile = json.loads((DEST / '2i-final-native-avx2-profile.json').read_text())
    prof = native_profile(raw_profile, final['avx2']['total_ms']['median'])
    overlap = {}
    for path in final:
        times = [pipeline(r) for r in traces[path]['runs']]
        ideal = statistics.median(times)
        overlap[path] = {'ideal_ms': summary(times), 'ideal_rtf': summary([3680/t for t in times]),
                         'sequential_to_ideal_speedup': final[path]['total_ms']['median']/ideal,
                         'workers_alone_meet_U1': ideal <= 1424.587,
                         'remaining_uniform_speedup_to_U1': max(1, ideal/1424.587),
                         'assumptions': 'unchanged per-call durations, independent resources, no contention or queue cost'}
    ratios = {p: {'over_U3': final[p]['total_ms']['median']/1733.871,
                  'over_U1': final[p]['total_ms']['median']/1424.587,
                  'starting_to_final_speedup': starting[p]['total_ms']['median']/final[p]['total_ms']['median'],
                  'starting_to_final_time_reduction_percent': 100*(1-final[p]['total_ms']['median']/starting[p]['total_ms']['median'])}
              for p in final}
    result = {'initial': starting, 'experiments': experiments, 'final': final, 'profile': prof,
              'ratios': ratios, 'overlap': overlap, 'rejected_experiments': [],
              'additional_resident_bytes': 0, 'upstream_reference_ms': {'U3': 1733.871, 'U1': 1424.587},
              'upstream_benchmark_rerun': False}
    (DEST / '2i-comparison.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    sections = []
    sections.append('Fresh starting medians\n\n'+table(['Stage / metric', 'Portable', 'Old AVX2/FMA'],
        [[k]+[f'{starting[p][k]["median"]:.3f}' for p in starting] for k in KEYS]))
    sections.append('Measured experiment medians\n\n'+table(['Technique', 'Prefill ms', 'AR ms', 'Mimi ms', 'Total ms', 'Total time reduction'],
        [[e['technique']]+[f'{e["after"][k]["median"]:.3f}' for k in ['prefill_ms','ar_including_sampler_ms','mimi_ms','total_ms']]+
         [f'{e["improvement_percent"]["total_ms"]:.2f}%'] for e in experiments]))
    for path in final:
        sections.append(path+' final measurements\n\n'+table(['Stage / metric', 'Minimum', 'Median', 'Maximum'],
            [[k]+[f'{final[path][k][v]:.4f}' for v in ['minimum','median','maximum']] for k in KEYS]))
    sections.append('Final AVX2/FMA diagnostic top ten\n\n'+table(['Domain / group', 'Inclusive ms', '% synthesis', 'Calls', 'ms/call'],
        [[g['domain']+' / '+g['stage'],f'{g["inclusive_ms"]:.3f}',f'{g["percent_synthesis"]:.2f}',g['calls'],f'{g["average_ms"]:.6f}'] for g in prof['top10']]))
    sections.append('Final AVX2/FMA exclusive families\n\n'+table(['Family', 'ms', '% synthesis'],
        [[g['family'],f'{g["exclusive_ms"]:.3f}',f'{g["percent"]:.2f}'] for g in prof['families']]))
    sections.append('Final overlap replay\n\n'+table(['Path', 'Ideal min ms', 'Ideal median ms', 'Ideal max ms', 'Ideal median RTF', 'Sequential / ideal', 'Meets U1'],
        [[p]+[f'{overlap[p]["ideal_ms"][v]:.3f}' for v in ['minimum','median','maximum']]+[f'{overlap[p]["ideal_rtf"]["median"]:.4f}',
         f'{overlap[p]["sequential_to_ideal_speedup"]:.3f}',overlap[p]['workers_alone_meet_U1']] for p in overlap]))
    (DEST / '2i-summary.md').write_text('\n\n'.join(sections)+'\n', encoding='utf-8')
    print(json.dumps({'final': final, 'ratios': ratios, 'overlap': overlap,
                      'profile_total_ms': prof['total_ms']}, indent=2))


if __name__ == '__main__':
    main()
