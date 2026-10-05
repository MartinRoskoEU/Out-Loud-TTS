"""Summarize five-run benchmarks and one-run profiles; no performance pass/fail gates."""
from __future__ import annotations

import json
from pathlib import Path
import statistics

DEST = Path(__file__).resolve().parents[2] / 'generated/perf'


def read(name):
    return json.loads((DEST / name).read_text(encoding='utf-8'))


def summary(values):
    return {'minimum': min(values), 'median': statistics.median(values), 'maximum': max(values)}


def pipeline(run, transfer_ms=0):
    """Replay actual A/B durations with independent resources, FIFO, no contention.

    Optional transfer_ms is a sensitivity parameter, not a measured queue cost.
    The final discarded candidate runs on A but is never sent to B.
    """
    a = run['prefill_ms']
    b = 0
    for candidate, decode in zip(run['candidates_ms'][:46], run['mimi_frames_ms']):
        a += candidate
        b = max(a, b) + transfer_ms + decode
    a += run['candidates_ms'][46]
    return max(a, b)


DENSE = {'input_projection', 'condition_projection', 'projection', 'qkv', 'attention_output',
         'ffn_linear1', 'ffn_linear2', 'eos', 'time_mlp_linear', 'adaln_linear', 'block_linear', 'final_projection'}
CONV = {'initial_conv1d', 'group1_residual_convs', 'group2_residual_convs', 'group3_residual_convs', 'final_conv1d'}
TRANSPOSE = {'depthwise_upsample', 'group1_convtranspose', 'group2_convtranspose', 'group3_convtranspose'}
ATTENTION = {'attention', 'attention_scores', 'softmax', 'attention_values'}
ROOTS = {'prefill', 'backbone', 'sampler_total', 'mimi_decode', 'mimi_transformer', 'seanet'}


def family(stage):
    if stage in DENSE:
        return 'dense linear (excluding child BF16 widening)'
    if stage in CONV:
        return 'Conv1d (excluding child BF16 widening)'
    if stage in TRANSPOSE:
        return 'ConvTranspose (excluding child weight preparation)'
    if stage in ATTENTION:
        return 'attention (scores, softmax, values)'
    if stage == 'normalization':
        return 'normalization'
    if stage in {'gelu', 'silu', 'elu'}:
        return 'activations'
    if stage in {'rope', 'time_features'}:
        return 'RoPE / time transcendentals'
    if stage in {'bf16_widening', 'weight_preparation'}:
        return 'BF16 widening / weight preparation'
    return 'miscellaneous'


def native_profile(raw, benchmark_total):
    total = raw['profiled_run']['total_ms']
    counters = raw['counters']
    domain_ms = {d: sum(c['exclusive_ms'] for c in counters if c['domain'] == d)
                 for d in ['other', 'flowlm', 'sampler', 'mimi']}
    groups = []
    families = {}
    for c in counters:
        item = dict(c, average_ms=c['inclusive_ms']/c['calls'],
                    percent_synthesis=100*c['inclusive_ms']/total,
                    percent_domain=100*c['inclusive_ms']/domain_ms[c['domain']])
        groups.append(item)
        label = family(c['stage'])
        families[label] = families.get(label, 0) + c['exclusive_ms']
    # Function-level groups include their weight preparation, but exclude its
    # nested counter from this ranking to avoid counting the same interval twice.
    ranked = [g for g in groups if g['stage'] not in ROOTS | {'bf16_widening', 'weight_preparation', 'attention'}]
    ranked.sort(key=lambda g:g['inclusive_ms'], reverse=True)
    accounted = sum(families.values())
    families['miscellaneous'] += total - accounted  # Unscoped dispatch/copies/timer gaps.
    return {'instrumented_not_benchmark': True, 'total_ms': total,
            'profile_to_benchmark_median_ratio': total/benchmark_total, 'domain_ms': domain_ms,
            'groups': groups, 'top10': ranked[:10],
            'families': [{'family': k, 'exclusive_ms': v, 'percent': 100*v/total}
                         for k, v in sorted(families.items(), key=lambda kv:kv[1], reverse=True)]}


def upstream_family(name):
    if name.startswith('group.'):
        return 'module wrapper / Python overhead'
    if name in {'aten::mm', 'aten::addmm', 'aten::matmul', 'aten::linear', 'aten::mv', 'aten::addmv'}:
        return 'dense linear / matrix multiply'
    if 'convolution' in name or 'conv' in name:
        return 'convolution (all dispatch paths)'
    if any(s in name for s in ['attention', 'softmax', 'aten::bmm', 'aten::baddbmm']):
        return 'attention-related'
    if 'norm' in name or name == 'aten::var':
        return 'normalization / variance'
    if any(s in name for s in ['gelu', 'silu', 'elu', 'sigmoid', 'tanh']):
        return 'activations'
    return 'other operators'


def table(headers, rows):
    return '\n'.join(['| ' + ' | '.join(headers) + ' |',
                      '| ' + ' | '.join(['---']*len(headers)) + ' |'] +
                     ['| ' + ' | '.join(str(x) for x in row) + ' |' for row in rows])


