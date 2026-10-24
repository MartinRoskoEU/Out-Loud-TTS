# Changelog

## 0.3.0

- Updated official Kyutai English Pocket TTS weights from `english_2026-09_2`
  to `english_2026-10`, preserving exact BF16 precision and the existing native
  inference architecture. Tokenizer and OLV v1 format are unchanged.
- Applied the new model configuration's apostrophe/whitespace normalization.
- Preserved the 0.2.1 SAPI volume handling, engine identity and existing
  in-place upgrade behavior. Existing voices are not regenerated; acoustic
  equivalence with older profiles is unverified. See [release notes](docs/releases/0.3.0.md).

## 0.2.1

- Fixed native SAPI baseline volume, initial-volume queries and live volume
  changes, including partial writes and speech-rate output/tails.
- Combined SAPI baseline and XML fragment volume as percentages, retaining
  fragment volume boundaries. Zero is silent; full volume preserves PCM bytes.
- Voice Manager retains normal SAPI playback at volume 100 and Windows audio
  controls. No new UI setting, model update or voice-format change.

## 0.2.0

- Updated the official Kyutai Pocket TTS English weights to immutable revision
  `7f364c05d1be3faf2b234cdbe4cbb68a026d4218`; the English architecture and tokenizer
  are unchanged. See [release notes](docs/releases/0.2.0.md) for hashes/provenance.
- Reusable developer-side automatic model discovery, verified download and OLP
  conversion, automatic engine versioning on changed weights, integrity checks,
  validation, model history and transactional rollback.
- Simplified Voice Manager and native voice handling: no per-voice model
  classifications or Legacy/Unknown warnings. Supported OLV v1 and prepared
  Safetensors voices remain importable/playable after strict structural and
  integrity validation. Existing voice identities and files are preserved.
- Explicit forward cross-version development deployment checks the expected
  installed version, updates the full package with the existing signing identity,
  and journals installed version metadata together with payload rollback.
- Expanded the default Voice Tester passage for manual listening tests.
- Optional historical-installer preservation during local packaging; existing
  artifact-retention defaults and release exclusion from Git remain unchanged.

Existing profiles prepared with older weights may sound different or exhibit
artifacts. Best quality may require preparing a new profile with the updated
model and original reference audio. Voices are not automatically converted or
regenerated; structural support does not guarantee acoustic quality.

## 0.1.0

First public release of Out Loud for Windows 11 x64.

- Offline English speech through a native Pocket TTS engine and Windows SAPI5.
- Voice Manager import of compatible `.olv` and prepared Pocket `.safetensors`
  voices, confirmed replacement/removal, portable voice export, speech testing
  and WAV export. No voices are bundled.
- Legacy Pocket TTS 6L `current_end` voice states and states without explicit
  padding normalize during import into the existing `.olv` format. Source files
  are preserved and strict tensor/model compatibility validation remains in place.
- Complete known-format identification for incompatible Kyutai TTS 1.6B / DSM
  `speaker_wavs` profiles, with an **Incompatible Voice Profile** notice explaining
  the supported Pocket TTS 6L format. Unknown/malformed files retain appropriate
  diagnostics; structural identification does not assert model provenance.
- SAPI site speech-rate control from -10 to +10 using statically linked Sonic,
  preserving pitch. Rate 0 bypasses Sonic and retains the original PCM.
- Inno Setup installation with prerequisite checks, explicit license and Kyutai
  policy acknowledgment, installation-owned signing, SAPI registration and
  checked maintenance/recovery.
- Inventory-aware updates add, replace and retire verified managed files while
  preserving voices and installation identity. Rollback restores the original
  files and inventory; the 23-to-24-file update is covered by isolated checks.
- Complete uninstall with explicit confirmation of permanent managed-voice and
  application-data deletion; external backups and shared Microsoft runtimes remain.
- Automatic build artifact cleanup removes verified intermediates and retains
  current/previous successful publication pairs, required stages and publication
  records. Referenced, incomplete or uncertain artifacts are preserved.
- MIT original-code licensing, Nendis copyright, Kyutai CC BY 4.0 attribution
  and prohibited-use policy, and Sonic Apache 2.0 license included.

Windows 10, ARM64, 32-bit Windows and Windows Server are unsupported.
Raw-recording voice creation and XML fragment-specific speech rates are not included.
