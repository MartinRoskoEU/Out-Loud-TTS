# Native Pocket SAPI adapter and shared-engine deployment

The native Windows x64 COM server is `OutLoudPocketSAPI.dll`, using the fixed CLSID
`{F38C3398-3552-43B9-8350-7DD301327D70}`. Its exports remain `DllGetClassObject`
and `DllCanUnloadNow`; there is no self-registration export or regsvr32 workflow.
Managed machine voices are documented in [MULTI_VOICE_SAPI.md](MULTI_VOICE_SAPI.md).
The initial adapter's numerical/design/validation evidence is
[archived](SAPI_ADAPTER_HISTORY.md); the development voice installation commands
in that record are retired.

## Runtime responsibilities

The adapter loads model, tokenizer and selected OLV from the assigned token's
existing runtime configuration. It does not select a library scope or import
voice sources. Absolute paths and generic AssetRoot-relative configuration
continue to work; model-loading behavior was not changed by management retirement.
Resident runtime ownership, two neural workers, thread affinity, PCM16 conversion,
streaming/partial writes, format negotiation and module/COM lifetime are unchanged.

[Cancellation](SAPI_CANCELLATION.md) and [bookmark behavior](SAPI_BOOKMARKS.md)
retain their current caller-thread controls, cooperative stop/join and audio
scheduling. FlowLM/Mimi mathematics, RNG, splitting and asset formats are unchanged.
The registration-free fake host and multi-voice checker remain necessary tests.

Operational failures are recorded once at adapter boundaries by the
[native error logger](NATIVE_ERROR_LOGGING.md), preserving HRESULT/IErrorInfo.
Logs are lazy, per-user and bounded; normal synthesis/cancellation does not log.

## Build and deployment separation

Builds produce workspace binaries; copying/signing/registration are separate
manual actions. [A11/A12](BUILD_AND_VALIDATION_IMPROVEMENTS.md) documents current
builds. A06/A08/A09/A11/A12 rebuilt workspace outputs; they did not deploy those
changes. Installed SAPI/WoW validation of them remains unverified.

`prepare-deployment.ps1` copies the shared engine, registration-free host checker,
the four Microsoft VC runtime DLLs, model/tokenizer, current documents and scripts
to a fresh versioned deployment. Inventory schema **2** describes an engine/runtime
bundle with no default voice token or bundled OLV voice. Voice conditioning files
belong to the independently managed ProgramData library.

```text
bin/OutLoudPocketSAPI.dll
bin/outloud-sapi-check.exe
bin/msvcp140.dll
bin/msvcp140_atomic_wait.dll
bin/vcruntime140.dll
bin/vcruntime140_1.dll
runtime/pocket-english.olp
runtime/pocket-english-tokenizer.bin
```

`sign-deployment.ps1` signs only project images with an explicitly chosen existing
certificate; vendor signatures remain intact. `verify-deployment.ps1` checks file
hashes, architecture and signature policy without loading the DLL. No installer,
certificate creation or automatic registry operation is introduced.

The copy helper is an engine-only development bundle, not a complete Voice Manager
installer. It also requires `outloud-sapi-check.exe`, which is absent from the
production-only configuration. Use a coherently rebuilt development output with
its prepared fixtures for this particular helper; do not assume `build-production`
alone supplies its checker. It neither bundles an OLV nor installs a library voice.

After successful build and review, this is the manual staging/new-registration
procedure. All placeholders must be supplied deliberately; none was executed
during A17:

```powershell
cmake --build build --config Release --parallel --target OutLoudPocketSAPI outloud-sapi-check
.\sapi\scripts\prepare-deployment.ps1 -ProjectRoot $PWD.Path -BuildDirectory 'build/Release' -DeploymentRoot '<fresh deployment>'
& '<deployment>\scripts\sign-deployment.ps1' -DeploymentRoot '<deployment>' -CertificateThumbprint '<existing code-signing certificate thumbprint>'
& '<deployment>\scripts\verify-deployment.ps1' -DeploymentRoot '<deployment>' -RequireSignatures
# Close WoW manually and use an elevated x64 shell before registration.
& '<deployment>\scripts\register.ps1' -DeploymentRoot '<deployment>'
```

`register.ps1` registers/verifies only the HKLM64 shared COM class. It preserves
ownership checks, signed-bundle verification, game-closed checks and rollback
snapshot behavior. It has no voice name/path/token/generation parameters and
never creates a voice token. Import voices through Voice Manager afterward;
see the [current application guide](VOICE_MANAGER_GUIDE.md).
An existing class using a different deployment is refused; switching deployments
is not implemented as an automatic upgrade workflow. Existing voice tokens also
retain their runtime paths; registering another build is not an update of all
managed voices. A03 packaging/install/uninstall/upgrade work remains open.
`OutLoudPocketSAPI-2M` remains the existing shared
COM ownership marker, independent of any retired voice identity.

`unregister.ps1 -ReportDirectory <fresh directory>` removes only the owned COM
class and refuses while any HKLM64 voice references its CLSID. It never removes
voices or library files. A per-user COM override is a security conflict checked
by both scripts; that read-only guard is not a user-voice management workflow.
These are administrator maintenance scripts, not concurrent mutation APIs; stop
voice management/playback activity before later execution.

Existing inventory-schema-1 bundles retain their original scripts/manifests.
The new verifier does not silently accept their voice-bearing layout as schema 2.
No installed bundle, registration, signature or model/voice file was changed.

The verifier checks staged hashes/PE architecture/signatures without loading the
engine; that is not a playback or game integration test. The bundle includes only
selected documents/scripts and no Voice Manager app/helper, complete prerequisites
installer or full documentation dependency closure. Relative links to repository
guides require the source checkout; A03 must settle distributable contents.
