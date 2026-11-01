# Native voice profile validation

`validate_native_profile` validates native `.safetensors` containers without
conversion or inference. Import and export copy the original file bytes; only
native profiles are accepted.

The existing mapped-file bounds, strict JSON/tensor descriptors, dtype widths,
shape overflow protection, complete/nonoverlapping payload bounds, opaque string
metadata and cancellation-aware SHA-256 validation are preserved. This boundary
does not infer a model identity or impose model/revision associations. The
official Rust runtime performs its existing structural checks when a selected
model and native profile are used together.

Errors retain the existing native error names and structured path/tensor/system
details. Managed-library operations add their existing path/ACL, identity,
locking and atomic-publication guards; see [VOICE_LIBRARY.md](VOICE_LIBRARY.md).
