"""Offline installed-PyTorch RNG reference; never writes protected fixtures."""
from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
import struct

import torch

ROOT = Path(__file__).resolve().parents[2]
DEST = ROOT / 'generated' / 'rng'
COMMIT = '5c4886908584029761b579af026dcfb627c84070'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def snapshot(generator):
    raw = generator.get_state().numpy().tobytes()
    assert len(raw) == 5056
    seed, left, seeded, position = struct.unpack_from('<QiIQ', raw)
    words = struct.unpack_from('<624Q', raw, 24)
    assert seeded == 1 and 1 <= left <= 624 and position <= 624
    assert all(w < 2**32 for w in words)
    # Empirically verified legacy serialization; retain complete state as well.
    return struct.pack('<QII624I', seed, left, position, *words), raw


def capture(seed, draws):
    generator = torch.Generator(device='cpu').manual_seed(seed)
    states, serialized, noise = [], [], []
    state, raw = snapshot(generator); states.append(state); serialized.append(raw)
    for _ in range(draws):
        value = torch.empty((1, 32), dtype=torch.float32, device='cpu')
        torch.nn.init.normal_(value, mean=0.0, std=0.3**0.5, generator=generator)
        assert torch.isfinite(value).all()
        noise.append(value.numpy().astype('<f4', copy=False).tobytes())
        state, raw = snapshot(generator); states.append(state); serialized.append(raw)
    return b''.join(noise), b''.join(states), b''.join(serialized)


def main():
    assert torch.__version__ == '2.14.1+cpu' and torch.version.git_version == COMMIT
    assert torch.backends.cpu.get_cpu_capability() == 'AVX2'
    torch.set_num_threads(1)
    cases = [(73, 192), (74, 48), (0, 2), (73 + 2**32, 2), (2**64 - 1, 2)]
    DEST.mkdir(parents=True, exist_ok=True)
    entries, data = [], bytearray()
    for seed, draws in cases:
        noise, states, raw = capture(seed, draws)
        assert (noise, states, raw) == capture(seed, draws)
        noise_offset = len(data); data.extend(noise)
        state_offset = len(data); data.extend(states)
        (DEST / f'torch-state-{seed}.bin').write_bytes(raw)
        entries.append(dict(seed=seed, draws=draws, noise_offset=noise_offset, state_offset=state_offset,
                            noise_sha=sha(noise), state_sha=sha(states), serialized_state_sha=sha(raw)))
    # Direct Generator and manual_seed/default-generator use the same sequence.
    torch.manual_seed(73)
    default = b''.join(torch.empty((1, 32), dtype=torch.float32).normal_(0, math.sqrt(.3)).numpy().tobytes()
                       for _ in range(48))
    canonical = bytes(data[:48*32*4])
    assert default == canonical
    negative_seed_mapping = {}
    for signed in (-1, -2, -(2**63)):
        negative_seed_mapping[str(signed)] = torch.Generator(device='cpu').manual_seed(signed).initial_seed()
        assert negative_seed_mapping[str(signed)] == signed % 2**64
    reset_probe = torch.Generator(device='cpu').manual_seed(73)
    initial = reset_probe.get_state().clone()
    torch.empty(1).normal_(generator=reset_probe) # exercise the small-shape cached-normal branch
    reset_probe.manual_seed(73)
    assert torch.equal(initial, reset_probe.get_state())
    baseline = json.loads((ROOT / 'reference' / 'generation-segment1-baseline.json').read_text())
    checkpoint = next(c for c in baseline['checkpoints'] if c['name'] == 'noise.stream')
    frozen = (ROOT / 'generated' / 'neural' / 'generation-segment1-f32.bin').read_bytes()
    assert sha(canonical) == checkpoint['sha256']
    assert canonical == frozen[checkpoint['offset']:checkpoint['offset'] + 48*32*4]
    package = Path(torch.__file__).parent
    sources = ['version.py', 'random.py', 'nn/init.py', 'include/ATen/CPUGeneratorImpl.h',
               'include/ATen/core/MT19937RNGEngine.h', 'include/ATen/core/DistributionsHelper.h',
               'include/ATen/core/TransformationHelper.h', 'include/ATen/native/cpu/DistributionTemplates.h',
               'include/ATen/native/cpu/avx_mathfun.h']
    hashes = {str(package / p): sha((package / p).read_bytes()) for p in sources}
    pocket = package.parent / 'pocket_tts' / 'models' / 'flow_lm.py'
    hashes[str(pocket)] = sha(pocket.read_bytes())
    metadata = dict(torch_version=torch.__version__, torch_commit=COMMIT, cpu_capability='AVX2',
                    shape=[1, 32], dtype='float32', std_double=math.sqrt(.3),
                    std_float_bits=struct.unpack('<I', struct.pack('<f', math.sqrt(.3)))[0],
                    canonical_matches_frozen=True, default_generator_matches=True, cases=entries,
                    bytes=len(data), sha256=sha(data), source_sha256=hashes,
                    state_format='seed:u64,left:u32,next:u32,state[624]:u32; 2512 bytes',
                    complete_torch_state_bytes=5056)
    metadata['negative_seed_mapping_verified'] = negative_seed_mapping
    metadata['manual_seed_clears_cached_normal_state'] = True
    (DEST / 'rng-reference.bin').write_bytes(data)
    (DEST / 'rng-reference.json').write_text(json.dumps(metadata, indent=2)+'\n', encoding='utf-8')
    lines = ['// Installed PyTorch reference, development only. Generated by export_rng_reference.py.',
             '#pragma once', '#include <array>', '#include <cstdint>', '#include <string_view>',
             'namespace outloud::test {',
             'struct RNGCase { std::uint64_t seed; std::uint32_t draws; std::size_t noise_offset, state_offset; };',
             f'inline constexpr std::size_t rng_fixture_bytes = {len(data)};',
             f'inline constexpr std::string_view rng_fixture_sha = "{sha(data)}";',
             f'inline constexpr std::array<RNGCase, {len(entries)}> rng_cases = {{{{']
    lines += [f'    {{{e["seed"]}ULL, {e["draws"]}, {e["noise_offset"]}, {e["state_offset"]}}},' for e in entries]
    lines += ['}};', '} // namespace outloud::test', '']
    (DEST / 'rng_reference.hpp').write_text('\n'.join(lines), encoding='utf-8')
    print('PASS: captured 192 seed-73 rows plus alternate seeds and every state; first 48 exactly match frozen noise.')
    print('FP32 std bits:', hex(metadata['std_float_bits']), 'fixture:', len(data), sha(data))


if __name__ == '__main__':
    main()
