# Voice Library backend

The product uses the existing protected machine library:

```text
%ProgramData%\OutLoud\VoiceLibrary\
  .library.lock
  voices\<library_id>\voice.safetensors
  voices\<library_id>\metadata.json
  .staging\<operation_id>\
  .trash\<operation_id>\
```

Only native `.safetensors` profiles are supported. Schema-2 metadata is unchanged:
library ID, display name, profile/source SHA-256, imported UTC, source filename,
and `import_kind=pocket_native`. Profiles have no model identity, revision or
provenance assertion. Names do not select paths; IDs remain random 128-bit
identities. Duplicate native content is determined by the complete file SHA-256.

Import validates source and staged bytes/metadata before atomic publication.
Export copies the original bytes and preserves the existing selected-name/hash
assertions, destination fingerprint, pinned file identity and atomic overwrite
checks. Rename retains profile identity. The native backend has no replacement
or conversion operation.

The existing file mapping, protected owner/ACL checks, ancestor/file pins,
reparse/hard-link rejection and shared/exclusive library locks remain. Refresh
validates each row in one read epoch and does not perform recovery or elevation.
Unknown metadata and unsupported files are reported. Normal refresh/import does
not migrate, clean up or convert existing legacy entries. The approved
[complete-uninstall cleanup policy](../installer/PRODUCTION.md#approved-file-cleanup-policy)
removes verified managed resources after confirmation and leaves unknown user
files outside the deletion plan without blocking known cleanup solely by their presence.

Removal retains snapshot name/content/health guards, reversible journaled detach,
owned-token fingerprint checks and authorized rollback/recovery. Current
individual-voice removal still stops on unknown, changed, unsafe or locked data;
this describes that operation's existing guards, not a blanket complete-uninstall
preservation policy. Unsupported pending transactions are checked as read-only
conflict evidence and retained for manual review, without conversion or adoption.
Explicit removal maintenance remains `outloud-sapi-voices recover-removals`;
normal refresh never invokes it.

WinUI uses the existing bridge and authenticated
elevated helper for native import and coordinated removal. See
[managed SAPI voices](MULTI_VOICE_SAPI.md).