def main():
    upstream = read('upstream-benchmark.json')
    reports = dict(upstream['modes'])
    for mode, path in [('N1', 'portable'), ('N2', 'avx2')]:
        reports[mode] = read(f'native-{path}-benchmark.json')
    keys = ['first_pcm_ms', 'total_ms', 'rtf', 'process_cpu_ms', 'cpu_wall_ratio',
            'prefill_ms', 'ar_including_sampler_ms', 'mimi_ms']
    modes = {}
    for mode, report in reports.items():
        runs = report['runs']
        if len(runs) != 5:
            raise ValueError('Expected exactly five measurements for '+mode)
        for run in runs:
            if run.get('samples', run.get('output', {}).get('samples')) != 88320:
                raise ValueError('Wrong canonical sample count for '+mode)
        modes[mode] = {k: summary([r[k] for r in runs]) for k in keys if k in runs[0]}
    median = lambda mode, key: modes[mode][key]['median']
    profiles = {mode: native_profile(read(f'native-{path}-profile.json'), median(mode, 'total_ms'))
                for mode, path in [('N1', 'portable'), ('N2', 'avx2')]}
    overlap = {}
    for mode in ['N1', 'N2']:
        values = [pipeline(run) for run in reports[mode]['runs']]
        bound = statistics.median(values)
        overlap[mode] = {'ideal_ms': summary(values), 'ideal_rtf': summary([3680/t for t in values]),
                         'sequential_to_ideal_speedup': median(mode, 'total_ms')/bound,
                         'workers_alone_reach_U1': bound <= median('U1', 'total_ms'),
                         'required_uniform_stage_speedup_to_U1': bound/median('U1', 'total_ms'),
                         'synchronization_cost': 'UNMEASURED; no realistic queue cost assumed',
                         'assumptions': 'independent stages; unchanged per-call durations; zero resource contention and transfer cost'}
    operators = read('upstream-profiler.json')['operators']
    upstream_families = {}
    for op in operators:
        label = upstream_family(op['name'])
        upstream_families[label] = upstream_families.get(label, 0) + op['self_cpu_us']/1000
    result = {'methodology': {'warmups': 1, 'timed_repetitions': 5, 'primary': 'median', 'profile_runs_per_path': 1},
              'environment': upstream['environment'], 'modes': modes, 'native_profiles': profiles,
              'upstream_profile_families_self_ms': upstream_families,
              'upstream_profile_modules': [o for o in operators if o['name'].startswith('group.')],
              'overlap': overlap,
              'ratios': {'N1_over_U3': median('N1', 'total_ms')/median('U3', 'total_ms'),
                         'N1_over_U1': median('N1', 'total_ms')/median('U1', 'total_ms'),
                         'N2_over_N1': median('N2', 'total_ms')/median('N1', 'total_ms'),
                         'U3_over_U1': median('U3', 'total_ms')/median('U1', 'total_ms'),
                         'U3_over_U2': median('U3', 'total_ms')/median('U2', 'total_ms'),
                         'U1_over_U2': median('U1', 'total_ms')/median('U2', 'total_ms')}}
    (DEST / 'comparison.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    sections = []
    for key, label, decimals in [('first_pcm_ms', 'First PCM ms', 3), ('total_ms', 'Total ms', 3), ('rtf', 'RTF (audio / wall)', 4)]:
        rows = [[mode]+[f'{modes[mode][key][k]:.{decimals}f}' for k in ['minimum', 'median', 'maximum']] for mode in modes]
        sections.append(label+'\n\n'+table(['Mode', 'Minimum', 'Median', 'Maximum'], rows))
    sections.append('Uninstrumented stage medians\n\n'+table(['Stage ms', 'U3', 'N1', 'N2', 'N1 / U3'],
        [[key]+[f'{median(m,key):.3f}' for m in ['U3', 'N1', 'N2']]+[f'{median("N1",key)/median("U3",key):.3f}']
         for key in ['prefill_ms', 'ar_including_sampler_ms', 'mimi_ms', 'total_ms', 'first_pcm_ms']]))
    for mode, profile in profiles.items():
        sections.append(mode+' instrumented top ten\n\n'+table(['Domain / operation', 'ms', '% synthesis', 'Calls', 'ms/call'],
            [[g['domain']+' / '+g['stage'], f'{g["inclusive_ms"]:.3f}', f'{g["percent_synthesis"]:.2f}', g['calls'], f'{g["average_ms"]:.6f}']
             for g in profile['top10']]))
        sections.append(mode+' exclusive families\n\n'+table(['Family', 'ms', '% synthesis'],
            [[f['family'], f'{f["exclusive_ms"]:.3f}', f'{f["percent"]:.2f}'] for f in profile['families']]))
        for domain in ['flowlm', 'sampler', 'mimi']:
            sections.append(mode+' / '+domain+' complete counters\n\n'+table(['Stage', 'Inclusive ms', 'Calls', 'ms/call', '% domain', '% synthesis'],
                [[g['stage'], f'{g["inclusive_ms"]:.3f}', g['calls'], f'{g["average_ms"]:.6f}', f'{g["percent_domain"]:.2f}', f'{g["percent_synthesis"]:.2f}']
                 for g in profile['groups'] if g['domain'] == domain]))
    sections.append('Overlap\n\n'+table(['Mode', 'Ideal min ms', 'Ideal median ms', 'Ideal max ms', 'Median ideal RTF', 'Sequential / ideal', 'Additional uniform speedup to U1'],
        [[m]+[f'{overlap[m]["ideal_ms"][k]:.3f}' for k in ['minimum','median','maximum']]+[f'{overlap[m]["ideal_rtf"]["median"]:.4f}',
         f'{overlap[m]["sequential_to_ideal_speedup"]:.3f}', f'{overlap[m]["required_uniform_stage_speedup_to_U1"]:.3f}'] for m in overlap]))
    sections.append('Upstream self CPU families (instrumented)\n\n'+table(['Family', 'Self CPU ms'],
        [[k, f'{v:.3f}'] for k,v in sorted(upstream_families.items(), key=lambda kv:kv[1], reverse=True)]))
    (DEST / 'summary.md').write_text('\n\n'.join(sections)+'\n', encoding='utf-8')
    print(json.dumps({'modes': modes, 'ratios': result['ratios'], 'overlap': overlap}, indent=2))


if __name__ == '__main__':
    main()
