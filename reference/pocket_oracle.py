from __future__ import annotations

import hashlib
import importlib.metadata
import json
import os
import platform
import random
import time
import wave
from pathlib import Path

# The reference oracle must never silently fetch newer files.
os.environ["HF_HUB_OFFLINE"] = "1"

import numpy as np
import torch
from pocket_tts import TTSModel


ROOT = Path(__file__).resolve().parents[1]

VOICE_PATH = ROOT / "voices" / "gnome_male.safetensors"
OUTPUT_DIR = ROOT / "generated"

TEXT = (
    "The watch captain needs your help before the next caravan arrives. "
    "Follow the river to the abandoned tower, speak with the scout waiting "
    "at its gate, and bring his report back to the village. "
    "Keep your lantern burning, for the path is narrow."
)

SEED = 73
TEMPERATURE = 0.3
SAMPLER_DECODE_STEPS = 1


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()

    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)

    return h.hexdigest()


def reset_rng() -> None:
    random.seed(SEED)
    np.random.seed(SEED)
    torch.manual_seed(SEED)


def generate_once(
    model: TTSModel,
    voice_state,
    label: str,
) -> tuple[torch.Tensor, dict]:
    reset_rng()

    chunks: list[torch.Tensor] = []

    start = time.perf_counter()
    first_chunk_seconds = None

    for chunk in model.generate_audio_stream(
        model_state=voice_state,
        text_to_generate=TEXT,
        copy_state=True,
    ):
        if first_chunk_seconds is None:
            first_chunk_seconds = time.perf_counter() - start

        chunks.append(
            chunk.detach()
            .cpu()
            .float()
            .contiguous()
            .view(-1)
        )

    generation_seconds = time.perf_counter() - start

    if not chunks:
        raise RuntimeError("Pocket TTS returned no audio chunks.")

    audio = torch.cat(chunks, dim=0).contiguous()

    sample_rate = model.sample_rate
    sample_count = audio.numel()
    audio_seconds = sample_count / sample_rate
    x_realtime = (
        audio_seconds / generation_seconds
        if generation_seconds > 0
        else 0.0
    )

    raw_f32 = (
        audio.numpy()
        .astype("<f4", copy=False)
        .tobytes()
    )

    result = {
        "label": label,
        "seed": SEED,
        "chunk_count": len(chunks),
        "sample_count": sample_count,
        "sample_rate": sample_rate,
        "audio_seconds": audio_seconds,
        "first_chunk_seconds": first_chunk_seconds,
        "generation_seconds": generation_seconds,
        "x_realtime": x_realtime,
        "float32_sha256": sha256_bytes(raw_f32),
    }

    return audio, result


def write_listening_wav(
    path: Path,
    audio: torch.Tensor,
    sample_rate: int,
) -> None:
    # This PCM16 WAV is only a listening artifact.
    # Exact oracle comparison uses the raw float32 output.
    pcm = (
        audio.clamp(-1.0, 1.0)
        .mul(32767.0)
        .round()
        .to(torch.int16)
        .numpy()
        .astype("<i2", copy=False)
    )

    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(sample_rate)
        wav.writeframes(pcm.tobytes())


