# Models and runtime updates

Out Loud 0.4.0 manages models and runtime updates in **Settings**. The official
Pocket TTS Rust executable is the only synthesis backend. Models are native
synthesis-only assets from the public
`kyutai/pocket-tts-without-voice-cloning` repository; no conversion or cloning
pipeline is used.

## Models

1. Open Settings and choose **Install** beside a supported model.
2. Confirm installation. Out Loud downloads and verifies the model/tokenizer
   assets before recording the installation.
3. Choose the installed model in **Selected model**. Installation alone does
   not select it, and an empty selection never picks a fallback automatically.

**Check for updates** compares installed assets with pinned public repository
metadata. It downloads metadata only and does not automatically replace weights.
Startup and completed model installations also queue the existing cached check;
manual checks bypass the cache interval. Unavailable metadata does not change
installed models or the selection. There is no model-update installation or
automatic repair action.

**Cancel** cancels the current operation and uses its recorded staging cleanup.
**Remove** unregisters the model and removes verified blobs that no retained
inventory record uses. Removing or invalidating the selected model clears its
selection. Assets held by active synthesis remain protected.

## Runtime

The application includes a verified baseline executable. **Check for updates**
queries the official Kyutai Pocket TTS GitHub release source. **Update**, when
available and confirmed, downloads and validates the archive, executable and
license, then activates the verified update atomically. SAPI uses that active
runtime without a version allowlist; an invalid selected update is an error,
not permission to silently substitute another runtime.

After successful activation, verified inactive downloaded runtime versions are
removed. Busy or interrupted cleanup is retried at startup under the existing
runtime lock. The bundled baseline remains part of the application inventory.
Cleanup does not run on the speech synthesis path.

## Storage and access

Models, runtime updates, inventories and selection are shared across accounts
under `%ProgramData%\OutLoud\ApplicationData`. Out Loud requests administrator
approval once at startup. External SAPI clients can play speech unelevated.
Native voice profiles remain independent of model IDs and revisions.

See [WinUI storage and verification](../ui/OutLoud.WinUI/README.md),
[Rust model resolution](RUST_RUNTIME_SESSION.md#production-integration-independent-native-profiles-and-settings-selection),
and [upgrade/uninstall data boundaries](../installer/PRODUCTION.md).
