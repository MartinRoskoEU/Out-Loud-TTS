# Models and runtime updates

Out Loud 0.4.3 manages models and runtime updates in **Settings**. The official
Pocket TTS Rust executable is the only synthesis backend. Models are native
synthesis-only assets from the public
`kyutai/pocket-tts-without-voice-cloning` repository; no conversion or cloning
pipeline is used.

Use the single **Check for updates** button at the top of Settings to check both
the runtime and installed models. Results remain in the runtime card and model
rows. The button is disabled while either check or a conflicting operation is
running. Checking never installs an update; each **Update** still requires its
own confirmation.

## Models

1. Open Settings and choose **Install** beside a supported model.
2. Confirm installation. Out Loud downloads and verifies the model/tokenizer
   assets before recording the installation.
3. Choose the installed model in **Selected model**. Installation alone does
   not select it, and an empty selection never picks a fallback automatically.

The model check compares installed assets with pinned public repository
metadata. It downloads metadata only and does not automatically replace weights.
Startup and completed model installations also queue the existing cached check;
manual checks bypass the cache interval. Unavailable metadata does not change
installed models or the selection.

When an update is available for a verified, runtime-compatible model, **Update**
appears immediately before **Remove** on that row. Click **Update** and confirm
the displayed repository revision. Out Loud downloads the exact public snapshot,
verifies every required hash/size and the weights' tensor layout, then atomically
replaces that model's inventory record. The explicitly selected alias stays
selected; updating a different model never selects it. Failure or cancellation
before activation preserves the previous installation and selection.

After activation, the existing durable cleanup removes superseded files no other
installed model references, including the old update receipt. Busy cleanup remains
recorded and retries at startup. Previous versions are not retained for rollback,
and there is no rollback action. Keep speech clients closed if they hold model
leases; activation waits for active model operations and can be cancelled while
waiting. Automatic model replacement and repair are not provided.

Updates retain the application's trusted configuration and repository paths.
Changed tensor names, shapes or dtypes require a compatible application release;
remote configuration changes and new aliases are not installed by this action.

**Cancel** cancels the current operation and uses its recorded staging cleanup.
**Remove** unregisters the model and removes verified blobs that no retained
inventory record uses. Removing or invalidating the selected model clears its
selection. Assets held by active synthesis remain protected.

## Runtime

The application includes a verified baseline executable. The shared **Check for
updates** action queries the official Kyutai Pocket TTS GitHub release source.
**Update**, when
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
Native voice profiles remain independent of model IDs and revisions. Updated
models use a separate immutable SHA-256-addressed receipt in the existing model
blob store. The native SAPI resolver checks that receipt, the fixed configuration
template, exact inventory fields and file bytes offline before synthesis. Model
update installation does not modify voice profiles or SAPI registrations.

See [WinUI storage and verification](../ui/OutLoud.WinUI/README.md),
[Rust model resolution](RUST_RUNTIME_SESSION.md#production-integration-independent-native-profiles-and-settings-selection),
and [upgrade/uninstall data boundaries](../installer/PRODUCTION.md).
