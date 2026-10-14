# Changelog

## 0.1.0

First public release of Out Loud for Windows 11 x64.

- Offline English speech through a native Pocket TTS engine and Windows SAPI5.
- Voice Manager import of compatible `.olv` and prepared Pocket `.safetensors`
  voices, confirmed replacement/removal, portable voice export, speech testing
  and WAV export. No voices are bundled.
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
- MIT original-code licensing, Nendis copyright, Kyutai CC BY 4.0 attribution
  and prohibited-use policy, and Sonic Apache 2.0 license included.

Windows 10, ARM64, 32-bit Windows and Windows Server are unsupported.
Raw-recording voice creation and XML fragment-specific speech rates are not included.