def main() -> None:
    if not VOICE_PATH.exists():
        raise FileNotFoundError(
            f"Reference voice not found: {VOICE_PATH}"
        )

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    print("=== Out Loud Pocket upstream oracle ===")
    print()
    print(f"Pocket TTS: {importlib.metadata.version('pocket-tts')}")
    print(f"PyTorch:    {torch.__version__}")
    print(f"Python:     {platform.python_version()}")
    print(f"CPU device: yes")
    print(f"Voice:      {VOICE_PATH}")
    print(f"Voice SHA:  {sha256_file(VOICE_PATH)}")
    print(f"Seed:       {SEED}")
    print()

    model_load_start = time.perf_counter()

    model = TTSModel.load_model(
        language="english",
        temp=TEMPERATURE,
        sampler_decode_steps=SAMPLER_DECODE_STEPS,
        quantize=False,
    )

    model_load_seconds = time.perf_counter() - model_load_start

    print(f"Model load: {model_load_seconds:.3f} s")
    print(
        f"Threads:    intra={torch.get_num_threads()} "
        f"interop={torch.get_num_interop_threads()}"
    )

    voice_load_start = time.perf_counter()

    voice_state = model.get_state_for_audio_prompt(
        str(VOICE_PATH)
    )

    voice_load_seconds = time.perf_counter() - voice_load_start

    print(f"Voice load: {voice_load_seconds:.3f} s")
    print()

    # First generation after model load.
    cold_audio, cold = generate_once(
        model,
        voice_state,
        "cold",
    )

    # Same model, same voice state, same seed.
    warm_audio, warm = generate_once(
        model,
        voice_state,
        "warm",
    )

    exact_same_process = torch.equal(
        cold_audio,
        warm_audio,
    )

    print("--- Cold generation ---")
    print(f"First PCM:  {cold['first_chunk_seconds'] * 1000:.1f} ms")
    print(f"Generation: {cold['generation_seconds']:.3f} s")
    print(f"Audio:      {cold['audio_seconds']:.3f} s")
    print(f"Speed:      {cold['x_realtime']:.3f}x realtime")
    print(f"Chunks:     {cold['chunk_count']}")
    print(f"Samples:    {cold['sample_count']}")
    print(f"SHA256:     {cold['float32_sha256']}")
    print()

    print("--- Warm generation ---")
    print(f"First PCM:  {warm['first_chunk_seconds'] * 1000:.1f} ms")
    print(f"Generation: {warm['generation_seconds']:.3f} s")
    print(f"Audio:      {warm['audio_seconds']:.3f} s")
    print(f"Speed:      {warm['x_realtime']:.3f}x realtime")
    print(f"Chunks:     {warm['chunk_count']}")
    print(f"Samples:    {warm['sample_count']}")
    print(f"SHA256:     {warm['float32_sha256']}")
    print()

    print(
        "Cold/warm exact float32 match: "
        f"{'YES' if exact_same_process else 'NO'}"
    )

    # The warm run is our reference artifact.
    raw_f32_path = OUTPUT_DIR / "pocket_oracle_gnome.f32"
    wav_path = OUTPUT_DIR / "pocket_oracle_gnome.wav"
    json_path = OUTPUT_DIR / "pocket_oracle_gnome.json"

    warm_np = (
        warm_audio.numpy()
        .astype("<f4", copy=False)
    )

    raw_f32_path.write_bytes(warm_np.tobytes())

    write_listening_wav(
        wav_path,
        warm_audio,
        model.sample_rate,
    )

    report = {
        "oracle": "Pocket TTS upstream",
        "pocket_tts_version": importlib.metadata.version(
            "pocket-tts"
        ),
        "torch_version": torch.__version__,
        "python_version": platform.python_version(),
        "text": TEXT,
        "voice_path": str(
            VOICE_PATH.relative_to(ROOT)
        ),
        "voice_sha256": sha256_file(VOICE_PATH),
        "temperature": TEMPERATURE,
        "sampler_decode_steps": SAMPLER_DECODE_STEPS,
        "quantize": False,
        "seed": SEED,
        "model_load_seconds": model_load_seconds,
        "voice_load_seconds": voice_load_seconds,
        "intra_op_threads": torch.get_num_threads(),
        "interop_threads": torch.get_num_interop_threads(),
        "cold": cold,
        "warm": warm,
        "cold_warm_exact_float32_match": exact_same_process,
    }

    json_path.write_text(
        json.dumps(report, indent=2),
        encoding="utf-8",
    )

    print()
    print(f"Raw oracle: {raw_f32_path}")
    print(f"WAV:        {wav_path}")
    print(f"Report:     {json_path}")


if __name__ == "__main__":
    main()