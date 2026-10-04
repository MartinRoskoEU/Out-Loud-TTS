# Pocket TTS Upstream Reference

This directory documents the untouched upstream Pocket TTS implementation used
as the reference oracle for the Out Loud Pocket engine.

## Environment

- Python: 3.12.10
- Pocket TTS: 3.3.0
- PyTorch: 2.14.1+cpu
- huggingface-hub: 1.33.0
- safetensors: 0.8.0
- Device: CPU
- Platform: Windows 11

## Runtime policy

- PyTorch intra-op threads before Pocket import: 12
- PyTorch inter-op threads before Pocket import: 12
- PyTorch intra-op threads after Pocket import: 1
- PyTorch inter-op threads after Pocket import: 12
- MKLDNN: enabled
- dtype: float32

## English model

Repository:

`kyutai/pocket-tts`

File:

`languages/english/model.safetensors`

Revision:

`983151f13aaeab1b13c1e5e3c2c383d49a9edf3f`

SHA-256:

`fb0dc01b0d4d2e1c905b7a3e0676e3d9c96d5ae460e24e3ab94981805babf997`

## Tokenizer

Repository:

`kyutai/pocket-tts-without-voice-cloning`

File:

`languages/english/tokenizer.json`

Revision:

`00eac05ed3d16bdc3f6b5d598874019c34a89214`

SHA-256:

`f498428e1eafee50492f7be13dc9bfafcfc12e508cd0eb1b01c92ecd5d8c6687`

## Generation defaults

- Temperature: 0.3
- Sample rate: 24000 Hz
- Channels: 1
- FlowLM dtype: float32
- Mimi dtype: float32
- Mimi frame rate: 12.5 Hz
- Audio frame duration: 80 ms

## Reference performance

Initial upstream CLI test:

- Text: `The watch captain needs your help before the next caravan arrives.`
- Audio duration: 3.360 s
- Generation time: 0.875 s
- Speed: 3.84x realtime
- Prompting time: 46 ms
- Average generation step: 17 ms

INT8 test:

- Audio duration: 3.280 s
- Generation time: 1.031 s
- Speed: 3.18x realtime
- Prompting time: 47 ms
- Average generation step: 22 ms

On this machine, the upstream FP32 configuration is currently faster than the
tested dynamic INT8 configuration.

## Reference voices

### Gnome male

File:

`voices/gnome_male.safetensors`

SHA-256:

`2118ea60dfe87bd515b27e051e1d677a7fe15663875740599147bde37a9c4cea`

### Undead male

File:

`voices/undead_male.safetensors`

SHA-256:

`dce632cddb3a33203e140c73b2fa3970325bfbe3984813ddfe3e43e328037b6`

The voice files themselves are local runtime/test assets and are intentionally
not tracked by Git.

## Oracle baseline

The upstream Python implementation is used as the authoritative reference
during development of the native Out Loud Pocket engine.

### Test configuration

- Voice: `gnome_male.safetensors`
- Voice SHA-256: `2118ea60dfe87bd515b27e051e1d677a7fe15663875740599147bde37a9c4cea`
- Seed: 73
- Temperature: 0.3
- Sampler decode steps: 1
- Quantization: disabled
- Sample rate: 24000 Hz
- Output type used for exact comparison: float32 PCM

### Test text

`The watch captain needs your help before the next caravan arrives. Follow the river to the abandoned tower, speak with the scout waiting at its gate, and bring his report back to the village. Keep your lantern burning, for the path is narrow.`

### Deterministic output

- Audio duration: 13.600 s
- Samples: 326400
- Streaming chunks: 170
- Chunk duration: 80 ms
- Float32 SHA-256: `e4e1004ffda5a83b205a6d131d34209a88596012b249657702657c924ec529b9`

Repeated generation in the same process with the same seed produced an exact
bit-for-bit float32 match.

### Reference performance

Model load:

- 0.515 s

Voice state load:

- 0.001 s

Cold generation:

- First PCM: 99.6 ms
- Generation time: 3.493 s
- Speed: 3.894x realtime

Warm generation:

- First PCM: 90.4 ms
- Generation time: 3.523 s
- Speed: 3.861x realtime

These timings are informational performance baselines. Exact regression
comparison is based on the deterministic float32 output, not timing.