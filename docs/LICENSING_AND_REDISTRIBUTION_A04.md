# A04: MIT licensing and redistribution audit

Audit date: **2026-10-10**. Entry HEAD: `c0911889610a8b887ad19407c7032d6305448cf2`
(`added .olv export and import`). The working tree was clean at entry.
This is a research report, not a license grant, installer implementation or release clearance.

## 1. Executive summary

**Out Loud is not yet cleared for the proposed public binary release.** The owner's
choice of MIT for original project-owned code is compatible with the verified
permissive dependencies. Model and tokenizer identities were established, and
their separate repositories declare **CC-BY-4.0**, not MIT. Three important rights
questions remain: the sampler's upstream DiT provenance; the relationship between
Kyutai's CC license and its additional prohibited-use statement; and the entitlement
for bundling Microsoft VC runtime binaries. A fourth question applies only if A03
bundles SignTool for signing ordinary DLLs.

The audit records **81 inventory entries: 31 component/provenance groups and 50
pinned offline Python packages**. These are inventory roles, not 81 independently
linked runtime libraries; Pocket and PyTorch appear in both runtime-provenance and
offline-tool contexts. Inspection covered 270 tracked files, current build wiring,
existing Release PE imports, local package licenses, conversion outputs and primary
upstream terms. No build, test or benchmark was run.

**No P0 prohibition against a proven mandatory incorporated component was
established.** There are P1 unresolved rights questions and P2 missing release
notices/compliance work. Noncommercial DiT code cannot simply be redistributed
under unrestricted MIT; whether protected DiT expression survives into Out Loud's
C++ implementation requires a focused provenance determination. This report does
not declare the entire engine noncommercial or change the project's chosen license.

Current staging copies no voices. It does include a development SAPI checker,
copies VC DLLs from System32, and lacks a complete model/tokenizer/Unicode/Pocket
notice set. It is a development staging workflow, not an audited public installer.

## 2. Confirmed distribution policy: free, open-source, MIT, no bundled voices

The owner's fixed policy is:

- Out Loud is free of charge and open-source. Original project-owned code will use MIT.
- Downstream commercial use of that code is allowed. No activation, subscription,
  payment service, commercial edition or DRM is proposed.
- The initial public installer contains **no voices**: no `.olv`, prepared voice
  `.safetensors`, recordings, Gnome/Human/Undead samples or development/test voices.
- Users obtain and import their own compatible voices. Asset validation establishes
  technical compatibility, not permission to use or distribute a voice.
- The installer should contain the inference `.olp` and binary tokenizer **only
  after their distribution conditions and the unresolved policy question are settled**.
- Third-party code, data and Microsoft binaries retain their own terms. MIT on
  Out Loud is not a relicensing of those materials.

The user reports successful manual WoW operation of the signed engine and successful
manual OLV import/export. These are user-reported integration results, not new A04
tests or evidence of redistribution rights.

## 3. Audit scope and evidence limitations

Read-only inspection included the requested architecture, Voice Manager/library,
asset/import, final-validation, OLV workflow and documentation-finalization documents;
`RNG_NOTICES.md`; source/public headers; CMake modules; WPF projects, resolved NuGet
metadata and resources; all pinned offline requirements; exporters and generated
manifests; deployment scripts/inventories; existing binary imports and asset headers.
Repository searches found no project `LICENSE`, general third-party notices file,
vendored external neural library or tracked voice/model/font/image binary.

Evidence labels throughout:

| Label | Meaning |
| --- | --- |
| **CONFIRMED** | Direct source, local artifact or authoritative upstream term inspected. |
| **LIKELY / PARTIALLY VERIFIED** | Supporting evidence exists, but a particular legal or historical inference is incomplete. |
| **UNRESOLVED** | Identified evidence is insufficient for a definitive permission/ownership conclusion. |
| **POTENTIAL RELEASE BLOCKER** | An unresolved issue could prevent the intended release; priority is in section 23. |

Online verification was performed on the audit date. Primary links identify exact
revisions where available. The gated model README at the pinned revision returned
HTTP 401; its public API metadata at that exact revision provided the license,
gating statement, asset identity and policy text. No authentication bypass or new
asset download was attempted. The tokenizer's pinned README was publicly readable.
No locally preserved model-card acceptance record was found.

This audit verifies published grants and observed provenance. It does not establish
contributors' assignments, every training-data right, patent clearance, personality
rights or contractual interpretation. Existing build outputs were inspected without
rebuilding; their imports identify those files, not a certification that every output
was produced from entry HEAD. Historical reports support context only. A03's final
installer technology, prerequisite packages, self-contained/framework-dependent
choice and signing-tool payload do not yet exist and require artifact-level review.

## 4. Complete component inventory

The following inventory tables are read together: this section records origin,
location and incorporation; sections 5–15 record permissions and obligations;
section 17 records release decisions. A row's referenced section supplies its
remaining uncertainty and notice/source obligations. No external component becomes
MIT merely through association with an MIT application.

### Application, data, incorporated code and platform groups

| ID | Component, purpose and origin/version | Local location; modification/incorporation | Intended public installer; evidence and terms |
| --- | --- | --- | --- |
| C01 | Original native engine, FlowLM/Mimi kernels, assets, Voice Library, SAPI adapter, bridge/helper and CLI; Out Loud, entry HEAD | `engine/`, `sapi/src/`, `frontend/`, native `tools/`; compiled project static libraries inside project EXE/DLLs | Production components yes; CLI selection pending. Original ownership is the boundary for MIT; upstream portions below are exceptions. Sections 5, 9, 16. |
| C02 | Original WPF application, XAML styles, scripts and docs; Out Loud, entry HEAD | `ui/OutLoud.VoiceManager/`, `scripts/`, `docs/`; own resources/managed assemblies | App yes; source/docs as selected. No external UI package found. Sections 12, 16. |
| C03 | Pocket TTS reference implementation; Kyutai, 3.3.0, commit `3dbee45d343d7dddd0d105468d17f8dcba14db3e` | `.venv/.../pocket_tts`, reference/export tools; numerical paths translated/reimplemented in native code, not Python-linked | Python package no; attributable implementation provenance yes. MIT, subject to C04/C05 chain. Section 5, source S01. |
| C04 | MAR MLP/diffusion sampler reference; Tianhong Li, `fe470ac24afbee924668d8c5c83e9fec60af3a73` | Pocket `modules/mlp.py` explicitly references MAR `models/diffloss.py`; native sampler follows this path | No MAR package; potential adapted expression in sampler. MIT copyright 2024 Tianhong Li; transitive issue C05. Section 5, S02. |
| C05 | DiT final-layer ancestor; Meta Platforms, Inc. and affiliates; historical `c9f545c56d212c32e73c57746550e3a30af897a4` | Upstream attribution chain to native `engine/src/flow_sampler.cpp`; no DiT dependency linked | Direct package/weights no; protected-expression incorporation unresolved. **CC-BY-NC-4.0**, P1. Section 5, S03. |
| C06 | GLIDE timestep-embedding reference; OpenAI, current upstream MIT notice copyright 2021 | Referenced in MAR timestep code; Out Loud uses model-stored frequencies and its own loops | GLIDE package/model no. Indirect permissive provenance, exact copied expression not established. S04; do not invent a bundled GLIDE model. |
| C07 | Original Pocket checkpoint; Kyutai, pinned model revision below | Hugging Face cache `languages/english/model.safetensors`; exporter reads original BF16 tensors | Original checkpoint not planned; its weights survive in C08. CC-BY-4.0 plus policy ambiguity. Sections 6–7, S05. |
| C08 | Converted Pocket `.olp`; original Kyutai weights in Out Loud container, OLP v1 | `generated/assets/pocket-english.olp`, installed runtime copy; subset of tensors, unchanged BF16 payload | Yes if cleared; CC-BY-4.0 for weights, own container code MIT. Sections 6–7, S05/S07. |
| C09 | Learned Unigram tokenizer data; Kyutai, distinct pinned repository below | Cached `tokenizer.json` -> `generated/tokenizer/pocket-english-tokenizer.bin`; converted vocabulary/scores/configuration | Binary yes if cleared. Repository declares CC-BY-4.0; policy question remains. Section 8, S06. |
| C10 | Unicode 15.0.0 properties/uppercase tables; Unicode, via Python 3.12.10 | Tokenizer exporter writes compressed `isspace`, `isupper`, `isdigit` ranges and uppercase mappings into binary | Yes, inside tokenizer. Unicode data license, separate from learned vocabulary; notice missing. Section 8, S08. |
| C11 | PyTorch RNG adaptation; PyTorch contributors, local 2.14.1+cpu, source commit `5c4886908584029761b579af026dcfb627c84070` | `engine/src/production_noise.cpp`; scalar altered subset, statically compiled; no LibTorch linkage | Yes. PyTorch BSD-3-Clause notice in `RNG_NOTICES.md`; full PyTorch wheel licenses are separate. Section 10, S09. |
| C12 | MT19937 implementation; Makoto Matsumoto/Takuji Nishimura, 1997–2002, via pinned ATen | Same RNG source; constants/state transition and initialization adaptations | Yes. BSD-3-Clause-style notice includes contributor acknowledgements. Section 10, S10. |
| C13 | `avx_mathfun`/`sse_mathfun` mathematical approximation lineage; Giovanni Garberoglio/Julien Pommier | Same RNG source; scalar altered subset of ATen `avx_mathfun.h`, not whole vector header | Yes. Zlib-style license/credits, alterations identified. Section 10, S11. |
| C14 | Microsoft STL template/inline source; Microsoft, MSVC 14.44.35207 | Compiler include headers, compiled inline/template code; no vendored source headers | Incorporated object portions yes. Apache-2.0 WITH LLVM-exception; not the license for MSVCP runtime DLLs. Section 11, S12. |
| C15 | Four release VC runtime DLLs; Microsoft, observed VS redist tree 14.44.35112 | Dynamic native dependencies; current development stage copies System32 DLLs | Bundle/prerequisite decision pending. Microsoft redistribution terms; distributor entitlement unresolved. Section 11, S13. |
| C16 | UCRT and CRT API-set contracts; Microsoft, supported Windows installation | Native/apphost imports; Windows-provided runtime | Normally no app-local UCRT/system binary shipment. Windows/SDK terms, not MIT project code. Section 11. |
| C17 | Windows SAPI5 and COM; Microsoft, SDK 10.0.26100.0 declarations | `sapi.h`, `sapiddk.h`, SDK import libraries; runtime Windows COM/SAPI | OS dependency, no SAPI implementation shipment. Interface use does not redistribute Windows. Section 11. |
| C18 | Other Windows system APIs/DLLs; Microsoft | `bcrypt`, `ktmw32`, `ADVAPI32`, `SHELL32`, `KERNEL32`, `USER32`, OLE APIs as relevant | OS dependencies only. No proposal to copy them. Section 11. |
| C19 | .NET apphost and .NET Core runtime; .NET Foundation/contributors and Microsoft; SDK 10.0.401, installed runtime 10.0.12 | WPF `.exe` contains Microsoft apphost; framework-dependent `net10.0-windows` output | Apphost yes; runtime prerequisite or later self-contained payload undecided. Source MIT; selected Microsoft binary-package terms/notices also matter. Section 11, S14. |
| C20 | WPF/Windows Desktop framework; .NET Foundation/contributors and Microsoft | Framework reference `Microsoft.WindowsDesktop.App.WPF`; no vendored WPF implementation | Runtime prerequisite, not currently copied framework assemblies. MIT source; package terms/third-party notices for any shipped runtime. Section 11, S15. |
| C21 | Segoe UI, Segoe MDL2 Assets, Consolas; Microsoft, OS-dependent versions | XAML font-family references/system glyphs, including refresh/chevron; no font/image file embedded | Font files no. Installed-font use; Windows font license is not a redistribution grant. Section 12, S16. |
| C22 | Visual Studio Build Tools/MSVC and Windows SDK; Microsoft, 17.14.41 / 14.44.35207 / 10.0.26100.0 | Local build tools, SDK headers and import libraries; no toolchain vendoring | No. Build-tool EULAs do not automatically prove C15 redistribution entitlement. Section 11, S13/S17. |
| C23 | SignTool, future signing option; Microsoft SDK 10.0.26100.0 | Current signing script references external installed executable; no SignTool in current stage | Not currently bundled; A03 unresolved. Identified redist grant is MSIX-purpose-limited. Section 11, S18. |
| C24 | .NET SDK/MSBuild/Roslyn; Microsoft/.NET ecosystem, SDK 10.0.401 | Local managed compilation tools, not application PackageReferences | No. Build outputs do not inherit all SDK tool licenses. Final apphost/runtime notice inventory still required. Section 11. |
| C25 | CMake/CTest; Kitware/contributors, installed 4.4.3 | Local build/check orchestration; `CMakeLists.txt`, `cmake/` are project files | Tool binaries no. Local CMake `LICENSE.rst` BSD-3-Clause; bundled tool dependencies irrelevant to app unless tools later shipped. Section 13. |
| C26 | Windows PowerShell; Microsoft/Windows | `.ps1` scripts run against OS interpreter; source scripts are project-owned | Interpreter no; selected scripts possibly yes. No copying PowerShell implementation. Section 13. |
| C27 | Git; Git contributors, 2.49.0.windows.1 | Local source-management tool only | No. GPL-2.0 tool use does not license the application under GPL. Section 13. |
| C28 | CPython; Python Software Foundation/contributors, 3.12.10 | `.venv` references installed Python; used for offline exporters | No. PSF-2.0/Python notices if later redistributed; Unicode output is separately C10. Section 13, S08. |
| C29 | Development fixtures, checkers and generated reference data | `tests/`, `reference/`, `generated/`, `cmake/Development.cmake`; some checker builds incorporate generated voice/reference arrays | Binary installer no. Code versus voice/model-derived data have separate rights; current dev stage still has one checker. Sections 13–14. |
| C30 | Prepared/OLV voices and recordings, including Gnome/Undead/user voices | Ignored `voices/`, `generated/**/*.olv`, historical deployment `voices/`, user/ProgramData libraries | **No**, fixed exclusion. Voice/content permissions unverified and not granted by MIT or technical conversion. Section 14. |
| C31 | Earlier Chatterbox SAPI behavior reference; neighboring project | Current adapter/support comments and historical read-only reference hashes; no linked Chatterbox library | No Chatterbox package. Behavior-reference evidence exists; historical source paths now unavailable, copying/owner permissions not conclusively reconstructable. Section 9. |

