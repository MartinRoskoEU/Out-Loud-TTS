# Step 4A: natural prosody and pause feasibility audit

Audit date: 2026-10-09. Repository baseline: `2b1ee6f` (Added voice tester).
Scope: source inspection, upstream research and inexpensive text-only checks.
No synthesis, listening experiment, profiling, build or test suite was run for
this audit. This report is the only repository change. Production code, assets,
installed software, registry and voice library were not changed.

Evidence labels used throughout:

- **VERIFIED**: directly inspected code, an identified primary source, or a
  check performed for this audit. This does not imply verified listening quality.
- **INFERRED**: a consequence suggested by the inspected architecture, requiring
  experimental confirmation when it concerns generated speech.
- **UNVERIFIED**: no demonstrated mechanism or relevant acoustic measurement.

## 1. Executive technical conclusion

**NO VERIFIED METHOD** was found for reliable, controllable **natural comma
pauses with continuous intonation** using the current Pocket TTS weights.
This classification applies to the requested control over pause position,
duration, preceding phrase ending and following continuation. It does not mean
that the model cannot naturally pause, or that inference-only improvement is
impossible.

**VERIFIED:** internal commas reach the model as text tokens. The entire example
below is one native generation segment; it is not split at either comma:

> We Forsaken, once servants of the Lich King, now fight for our freedom.

