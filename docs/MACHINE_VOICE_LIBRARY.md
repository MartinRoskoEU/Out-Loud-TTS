# Machine-wide Voice Library

The only supported production library is resolved by
`SHGetKnownFolderPath(FOLDERID_ProgramData, KF_FLAG_DONT_VERIFY)` plus
`OutLoud\VoiceLibrary`, normally `C:\ProgramData\OutLoud\VoiceLibrary`.
Managed tokens use HKLM Registry64 and the shared machine COM engine.
See [managed SAPI voices](MULTI_VOICE_SAPI.md), [library storage](VOICE_LIBRARY.md)
and [the current architecture](ARCHITECTURE.md).

## Storage and security

Constructing or reading the default library does not create it. Mutations create
it lazily with the existing protected ACL/owner contract: SYSTEM/Administrators
full control, ordinary Users read/execute/list. Existing root, directory and
file owners/ACLs are checked; unsafe permissions are refused, not silently repaired.
Mutations require enabled administrator/SYSTEM authority. Ordinary reads remain
read-only. No environment variable or alternate scope selects production storage.

IDs remain random immutable 128-bit lowercase hexadecimal strings. Metadata,
native `.safetensors` integrity, structure, paths and entry identity retain their strict
validation. Reparse points, hard links, traversal/device/network paths and
unexpected files cannot expand access or deletion. Shared/exclusive locking,
staging, trash and journaled removal recovery remain enforced.
Current removal boundaries are described in [library operations](VOICE_LIBRARY.md).

The C++ library's explicit-root constructor and the manager's explicit
`MachineVoiceLibraryPolicy::development_override` remain for isolated fixture
libraries and fake-registry tests. Neither management CLI exposes those inputs;
the production helper and snapshot always use the default production root/policy.
These test seams do not select HKCU or create a special development voice.

## Current operation boundaries

WinUI -> service -> read-only native bridge is the snapshot path.
Confirmed Import/Remove go through the authenticated elevated helper.
Requests carry selected immutable identity/content assertions, not roots/hives.
The application requests administrator approval at startup. Library mutations
retain the helper's validation; the UI does not register the COM class or write
library files directly. External SAPI clients remain unelevated.
Failure and partial/cleanup outcomes remain explicit; refresh preserves selection
by ID and reports read failures without inventing successful state.

SAPI registration uses `OutLoud_Pocket_<library_id>`, exact entered display names,
Vendor `Out Loud`, and the existing strict managed ownership schema. Shared
runtime comes from the fixed HKLM64 COM registration. Token transactions and
file/engine identity checks remain intact.

The current `outloud-sapi-voices` CLI and production helper use the canonical
ProgramData library. The removed standalone library CLI is not a current
workflow. Prefer coordinated WinUI removal for ordinary voice mutations.

See the [current Voice Manager guide](VOICE_MANAGER_GUIDE.md) and
[build/validation guide](BUILD_AND_VALIDATION.md). Latest workspace
builds do not imply deployment or installed-host validation.
