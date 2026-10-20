# Read-only Pocket model comparison

The shared discovery function also powers the reusable
[CHECK/UPDATE workflow](../../docs/MODEL_UPDATES.md). Individual voices are not
model-version tracked or classified. Supported structure and integrity determine
voice availability; changed model weights may still affect speech behavior.

From the repository root, use the existing reference environment:

```powershell
.venv\Scripts\python.exe -B tools\assets\compare_model.py --stats
```

This developer diagnostic requires the already installed `huggingface_hub` and
`PyYAML` packages; optional `--stats` also uses NumPy. It does not import Pocket
TTS, PyTorch, the production exporter, or any model-execution code. No inference,
conversion, deployment, signing, installer build, or voice mutation occurs.

Each invocation resolves the current official default English configuration at
an immutable Kyutai GitHub commit, then its immutable Hugging Face weights pin.
The default must be six layers. Dated stable English aliases are identified by
matching the published weight hash, not merely a name. Experimental/drifting,
24-layer, multilingual and community configurations are never fallback choices.
If the default changes architecture or no longer agrees with HF HEAD, the tool
refuses selection or reports the discrepancy instead of silently selecting an
unreviewed model. Repository HEAD, the configured weight revision, and the
weight file's last commit are reported separately.

Local HF caches are read first. Necessary official downloads go only to
`generated/model-comparison/cache/`; existing caches and production assets are
never overwritten. Standard HF authentication is reused for gated weights;
credentials are never included in reports. `--no-download` permits metadata and
configuration queries but requires weight/tokenizer files already cached.
`--current-source PATH` selects a read-only original checkpoint and requires it
to match the project's pinned SHA256. No CLI option writes model assets.

Reports go to a fresh ignored `generated/model-comparison/run-*/` directory:

- `comparison.md`: concise human-readable findings, incompatibilities, risks
  and unknowns; also printed to the terminal.
- `comparison.json`: immutable source revisions, configuration snapshots,
  verified file hashes/sizes, complete tensor lists/shapes/dtypes, changed and
  unchanged tensor hashes, and optional per-tensor numerical differences.

The local model is identified through the reviewed JSON/C++ schema, packaging
manifest, OLP header/table/integrity hashes, and actual source tensor bytes.
Windows discovery reads the install record and matching SAPI token model paths;
it does not open user OLV files or enumerate their audio. An inaccessible or
different installed asset is reported explicitly. The full pinned checkpoint is
optional: if unavailable, the actual 169 OLP tensors can still be compared,
with excluded tensors reported as unknown. No OLP file hash is equated with a
Safetensors file hash; comparisons use the corresponding raw tensor bytes.

Exit status 0 means a tensor comparison completed and discovered installed
models validated; 2 means a partial comparison or installed-model discrepancy.
Unrecoverable malformed inputs fail with a diagnostic rather than implying
compatibility. The report never treats equal dimensions as voice provenance.

An update needs separately reviewed source/payload/schema/compatibility pins,
packaging and attribution updates, and a native rebuild: both the exporter and
native loader intentionally bind the existing weights. Matching topology can
allow the existing export algorithm and inference kernels to be reused, but is
not proof of end-to-end runtime correctness. User voices contain prepared KV
caches; changed preparation weights require regeneration, ordinarily from the
original reference audio. This diagnostic never changes voice IDs or data.