**VERIFIED:** that segment retains text and previous acoustic context during
autoregressive generation. There is no comma-specific duration prediction,
word-to-audio alignment, silence latent, phrase-boundary instruction or
inference parameter expressing a pause in milliseconds. The pinned upstream
README lists explicit pauses from text as unsupported. Its implementation
contains a teacher-forced continuation TODO, rather than a working continuation
API. See the [pinned feature limitations](https://github.com/kyutai-labs/pocket-tts/blob/3dbee45d343d7dddd0d105468d17f8dcba14db3e/README.md#unsupported-features)
and [continuation TODO](https://github.com/kyutai-labs/pocket-tts/blob/3dbee45d343d7dddd0d105468d17f8dcba14db3e/pocket_tts/models/tts_model.py#L702-L705).

**INFERRED:** a different punctuation cue could change the learned boundary
realization without resetting the model. This is the most economical hypothesis
to test first, not an established solution or a recommendation to rewrite every
comma. Replacing commas with stronger cues could also create hesitation or
sentence-final intonation and make narration worse.

**Recommendation:** keep production inference unchanged. Run the single bounded
A/B experiment in section 11 before choosing a production approach. It compares
the original sentence with an ellipsis-cue variant under continuous generation,
and includes pinned Python controls to distinguish a native discrepancy from a
shared model/voice limitation. Do not implement stitching, latent silence
injection, or fine-tuning on the evidence available here.

## 2. Current native generation path

The effective path is:

```text
SAPI Speak text fragments -> UTF-8 request
  -> prepare_text / process_text -> tokenizer IDs per segment
  -> .olv voice-prefix KV state + full segment text prefill
  -> learned BOS -> previous normalized acoustic latent, autoregressively
  -> FlowLM hidden state + EOS logit -> one-step flow sampler
  -> bounded latent queue -> stateful Mimi decoder -> FP32 PCM frames
  -> PCM16 conversion -> caller-thread SAPI Write
```

**VERIFIED:** FlowLM has six causal transformer layers, width 1024, sixteen
heads, and 32-dimensional continuous acoustic latents. Each emitted latent
decodes to 1920 samples at 24 kHz mono: **80 ms**, or 12.5 latent frames/second.
Mimi upsamples to sixteen transformer steps per latent, then uses its learned
convolutional decoder. A latent frame is not a word, phoneme, comma or explicit
duration token. Architectural constants are in `PocketArchitecture`; source
locations for the entire path are indexed in section 13.

The phase-1 questions have these answers:

| Question | Finding and evidence |
| --- | --- |
| A. Comma representation | **VERIFIED:** the pinned vocabulary contains ID **262** for `,`; ID **1733** contains comma plus quotation mark. Tokenization is Unigram with metaspace and byte fallback, so do not assume every comma-containing string always yields a separate 262. The example yields two separate 262s. `Tokenizer::encode_ordinary`, `encode_piece`; pinned tokenizer JSON. |
| B. Does FlowLM see commas? | **VERIFIED:** IDs are embedded by `FlowLMBackbone::embed` and included in causal text prefill. Internal commas are not stripped by the English preparation path. |
| C. Full-sentence conditioning? | **VERIFIED:** all text in the **current segment** is cached before BOS. Subsequent acoustic queries can attend to every text position, the voice prefix and previous acoustic positions. There is no finite FlowLM attention window in this path. Text in another independently prepared segment is not retained. `prefill`, `backbone_into`, `cpu::causal_attention`, `UtterancePipeline::run`. |
| D. Implicit duration? | **VERIFIED:** acoustic content and the number of emitted frames determine duration. Learned EOS prediction, the minimum-EOS gate, tail and maximum-length guard determine termination. There is no explicit phoneme-duration head. `FlowSampler::candidate_into`, `GenerationStopPolicy::emit`, `estimate_max_gen_len`. |
| E. Decision to continue? | **VERIFIED:** `WorkerPipeline::Impl::produce` repeatedly computes a candidate and EOS logit. The stop policy latches the first eligible EOS and applies the tail; otherwise generation continues until the safety limit. EOS is utterance termination, not a comma-boundary prediction. |
| F. Natural silence? | **VERIFIED:** the generated continuous latent sequence is reconstructed by Mimi, including whatever low-energy intervals it encodes. The production path adds no comma silence. **INFERRED:** whether this particular sentence receives appropriate boundaries depends on learned conditioning and the sampled trajectory. |
| G. Explicit pause/duration controls? | **VERIFIED:** none found. Positive-duration `SPVA_Silence` is rejected with `E_NOTIMPL` by `collect_request`; zero-duration silence fragments are ignored. SAPI rate/volume are queried but do not transform audio. |
| H. Punctuation normalization? | **VERIFIED:** preparation trims Unicode whitespace, replaces line breaks with spaces, applies its space replacement and capitalizes the first character. At the end it preserves strong punctuation, replaces a trailing weak comma/semicolon/colon/dash with a period, or appends a period. Chunk preparation repeats this terminal normalization. |
| I. Parameters affecting rhythm/duration? | **VERIFIED:** seed, sampled noise, EOS threshold, tail and maximum frame count can change the output or its end. Native defaults fix temperature at 0.3, no noise clamp and one LSD sampling step. **UNVERIFIED:** any predictable per-comma rhythm or duration control through these parameters. |
| J. Native differences? | **VERIFIED:** selected English text cases match upstream; native neural arithmetic and decoder batching differ from default Python execution. **UNVERIFIED:** whether those differences account for the reported listening issue. Section 3 separates parity evidence from acoustic quality. |

**VERIFIED:** `process_text` uses a default soft budget of **50 tokens**. It first
uses sentence punctuation; for an oversized sentence it also considers comma,
semicolon and colon boundaries. Regrouping can keep punctuation-delimited
pieces in one segment. If a long chunk ends at a comma, preparation changes
that terminal comma to a period. `WorkerPipeline::prepare` resets both FlowLM
and Mimi for each resulting segment. The SAPI `MaximumSegmentTokens` setting
(default 128) reserves/limits resident capacity; it does **not** replace the
50-token chunking default in `UtterancePipeline::prepare`.

**INFERRED:** long-text comma splits can cause stronger endings and less
continuity than a single segment, even though this behavior follows the pinned
upstream policy. It cannot explain the example by a chunk reset: its 22 tokens
fit in one segment.

## 3. Comparison with original Pocket TTS

### Source identity and limits of the comparison

**VERIFIED:** the installed Python distribution is Pocket TTS **3.3.0**.
Official tag `v3.3.0` resolves to commit
`3dbee45d343d7dddd0d105468d17f8dcba14db3e`. Eight relevant installed source/config
files were compared with raw files from that immutable commit. All were
byte-identical and also matched their wheel `RECORD` hashes. The source list and
hashes appear in section 13. Moving upstream `main` is not the parity reference.

**VERIFIED:** the port's model contract records English weights at revision
`983151f13aaeab1b13c1e5e3c2c383d49a9edf3f`, source SHA-256
`fb0dc01b0d4d2e1c905b7a3e0676e3d9c96d5ae460e24e3ab94981805babf997`.
The tokenizer reference revision is
`00eac05ed3d16bdc3f6b5d598874019c34a89214`; its locally rechecked JSON SHA-256 is
`f498428e1eafee50492f7be13dc9bfafcfc12e508cd0eb1b01c92ecd5d8c6687`.
These identify the audited contract; this audit did not revalidate the deployed
model file or load its weights into a runtime.

### Source comparison

| Component | Pinned Python and native comparison |
| --- | --- |
| Preparation | **VERIFIED:** `prepare_text_prompt` and `_ensure_terminal_punctuation` correspond to native `prepare_text` / `terminal_punctuation` for the pinned English settings. English has no character replacements, semicolon removal or padding enabled. Upstream has additional configurable/language-specific policies; their absence from this fixed English port is not evidence of an English punctuation defect. |
| Tokenization/chunking | **VERIFIED:** `JsonTokenizer` uses the pinned tokenizer JSON; native implements its Unigram/metaspace behavior. `split_into_best_sentences` uses the same 50-token budget and fallback punctuation policy. Text checks below cover seven cases, not every Unicode/punctuation combination. |
| Text and acoustic conditioning | **VERIFIED:** Python embeds text and projects normalized acoustic latents before the causal transformer. Native mirrors these operations using the precomputed voice prefix and mutable suffix. BOS starts acoustic generation after text prefill. |
| Sampling | **VERIFIED:** Python's default is temperature 0.3, one LSD step, no clamp. Native implements that fixed inference path and supplies ATen-compatible FP32 normal noise. Python also exposes other temperatures, clamp settings and step counts; native is not a general implementation of all those modes. |
| Autoregressive scheduling | **VERIFIED:** both make a sampled but discarded text-prefill call, then BOS and previous-latent calls. Eligible EOS begins at zero-based candidate index 6, threshold defaults to -4; emitted tail is 5 frames for short prepared text and 3 otherwise. The terminating candidate is computed but not emitted. Maximum length is `ceil((token_count / 3 + 2) * 12.5)`. |
| Segment state | **VERIFIED:** Python normally copies the original voice state per chunk and creates fresh Mimi state; native resets to the voice prefix and clears Mimi per segment. RNG advances across the chunks of an utterance. Neither ordinary path preserves acoustic context across independently generated chunks. |
| Decoder batching | **VERIFIED:** Python's default decoder worker drains available queued latents into a call; `max_decoder_frames_per_call = 1` enforces frame-by-frame decoding. Native always decodes one latent per call with a queue capacity of two. Existing documentation records PCM differences with coalescing. |
| Numerical implementation | **VERIFIED:** native CPU kernels and Python/PyTorch are not generally bit-identical neural computations. Existing tests establish specified numerical tolerances and scheduling/RNG properties. **UNVERIFIED:** their effect on comma perception outside tested examples. |

**VERIFIED (historical evidence, not rerun):**
[NATIVE_ORCHESTRATION.md](NATIVE_ORCHESTRATION.md) reports a canonical native
sequence of 170 frames / 326400 samples, with native FP32 SHA-256
`36788a2c1cfde712fb9ef68151a93b1b78eec40b1e440e99ff1b1b59d1a8bcf0`.
The corresponding upstream one-frame-decoding hash is different:
`e4e1004ffda5a83b205a6d131d34209a88596012b249657702657c924ec529b9`.
Its comparison reports maximum absolute error 0.00400758, RMS 7.93275e-5 and
correlation 0.999999005. Thus exact native canonical recovery and exact RNG
matching should not be described as arbitrary-text, bit-identical upstream PCM
or verified punctuation quality.

**INFERRED:** floating-point changes can alter a free-running trajectory or an
EOS decision; decoder grouping can change reconstructed audio. Neither fact
demonstrates a punctuation bug. The source path contains no identified native
rule that deletes the two commas in the example. Source parity and seven text
checks still cannot exclude a numerical, voice-selection or SAPI-input issue.

### Text-only checks performed for this audit

**VERIFIED:** seven inputs were compared using the existing native
`outloud-tokenizer-check --text` and the installed upstream tokenizer,
preparation and splitter. All matched in whole prepared text, whole token-ID
sequence and segment strings. No model was constructed and no audio generated.

| Input variation | Observed result in both implementations |
| --- | --- |
| Original Forsaken sentence | 22 tokens, one segment; comma ID 262 at zero-based positions 5 and 14. |
| Same words with both commas removed | 20 tokens, one segment. |
| Same words with both commas replaced by `...` | 22 tokens, one segment; both ellipsis cues preserved. |
| `Wait, traveler,` | Prepared as `Wait, traveler.`; 6 tokens. Internal comma kept, terminal comma changed. |
| `wait; traveler:` followed by newline and `return  here!` | Prepared as `Wait; traveler: return here!`; 10 tokens. Internal semicolon/colon kept. |
| Original plus `Stay here, and wait for my return.` | 33 tokens, one segment after regrouping. |
| Long 68-token sentence below | Two segments, with the first ending in a period substituted for a comma. |

Long input:

```text
We Forsaken, once servants of the Lich King, now fight for our freedom, and we remember the long years when our lives were ruled by shadows, but we will travel through the mountains and the silent valleys until our people are free again.
```

Both produced these segments:

```text
We Forsaken, once servants of the Lich King, now fight for our freedom, and we remember the long years when our lives were ruled by shadows.
But we will travel through the mountains and the silent valleys until our people are free again.
```

## 4. What actually controls intonation

| Part | Role and evidence |
| --- | --- |
| Learned FlowLM/backbone and sampler weights | **VERIFIED:** map text, voice-prefix state and previous acoustic context to hidden states, acoustic latents and EOS. **INFERRED:** learned associations determine linguistic rhythm and phrase-boundary behavior; no separate editable comma-prosody table exists. |
| Voice conditioning (`.olv`) | **VERIFIED:** supplies six layers of model-specific prefix keys/values derived from a voice prompt. It is not merely a speaker ID or a universal embedding. **INFERRED:** prompt delivery/style can influence the generated rhythm and intonation as well as identity. **UNVERIFIED:** how much the existing Gnome/Undead conditioning changes these two commas. |
| Text conditioning | **VERIFIED:** punctuation and words enter learned embeddings and cached transformer context. **INFERRED:** changing punctuation can affect surrounding acoustic realization, including pre-boundary pitch/lengthening and continuation. There is no enforced punctuation-to-pause mapping. |
| Acoustic generation | **VERIFIED:** the previous normalized latent feeds the next transformer step; the whole acoustic suffix remains available to FlowLM. The generated sequence determines local sound content and the number of 80-ms blocks. **INFERRED:** continuity depends on the sampled acoustic history and conditioning, not just reuse of a cache object. |
| Sampling | **VERIFIED:** supplied random noise is combined with the hidden-conditioned flow result. Seed and noise distribution affect the trajectory. **UNVERIFIED:** an interpretable, monotonic relation between a temperature/seed change and comma pause duration or pitch. |
| Mimi | **VERIFIED:** denormalizes latents, applies learned projection, upsampling, causal attention and convolutional decoding with state. It reconstructs the waveform, including pitch and fine timing; it receives no text/comma metadata. **UNVERIFIED:** a decoder setting that selectively fixes comma intonation. |
| Postprocessing/SAPI | **VERIFIED:** native PCM16 conversion clips/scales/rounds samples without changing time. The adapter writes them without a prosodic transformation. **INFERRED:** inserted PCM silence can change perceived timing but cannot make the previously generated phrase ending anticipate that silence. |

**INFERRED:** rhythm, pitch contours, sound duration and pause duration are
jointly determined by the learned conditional acoustic sequence and its
reconstruction. Frame count gives total duration, while transitions within
decoded frames determine finer timing. EOS/tail affect utterance completion,
not a duration allocation for each comma.

Without retraining, one can change text, choose existing voice conditioning,
seed, or implemented generation settings; experimental code could also feed
different context or latents. That mechanical ability does not establish a
trained control mechanism. Reliable new instructions such as a boundary-type
and requested-duration input would need evidence that the current model already
understands them, or suitable training/adaptation.

**UNVERIFIED:** static inspection cannot assign the reported issue mainly to
weights, reference voice, native inference or their interaction. The matched
native/Python and two-voice controls in section 11 provide a minimal diagnostic
screen. They cannot fully separate speaker identity from reference-prompt style
or prove a general model limitation.

## 5. What actually controls natural pauses

**VERIFIED:** there is no special silence token in the 32-dimensional continuous
output path. Mimi's use of a learned continuous representation is different from
selecting a discrete silence code from a token vocabulary. Multiple frames can
be emitted before EOS without a word or punctuation counter advancing in the
host; the decoder can reconstruct low-energy content in those frames.

**INFERRED:** a natural between-phrase pause can be represented by an acoustic
sequence spanning one or more frames, potentially accompanied by pre-boundary
lengthening, a pitch contour, breath or other transitions. Its timing is learned
implicitly through weights and conditioning. **UNVERIFIED:** that a desired
pause will be generated in the example, or that a specific comma should map to
a particular number of low-energy frames. No audio was inspected in this step.

**VERIFIED:** the generation/decoder boundary is coarse at 80 ms per emitted
latent, but decoded waveform events can occur inside a frame. Therefore natural
pause duration is not necessarily an integer multiple of 80 ms. Adding complete
latent frames would add 80-ms blocks; it would not supply fine-grained semantic
placement.

### Alignment and intervention points

**VERIFIED:** current inference returns latent/PCM sequences, counts and EOS
information, not word timings, phoneme durations, a monotonic alignment, or the
audio position of a comma. Native attention computes weighted values without
publishing a linguistic alignment. Upstream's training workflow uses external
word alignments for data preparation; that is not an inference alignment API.
See [pinned training documentation](https://github.com/kyutai-labs/pocket-tts/blob/3dbee45d343d7dddd0d105468d17f8dcba14db3e/training/README.md).

**INFERRED:** attention weights could be instrumented as a research heuristic,
but their interpretation would require validation; a comma can influence many
frames without receiving a unique aligned position. An audio-energy detector
can locate quiet intervals but cannot label which comma they belong to. Manual
listening/annotation can locate these boundaries for a small audit; automatic
reliable placement is **UNVERIFIED** without an alignment method.

The candidate loop exposes a point to change the latent stream, and the sink
exposes a point to edit PCM. **VERIFIED:** neither exposes the semantic boundary
signal necessary for placing a controlled pause at the intended comma. Queue
backpressure or sleeping between compute calls changes wall-clock delivery,
not the encoded PCM duration; relying on playback starvation is not controlled
audio generation.

### Why zero or repeated latents are not established silence controls

**VERIFIED:** `MimiDecoder::decode_frame` first applies
`latent * std + mean`. A zero **normalized** vector becomes the model's mean
latent, not a zero waveform. Learned projections, biases, convolutional carry
and attention history further affect its output. Even a zero denormalized
vector does not guarantee decoded silence. Repeating a prior latent is equally
unsupported as a silence definition.

**INFERRED:** injecting arbitrary vectors is shape-valid at a low-level decoder
interface, but could generate a sustained sound, artifact or unintelligible
transition. If an injected frame is only decoded, FlowLM does not know it
occurred. If it is also fed into the AR model, it changes acoustic conditioning
and cache positions and may be outside the training distribution. Neither
method guarantees that future speech preserves the intended prosody.

**VERIFIED:** the latent generated at one step becomes input at the **next**
step. Any future forced-context implementation must respect that one-step shift,
the learned BOS, positional offsets, EOS latch and RNG scheduling. Simply
appending a decoded frame while leaving these unchanged is not an equivalent
autoregressive continuation.

**UNVERIFIED:** no validated latent-silence sequence, duration-conditioned latent
generator or feedback rule was found that provides natural comma pauses with
these weights. PCM zeros can guarantee an inserted interval's duration, but
they do not update the model's preceding or following prosodic plan.

## 6. Supported and unsupported mechanisms

| Mechanism | Status |
| --- | --- |
| Punctuation-conditioned single-segment generation | **VERIFIED implemented:** normal inference, with learned rather than guaranteed realization. |
| Previous generated acoustic context | **VERIFIED implemented:** normalized latent feedback plus causal KV state during a segment. |
| Voice/audio-prompt conditioning | **VERIFIED upstream:** `get_state_for_audio_prompt` encodes an audio prompt or imports an exported state. Native consumes the corresponding ready `.olv` cache. This is voice conditioning, not an aligned phrase-continuation control. |
| Temperature/noise clamp/multi-step decoding | **VERIFIED upstream parameters:** not all implemented as native runtime options; none documents per-comma duration semantics. |
| Comma pause length, phrase-boundary type, per-word durations | **VERIFIED absent from the inspected inference APIs:** no supported input/output contract found. Desired acoustic effects remain **UNVERIFIED**. |
| SSML or `[pause]` interpreted by the model | **VERIFIED unsupported in the pinned API:** there is no trained pause parser/control here. Plain-text tags could be treated as text; positive SAPI silence is rejected. |
| Continuous teacher-forced chunk continuation | **VERIFIED proposal/TODO, not a completed pinned feature.** |
| Text-aligned silence-latent insertion | **UNVERIFIED:** no silence representation or reliable comma alignment identified. |
| Runtime classifier-free guidance for pause prosody | **UNVERIFIED:** no such public native/pinned inference knob. Training documentation describes guidance baked into distillation; do not equate that with an available runtime controller. |

The relevant upstream research is distinct from implemented production support:

- **VERIFIED:** [issue #151](https://github.com/kyutai-labs/pocket-tts/issues/151)
  is an open continuation experiment proposal. It suggests regenerating with
  combined text while teacher-forcing earlier audio and returning only the new
  suffix. The issue explicitly leaves success uncertain. The pinned public API
  has no `teacher_force_with` argument. **UNVERIFIED:** natural continuity,
  boundary alignment and cost of this recipe in Pocket.
- **VERIFIED:** open [pause PR #242](https://github.com/kyutai-labs/pocket-tts/pull/242)
  parses pause notation and emits PCM zeros between separately generated spoken
  parts. Its [diff](https://github.com/kyutai-labs/pocket-tts/pull/242/files)
  does not establish model-mediated pauses or preserved acoustic state. It is
  not part of 3.3.0 and does not answer the continuous-intonation requirement.
- **VERIFIED:** open [EmoTTS PR #194](https://github.com/kyutai-labs/pocket-tts/pull/194)
  adds a hidden-state shift with external learned adapter weights and emotion
  parameters. Its [diff](https://github.com/kyutai-labs/pocket-tts/pull/194/files)
  is not evidence for an existing-weights-only comma controller. **UNVERIFIED:**
  any improvement in the project's pause/continuation quality.
- **VERIFIED research context:** the [CALM paper](https://arxiv.org/abs/2509.06926)
  studies continuous acoustic autoregression and a conditional flow head.
  It motivates this representation, but its research architectures/settings
  must not be imported as features of this fixed native model. **UNVERIFIED:**
  controllable comma pauses from the paper's concepts alone.

These issue/PR statuses were checked on the audit date; they can change. Later
upstream changes are not silently included in the pinned source comparison.

## 7. Continuous-generation feasibility

The table separates current building blocks from an established quality result.
Compatibility means the original weights and validated `.olv` bytes can remain
unchanged; it does not establish that a new conditioning schedule works well.

| Approach / phase-4 question | Current support and accessible information | Inference, weights, inputs and assets | Risks and evidence |
| --- | --- | --- | --- |
| A. Phrase-boundary conditioning via text | **VERIFIED:** punctuation tokens and whole-segment context already supported. **UNVERIFIED:** an instruction for boundary type/duration. | A text-only test needs a harness/input variant; no weight or asset changes, no new input modality. A new numeric duration/control embedding is not trained support. | **INFERRED:** stronger punctuation might improve a boundary or make it sound final/hesitant. Text-only probes show it can remain one segment; listening is required. |
| B. Preserve existing text/acoustic context | **VERIFIED:** current AR cache already preserves it inside one segment. Previous normalized latents are available in the candidate loop. | No change needed to synthesize the original short sentence continuously. Preserving state across separate `prepare` calls would require a new inference lifecycle; no weights intrinsically needed just to retain memory. Assets remain shape-compatible. | Cache persistence proves access to context, not a learned boundary command. Ordinary preparation deliberately resets both neural states. |
| C. Continue after a controlled pause | **VERIFIED:** computation can wait without destroying state. **UNVERIFIED:** a model-mediated pause of a requested duration. | A host wait needs no weights, but adds no audio samples. Injecting/forcing acoustics needs code, a valid context sequence and placement information; no trained duration input currently exists. Current assets could only support an experimentally validated same-dimensional mechanism. | A paused CPU thread does not produce a pause in an already buffered stream. PCM insertion leaves AR planning unaware; forced latents may damage continuation. |
| D. Reuse/extend KV across generation phases | **VERIFIED:** `backbone_into` appends embeddings at an offset; `prefill` explicitly requires a fresh voice-prefix state. | Reusing a single full-text AR run is ordinary behavior. Adding later text after generated audio needs experimental control code. No mathematical necessity to retrain just to append, but text-after-audio is not the ordinary conditioning schedule. Existing assets fit the interface, not a quality guarantee. | With revised/full text, earlier text/acoustic KV values are not generally interchangeable with a freshly conditioned cache. Capacity, positions and state ownership matter. Cache reuse alone proves no prosodic result. |
| E. Feed generated acoustic context back | **VERIFIED:** already done every frame; `frame_candidate_into` accepts a normalized 32-vector. | A forced-prefix research harness could feed recorded vectors using existing weights/assets. It must replay correct BOS/positions and decide what output is retained. No new modality for same-format generated latents. | No current utterance API accepts a forced prefix, text/audio alignment or resume token. Feeding arbitrary silence vectors is not equivalent to replaying valid generated speech. |
| F. Teacher-forced continuation | **VERIFIED:** low-level latent-input building blocks; upstream TODO/issue. **UNVERIFIED:** complete continuation recipe. | Needs new inference orchestration, prefix replay/recomputation, EOS handling and output cropping. Same-model generated latents need no extra weights. Re-encoding PCM upstream is possible, but native `.olp` excludes Mimi encoder and speaker-projection tensors; that route needs additional assets/code. | Full-text replay costs prefill/acoustic work; continuation crop and decoder context must match. Reusing old KVs under changed text can be wrong. No pause-duration guarantee follows from teacher forcing. |
| G. Logical phrases with natural continuity | **VERIFIED:** a single complete-text segment has no resets at internal commas. Independent chunks do reset. **UNVERIFIED:** a staged phrase schedule with both controllable pauses and natural continuation. | Keeping all text in one existing segment needs no changes. A new phrase scheduler requires a boundary signal and validated conditioning/lifecycle changes; trained new controls could require training. Original assets are compatible only while the model contract remains unchanged. | Earlier synthesis based on incomplete future text cannot retroactively anticipate it. Preserving acoustic history might help but does not prove appropriate pre-pause intonation. |

**VERIFIED:** upstream `copy_state=False` is not a ready-made solution. It can
retain FlowLM state between calls, but the ordinary short-text path still starts
with BOS and constructs fresh Mimi state. That flag alone does not implement
the necessary acoustic resume, changed-text prefill or continuous decoding.

**VERIFIED:** `.olp` exports the synthesis subset. `export_schema.excluded`
omits `mimi.encoder.*`, `mimi.encoder_transformer.*`, `mimi.downsample.*`,
`flow_lm.speaker_proj_weight` and `flow_lm.bos_before_voice`. Upstream raw audio
conditioning uses the omitted encoder/projection; native `.olv` supplies a
precomputed voice prefix instead. Do not propose feeding raw PCM back through
a native encoder that is not present.

**UNVERIFIED:** preserving full-sentence text and acoustic state while introducing
an appropriately shaped pause may be architecturally plausible, but no validated
latent sequence, boundary locator or steering objective was found. This is a
research risk, not an identified inference-only solution.

## 8. Evaluation of possible approaches

The following two tables cover all ten requested axes. Quality/performance
predictions are **INFERRED** unless an implementation fact is explicitly marked
**VERIFIED**. No approach has a measured quality result from this audit.

| Option | Technical feasibility and current model support | Natural intonation | Pause guarantee | Retraining |
| --- | --- | --- | --- | --- |
| A. Text-only cues, one continuous run | **VERIFIED:** existing tokenizer/conditioning supports punctuation variants; no production change needed for a harness. | **UNVERIFIED:** may improve boundaries; ellipses/periods may instead create hesitation or finality. Continuous state is retained. | No predictable position/duration guarantee. | Not for a text experiment. |
| B. Existing inference parameters | **VERIFIED:** upstream exposes noise temperature/clamp/steps; native exposes only its fixed path plus seed/termination settings. Extending native modes needs code. | **UNVERIFIED:** global trajectory changes may help or degrade diction/voice. | No per-comma guarantee; EOS/tail change ending, maximum length can truncate. | Not for supported upstream settings; native parity work would be needed for new modes. |
| C. Acoustic/hidden-state steering | **VERIFIED:** tensors can be modified mechanically; **UNVERIFIED:** a usable direction, objective or silence context with current weights. | **UNVERIFIED:** off-distribution controls could damage identity or continuation. | No validated mechanism. | Not necessarily for a research intervention; a learned adapter/control may need additional training/weights. |
| D. Context-preserving phrase generation | **VERIFIED:** continuous AR building blocks exist; **UNVERIFIED:** a complete teacher-forced/staged phrase recipe. | Potentially preserves more context than resets, but quality is unverified. | Continuation alone does not guarantee a pause or its duration. | Not necessarily for replay with unchanged weights; new semantic controls are unsupported. |
| E. Independent phrases plus stitching | **VERIFIED:** independent segments can already be generated and concatenated. | **INFERRED:** resets and terminal periods can give each phrase a new opening/final ending; crossfade does not restore a shared plan. | No natural gap guarantee unless one also inserts silence. | No. |
| F. Insert PCM silence | **VERIFIED:** technically possible at a PCM interface; production does not do this. Correct comma placement needs alignment. | **INFERRED:** preceding and subsequent generated contours remain unchanged, so naturalness is not assured. | Exact inserted sample duration, once a valid insertion point is known; not a natural-phrase guarantee. | No. |
| G. Fine-tuning | **VERIFIED:** upstream provides a training workflow; a punctuation/pause objective and dataset would need design. | **UNVERIFIED:** improvement and identity preservation require evaluation. | No automatic guarantee from generic fine-tuning; explicit requested-duration behavior needs an appropriate objective/input. | Yes; possibly teacher/student distillation or adaptation. |

| Option | CPU impact | Latency risk | Existing `.olv` voices | Native SAPI suitability | Required validation |
| --- | --- | --- | --- | --- | --- |
| A | Little text-processing cost; changed acoustics can change frame count. | Possible longer speech/generation; prefill remains small for this example. | Compatible with unchanged weights/contract. | Best first **experiment**, not a global rewrite policy. | Native/Python A/B, boundary naturalness, continuation, omissions, identity, frame count and timing. |
| B | Noise changes usually cheap; more sampling steps increase work and native would need implementation. | Extra head steps or frames may delay output/completion. | Compatible if weights/voice-cache contract stay unchanged. | Only after meaningful quality evidence; not an arbitrary seed/temperature search presented as control. | Quality across cases/seeds, EOS/truncation, numerical/scheduling behavior, cost. |
| C | Unknown; simple shifts cheap, search/gradient-based methods could be expensive. | Unknown; steering/alignment/lookahead may add substantial delay. | Shape-compatible for unchanged model, but audible identity unverified; external adapter semantics not covered by current format. | Research only at present. | Steering objective/direction, boundary alignment, natural continuation, artifacts, identity and cost. |
| D | Additional combined-text prefill/latent replay can increase work. | Replay/buffering may delay the next phrase or first output. | Compatible in principle for same weights and generated-latent format; raw-PCM feedback needs omitted components. | Research only until state ownership, cancellation and streaming are proven. | Full lifecycle, text/audio ordering, crop boundary, EOS, Mimi continuity, quality and replay overhead. |
| E | Repeated text prefills/BOS/decoder starts; not automatically cheaper. | More starts, possibly buffering and stitching. | Same prefixes usable, but prosodic consistency is not guaranteed. | Technically feasible but conflicts with the primary continuous-generation goal. | Audible resets, joins, completeness, identity, first-output and total latency. |
| F | Sample insertion itself cheap; reliable alignment might not be. | Need an insertion point/lookahead; streaming could require holding audio. | Compatible; model conditioning is untouched. | Can supply mechanical timing if explicitly desired later; does not solve this quality requirement. | Correct location, perceptual boundary quality, streaming/cancellation and exact sample ledger. |
| G | Training is separate/offline; same inference architecture might retain CPU cost. | New architecture/longer outputs could add delay; cannot predict before training. | **Not guaranteed compatible:** caches depend on model weights and validated compatibility identity. Regenerating from original prompts may be needed. | A later option only if inference tests show a justified limitation. | Held-out prosody quality, clone identity, regenerated cache compatibility, export/native parity and CPU/latency. |

**INFERRED:** fine-tuning a backbone changes the meaning of cached keys/values,
even if tensor shapes are identical. `.olv` model compatibility must not be
bypassed. A narrowly changed head might leave some cached computations
mathematically unaffected, but that is not evidence that today's asset identity
or validation permits using the old file with new weights.

## 9. Recommended next approach and justification

**Recommendation, conditional on evidence:** investigate **option A** first with
a single-segment punctuation-cue experiment and pinned Python controls. It uses
existing voice identities, frozen weights, offline CPU inference and the current
continuous acoustic context. It requires no new conditioning modality or
speculative silence vector. The experiment is justified by verified punctuation
visibility, not by a claim that ellipses will improve narration.

**UNVERIFIED:** there is currently no deployable recommendation that guarantees
natural comma pauses or specified duration. An improved isolated example would
justify more quality evaluation, not a rule changing every comma into an
ellipsis or period. Believable NPC narration requires appropriate continuation
and phrase meaning, not merely a longer low-energy interval.

If Python and native differ materially on the original input, investigate that
discrepancy before changing punctuation. If both share the problem and the cue
variant worsens finality/hesitation, leave production behavior alone; the cue
hypothesis has failed. Context-preserving continuation remains a possible
research direction, but lacks the boundary/control evidence needed to recommend
implementation. Neither outcome by itself establishes a need for fine-tuning.

## 10. Major unknowns and technical risks

- **UNVERIFIED:** pause length and pre/post-boundary intonation in the exact
  native/Python outputs for the reported sentence and voices. Static token
  parity does not measure speech.
- **UNVERIFIED:** whether the real SAPI/WoW request exactly matches the stated
  plain text, voice cache, seed and model configuration. This audit did not
  inspect or alter live registry settings or capture game requests.
- **UNVERIFIED:** sensitivity to numerical drift, seed and voice-prompt style.
  The canonical numerical reference is reassuring but has limited coverage.
- **INFERRED:** stronger punctuation might encourage a full ending rather than
  parenthetical continuation. A pause-only score could incorrectly reward this.
- **VERIFIED:** long-text splitting can replace terminal commas and reset both
  neural states. It is a separate boundary risk that must not be confused with
  the unsplit example.
- **UNVERIFIED:** a reliable automatic comma-to-audio locator. Silence/VAD and
  attention heuristics are not validated linguistic alignment.
- **UNVERIFIED:** valid forced silence latents and their effect on AR/Mimi state,
  following pitch and speaker identity.
- **INFERRED:** replay/teacher-forcing can increase CPU work and latency; changing
  conditioned text invalidates simplistic reuse of prior cached computations.
- **VERIFIED:** raw-PCM feedback is not available from the existing native
  synthesis-only assets; model changes also threaten precomputed voice-cache
  compatibility.

## 11. One minimal proposed experiment — continuous punctuation A/B

**Proposed only. No harness was added and no renders were performed.**

### Hypothesis and bounded design

**UNVERIFIED hypothesis:** replacing the two internal commas with ellipses, while
keeping the same words and one generation segment, encourages model-generated
phrase boundaries that improve both the pre-boundary realization and following
continuation, without audible sentence restarts or a loss of voice identity.

Use **eight short renders**: two voices × two text arms × two implementations,
with the single fixed seed **73**, reset before every render. The two existing
workspace pairs are:

- `generated/assets/gnome_male.olv` ↔ `voices/gnome_male.safetensors`.
- `generated/assets/undead_male.olv` ↔ `voices/undead_male.safetensors`.

The file pairs exist; their pairing/compatibility must be validated before this
future experiment. Python imports the exact original exported voice state,
not a re-encoded prompt that would introduce another variable. Native uses the
existing `.olv` bytes. Do not import either pair into the live library.

This is a small within-voice intervention plus an implementation/voice
diagnostic screen, not a statistical study of all seeds. One fixed reproducible
seed keeps the first experiment small. General reliability across other seeds,
punctuation patterns and voices would remain unverified even after a positive
result; do not add an automatic sweep to this experiment.

Arm A, original:

```text
We Forsaken, once servants of the Lich King, now fight for our freedom.
```

Arm B, punctuation intervention only:

```text
We Forsaken... once servants of the Lich King... now fight for our freedom.
```

**VERIFIED preparation prerequisite:** both have 22 tokens and one segment in
the audit probes. Recheck this in the future harness. The native/Python word
sequence and final period must remain identical. Do not split the three phrases
or insert PCM silence.

### Temporary code and files

Existing native synthesis checkers use canonical inputs; they are not an
arbitrary-text/voice A/B tool. Add a **development-only harness**, if this next
experiment is authorized later, based on `tools/rng-synth/main.cpp` and
`tools/utterance-check/main.cpp`. A possible new location is
`tools/prosody-check/main.cpp`, with a temporary build target, plus an isolated
Python control script under `tools/prosody/`. These names are proposals, not
current commands/files.

The harness should accept text, model/tokenizer/voice paths and seed; call the
existing `UtterancePipeline` with a preallocated output sink, without changing
production classes. Assert one segment outside timing and save WAVs/metadata
after generation. Python should use the installed pinned 3.3.0 API, local
weights/tokenizer and corresponding exported voice state, with
`max_decoder_frames_per_call = 1` to remove the default coalescing difference.
Reset its RNG after loading/validation and before each render. No network
download or new dependency is necessary.

Keep temperature 0.3, noise clamp disabled, LSD one-step, EOS threshold -4,
normal tail/max-length policy and native kernel selection fixed. Keep weights,
tokenizer, voice assets, native ABI, formats, production source, deployed DLL,
SAPI configuration, WPF and registry untouched. Use workspace outputs such as
`generated/prosody/4a/`; do not install, register or play through WoW as part of
this first model-level experiment.

### Quality comparison and falsification

Produce unchanged-gain 24-kHz mono WAVs with matched sample conversion. Randomize
A/B filenames for listening and record the mapping separately. Listen for both
boundaries, after **Forsaken** and **King**:

1. Are the words intact and intelligible, and is the original voice identity
   preserved?
2. Does the preceding phrase sound like a natural parenthetical boundary,
   including appropriate pitch/lengthening, rather than a clipped interruption
   or a full utterance ending?
3. Is the pause appropriate for this sentence, and does the following phrase
   continue naturally rather than restart or hesitate?

For this small set, manually annotate the same linguistic boundary regions in
each WAV; record the annotation uncertainty. Measure local low-energy durations
with a common 10-ms RMS window and a predeclared threshold (for example
-40 dBFS), along with boundary times, total samples/seconds, latent frame count
and EOS termination. Treat this as a supporting descriptor: quiet consonants,
breath and background sound mean low energy is not a complete pause/naturalness
metric. Do not count all low-energy frames globally as comma pauses. No external
ASR/alignment package or newly invented objective prosody score is required.

**Expected result if the hypothesis holds:** B has more appropriate audible
boundaries and natural continuation, with matching words/identity and measured
local timing consistent with the listening judgment. Longer silence alone does
not pass.

**Falsification:** B merely adds hesitation/final endings, loses words or voice
quality, or changes timing without improving the whole boundary/continuation.
An improvement in only one voice limits the hypothesis to that tested case;
it does not establish a general comma rule. An improved B still does not prove
user-selectable pause durations.

Interpret the paired implementation controls separately:

- Native poor, Python natural on A: evidence to investigate the native/input
  path; first compare schedules, latents/EOS and decoder execution. It is not
  automatically proof of a specific bug.
- Both poor on A, with similar B improvement: supports a shared conditioning
  response and the cue hypothesis for this case.
- Both poor and B does not help: this cue intervention failed; it does not prove
  all inference-only approaches impossible.
- Different behavior between Gnome and Undead in both implementations: supports
  a voice-conditioning interaction; it cannot isolate speaker identity from
  prompt style or make weights irrelevant.

### Timing, reproducibility and stopping

Load and validate assets before timing. Run one untimed warm-up of the original
sentence per implementation/voice pair, then reset seed and neural state before
each of the eight measured renders. Capture generation-start-to-first-PCM and
total native generation time using the ordinary sink; do not add profiler or
per-candidate hooks to those timed runs. Record Python first yield/total time
with its frame-decoding setting documented; these are comparable observations,
not guaranteed identical timing boundaries.

No WAV/JSON writing or output validation belongs inside the timed region.
Preallocate output storage and validate segment/frame/sample scheduling outside
it. Report the single timing observation per arm/voice/implementation, emitted
sample count and audio duration; do not infer a speedup distribution from these
quality renders. Two different voices/arms are not timing repetitions.

If a formal performance comparison later becomes necessary, use the existing
rule per main mode: asset validation outside timing, one untimed warm-up,
**five timed repetitions**, seed/state reset before every run, scheduling and
file output outside timing, no hooks/profiler; report min/median/max and compare
**medians**. Do not add that campaign to this small quality screen or create
performance CTests. Any later inference implementation must retain the existing
correctness suite; a documentation-only audit does not require rerunning it.

Stop after the eight captures and analysis. A positive finding authorizes no
automatic production rewrite, seed sweep or expanded performance run.

## 12. Expected integration impact on Out Loud

**VERIFIED:** this audit has no runtime integration impact. Current Voice Tester
uses real SAPI playback and supplies plain text; it does not provide a new
model-pause control. Existing native streaming and caller-thread SAPI output
remain unchanged.

**INFERRED, conditional future work:** if a cue policy survives broader quality
evaluation, it could be exposed first as an explicit experimental text mode in
Voice Tester through its existing backend abstraction, with the original text
available for comparison. That is not a proposed UI change in this step. A
native production policy would require documented input semantics, tests for
chunking/selection/cancellation and safeguards against unintended changes to
names, quoted speech and existing voices.

With unchanged weights and conditioning, an eventual text-only approach could
retain `.olp/.olv` formats, offline CPU inference and SAPI deployment. A validated
continuation implementation would have broader lifecycle/streaming consequences;
raw-audio feedback or trained new conditioning would require a separate asset
and compatibility design. No such rearchitecture is warranted by this audit.

## 13. Exact source-code references and provenance

### Native and frontend sources

Line numbers refer to repository baseline `2b1ee6f`. Links identify files; the
listed functions/line spans locate the inspected behavior.

| Source | Location / responsibility |
| --- | --- |
| [text_pipeline.cpp](../engine/src/text_pipeline.cpp) | `terminal_punctuation`, line 30; `prepare_text`, 79; `process_text`, 102: normalization, punctuation boundaries, regrouping and per-chunk tails. |
| [text_pipeline.hpp](../engine/include/outloud/text_pipeline.hpp) | `process_text`, lines 25–26: default token budget 50. |
| [tokenizer.cpp](../engine/src/tokenizer.cpp) | Constructor, 61; `encode_piece`, 162; `encode_ordinary`, 211; `encode`, 230; `decode`, 252: pinned schema, Unigram, metaspace, byte fallback and special tokens. |
| [flowlm.cpp](../engine/src/flowlm.cpp) | `FlowLMPrefillState::reset`, 28; `embed`, 66; `prefill`, 77; `backbone_into`, 91; attention call, 135: voice/text/acoustic KV layout, positions and hidden/EOS output. |
| [kernels.cpp](../engine/src/cpu/kernels.cpp) | `rope`, 169; `causal_attention`, 198; visible-position calculation, 213: causal prefix/suffix attention. |
| [flow_sampler.cpp](../engine/src/flow_sampler.cpp) | `sample`, 26; one-step latent result, around 94; `bos_candidate_into`, 114; `frame_candidate_into`, 118; `candidate_into`, 123: hidden-conditioned flow and normalized feedback. |
| [production_noise.cpp](../engine/src/production_noise.cpp) | `reseed`, 72; `next`, 96; standard-deviation constant, 98: fixed-temperature ATen-compatible noise. |
| [generation.hpp](../engine/include/outloud/generation.hpp) | `GenerationOptions`, 26–29; `GenerationCall` / observer, 44–53: termination options and available development observations. |
| [generation.cpp](../engine/src/generation.cpp) | `GenerationStopPolicy::emit`, 38; `estimate_max_gen_len`, 43; `generate_segment`, 56 onward: scheduling and EOS policy. |
| [worker_pipeline.cpp](../engine/src/worker_pipeline.cpp) | Worker reset loop, 95–110; `produce`, 132; `consume`, 186; `prepare` overloads, 232/239; `run`, 265: AR feedback, two-worker queue, resets and decode. |
| [mimi.cpp](../engine/src/mimi.cpp) | `MimiDecoderState::reset`, 23; `decode_frame`, 101; denormalization, 113: persistent decoder state and latent-to-PCM reconstruction. |
| [orchestration.cpp](../engine/src/orchestration.cpp) | `UtterancePipeline::prepare`, 45; `run`, 64; reseed, 75; per-segment worker preparation, 83: text budget, independent segment state and concatenated PCM. |
| [assets.hpp](../engine/include/outloud/assets.hpp) | `PocketArchitecture`, 18–27; `OutLoudVoice`, 59–77: temporal constants and model-specific voice-prefix API. |
| [export_schema.py](../tools/assets/export_schema.py) | `excluded`, 12–14: encoder and voice-projection tensors omitted from native synthesis assets. |
| [support.hpp](../sapi/src/support.hpp) | `native_format`, 13; `collect_request`, 34: text/bookmark fragments, silence rejection and 24-kHz PCM format. |
| [adapter.cpp](../sapi/src/adapter.cpp) | `Configuration`, 43; `read_configuration`, 52; `Resident`, 91 onward; `RequestControl`, 102 onward; `Delivery`, 140 onward; `write_frame`, 164; `stream`, 181; `Engine::Speak`, 266: runtime options, controls, conversion and streaming. |
| [pcm16.hpp](../engine/include/outloud/pcm16.hpp) | `float_to_pcm16`, 11; `convert_pcm16`, 20: sample conversion without timing/prosody transformation. |
| [VoiceTesterViewModel.cs](../ui/OutLoud.VoiceManager/ViewModels/VoiceTesterViewModel.cs) | Test request construction: plain text passed to the existing test backend. |
| [SapiTestSessionFactory.cs](../ui/OutLoud.VoiceManager/Services/SapiTestSessionFactory.cs) | `PlainAsync` and SAPI `Speak`: current plain-text asynchronous playback. |
| [rng-synth/main.cpp](../tools/rng-synth/main.cpp), [utterance-check/main.cpp](../tools/utterance-check/main.cpp) | Existing fixed-canonical synthesis/checker examples; not a general prosody A/B CLI. |
| [pipeline-baseline.json](../reference/pipeline-baseline.json), [pocket-model-schema.json](../reference/pocket-model-schema.json) | Pinned model/tokenizer reference identities and fixed synthesis asset contract. |

Existing [NATIVE_RNG.md](NATIVE_RNG.md),
[NATIVE_ORCHESTRATION.md](NATIVE_ORCHESTRATION.md),
[POCKET_PIPELINE.md](POCKET_PIPELINE.md),
[VOICE_IMPORT.md](VOICE_IMPORT.md) and
[VOICE_MANAGER_TESTER.md](VOICE_MANAGER_TESTER.md) were used for historical
validation and frontend/voice context. Their previous build/test/performance
results were not rerun for Step 4A.

### Installed Python functions, matched to the immutable upstream commit

Paths below are under `.venv/Lib/site-packages/pocket_tts/`; linked equivalents
use the full verified upstream commit.

| Pinned source | Function / relevant lines |
| --- | --- |
| [models/text_chunking.py](https://github.com/kyutai-labs/pocket-tts/blob/3dbee45d343d7dddd0d105468d17f8dcba14db3e/pocket_tts/models/text_chunking.py) | `prepare_text_prompt`, 15; `_ensure_terminal_punctuation`, 61; decimal boundary logic, 82; `split_into_best_sentences`, 143. |
| [modules/text_conditioner.py](https://github.com/kyutai-labs/pocket-tts/blob/3dbee45d343d7dddd0d105468d17f8dcba14db3e/pocket_tts/modules/text_conditioner.py) | `JsonTokenizer`, 57; encode/decode, 66/69; `LUTConditioner` prepare/forward, 139/142. |
| [models/flow_lm.py](https://github.com/kyutai-labs/pocket-tts/blob/3dbee45d343d7dddd0d105468d17f8dcba14db3e/pocket_tts/models/flow_lm.py) | `lsd_decode`, 20; `forward`, 121; positive-step assertion, 151; EOS/noise, 154–165; `backbone`, 167: text/audio projection and flow sampling. |
| [models/tts_model.py](https://github.com/kyutai-labs/pocket-tts/blob/3dbee45d343d7dddd0d105468d17f8dcba14db3e/pocket_tts/models/tts_model.py) | Timing/EOS constants, 85–89; `load_model`, 285; `_run_flow_lm_and_increment_step`, 390; `_run_flow_lm`, 421; `_encode_audio`, 448; cache helpers, 459/492; `_decode_audio_worker`, 503; `generate_audio_stream`, 635; teacher-forcing TODO, 702–705; `_generate_audio_stream_short_text`, 741; `_generate`, 821; `_autoregressive_generation`, 858; `get_state_for_audio_prompt`, 914; `_estimate_max_gen_len`, 1040. |
| [modules/attention.py](https://github.com/kyutai-labs/pocket-tts/blob/3dbee45d343d7dddd0d105468d17f8dcba14db3e/pocket_tts/modules/attention.py) | `_LinearKVCacheBackend`, 58; append/get, 93; `StreamingMultiheadAttention.forward`, 170: cache append, offsets and causal masks. |
| [models/mimi.py](https://github.com/kyutai-labs/pocket-tts/blob/3dbee45d343d7dddd0d105468d17f8dcba14db3e/pocket_tts/models/mimi.py) | `decode_from_latent`, 92; continuous latent encode/decode interfaces. |
| [utils/config.py](https://github.com/kyutai-labs/pocket-tts/blob/3dbee45d343d7dddd0d105468d17f8dcba14db3e/pocket_tts/utils/config.py), [config/english.yaml](https://github.com/kyutai-labs/pocket-tts/blob/3dbee45d343d7dddd0d105468d17f8dcba14db3e/pocket_tts/config/english.yaml) | Text preparation flags and fixed English model configuration. |
| [default_parameters.py](https://github.com/kyutai-labs/pocket-tts/blob/3dbee45d343d7dddd0d105468d17f8dcba14db3e/pocket_tts/default_parameters.py) | Default one-step sampler, no clamp, EOS -4 and 50-token chunking. |

The `forward` docstring mentions a zero-step loss mode, but the inspected
implementation asserts a positive step count. **VERIFIED:** the docstring is
not proof of a callable teacher-forcing/training mode in this inference path.

Byte identity checks performed for this audit:

| File | SHA-256: installed file = wheel RECORD = pinned upstream raw file |
| --- | --- |
| `models/text_chunking.py` | `215a78549b527495206b93e0941449a8ffb30be1bcf6aa311f5de8ea0d8716d3` |
| `models/tts_model.py` | `7abdbb4c47615c7b8c04359d13be4cabf3ece7d226205cd5556fb5ff4c06dd22` |
| `models/flow_lm.py` | `803333bff6a6f5bc17107ce5e6479e17e12ee012fd5279494e24e1ed955e94a5` |
| `models/mimi.py` | `93996a84525e41fb204a53f1edd72abdf5925f6bb8f92304e87d29c7a431054b` |
| `modules/attention.py` | `4b1763c527e6bb87c4152c7f17f5aa30a88164f1ed11136110539a53d0dbd296` |
| `modules/text_conditioner.py` | `d3f3de0f7eb9f400ed7bd484981f6682a3690c09ea9e8c25ebb6d88564ce18ac` |
| `utils/config.py` | `dcabaccaf030fd215989e75b89b1867351eaa905132091f6cfea31539980f4fe` |
| `config/english.yaml` | `1b06236b6a4405a4010c731da097745c23d3d5c543e712e31086b6293fd1124a` |

## 14. Upstream documentation and research references

Primary references, with their evidentiary limits:

| Reference | What it establishes |
| --- | --- |
| [Pocket TTS 3.3.0 README](https://github.com/kyutai-labs/pocket-tts/blob/3dbee45d343d7dddd0d105468d17f8dcba14db3e/README.md) | Pinned feature surface and explicit-pause limitation; not quality evidence for this sentence. |
| [Pause feature request #6](https://github.com/kyutai-labs/pocket-tts/issues/6) | Upstream request/discussion about explicit pauses; not a supported inference recipe. |
| [Teacher-forcing proposal #151](https://github.com/kyutai-labs/pocket-tts/issues/151) | Exploratory combined-text/forced-audio continuation idea, consistent with the pinned TODO. |
| [Pause PR #242](https://github.com/kyutai-labs/pocket-tts/pull/242), [code diff](https://github.com/kyutai-labs/pocket-tts/pull/242/files) | Proposed parser plus PCM-zero insertion; not continuous model-mediated prosody control. |
| [EmoTTS PR #194](https://github.com/kyutai-labs/pocket-tts/pull/194), [code diff](https://github.com/kyutai-labs/pocket-tts/pull/194/files) | Proposed external learned adapter/hidden-state steering, not current-weights-only comma control. |
| [Pinned training README](https://github.com/kyutai-labs/pocket-tts/blob/3dbee45d343d7dddd0d105468d17f8dcba14db3e/training/README.md) | Available training/fine-tuning workflow, training-data alignments and teacher/student distillation; not a reason to retrain before testing current inference. |
| [Continuous Audio Language Models, arXiv:2509.06926](https://arxiv.org/abs/2509.06926), [paper text](https://arxiv.org/html/2509.06926v1) | Research background for continuous acoustic generation; does not establish this model's comma timing controller. |

**VERIFIED report validation:** all fourteen requested sections are present;
local file links resolve; the eight recorded source hashes match the installed
files; no trailing whitespace was found. Git whitespace checks found no errors
in the report. The seven text-only comparisons described in section 3 passed.
No new test or performance gate was added; previous native/frontend test results
are historical evidence, not Step 4A test runs.

**Audit stop point:** research report completed; no experiment implemented or
executed, no production or deployment changes, and nothing staged, committed
or pushed.
