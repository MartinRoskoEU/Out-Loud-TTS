# A04.1: focused FlowSampler provenance review

Date: **2026-10-10**. Repository HEAD: `c0911889610a8b887ad19407c7032d6305448cf2`.

**Classification A: no identifiable DiT-derived protected expression found in the
inspected native code.** This is a bounded technical finding, not proof of clean-room
authorship or absolute legal clearance. No sampler change is recommended.

## 1. Scope

Read sections 5, 16 and 23 of [the original A04 audit](LICENSING_AND_REDISTRIBUTION_A04.md).
Compared [flow_sampler.cpp](../engine/src/flow_sampler.cpp), its
[header](../engine/include/outloud/flow_sampler.hpp), and the directly called
[sampler primitives](../engine/src/cpu/sampler_kernels.cpp) against the identified
Pocket -> MAR -> DiT source chain. Existing CMake wiring and the sampler's initial
Git revision were inspected narrowly for dependency/history context.

No model, tokenizer, voice, runtime or general licensing re-audit was performed.
Source comparisons were static; no numerical comparison, execution test, build,
benchmark, deployment or signing occurred. The original A04 report is unchanged.

## 2. Upstream revisions examined

| Reference | Exact revision and file | Verification |
| --- | --- | --- |
| **P**: Pocket TTS 3.3.0 | `3dbee45d343d7dddd0d105468d17f8dcba14db3e`; [pocket_tts/modules/mlp.py](https://github.com/kyutai-labs/pocket-tts/blob/3dbee45d343d7dddd0d105468d17f8dcba14db3e/pocket_tts/modules/mlp.py) | Installed local file reused; text matches pinned upstream after newline normalization; 210 lines |
| **M**: MAR | `fe470ac24afbee924668d8c5c83e9fec60af3a73`; [models/diffloss.py](https://github.com/LTH14/mar/blob/fe470ac24afbee924668d8c5c83e9fec60af3a73/models/diffloss.py) | Exact source read online in memory; 248 lines |
| **D**: historical DiT | `c9f545c56d212c32e73c57746550e3a30af897a4`; [models.py](https://github.com/facebookresearch/DiT/blob/c9f545c56d212c32e73c57746550e3a30af897a4/models.py) | Exact historical source read online in memory; 370 lines |

Raw upstream SHA-256 values, for reproducibility:

- P: `1760099c33241ce78bae0bfaf44dd9bc138f48eec20d6f713bf676ff322b95de`
- M: `7c737d3f395feedf2cc8ee71c85dac11b9b7686934e3e1b27a95bc1bdfd2827b`
- D: `1b8031a1340a3d1045c0bdb382334068f5f20e32edf67b3e6aba961ba91846ca`

Line numbers below refer to physical source lines, not a web reader's reformatted
line numbering. **N** means current native `flow_sampler.cpp`; **K** means
`sampler_kernels.cpp`. No upstream source file was added to the repository.

## 3. Relevant implementation comparison

Confidence describes source observations, not legal protectability.

| Operation | Pocket P | MAR M | DiT D | Out Loud N/K | Observed similarity | Provenance conclusion | Confidence |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Timestep features | 58–85: stored frequencies, cosine/sine, normalized MLP | 59–96: generated frequencies, odd-width handling | 27–64: similar generic embedding helper | N24–45; K61–68: BF16 stored frequencies, fixed 256 features, two cached times | Fourier features and dense/SiLU/dense | Functional Pocket implementation; native omits generic Python helper/device/padding machinery | High |
| Time/condition combination | 189–210: averaged multiple times | 217–237: single time plus condition | 233–248: time plus class embedding | N57–69: hidden projection plus averaged s=0/t=1 caches | Conditioning addition | Released Pocket-specific graph, not DiT image/class conditioning | High |
| SiLU and normalization | 20–55, 98, 123: custom variance operations | 110, 121, 137: framework modules | 35, 107–115, 131: framework modules | K17–58: explicit reductions and sigmoid arithmetic | Standard activation/normalization | No inherited Python module bodies identified in native primitives | High |
| Adaptive residual blocks | 88–113: three-way shift/scale/gate MLP | 99–128: same block organization | 101–122: six-way attention/MLP modulation | N72–95: six dense blocks, span partitions, explicit updates | Conditioned residual computation | Native follows Pocket/MAR architecture; DiT attention-block expression not found | High |
| **Final layer** | **116–133**: credited DiT adaptation | **131–148**: credited DiT adaptation | **125–142**: conditioned normalized projection | **N96–105**: scalar spans/loops, fixed tensor IDs, explicit kernel calls | Same final mathematical pipeline | Strong upstream source-adaptation evidence between Python files; no distinctive DiT expression identified in C++ | High facts; moderate technical attribution inference |
| Modulation/layout | 16–17: elementwise operation | 55–56: elementwise operation | 19–20: sequence-axis broadcasting | N81–87, 97–102: contiguous 512-element slices and indexing | Normalized value times (1 + scale), then shift | Formula shared; no broadcast/patch-layout implementation transferred | High |
| Output/control organization | 136–210: module/config dispatch | 151–248: training, checkpointing, CFG | 145–264: image patches, initialization, CFG | N47–108, 132–141: supplied-noise API, 32-wide result and backbone calls | Projection/inference concepts | Different execution/storage organization; no image reconstruction/training machinery | High |

## 4. Mathematical similarities

The native final layer computes a conditioned affine transform of a normalized
512-element vector, followed by a learned projection to 32 velocity values. Its
modulation is the mathematical relationship:

`output[i] = normalized[i] * (1 + scale[i]) + shift[i]`

Shift/scale arise from a projection of the SiLU-transformed condition. LayerNorm
uses mean and population variance with epsilon `1e-6`; SiLU evaluates the standard
sigmoid-weighted input. K17–29 performs explicit eight-lane reductions. K50–58's
separate sample-variance normalization uses a different denominator/epsilon and
belongs to Pocket's time-conditioning path.

These identities, dependencies and tensor dimensions explain the corresponding
operation order without requiring copied source wording. They establish functional
design similarity, not historical independence. Epsilon and widths are numerical
compatibility choices, not distinctive evidence of DiT authorship by themselves.

## 5. Source-expression similarities

**Confirmed upstream adaptation:** P116–133 and M131–148 share class/member structure
and the short final forward sequence with D125–142. Their attribution is supported
by actual close source structure, beyond a matching result. That establishes a
meaningful Python-source connection, without deciding which portions are protected.

**Native findings:** N96–105 retains the functional sequence but expresses it through
project storage and interfaces. A shared scratch array is sliced into output spans;
model tensor IDs 6–9 select parameters; kernels consume BF16 weights; empty scale/bias
spans request affine-free normalization; a loop applies modulation; observers expose
checkpoints. There is no native `FinalLayer` class, Python sequential-module definition,
tensor `chunk`/`unsqueeze` body, copied final-layer docstring or image-patch projection.

The names `shift`, `scale`, `linear`, `x` and `y` and normalization-before-modulation
order are similarities. Here they describe the graph's operands and dependencies;
none identifies a particular nonfunctional DiT fragment. The long `PocketTensorId`
identifier containing `adaLN_modulation` preserves checkpoint naming, not a translated
DiT function body. Absence of a copied name/comment alone would not establish
independence; the assessment also considers the actual storage, operations and structure.

N24–45 and N69 cache fixed time embeddings and reuse the condition activation,
avoiding repeated evaluations within the Pocket graph. Those differences are corroborating
implementation facts, not a rule that optimization removes an inherited copyright.

The initial native sampler in commit `1a36789` already uses arrays, tensor-ID lookup,
span slices and scalar loops. Later optimization did not introduce all those
differences. That Git record proves an earlier native form exists, **not** that its
author never consulted DiT or translated protected expression.

## 6. Relationship to DiT

For the upstream Python chain, the final-layer relationship is an identifiable
source adaptation. For the inspected native file, the demonstrated relationship
is **implementation of the inherited mathematical architecture through Pocket**.
No DiT-specific copied expression was identified beyond that functional structure.

There is no tracked DiT source file/package dependency in the inspected repository
inventory or production build wiring. Python references live in the development
environment, not the native source payload. The attribution chain is genuine;
it cannot alone identify a protected derivative in the C++ sampler.

## 7. MIT compatibility implications

The technical comparison **does not identify a concrete DiT licensing conflict
in current FlowSampler**. Accordingly it supplies no basis for changing MIT on
original project-owned code, imposing noncommercial terms on the native sampler,
or rewriting particular lines merely to change syntax. Existing applicable Pocket/
other upstream notice obligations remain separate from this negative finding.

The original audit's DiT P1 was an unresolved possibility. This follow-up narrows
that concern: no specific native expression requiring remediation was identified.
It does not grant legal clearance for every possible derivative-work interpretation,
resolve other A04 issues or retroactively alter the original audit. Free distribution,
MIT original code and the zero-bundled-voices policy remain unchanged.

## 8. Remaining uncertainty

Source inspection cannot reconstruct the author's thought process, unavailable
drafts, prompts or correspondence, nor prove independent authorship/clean-room
development. This review does not decide the protectability of a model-specific
selection/arrangement of standard operations or whether any jurisdiction would
treat the native implementation as a copyright derivative despite different syntax.
Legal interpretation and technical similarity are distinct questions.

The inspected revisions bound the conclusion. No fuzzy-match score, compiled
execution or numerical equality result was used as a proxy for copyright status.

## 9. Recommended action and explicit assessment

**Keep FlowSampler unchanged.** Retain this focused comparison as the follow-up to
A04 and preserve applicable upstream credits in later authorized licensing work.
There is no identified region justifying a sampler rewrite, new test campaign or
default maintainer correspondence. If a formal legal assurance is required, it needs
separate interpretation of the limited facts; this review does not supply it.

| Question | Answer |
| --- | --- |
| **Q1: Is DiT source directly included?** | No directly included/vendored DiT source or linked DiT dependency was found in the repository/production wiring. |
| **Q2: Evidence of distinctive DiT expression copied/translated into Out Loud?** | No identifiable distinctive DiT fragment was found in the inspected native implementation. The upstream Python adaptations and native functional correspondence are confirmed, not proof of native clean-room authorship. |
| **Q3: Explainable by mathematics/architecture?** | Yes. The observed native similarities are explained by conditioning, normalization, modulation, dense projection and the Pocket checkpoint graph. |
| **Q4: Concrete DiT licensing conflict identified?** | No concrete conflict was established for current FlowSampler. Legal derivative-work interpretation is not conclusively settled by this comparison. |
| **Q5: Specific code change justified?** | No. No suspicious nonfunctional code region requiring correction was identified. |
| **Q6: What remains uncertain?** | Historical authorship/access and legal protectability/derivation of the arrangement; source inspection alone cannot establish absolute clearance. |

**Final classification: A — no identifiable DiT-derived protected expression found
in the inspected code.** This does not mean that all possible legal questions or
the broader A04 release conditions are resolved.

Only this report was added. No existing source, audit, license, configuration,
asset or system state was modified. No builds/tests, staging, commits or pushes.