### Offline dependency inventory and shared interpretation

All 50 versions below are pinned in UTF-16 `reference/requirements-reference.txt`
and were inspected through installed `.dist-info/METADATA` and available package
license files. Locations are `.venv/Lib/site-packages/<package>` and associated
`.dist-info`; imports are **offline Python dependencies**, not native linkage.
No local package-source modifications were identified or requested; this is not
a byte-for-byte wheel authenticity audit. Every row is **NOT APPLICABLE — NOT
DISTRIBUTED** for the initial installer. No project source-disclosure obligation
is triggered merely by running these tools. Their outputs are separately C08–C10
and C29–C30, not automatically covered by the tools' licenses.

MIT/BSD/ISC/Zlib packages allow commercial redistribution with their respective
notice conditions; Apache packages also retain license, change notices and applicable
NOTICE material. MPL-covered files require file-level compliance if actually shipped.
These package licenses are evidence for the packages, not grants over the model.
Package origins below identify maintainers; exact copyright lists remain in the
installed license texts and must not be replaced by this abbreviated inventory.

| ID | Package/version | Purpose | Upstream/maintainer | Inspected license; additional uncertainty if any |
| --- | --- | --- | --- | --- |
| D01 | annotated-doc 0.0.5 | Annotation documentation | [FastAPI](https://github.com/fastapi/annotated-doc) | MIT |
| D02 | annotated-types 0.8.0 | Validation constraints | [annotated-types](https://github.com/annotated-types/annotated-types) | MIT |
| D03 | anyio 4.15.1 | Async support | [Alex Gronholm](https://github.com/agronholm/anyio) | MIT |
| D04 | certifi 2026.7.22 | HTTP CA data | [certifi](https://github.com/certifi/python-certifi) | MPL-2.0; CA data/package obligations would matter if bundled |
| D05 | charset-normalizer 3.5.2 | HTTP character decoding | [charset-normalizer](https://github.com/jawah/charset_normalizer) | MIT |
| D06 | click 8.5.0 | CLI support | [Pallets](https://github.com/pallets/click) | BSD-3-Clause |
| D07 | colorama 0.4.6 | Console formatting | [Jonathan Hartley](https://github.com/tartley/colorama) | BSD-3-Clause |
| D08 | einops 0.8.2 | Tensor rearrangement | [Alex Rogozhnikov](https://github.com/arogozhnikov/einops) | MIT |
| D09 | fastapi 0.142.2 | Upstream server tools | [FastAPI](https://github.com/fastapi/fastapi) | MIT |
| D10 | filelock 4.0.10 | Cache/file locking | [tox-dev](https://github.com/tox-dev/py-filelock) | MIT |
| D11 | fsspec 2026.9.0 | Filesystem adapters | [fsspec](https://github.com/fsspec/filesystem_spec) | BSD-3-Clause |
| D12 | h11 0.16.0 | HTTP protocol | [python-hyper](https://github.com/python-hyper/h11) | MIT |
| D13 | hf-xet 1.6.0 | Hugging Face transport | [Hugging Face](https://github.com/huggingface/xet-core) | Apache-2.0 |
| D14 | httpcore 1.0.9 | HTTP transport | [Encode](https://github.com/encode/httpcore) | BSD-3-Clause |
| D15 | httpx 0.28.1 | HTTP client | [Encode](https://github.com/encode/httpx) | BSD-3-Clause |
| D16 | huggingface_hub 1.33.0 | Asset retrieval/cache | [Hugging Face](https://github.com/huggingface/huggingface_hub) | Apache-2.0; access to a repository is not a model redistribution grant |
| D17 | idna 3.20 | Domain-name handling | [Kim Davies](https://github.com/kjd/idna) | BSD-3-Clause |
| D18 | Jinja2 3.1.6 | Templating | [Pallets](https://github.com/pallets/jinja) | BSD-3-Clause |
| D19 | markdown-it-py 4.2.0 | Markdown rendering | [Executable Books](https://github.com/executablebooks/markdown-it-py) | MIT; package notice material retained if shipped |
| D20 | MarkupSafe 3.0.4 | Template escaping | [Pallets](https://github.com/pallets/markupsafe) | BSD-3-Clause |
| D21 | mdurl 0.1.2 | Markdown URLs | [Executable Books](https://github.com/executablebooks/mdurl) | MIT |
| D22 | mpmath 1.3.0 | Symbolic/numeric dependency | [Fredrik Johansson/contributors](https://github.com/fredrik-johansson/mpmath) | BSD-3-Clause |
| D23 | networkx 3.7 | Torch graph support | [NetworkX](https://github.com/networkx/networkx) | BSD-3-Clause |
| D24 | numpy 2.5.3 | Offline tensor/asset work | [NumPy](https://github.com/numpy/numpy) | Metadata BSD-3-Clause AND 0BSD AND MIT AND Zlib AND CC0-1.0; wheel also carries BLAS/LAPACK/compiler-runtime terms |
| D25 | opentelemetry-api 1.45.0 | Tool telemetry API dependency | [OpenTelemetry](https://github.com/open-telemetry/opentelemetry-python) | Apache-2.0 |
| D26 | packaging 26.3 | Package/version handling | [PyPA](https://github.com/pypa/packaging) | Apache-2.0 OR BSD-2-Clause |
| D27 | pocket-tts 3.3.0 | Independent upstream oracle | [Kyutai](https://github.com/kyutai-labs/pocket-tts) | MIT root; transitive MAR/DiT issue in section 5 |
| D28 | pydantic 2.13.5 | Upstream configuration | [Pydantic](https://github.com/pydantic/pydantic) | MIT |
| D29 | pydantic_core 2.46.5 | Validation engine | [Pydantic](https://github.com/pydantic/pydantic-core) | MIT |
| D30 | Pygments 2.21.0 | Syntax formatting | [Pygments](https://github.com/pygments/pygments) | BSD-2-Clause |
| D31 | python-multipart 0.0.32 | Server form parser | [Kludex/contributors](https://github.com/Kludex/python-multipart) | Apache-2.0 |
| D32 | PyYAML 6.0.3 | Upstream YAML configs | [PyYAML](https://github.com/yaml/pyyaml) | MIT |
| D33 | requests 2.34.2 | HTTP retrieval | [PSF/Requests](https://github.com/psf/requests) | Apache-2.0 |
| D34 | rich 15.0.0 | CLI formatting | [Textualize](https://github.com/Textualize/rich) | MIT |
| D35 | safetensors 0.8.0 | Offline checkpoint reader | [Hugging Face](https://github.com/huggingface/safetensors) | Apache-2.0; native runtime uses project parser, not this package |
| D36 | scipy 1.18.1 | Offline reference mathematics | [SciPy](https://github.com/scipy/scipy) | BSD-3-Clause primary; full wheel includes additional notices, BLAS/LAPACK and GCC runtime exception material |
| D37 | sentencepiece 0.2.2 | Reference tokenizer tools | [Google](https://github.com/google/sentencepiece) | Apache-2.0 metadata/upstream; does not license Kyutai vocabulary |
| D38 | setuptools 84.0.0 | Build/package tooling | [PyPA](https://github.com/pypa/setuptools) | MIT primary; vendored components have additional licenses, including LGPL material |
| D39 | shellingham 1.5.4 | CLI shell detection | [Tzu-ping Chung](https://github.com/sarugaku/shellingham) | ISC |
| D40 | starlette 1.7.0 | Upstream ASGI support | [Kludex/contributors](https://github.com/Kludex/starlette) | BSD-3-Clause |
| D41 | sympy 1.14.0 | Torch symbolic support | [SymPy](https://github.com/sympy/sympy) | BSD-3-Clause primary plus bundled notices |
| D42 | tokenizers 0.23.2 | Independent tokenizer reference | [Hugging Face](https://github.com/huggingface/tokenizers) | Apache-2.0 metadata/upstream; native tokenizer does not link it |
| D43 | torch 2.14.1 | Offline reference inference/RNG | [PyTorch](https://github.com/pytorch/pytorch) | Wheel metadata Apache-2.0 AND Apache-2.0 WITH LLVM-exception AND BSD-2-Clause AND BSD-3-Clause AND BSL-1.0 AND MIT; 95 installed license files, not just root BSD |
| D44 | tqdm 4.70.1 | Progress display | [tqdm](https://github.com/tqdm/tqdm) | MPL-2.0 AND MIT; not an unrestricted MIT-only package |
| D45 | typer 0.27.2 | CLI support | [FastAPI](https://github.com/fastapi/typer) | MIT |
| D46 | typing-inspection 0.4.4 | Type introspection | [Pydantic](https://github.com/pydantic/typing-inspection) | MIT |
| D47 | typing_extensions 4.16.0 | Type support | [Python](https://github.com/python/typing_extensions) | PSF-2.0 |
| D48 | urllib3 2.8.0 | HTTP support | [urllib3](https://github.com/urllib3/urllib3) | MIT |
| D49 | uvicorn 0.54.0 | Upstream server runner | [Kludex/contributors](https://github.com/Kludex/uvicorn) | BSD-3-Clause |
| D50 | wheel 0.48.0 | Packaging tool | [PyPA](https://github.com/pypa/wheel) | MIT |

The full vendored subcomponent inventory of every offline wheel is outside the
public installer payload. The mixed licenses above are explicitly recorded rather
than incorrectly labeling entire wheels MIT/BSD. Shipping `.venv`, a Python SDK
bundle or offline preparation environment later requires a separate complete wheel
inventory and all applicable notices/source offers. No such bundle is proposed here.

## 5. Pocket TTS software licensing

**CONFIRMED:** the official software reference is Kyutai's Pocket TTS **3.3.0**.
Its tag resolves to `3dbee45d343d7dddd0d105468d17f8dcba14db3e`. The installed package
license matches the [pinned upstream MIT text](https://raw.githubusercontent.com/kyutai-labs/pocket-tts/3dbee45d343d7dddd0d105468d17f8dcba14db3e/LICENSE)
(1,023 bytes, SHA-256 `23f18e03dc49df91622fe2a76176497404e46ced8a715d9d2b67a7446571cca3`).
That file does not identify a copyright holder/year line; none is invented here.
Kyutai maintains the project. Model-card authorship is not a substitute legal
ownership statement for every software contribution.

The grant allows copying, modification and commercial distribution while retaining
the relevant copyright and permission notice. It has no source-publication condition
and no express patent grant. Preserve the actual upstream MIT text and source
credits rather than replacing them with Out Loud's future copyright notice.

Out Loud's FlowLM, sampler, Mimi and preparation paths were developed against this
implementation and its reference outputs. Porting the operations to C++ does not
prove legal independence. Conservatively treat translated/adapted protected material
as upstream-derived, retain its license, and license original additions under MIT.

**POTENTIAL RELEASE BLOCKER, P1: transitive sampler provenance.** The installed
Pocket `modules/mlp.py` explicitly identifies MAR's pinned `diffloss.py` as its
source. [MAR's license](https://github.com/LTH14/mar/blob/fe470ac24afbee924668d8c5c83e9fec60af3a73/LICENSE)
is MIT, copyright 2024 Tianhong Li, but its
[final-layer implementation](https://github.com/LTH14/mar/blob/fe470ac24afbee924668d8c5c83e9fec60af3a73/models/diffloss.py)
credits DiT. Pocket preserves that attribution. Native `flow_sampler.cpp` implements
the corresponding final-layer computation with explicit C++ arrays/loops: condition
SiLU/projection, shift/scale, affine-free normalization, modulation and output projection.

The inspected historical [DiT license](https://github.com/facebookresearch/DiT/blob/c9f545c56d212c32e73c57746550e3a30af897a4/LICENSE.txt)
and [README](https://github.com/facebookresearch/DiT/blob/c9f545c56d212c32e73c57746550e3a30af897a4/README.md)
identify **CC-BY-NC-4.0 for code and weights**; `models.py` identifies Meta Platforms,
Inc. and affiliates. This revision predates the pinned MAR reference. Original DiT
code under those terms does not carry unrestricted commercial redistribution rights.

**UNRESOLVED:** attribution and shared mathematical architecture do not, alone,
prove that copyright-protected DiT expression remains in the C++ implementation.
No direct DiT source is vendored or linked. Equally, MAR/Pocket MIT labels do not
automatically erase an earlier holder's rights if protected material was inherited.
The report therefore flags a focused provenance/legal determination, not a proven
violation or automatic noncommercial license on all Out Loud code or Pocket weights.
Possible resolutions are documented compatible permission, a supported determination
that the relevant implementation is independently expressed mathematical functionality,
or a separately authorized, documented independent implementation. No code was changed.

MAR's timestep helper also references GLIDE. The current
[GLIDE MIT license](https://github.com/openai/glide-text2im/blob/main/LICENSE) credits
OpenAI, 2021. No GLIDE model or package is bundled; historical expression provenance
is only partially verified. If protected GLIDE code is retained, preserve its actual
notice. Referenced papers and mathematical algorithms are not themselves a license
grant over copied implementation code.

## 6. Pocket TTS model/checkpoint licensing

**CONFIRMED identity:** source repository **`kyutai/pocket-tts`**, revision
**`983151f13aaeab1b13c1e5e3c2c383d49a9edf3f`**, file
**`languages/english/model.safetensors`**. The local source file was rehashed and
matches the pinned repository's LFS hash and size. The exact-revision
[Hugging Face metadata](https://huggingface.co/api/models/kyutai/pocket-tts/revision/983151f13aaeab1b13c1e5e3c2c383d49a9edf3f?blobs=true)
declares **`cc-by-4.0`**, automatic gating and an additional prohibited-use statement.
No separate model LICENSE/NOTICE file was listed. The public
[model repository](https://huggingface.co/kyutai/pocket-tts) corroborates the publisher;
its current page is not substituted for the pinned revision's terms.

CC-BY-4.0 supports commercial/free redistribution and adaptation, including converted
weights, subject to attribution, license identification/link, preservation of supplied
notices, indication of changes and no additional restrictions on the licensed rights.
It does not require publishing training data or conversion source. The authoritative
[legal code](https://creativecommons.org/licenses/by/4.0/legalcode.en) controls; the
[deed](https://creativecommons.org/licenses/by/4.0/) is a summary.

**UNRESOLVED, P1:** the repository also publishes a use policy covering unlawful,
harmful/deceptive activity and unauthorized voice impersonation/cloning, among other
uses. The pinned metadata supplies that wording. The actual accepted access agreement
and its relationship to downstream CC licensing were not established. Section 7(b)
of the legal code allows separate arrangements; a policy field must not be silently
treated either as a new CC license restriction or as meaningless. Obtain clarification
of the policy's legal status, asset scope and redistribution/downstream effect.

The checkpoint license declaration is confirmed; **overall bundling clearance remains
unresolved**. No commercial prohibition on these Pocket weights was demonstrated.
There is no evidence here that DiT's model weights occur in this checkpoint, and
the software-provenance question must not be transferred to model data without evidence.

The model card identifies Kyutai and its authors, including Manu Orsini, Simon Rouard,
Gabriel De Marmiesse, Václav Volhejn, Neil Zeghidour and Alexandre Défossez. Attribute
the named publisher/creators as supplied; do not invent missing copyright assignments.
No independent audit of every training recording or publicity/personality right was
possible. The published license does not promise those unrelated rights are cleared.

## 7. OLP conversion and model provenance

Inspection of `export_model.py`, `asset_format.py`, the schema and binary header shows
that OLP is a new container for an inference subset of the original BF16 tensors.
It is not a newly trained model or independently owned replacement for the weights.

| Artifact/measurement | Verified value |
| --- | --- |
| Source checkpoint size | 219,029,196 bytes |
| Source checkpoint SHA-256 | `fb0dc01b0d4d2e1c905b7a3e0676e3d9c96d5ae460e24e3ab94981805babf997` |
| Original tensor records | 214 |
| Included inference tensors/elements | 169 / 99,719,330 |
| Omitted tensors/elements | 45 / 9,782,816 |
| Included original BF16 bytes, before padding | 199,438,660 |
| Aligned payload bytes | 199,438,784 |
| OLP size | 199,449,856 bytes |
| OLP SHA-256 | `30889426a3bed523e725baafe70ec9064044898aa203975980741be1f3d70406` |
| OLP aligned-payload SHA-256 | `dc804d42e457b49614f279d3656d81cf3f493df7459c8ecb9c4c423ece8feeac` |
| Compatibility ID | `993a3a9749dc64d60815aa0e71fa4c18f53d478d05d0c4da8c7c77164387c59a` |

The exporter preserves original BF16 tensor bytes and row-major layout. It adds
numeric tensor records, names/container metadata and 64-byte alignment. There is
no quantization or persistent FP32 expansion. Omitted tensors belong to encoder/
encoder-transformer/downsample, speaker projection and pre-voice BOS paths. Existing
A08 prepared tensors are process-local inference caches, not new `.olp` payload.

`OLPMOD01` stores the source hash at offset 64. The workspace artifact and installed
`C:\OutLoud\OutLoud-Pocket-development-01\runtime\pocket-english.olp` have matching
full hashes and source-header identity. The cache snapshot path records the exact
model revision. This is stronger provenance than merely recognizing a filename.

The conversion adds **no `.olv` voice prefix or Gnome/Undead recording**. Learned
model weights can still reflect training data; excluding standalone voices is not
a statement that all speaker information or training rights disappear. Mark the
subset/container conversion as a modification in attribution. Underlying weights
retain section 6's license and unresolved policy issue.

## 8. Tokenizer licensing and provenance

The production tokenizer comes from a **different** repository:
**`kyutai/pocket-tts-without-voice-cloning`**, revision
**`00eac05ed3d16bdc3f6b5d598874019c34a89214`**, file
**`languages/english/tokenizer.json`**. Exact-revision
[API metadata](https://huggingface.co/api/models/kyutai/pocket-tts-without-voice-cloning/revision/00eac05ed3d16bdc3f6b5d598874019c34a89214?blobs=true)
declares CC-BY-4.0 and no access gate. The pinned
[README/model card](https://huggingface.co/kyutai/pocket-tts-without-voice-cloning/raw/00eac05ed3d16bdc3f6b5d598874019c34a89214/README.md)
was read directly and contains the same additional use-policy wording. There is
no separate tokenizer-specific contrary license in the inspected repository inventory.
The repo-level grant is evidence for the data, not proof of its original training corpus.

| Artifact | Size; SHA-256 |
| --- | --- |
| Source `tokenizer.json` | 245,020 bytes; `f498428e1eafee50492f7be13dc9bfafcfc12e508cd0eb1b01c92ecd5d8c6687` |
| Binary tokenizer | 93,325 bytes; `45ffaf8c702dbb6c954bd54280d83ea00f8bf1d47692aca37e078c8deb26935a` |
| Pinned card fetched in memory | 11,187 bytes; `ae2ebac6f8039d761ca90e2b742136dce9d7872ec8dd2105e3b2de1e3021e3aa` |

`tools/tokenizer/export_tokenizer.py` serializes 4,000 Unigram pieces, FP64 scores,
special/byte-fallback/configuration data, plus Unicode-derived properties. It does
not serialize an external Tokenizers or SentencePiece executable. Header `OLPTOK01`
stores the matching source SHA at offset 24. Installed and workspace binary hashes
match. Native Viterbi/normalization/UTF-8 processing uses project code, with no linked
Hugging Face Tokenizers/SentencePiece library. Reproduction of behavior alone does
not prove all implementation expression independently owned.

The learned vocabulary's license remains CC-BY-4.0 after binary conversion; Apache
licenses of tokenizer software tools do not replace it. Commercial/free asset
redistribution is supported by that grant, but the additional policy question in
section 6 also needs clarification for this repository. Do not assume the model
repository's different gate or checkpoint applies to the tokenizer.

**Additional shipped data:** the exporter uses Python 3.12's Unicode 15.0.0 database
and `str` operations to generate whitespace/uppercase/digit ranges and uppercase
mappings. Python's [pinned generator](https://github.com/python/cpython/blob/v3.12.10/Tools/unicode/makeunicodedata.py)
identifies the UCD version. These tables remain Unicode-derived data even though
CPython itself will not ship. The historical Unicode 15-era permission text is
preserved in the Unicode section of
[ICU release 72.1's license](https://github.com/unicode-org/icu/blob/release-72-1/icu4c/LICENSE)
(Unicode-DFS-2016; copyright 1991–2022 Unicode, Inc.). The
[current Unicode license](https://www.unicode.org/license.txt) is Unicode-3.0.
Both inspected grants permit copying/modification/commercial distribution with
the copyright/permission notice in the data or associated documentation, without
source publication or endorsement permission. Preserve the relevant historical
data notice; do not label the entire tokenizer MIT or copy unrelated ICU notices
for libraries not present. No copied CPython implementation was identified here;
ordinary table generation is not a reason to impose every Python tool license.

## 9. Native dependency licensing

**CONFIRMED:** CMake production libraries are project libraries linked statically
into the project's binaries. No runtime LibTorch, NumPy, safetensors package, BLAS,
Eigen, ggml, ONNX Runtime, external JSON library or compression library was found.
There is no FetchContent/vendor chain introducing such a library. JSON parsing and
asset parsing are project implementations; hashing/security operations use Windows
BCrypt. Compiler STL and Windows/Microsoft binaries are separately inventoried.

Actual inspected PE dependency groups:

| Existing output | External imports of licensing relevance |
| --- | --- |
| `OutLoudPocketSAPI.dll` | Four VC DLLs in section 11; OLE/COM, Shell32, BCrypt, Kernel32 and UCRT contracts |
| `OutLoudVoiceManagerNative.dll`, `OutLoudVoiceImportHelper.exe` | Three VC DLLs, Windows/UCRT, registry/security/transaction APIs including ADVAPI32/ktmw32 |
| Three native CLIs | Three VC DLLs and Windows/UCRT APIs |
| WPF apphost `.exe` | Windows/UCRT imports; managed runtime loaded through .NET hosting |

No external proprietary neural runtime is needed. Static compilation does not
remove copied-code notice obligations; dynamic loading does not make a copied DLL
free to redistribute.

Current SAPI comments identify earlier Chatterbox behavior policies (caller affinity,
control handling and abort outcomes). Historical `generated/perf/2n-reference-hashes.json`
records read-only reference files; the referenced neighboring paths are unavailable
in this workspace now. No Chatterbox library/source dependency is included by CMake.
**PARTIALLY VERIFIED:** behavior study is established; complete historical expression
comparison and that project's license are not. No unsupported claim that interface
policy study is copied code, or that all related source is independently owned, is
made. Preserve provenance and resolve actual copied expression if later identified.

## 10. RNG and copied-code notices

`engine/src/production_noise.cpp` identifies an altered scalar subset of the
installed PyTorch RNG/math implementations. Its notices are collected in
[`RNG_NOTICES.md`](../RNG_NOTICES.md).

| Material | Inspected rights and preservation obligation |
| --- | --- |
| PyTorch root license | BSD-3-Clause; full contributor copyright list, conditions and disclaimer in source and binary documentation; no endorsement; no source-publication condition |
| MT19937 | BSD-3-Clause-style permission; copyright 1997–2002 Matsumoto/Nishimura, listed acknowledgements, conditions/disclaimer retained in source and binary materials |
| `avx_mathfun` | Zlib-style permission; Garberoglio copyright/credits to Pommier; retain source notice, do not misrepresent origin, mark altered versions. Binary acknowledgement encouraged rather than separately mandated by that text |

All three grants support modification/commercial use and coexist with MIT original
code. Do not erase their notices or declare those fragments exclusively owned by
Out Loud. None requires publishing all Out Loud source or relinking rights.

**CONFIRMED:** the RNG document reproduces the direct sources' relevant license
blocks and marks the scalar implementation altered. Its PyTorch block matches the
installed package root license after newline normalization. The installed root
license also matches the pinned online commit. Header differences against that
commit are wheel-added stable-API preprocessor guards, not different license blocks.
No missing direct RNG license block was identified. This is not a finding that
`RNG_NOTICES.md` covers Pocket, Unicode, model data or the entire PyTorch wheel.

| Local evidence file | SHA-256 |
| --- | --- |
| `torch-2.14.1.dist-info/licenses/LICENSE` | `0fc92425d130d9c87a5eb7427082e3afdbff6ef169bc678e15abba4c2d37c934` |
| `torch/include/ATen/core/MT19937RNGEngine.h` | `3987a82cf8c8fe6672badbe9bd885c3cd69f9e30fe17e4ffc4294aa921856132` |
| `torch/include/ATen/native/cpu/avx_mathfun.h` | `4bc6ace2a4ef97a1328d1a919e81ec9b1d58a869d73102220a6208320ebc0470` |

Exact primary sources: [PyTorch license](https://github.com/pytorch/pytorch/blob/5c4886908584029761b579af026dcfb627c84070/LICENSE),
[MT19937 header](https://github.com/pytorch/pytorch/blob/5c4886908584029761b579af026dcfb627c84070/aten/src/ATen/core/MT19937RNGEngine.h),
[math header](https://github.com/pytorch/pytorch/blob/5c4886908584029761b579af026dcfb627c84070/aten/src/ATen/native/cpu/avx_mathfun.h).
The reference manifest records `2.14.1+cpu` and the same commit. Full wheel
subdependencies are development-only; their licenses do not automatically apply
to these specifically identified scalar adaptations.

## 11. Microsoft/.NET/runtime redistribution

### Windows dependencies and compiler source

SAPI, COM, BCrypt, transaction/registry APIs and normal Windows system DLLs are
supplied by the OS. Do not copy OS DLLs/import libraries/SDK headers into the
installer merely because `dumpbin` lists an import. Windows 10/11 supply the UCRT;
no separate UCRT payload is presently needed. SDK distributable-code grants apply
only to listed items and conditions, not the whole SDK.

MSVC 14.44.35207's `yvals_core.h` identifies Microsoft STL source as
**Apache-2.0 WITH LLVM-exception**. The [STL license](https://github.com/microsoft/STL/blob/main/LICENSE.txt)
contains an exception for embedded object portions that removes specified Apache
notice/license-copy requirements for those portions. No STL source is vendored.
If headers/source are later copied, their source terms must be preserved. Apache's
patent terms apply to its actual licensed material; MIT has no equivalent express
grant. This does **not** relicense the Microsoft VC DLL package under Apache/MIT.

### VC runtime DLLs: separate binary redistribution entitlement

The engine requires release x64 `msvcp140.dll`, `msvcp140_atomic_wait.dll`,
`vcruntime140.dll` and `vcruntime140_1.dll`. Other native components require the
three non-atomic-wait DLLs. The installed VS redist tree is
`VC/Redist/MSVC/14.44.35112/x64/Microsoft.VC143.CRT`; the compiler is 14.44.35207.
Version trees, System32 availability and signatures are technical evidence only.

Microsoft's [VS 2022 redistribution list](https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution)
and [VC redistribution guidance](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files?view=msvc-170)
identify eligible unmodified release redistributables and require the relevant
licensed distributor entitlement. Do not ship debug/non-redist/preview components
or presume that any installed System32 DLL can be copied by any MIT downstream user.

**UNRESOLVED, conditional P1:** this workspace has Build Tools 2022 17.14.41.
The inspected [Build Tools license](https://visualstudio.microsoft.com/wp-content/uploads/2024/03/Visual-Studio-2022-Diagnostic-Build-Tools-Agent-License_Update-March-2024_EN.docx)
did not establish a distributable-code grant for these DLLs. The
[VC runtime end-user terms](https://visualstudio.microsoft.com/license-terms/vs2022-cruntime/)
are not the same as a Visual Studio distributor grant. No owner entitlement was
documented. This is missing evidence, not a finding that the owner is unlicensed.

Before bundling, identify the distributor's applicable grant and exact authorized
package/source. Alternatively detect the prerequisite and have users install the
official Microsoft runtime themselves, avoiding Microsoft binary redistribution by
Out Loud. Preserve vendor bytes/signatures; any authorized package must retain its
terms. MIT downstream users receive Out Loud code rights, not automatic entitlement
to redistribute Microsoft binaries under MIT.

### .NET 10 and WPF

The app is x64 `net10.0-windows`, `asInvoker`, **framework-dependent**. Resolved
application/test NuGet assets contain no external package libraries; validation's
project reference is not a third-party package. Runtime configuration references
.NET Core and Windows Desktop 10.0.0 with ordinary runtime resolution; installed
SDK/runtime versions are 10.0.401/10.0.12. No ASP.NET runtime is required.

The [runtime source license](https://github.com/dotnet/runtime/blob/v10.0.12/LICENSE.TXT)
and [WPF source license](https://github.com/dotnet/wpf/blob/v10.0.0/LICENSE.TXT)
are MIT, copyright .NET Foundation and Contributors. No project-wide reciprocal
source disclosure follows from their use. Microsoft describes .NET as
[free for commercial use](https://dotnet.microsoft.com/en-us/platform/free).

**Do account for the apphost:** even framework-dependent `.exe` output contains
Microsoft hosting code. Carry its applicable copyright/permission notice with the
binary release. Pin the final host-pack provenance when publishing; the installed
runtime patch is not proof of the exact apphost pack embedded in every older output.

For a bundled Desktop Runtime installer or self-contained framework, preserve the
actual selected package's licenses and ThirdPartyNotices rather than assuming every
binary is solely MIT. Local `C:\Program Files\dotnet\LICENSE.txt` contains Microsoft's
.NET Library binary terms; `ThirdPartyNotices.txt` contains substantial additional
component notices. That directory contains several runtime generations, so those
global files alone do not identify an exact future .NET 10 payload. The inspected
binary terms permit conditional object redistribution and require applicable notice/
distribution protections; use the selected official package's terms as the authority.
[Packaging guidance](https://learn.microsoft.com/en-us/dotnet/core/distribution-packaging)
and [Windows installation guidance](https://learn.microsoft.com/en-us/dotnet/core/install/windows)
support distinguishing SDK, desktop runtime and deployment choices. Final artifact
terms/notices are an A03 compliance action; no paid .NET license system is needed.

### SignTool: identified grant does not cover ordinary DLL signing

The installed SDK license points to Microsoft's
[Windows SDK redistributable list](https://learn.microsoft.com/en-us/legal/windows-sdk/redist).
SignTool appears in the Windows Packaging Tools list with associated files under
a grant restricted to MSIX/MSIXbundle packaging operations. That identified grant
does not authorize bundling it solely to sign classic SAPI DLLs. No broader applicable
grant was established. This is a conditional issue only if A03 chooses that payload.

Do not claim SignTool can never be redistributed, or that it is open-source/MIT.
Either establish another applicable permission, avoid bundling the tool by using a
separately installed SDK, pre-sign release binaries where suitable, or investigate a
project-owned Windows signing implementation. Those are future engineering choices,
not work performed here. Local certificate creation/trust/signing remains A03.

## 12. WPF/UI asset licensing

**CONFIRMED:** the app uses WPF-native common/light/dark/high-contrast XAML resources
and project views. No MahApps, ModernWpf, Fluent UI package or other external theme
library is referenced. No tracked `.ttf`, `.otf`, `.ico`, `.png`, `.svg` or similar
font/image resource was found. No externally licensed artwork was identified.

Font families are Segoe UI, Consolas and Segoe MDL2 Assets. Refresh/chevron glyphs
are rendered from the user's installed system font. Microsoft's
[font redistribution FAQ](https://learn.microsoft.com/en-us/typography/fonts/font-faq)
distinguishes ordinary application use from font-file redistribution. The
[system icon-font reference](https://learn.microsoft.com/en-us/windows/apps/design/iconography/segoe-ui-symbol-font)
documents glyph use. No font file is currently copied, and no font redistribution
permission is inferred. Do not add font binaries or extracted third-party icon
libraries to A03 without a separate grant.

The original UI can follow the owner's MIT policy. Absence of an external package
does not establish every contributor's copyright ownership; preserve credits if
actual copied styles/assets are later found. No UI/source change was performed.

## 13. Offline preparation dependencies

The 50-package inventory in section 4, CPython, CMake, Git, SDK/MSVC and .NET SDK
are development dependencies, not end-user inference requirements. Server-side
FastAPI/Uvicorn dependencies in the oracle environment do not create an Out Loud
cloud-service dependency.

Actual release-relevant outputs must be treated separately:

| Tool/workflow | Material reaching an output | Licensing result |
| --- | --- | --- |
| `tools/assets/export_model.py` | Original checkpoint BF16 subset | Model rights survive; section 6 |
| `tools/tokenizer/export_tokenizer.py` | Learned vocabulary plus Unicode tables | CC asset terms plus Unicode notice; section 8 |
| `tools/assets/export_voice.py` | Six layers' FP32 voice K/V prefixes in `.olv` | Voice/source permissions survive; excluded from installer |
| Native prepared/OLV import | Converted or copied user conditioning | Does not grant third-party voice rights |
| Reference/neural/RNG/utterance exporters | Model/voice-derived states, reference vectors and PCM | Test/development data; not automatically licensed as project source |
| Compiler/SDK builds | Apphost, STL object portions, normal linked runtime imports | Sections 10–11, not all tool licenses propagated to app |

The local CMake 4.4.3 license is BSD-3-Clause (Kitware/contributors); Git is GPL-2.0;
CPython has PSF/Python license material. NumPy/SciPy wheels include BLAS and compiler
runtime notices, including GPL with GCC runtime exception; setuptools has vendored
additional licenses. None of these packages is linked into the shipped native
engine. Their presence in `.venv` does not impose GPL/LGPL/MPL on original Out Loud
source. This conclusion depends on excluding those tools/wheels from release payloads.

## 14. User-provided voice policy

**Confirmed owner policy: zero bundled voices.** Technical support for importing,
replacing, exporting and synthesizing user voices remains intact and is not a
license to distribute the files, source recordings, character voices or likenesses.
OLV integrity/compatibility checks do not verify speaker consent or content ownership.

Current code and artifacts show:

- `prepare-deployment.ps1` has model/tokenizer inputs and an explicit source map;
  **no VoicePath parameter or `.olv` copy**. Schema-2 inventory lists only the two
  shared runtime assets. Current registration scripts register the shared engine,
  not a bundled default voice.
- `deployment-common.ps1` fixes allowed PE files and the two asset inventory entries.
  Its extra-file rejection concerns **PE files**; it is not a universal rejection of
  arbitrary extra `.olv` files in a directory. A03 needs a complete payload allowlist.
- The existing signed stage's manifest has 20 file records and no voice path.
  A manifest without voices is evidence for that stage, not every future archive.
- `tools/assets/export_voice.py` defaults to `voices/gnome_male.safetensors` and writes
  `generated/assets/<stem>.olv`. It creates development voice output, not installer
  content. Fixture exporters and development CMake targets consume voice-derived
  data, and some checker binaries compile generated fixture arrays.
- Ignored prepared Gnome/Undead files, generated Gnome/Undead/legacy/imported OLVs,
  scratch libraries and historical installed `voices/gnome_male.olv` exist outside
  tracked source. Never recursively package `generated/`, `.venv`, a build tree,
  production Voice Library or the historical installation directory.
- No tracked `.olv`/prepared voice binary was found. Git ignore rules do not enforce
  an installer payload policy. The development stage's `outloud-sapi-check.exe` must
  also be excluded from the public installer; absence of standalone OLV files does
  not exclude voice conditioning embedded in a checker.

No current tracked deployment script was found staging `.olv` voices. The concrete
risks are explicit voice-generation outputs, historical installed content and an
overbroad future directory copy. It would be inaccurate to report a current automatic
voice-staging defect that was not found.

Recommended future user-facing statement: “Import and share only voices you are
authorized to use and redistribute. Out Loud's MIT license does not license voice
files, source recordings or a person's likeness.” This is a recommendation, not a
new license restriction on original code and not an implemented UI change.

## 15. World of Warcraft integration scope

The Windows engine exposes standard SAPI5 COM interfaces. No Blizzard game binary,
logo, proprietary UI resource, recording or addon source was identified as a tracked
Windows application dependency/payload. Documentation's references to WoW and named
development voices are not evidence of redistribution permission for game assets.

Using SAPI does not redistribute WoW's implementation. The user reports that the
existing signed engine works in WoW; signature acceptance is not a copyright grant.
No client was launched, stopped or inspected internally. No claim about a universal
Blizzard signing requirement, endorsement or addon distribution license is made.

A separately bundled addon or branded artwork would need its own exact source,
license and packaging decision. No addon is included in this proposed Windows
installer inventory; no addon work or new Blizzard policy analysis is necessary
to establish the present payload boundary.

## 16. MIT compatibility assessment

| Material | Original Out Loud code can remain MIT? | Conditions/limits |
| --- | --- | --- |
| Original engine/backend/frontend/helper/CLI/scripts | Yes, for rights actually owned by project contributors | Owner supplies copyright identity; inherited material remains identified |
| Pocket/MAR permissive material | Yes for original additions | Preserve upstream MIT texts/credits; resolve DiT chain separately |
| Potential DiT protected expression | Not established for unrestricted commercial release | P1, determine actual incorporation/permission; simply adding MIT or a notice is insufficient |
| RNG BSD/Zlib adaptations | Yes | Retain notices and alteration statement; do not erase original ownership |
| STL embedded object portions | Yes | Applicable Apache/LLVM exception; separate Microsoft binary terms |
| Model/vocabulary/Unicode data | Yes for application code | Separate asset licenses; no MIT-only claim over those data |
| WPF/.NET | Yes | Host/runtime/package notices according to exact distributed payload |
| Proprietary Microsoft redistributables | Yes for app code | Separate entitlement/package conditions; downstream MIT is not a DLL relicensing |
| Offline MPL/LGPL/GPL material | Yes under current exclusion | Reassess if those actual packages/tool binaries are ever bundled |
| Voices | Yes for app code | No voice grant; excluded by policy |

No verified runtime dependency demands wholesale reciprocal source publication or
a change of Out Loud's original-code license. CC-BY-4.0 data may coexist with MIT
code; they remain separately licensed. The potential NC code issue differs because
it concerns possible incorporated implementation expression, not merely a separately
marked data file. Do not apply a blanket MIT copyright header to adapted RNG source,
upstream code or asset license files.

## 17. Redistribution decision matrix

Each row covers the stated artifact/use. C/D identifiers map to the complete inventory.
“Commercial” describes rights under the identified terms, not an assumption of
copyright ownership or a declaration that every package artifact is already compliant.

| Component | Version/origin | License | Public installer? | MIT compatibility | Redistribution status | Commercial reuse | Required notices | Evidence | Remaining uncertainty |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| C01–02 original code | Entry HEAD, Out Loud | Intended MIT | Production app yes | Compatible for owned code | PERMITTED WITH VERIFIED CONDITIONS | Yes for owned code | Future project MIT notice | Owner policy/current source | Copyright holder/actual contribution rights; upstream exceptions |
| C03 Pocket-derived expression | Kyutai 3.3.0 | MIT | Native provenance yes | Compatible, except separate chain | PERMITTED WITH VERIFIED CONDITIONS for identified MIT material | Yes under MIT | Pocket MIT/credits | S01/local matching license | Does not resolve C05 |
| C04 MAR-derived expression | Pinned MAR | MIT | Potential incorporation | Compatible except ancestor issue | PERMITTED WITH VERIFIED CONDITIONS for identified MIT material | Yes under MIT | MAR MIT/copyright | S02/Pocket source | C05 protected-expression question |
| C05 original DiT code under MIT-only commercial terms | Pinned historical DiT | CC-BY-NC-4.0 | No direct package | Incompatible with that use | NOT PERMITTED UNDER IDENTIFIED TERMS | No unrestricted commercial grant | Attribution cannot waive NC | S03 | Actual protected DiT expression in C++ remains UNRESOLVED — MORE EVIDENCE REQUIRED |
| C06 GLIDE reference | Current upstream license | MIT | No package; possible indirect expression | Permissive | NOT APPLICABLE — NOT DISTRIBUTED as a package | Yes under source grant | Actual notice if copied expression established | S04 | Exact inherited expression/history partially verified |
| C08 `.olp` weights | Pinned Kyutai checkpoint | CC-BY-4.0 + published policy question | Intended yes | Separately licensed asset | UNRESOLVED — MORE EVIDENCE REQUIRED overall | CC grant yes | Creator/source/license/change notices | S05/S07 + hashes | Policy/access agreement relationship |
| C09 tokenizer vocabulary | Distinct pinned Kyutai repo | CC-BY-4.0 + policy question | Intended yes | Separately licensed asset | UNRESOLVED — MORE EVIDENCE REQUIRED overall | CC grant yes | Separate source/license/change attribution | S06 + hashes | Policy scope and effect |
| C10 Unicode tables | UCD 15.0.0 | Unicode-DFS-2016; current Unicode-3.0 reference | Embedded yes | Compatible | PERMITTED WITH VERIFIED CONDITIONS | Yes | Relevant Unicode copyright/permission | S08/exporter | Final notice must be added |
| C11–13 scalar RNG | Pinned Torch/MT/math headers | BSD-3-Clause / Zlib-style | Embedded yes | Compatible | PERMITTED WITH VERIFIED CONDITIONS | Yes | Complete `RNG_NOTICES.md`; source credits/alteration | S09–11 | No direct notice gap found |
| C14 STL object portions | MSVC 14.44 | Apache-2.0 WITH LLVM-exception | Embedded yes | Compatible | PERMITTED WITH VERIFIED CONDITIONS | Yes | Apply exception; source notices if vendored | S12/local headers | No STL source vendored |
| C15 VC DLLs | Microsoft release x64 | Microsoft distributor terms | Undecided | Separate binary terms | UNRESOLVED — MORE EVIDENCE REQUIRED if bundled | Authorized distributor grant needed | Selected package terms/vendor notices | S13/local tools | Owner entitlement not established |
| C16–18 Windows components | OS / SDK interfaces | Windows/SDK | OS files no | Interface use compatible | NOT APPLICABLE — NOT DISTRIBUTED | App can be commercial | No imported DLL copied | PE imports/local SDK | Recheck only if payload changes |
| C19 apphost | .NET SDK host pack | MIT source/applicable host terms | Yes | Compatible | PERMITTED WITH VERIFIED CONDITIONS | Yes | Host copyright/license | S14/current apphost | Pin exact final host pack |
| C19–20 runtime payload | Future Desktop Runtime package | MIT sources + binary/package notices | Prerequisite or bundle pending | Compatible with separate notices | UNRESOLVED — MORE EVIDENCE REQUIRED for exact future payload | .NET supports commercial use | Selected official license/ThirdPartyNotices | S14–15/local dotnet terms | No package chosen; not a blanket prohibition |
| C21 font files | Microsoft system fonts | Windows font terms | No | Installed-font use compatible | NOT APPLICABLE — NOT DISTRIBUTED | Ordinary app use supported | No bundled-font license invented | S16/XAML | New font redistribution would need grant |
| C23 SignTool for classic DLL signing | SDK 10.0.26100.0 | SDK, identified MSIX-limited grant | Not currently; future option | Separate proprietary tool | NOT PERMITTED UNDER IDENTIFIED TERMS for that purpose | No applicable broader grant verified | Notice alone does not fix purpose limit | S17–18 | Alternate permission/design UNRESOLVED |
| C22,24–28 and D01–50 | Build/oracle tools, exact versions above | Respective licenses | No | Tool use does not relicense app | NOT APPLICABLE — NOT DISTRIBUTED | Per tool's own license if redistributed | No full `.venv` notice set needed for excluded tools | Local package licenses/requirements | Future developer bundle needs separate inventory |
| C29–30 fixtures/voices | Developer/user/historical assets | Content-specific, unverified | **No** | No grant from MIT | NOT APPLICABLE — NOT DISTRIBUTED | Not established for those assets | Future users need their own rights | Scripts/current inventory | Exclusion must include embedded checker data |
| C31 behavior reference | Earlier Chatterbox | Not established here | No package | Actual copied expression would need review | NOT APPLICABLE — NOT DISTRIBUTED as a package | Not concluded | Preserve provenance; actual inherited notices if found | Comments/historical hashes | Unavailable historical source comparison |

## 18. Required third-party notices

These are **proposed future filenames**, not new files created by this audit.
Licenses usually require particular contents/preservation, not these exact names.
The following gives A03 a concrete packaging target while keeping that distinction.

| Proposed file | Contents and obligation | Applies to |
| --- | --- | --- |
| `LICENSE` | Project MIT text, owner-confirmed copyright identity/year; scope original owned code | Source and binary releases |
| `THIRD_PARTY_NOTICES.md` | Component/license map, upstream origins, exceptions to project MIT, pointer to full texts/data notices | Recommended consolidation; individual underlying notices mandatory |
| `licenses/Pocket-TTS-MIT.txt` | Exact pinned Pocket license and supplied credits, without invented holder | Source/compiled upstream-derived material |
| `licenses/MAR-MIT.txt` | Actual MAR MIT/copyright where protected adaptation is retained | Conditional inherited-material obligation; conservative inclusion recommended pending provenance resolution |
| `RNG_NOTICES.md` | Existing complete PyTorch/MT/math blocks and alteration identification | Source and compiled RNG release materials; preserve unchanged contents |
| `licenses/Unicode-Data.txt` | Unicode 15-era copyright/permission notice for generated tables; identify source/version and transformations | Binary tokenizer and distributed generated-data source |
| `licenses/Pocket-Model-Attribution.md` | Kyutai model identity/revision/source link, creator credits as supplied, CC license URI, retained notices and subset/container modification description | Bundled OLP, after policy clarification |
| `licenses/Pocket-Tokenizer-Attribution.md` | Distinct tokenizer repo/revision/source, supplied attribution, CC URI and conversion description; separate Unicode reference | Bundled tokenizer, after policy clarification |
| `licenses/CC-BY-4.0.txt` | Full unaltered CC legal code | Recommended offline usability; license identification/URI and other required attribution are mandatory, full local legal-code copy is not universally required by CC |
| `licenses/DotNet-AppHost-MIT.txt` | Applicable runtime/apphost copyright and permission text, plus any applicable host subcomponent notices | Current apphost-containing executable |
| Selected runtime vendor license/ThirdPartyNotices files | Actual official .NET Desktop Runtime and authorized VC package's required materials; preserve upstream contents | Only binaries/prerequisites actually redistributed |
| `licenses/GLIDE-MIT.txt` | Actual GLIDE notice if protected source expression was inherited | Conditional; no model/package bundled |

A DiT attribution/license file alone would **not** cure the unrestricted-commercial
compatibility question. Obtain the relevant permission/provenance resolution first;
then document the resulting terms. Likewise do not invent mandatory copies of all
95 Torch-wheel licenses when only the identified scalar source is shipped.
STL's object-code exception is addressed in section 11; including a source-license
reference is useful documentation, not a claim that the exception still mandates
every Apache notice for ordinary compiler output.

## 19. Model and tokenizer distribution conditions

For the intended exact assets, prepare **separate** attribution records linked to
the hashes in sections 7–8. Identify the publisher/creators as actually supplied,
upstream revision/file, license URI and conversion changes. Preserve any supplied
copyright, disclaimer or attribution notices. Do not put an MIT-only label on the
runtime directory or impose application EULA/DRM terms that restrict CC-granted
rights. License code and assets visibly separately.

CC conditions and artifact identities are confirmed; section 6's additional-policy
question remains unresolved. Obtain and preserve an authoritative clarification or
applicable agreement, including whether the tokenizer is within its scope and whether
redistribution passes any separate contractual requirement to recipients. Do not
invent a consent-click, NC restriction or downstream usage agreement as a fix.

No new download, regeneration, model modification or user-voice inclusion is needed
to answer the confirmed identity questions. The currently verified converted bytes
can be the future pinned release input once the rights question is resolved.

## 20. Free distribution and downstream commercial reuse implications

Charging zero does not eliminate notice or ownership obligations. MIT permits a
downstream party to sell or commercially use original Out Loud code with its
notices. That party must also comply with each separately redistributed asset or
binary license; Out Loud cannot grant rights belonging to Microsoft, Unicode or
another source owner.

The verified permissive software/RNG licenses are consistent with that policy.
Separately attributed CC-BY model/tokenizer data can support commercial reuse under
their grant. The policy question must be resolved rather than silently narrowing
MIT. Original DiT noncommercial code, if actually incorporated as protected expression,
would pose a substantive issue even if Out Loud itself charges nothing. A free-only
initial release is not a substitute for the owner's intended commercial permission.

No licensing server, DRM, activation flow or alternate project license is proposed.
Voice redistribution rights remain the user's/content owner's responsibility and
are not supplied by the project license.

## 21. Requirements for future MIT LICENSE and THIRD_PARTY_NOTICES files

Before creating a project license, confirm the owner's preferred copyright holder
spelling, applicable year(s) and any contributor rights/credits. No holder name was
chosen from Git author metadata. Use the standard MIT grant for project-owned
contributions and clearly identify third-party exceptions through a scope statement
and notice map. Do not alter upstream grants or insert ownership claims over them.

For source releases, retain original notices near inherited source or clearly linked
notice files, including the RNG alteration statement. For binary releases, ship the
notice/license materials required for compiled code and assets; an upstream website
link alone is insufficient for BSD/MIT license-text preservation. CC attribution
can use its required URI, with an offline full text recommended. Do not assert that
releasing the project openly fulfills a hypothetical missing permission.

The future README should say that original project code is MIT, model/tokenizer and
Unicode data retain separate licenses, Microsoft prerequisites have their own terms,
and no voice files are included. These edits were not made in A04.

## 22. A03 installer compliance checklist

| Classification | Action before public packaging |
| --- | --- |
| **MANDATORY UNDER VERIFIED TERMS** | Preserve MIT/BSD/Zlib copyright, permission, conditions/disclaimers and required altered-source identification for actual inherited material. |
| **MANDATORY UNDER VERIFIED TERMS** | Provide compliant attribution/license/change information for each bundled CC asset; retain the relevant Unicode data notice. |
| **MANDATORY UNDER VERIFIED TERMS** | Respect selected Microsoft distributable-code/package conditions; preserve applicable vendor terms and notices. Do not use an MSIX-only grant for classic DLL signing. |
| **MANDATORY UNDER VERIFIED TERMS** | Do not claim MIT relicenses third-party material or remove its ownership/license notices. |
| **UNRESOLVED PENDING EVIDENCE** | Resolve the sampler's protected-expression/DiT rights question without changing the owner's MIT policy. |
| **UNRESOLVED PENDING EVIDENCE** | Obtain clarification of Kyutai's additional use-policy/access agreement and model/tokenizer downstream distribution scope. |
| **UNRESOLVED PENDING EVIDENCE** | Confirm owner entitlement before redistributing VC DLLs/installer, or choose a user-installed official prerequisite. |
| **UNRESOLVED PENDING EVIDENCE** | Select exact .NET Desktop Runtime/host artifacts and framework-dependent versus self-contained packaging; identify their actual notices. |
| **UNRESOLVED PENDING EVIDENCE** | If SignTool will be bundled, establish an applicable grant for the actual signing purpose or select a design that excludes it. |
| **UNRESOLVED PENDING EVIDENCE** | Confirm project copyright holder/contributor authority before publishing the MIT notice. |
| **RECOMMENDED** | Use a production-only file allowlist covering every file, not just PE binaries. **Zero voices is a mandatory owner requirement**, regardless of license; reject standalone and checker-embedded voice/test data. |
| **RECOMMENDED** | Exclude `.venv`, SDKs, caches, reference checkers/fixtures, old deployed bundles and ProgramData libraries. Current development staging is not the public package. |
| **RECOMMENDED** | Preserve full offline legal texts, exact source/asset hashes, provenance and the final package's notices in the release receipt. |
| **RECOMMENDED** | Prefer official prerequisite packages/detection to ad hoc System32 copying; preserve vendor files/signatures and never sign them as project-owned code. |
| **RECOMMENDED** | Make the separate code/data/voice terms accessible in installed documentation or an About/legal view; explain user voice rights. |
| **OPTIONAL** | Add an SPDX/SBOM export and automated notice/asset allowlist checks in a later authorized packaging task. |
| **OPTIONAL** | Add upstream acknowledgements beyond required notices; do not imply vendor endorsement. |

No checklist action has been implemented. Local certificate creation/trust and
installer signing architecture remain A03 engineering work, independent of whether
the unsigned underlying content may legally be redistributed.

## 23. Release blockers and uncertainty

| Priority | Component/issue | Finding; exact evidence/action needed |
| --- | --- | --- |
| **P0** | Mandatory distributed components | **None proven.** DiT's identified noncommercial grant is a prohibition for original DiT code under unrestricted-commercial terms, but protected incorporation into the mandatory Out Loud sampler is not established. |
| **P1** | C03–05 sampler provenance | Pocket explicitly references MAR; final-layer attribution leads to DiT's NC license. Determine protected-expression incorporation and compatible authority, or document independent implementation. Notice-only remediation is insufficient if an NC derivation is confirmed. |
| **P1** | C08–09 model/tokenizer policy | Exact revisions declare CC-BY-4.0 and additional prohibited-use wording. Establish the applicable access agreement/policy status, asset scope and downstream implications; preserve evidence. Do not classify missing clarification as a proven ban. |
| **P1, conditional** | C15 VC runtime bundling | Identify owner's actual authorized redistributable entitlement. Build Tools installation/signature/System32 availability is not sufficient. This blocker can be avoided by not redistributing those binaries. |
| **P1, conditional** | C23 SignTool classic-DLL payload | The inspected SDK list covers MSIX packaging, not this purpose. Obtain another grant or exclude the tool from installer. No present SignTool payload exists. |
| **P2** | MIT publication | Project `LICENSE` absent; confirm holder/contributor scope and create the intended notice in a separately authorized task. |
| **P2** | Software/RNG/apphost notices | Preserve existing RNG notices in source/binary releases; add actual Pocket/apphost and applicable inherited upstream license material. |
| **P2** | Model/tokenizer/Unicode notices | Required asset attribution and Unicode text absent from current development notice set. Add after rights clarification; actual asset licenses remain separate. |
| **P2** | Final Microsoft payload | Select authorized release artifacts and carry their exact terms/ThirdPartyNotices; identify apphost pack. Runtime/source license labels alone are not a final installer inventory. |
| **P2, owner requirement** | Public payload boundary | Zero voices, no embedded checker voice data, no developer environment. Current scripts comply with standalone-voice exclusion but still stage a checker and do not reject every extra non-PE file. |
| **P3** | Historical references/SBOM | Preserve historical Chatterbox provenance and improve machine-readable inventory; investigate specific copied expression if new evidence appears. No copied Chatterbox component was established. |
| **P3** | Excluded voice rights | No permission conclusion for Gnome/Undead or other voices. Not a blocker under the fixed no-voices policy; re-audit if policy changes. |

The overall result is **not cleared**, not “prohibited.” Verified grants cover many
components; specific evidence gaps and missing mandatory notices prevent a complete
public-package clearance. A03 can use this inventory for design, but neither installer
implementation nor public release was authorized by this audit.

## 24. Source references and evidence

All sources below were inspected on **2026-10-10**. Online pages are primary
maintainer/license/publisher sources; local paths identify the actual files inspected.
Current-main sources are explicitly identified where an exact historical version
was not established. Critical asset conclusions use exact-revision metadata.

| Ref | Source/document and version | Supported conclusion; confidence |
| --- | --- | --- |
| S01 | [Pocket license, pinned software commit](https://github.com/kyutai-labs/pocket-tts/blob/3dbee45d343d7dddd0d105468d17f8dcba14db3e/LICENSE); installed `.venv/Lib/site-packages/pocket_tts-3.3.0.dist-info/licenses/LICENSE` and `pocket_tts/modules/mlp.py` | MIT grant/text match and direct MAR provenance; CONFIRMED. Final derivation interpretation unresolved. |
| S02 | [MAR pinned license](https://github.com/LTH14/mar/blob/fe470ac24afbee924668d8c5c83e9fec60af3a73/LICENSE) and [sampler code](https://github.com/LTH14/mar/blob/fe470ac24afbee924668d8c5c83e9fec60af3a73/models/diffloss.py), reference commit dated 2024-09-27 | MIT notice and DiT/GLIDE source credits; CONFIRMED. |
| S03 | [DiT historical license](https://github.com/facebookresearch/DiT/blob/c9f545c56d212c32e73c57746550e3a30af897a4/LICENSE.txt), [README](https://github.com/facebookresearch/DiT/blob/c9f545c56d212c32e73c57746550e3a30af897a4/README.md), [models.py](https://github.com/facebookresearch/DiT/blob/c9f545c56d212c32e73c57746550e3a30af897a4/models.py), source revision 2023-02-12 | Code/weights NC grant, source holder/structure; CONFIRMED terms, UNRESOLVED protected C++ inheritance. Raw license SHA `7c6858d0e71dc8b858255270ad4b5004e80b5b058bf5bbc408a28430d6ddbf4b`. |
| S04 | [GLIDE LICENSE, current main](https://github.com/openai/glide-text2im/blob/main/LICENSE) | MIT, OpenAI 2021; CONFIRMED current terms, historical incorporation PARTIALLY VERIFIED. |
| S05 | [Exact Pocket model API](https://huggingface.co/api/models/kyutai/pocket-tts/revision/983151f13aaeab1b13c1e5e3c2c383d49a9edf3f?blobs=true), [model repository](https://huggingface.co/kyutai/pocket-tts) | Pinned model hash/size/CC declaration/gating/policy; CONFIRMED metadata. Pinned raw card access HTTP 401, acceptance contract UNRESOLVED. |
| S06 | [Exact tokenizer API](https://huggingface.co/api/models/kyutai/pocket-tts-without-voice-cloning/revision/00eac05ed3d16bdc3f6b5d598874019c34a89214?blobs=true) and [pinned card](https://huggingface.co/kyutai/pocket-tts-without-voice-cloning/raw/00eac05ed3d16bdc3f6b5d598874019c34a89214/README.md) | Distinct revision/file, CC declaration and extra policy; CONFIRMED. Learned-data rights not replaced by tool licenses. |
| S07 | [CC-BY-4.0 legal code](https://creativecommons.org/licenses/by/4.0/legalcode.en), [deed](https://creativecommons.org/licenses/by/4.0/) | Asset reuse/adaptation/attribution and separate-arrangement distinction; CONFIRMED grant conditions, Kyutai contract interpretation UNRESOLVED. |
| S08 | [Unicode license](https://www.unicode.org/license.txt), [ICU 72.1 Unicode section](https://github.com/unicode-org/icu/blob/release-72-1/icu4c/LICENSE), [CPython 3.12.10 generator](https://github.com/python/cpython/blob/v3.12.10/Tools/unicode/makeunicodedata.py) | Current/historical permissive Unicode terms, UCD version; CONFIRMED. No claim ICU code is linked. |
| S09 | [Pinned PyTorch LICENSE](https://github.com/pytorch/pytorch/blob/5c4886908584029761b579af026dcfb627c84070/LICENSE), installed Torch license and `generated/rng/rng-reference.json` | Scalar source grant, version/commit and notice match; CONFIRMED. |
| S10 | [Pinned MT header](https://github.com/pytorch/pytorch/blob/5c4886908584029761b579af026dcfb627c84070/aten/src/ATen/core/MT19937RNGEngine.h) | Actual adapted-source notice; CONFIRMED. |
| S11 | [Pinned math header](https://github.com/pytorch/pytorch/blob/5c4886908584029761b579af026dcfb627c84070/aten/src/ATen/native/cpu/avx_mathfun.h) | Garberoglio/Pommier credits and Zlib-style conditions; CONFIRMED. |
| S12 | [Microsoft STL LICENSE, current main](https://github.com/microsoft/STL/blob/main/LICENSE.txt); local MSVC 14.44.35207 `include/yvals_core.h` | Apache/LLVM exception source label and object exception; CONFIRMED. No inference about proprietary DLL license. |
| S13 | [VS 2022 redist list](https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution), [VC guidance](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files?view=msvc-170), [Build Tools terms](https://visualstudio.microsoft.com/wp-content/uploads/2024/03/Visual-Studio-2022-Diagnostic-Build-Tools-Agent-License_Update-March-2024_EN.docx), [VC end-user terms](https://visualstudio.microsoft.com/license-terms/vs2022-cruntime/) | Distinguish distributor and user grants; CONFIRMED documents, owner entitlement UNRESOLVED. Local `Licenses/1033/Redist.txt` points to VS 17 redist list. |
| S14 | [Runtime v10.0.12 LICENSE](https://github.com/dotnet/runtime/blob/v10.0.12/LICENSE.TXT), [.NET free-use statement](https://dotnet.microsoft.com/en-us/platform/free), [packaging](https://learn.microsoft.com/en-us/dotnet/core/distribution-packaging), [Windows install guide](https://learn.microsoft.com/en-us/dotnet/core/install/windows); local dotnet LICENSE/ThirdPartyNotices | Source MIT and commercial use, binary-package distinction; CONFIRMED. Final payload/host pack pending. |
| S15 | [WPF v10.0.0 LICENSE](https://github.com/dotnet/wpf/blob/v10.0.0/LICENSE.TXT) | WPF source MIT; CONFIRMED. Selected runtime package retains its own notices. |
| S16 | [Microsoft font FAQ](https://learn.microsoft.com/en-us/typography/fonts/font-faq), [Segoe icon-font guidance](https://learn.microsoft.com/en-us/windows/apps/design/iconography/segoe-ui-symbol-font) | System-font reference versus redistribution; CONFIRMED. |
| S17 | `C:\Program Files (x86)\Windows Kits\10\Licenses\10.0.26100.0\sdk_license.rtf` and `sdk_third_party_notices.rtf` | SDK distributable-code conditions/list references; CONFIRMED local terms. |
| S18 | [Windows SDK redist list](https://learn.microsoft.com/en-us/legal/windows-sdk/redist) | Identified SignTool grant limited to MSIX packaging; CONFIRMED scope, alternate permission UNRESOLVED. |
| S19 | `.venv/Lib/site-packages/*/*.dist-info` metadata/license locations; [pinned requirements](../reference/requirements-reference.txt) | Actual 50 offline package versions/licenses; CONFIRMED package-level inspection, not complete unshipped-wheel subcomponent audit. |

Local implementation/artifact references:

| File/group | Evidence inspected |
| --- | --- |
| [CMakeLists.txt](../CMakeLists.txt), [Development.cmake](../cmake/Development.cmake), [Validation.cmake](../cmake/Validation.cmake) | Production/internal linkage, development/checker/fixture boundaries; no external neural dependency |
| [production_noise.cpp](../engine/src/production_noise.cpp), [RNG_NOTICES.md](../RNG_NOTICES.md), [flow_sampler.cpp](../engine/src/flow_sampler.cpp) | Altered RNG source and numerical sampler implementation |
| [export_model.py](../tools/assets/export_model.py), [asset_format.py](../tools/assets/asset_format.py), [model schema](../reference/pocket-model-schema.json) | Original checkpoint source, tensor selection, exact payload/container generation |
| [export_tokenizer.py](../tools/tokenizer/export_tokenizer.py), [tokenizer.cpp](../engine/src/tokenizer.cpp), [text_pipeline.cpp](../engine/src/text_pipeline.cpp) | Vocabulary source/conversion, Unicode-derived output and native implementation |
| [export_voice.py](../tools/assets/export_voice.py), [export_test_fixtures.py](../tools/assets/export_test_fixtures.py) | Development voice/default output and fixture risks |
| [WPF project](../ui/OutLoud.VoiceManager/OutLoud.VoiceManager.csproj), `ui/*/obj/project.assets.json`, app runtimeconfig, themes/MainWindow XAML | Framework-dependent x64/asInvoker, no external NuGet UI package, bridge/helper copies, font references |
| [prepare-deployment.ps1](../sapi/scripts/prepare-deployment.ps1), [deployment-common.ps1](../sapi/scripts/deployment-common.ps1), [inventory](../sapi/deployment-inventory.json), register/sign/verify scripts | Current explicit staging list, no voice copy, checker/runtime handling, narrow extra-PE check |
| `generated/final-validation/signed-stage/deployment.json` | Existing 20-record stage, no voices; not a future public-package receipt |
| `build-production/Release` and existing WPF output | Read-only dumpbin imports for engine/bridge/helper/three CLIs/apphost; no new build |
| [README](../README.md), [architecture](ARCHITECTURE.md), [manager guide](VOICE_MANAGER_GUIDE.md), [library](VOICE_LIBRARY.md), [machine library](MACHINE_VOICE_LIBRARY.md), [assets](NATIVE_ASSETS.md), [voice import](VOICE_IMPORT.md) | Current architecture and scope; reconciled against actual code |
| [final validation](FINAL_ENGINE_VALIDATION.md), [OLV integration](VOICE_MANAGER_OLV_IMPORT_EXPORT.md), [documentation finalization](DOCUMENTATION_FINALIZATION_A17.md) | Historical validation/implementation context, not new A04 executions |

Source files in the Hugging Face cache were read at the exact snapshot paths under
`C:\Users\rosko\.cache\huggingface\hub\models--kyutai--pocket-tts` and
`models--kyutai--pocket-tts-without-voice-cloning`. Their hashes match sections 7–8.
No production model/tokenizer was downloaded or replaced. Online legal/source text
was read in memory; no private credentials, certificate keys or gated asset access
were used. Only this report was written.

## 25. Recommended next actions

1. Resolve the targeted sampler provenance question with the relevant holders or
   a documented legal/technical determination. Preserve the explicit chain; do not
   change the owner's MIT choice or simply attach an NC license to the whole project.
2. Obtain Kyutai clarification for the exact checkpoint and tokenizer repositories:
   additional-policy status, any accepted access contract, asset scope, converted
   binary bundling and downstream commercial redistribution. Preserve the answer
   with pinned provenance.
3. Confirm original-code copyright holder/contributor authority and authorized
   Microsoft redistribution entitlement. Choose prerequisite handling and a signing
   design that fits the grants; avoiding bundled VC/SignTool binaries is an available
   packaging alternative, not an implementation performed here.
4. In a separately authorized licensing task, create the scoped MIT license and
   actual third-party texts/attribution map described in section 18; preserve existing
   RNG notices. Pin the selected apphost/runtime package notices.
5. Use an explicit production payload for A03, excluding all standalone voices,
   developer checkers and embedded fixture data. Recheck the final package inventory,
   notices and licenses once its actual contents exist.

### Explicit answers to the eight requested questions

**Question 1 — Can all ORIGINAL project-owned source be released under MIT?**
**Yes, for contributions whose rights the project actually owns**, consistent with
the owner's decision and verified permissive dependencies. Copyright-holder identity
and contributor authority must be confirmed. An unconditional statement that the
whole current implementation is exclusively original/unrestricted is not established:
RNG/Pocket provenance is known and the sampler's DiT chain needs resolution.

**Question 2 — Can the converted Pocket model be included in the free installer?**
Its exact source is established; **CC-BY-4.0 permits the intended converted-weight
distribution with attribution, license/change information and preserved notices**.
Overall clearance remains **UNRESOLVED** because the additional policy/access
agreement relationship has not been settled. It is not MIT and no Pocket-weight
commercial prohibition was proven.

**Question 3 — Can the production tokenizer be included?**
The exact distinct source repository/revision and converted bytes are established.
Its vocabulary is declared **CC-BY-4.0**, and generated tables have **separate
permissive Unicode data terms**. Supply separate asset attribution/change information
and the Unicode notice. Overall clearance is **UNRESOLVED** pending the policy's
scope/effect, not because tokenizer identity is missing or its software tools are Apache.

**Question 4 — Can native dependencies accompany the MIT application?**
Identified scalar RNG BSD/Zlib material and STL object portions permit it with
their conditions. Windows APIs are not redistributed. The Pocket/MAR sampler chain
is the focused compatibility uncertainty. Bundled VC binaries need a documented
Microsoft distributor grant, or remain a user-installed prerequisite; source MIT
does not convey those binary rights. No external neural runtime is shipped.

**Question 5 — What exact license/notice files must accompany release?**
The proposed concrete set is section 18: scoped project `LICENSE`, consolidated
`THIRD_PARTY_NOTICES.md`, actual Pocket MIT, existing `RNG_NOTICES.md`, relevant
Unicode data text, separate model/tokenizer attribution, and applicable apphost/
selected-runtime terms. MAR/GLIDE texts depend on retained protected material;
DiT needs a rights resolution, not merely a notice. An offline CC legal-code copy is
recommended. **Required contents are verified; those exact filenames are a proposed
layout, not license-mandated names.** Final Microsoft package notice filenames depend
on the as-yet-unselected payload.

**Question 6 — Which scripts might accidentally include development voices?**
**No current deployment script was found copying `.olv` files.** `export_voice.py`
creates them, fixture/export tools consume voice-derived data, old installations
contain them, and development checker binaries can embed fixtures. Current staging
includes a checker and only rejects extra PE files. A future recursive copy of those
directories would violate the owner's policy; use a complete production allowlist.

**Question 7 — What prevents public release now?**
P1 sampler provenance and model/tokenizer policy clarification; conditional P1
Microsoft runtime entitlement and SignTool purpose if bundled; P2 missing scoped
MIT/third-party/data/apphost notices and final compliant payload. **No P0 mandatory
component prohibition was established.** Excluded voices' unknown rights do not
block a genuinely zero-voice installer.

**Question 8 — What decisions/evidence are needed before A03?**
Settle those targeted rights questions; confirm copyright identity; decide exact
authorized VC/.NET prerequisite or bundle artifacts, framework-dependent versus
self-contained output and signing-tool inclusion; approve the notice layout and
production-only zero-voice payload. Keep MIT for original code. Do not begin from
the historical installed directory or treat its valid signatures as a license grant.

**Task boundary:** no implementation, existing document, configuration, asset,
voice/library entry, registry, deployment, certificate or trust setting was changed.
No tests, builds, benchmarks, production helpers, signing, game actions or package
installation ran. Nothing was staged, committed or pushed. The only new
tracked-content candidate is this report.
