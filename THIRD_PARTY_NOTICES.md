# Out Loud third-party notices

Out Loud is free and open-source. [LICENSE](LICENSE) contains the MIT license for
original project-owned code and contributions the project has the necessary rights
to license. **Copyright (c) 2026 Nendis.** MIT permits downstream commercial use;
Out Loud's free distribution policy does not narrow that grant.

Third-party code, model weights, vocabulary, Unicode data and vendor binaries retain
their own licenses and ownership. The project MIT notice does not replace them.
This document accompanies the Out Loud 0.1.0 installer. Source provenance is
recorded in [A04](docs/LICENSING_AND_REDISTRIBUTION_A04.md) and its focused
[A04.1 follow-up](docs/FLOWSAMPLER_PROVENANCE_A04_1.md); the distribution scope
below describes the current package.

## Reference implementations and native adaptations

| Component / original author | Verified version or revision; authoritative source | Relationship, license and attribution | Complete terms |
| --- | --- | --- | --- |
| **Sonic**, Bill Cox | `b93885dcb70aae50c6f76b0fe4e0868f029a077e`; [upstream source](https://github.com/waywardgeek/sonic/tree/b93885dcb70aae50c6f76b0fe4e0868f029a077e) | Unmodified C sources vendored in `third_party/sonic`, statically linked into the SAPI engine for pitch-preserving speech speed adjustment. **Apache-2.0**; source attribution **Copyright 2010 Bill Cox**; upstream README also credits Copyright 2010, 2011 Bill Cox. Preserve supplied source notices and license. | [Sonic-Apache-2.0.txt](licenses/Sonic-Apache-2.0.txt), exact upstream license; original README and LICENSE retained with vendored sources |
| **Pocket TTS**, Kyutai | 3.3.0; `3dbee45d343d7dddd0d105468d17f8dcba14db3e`; [upstream license](https://github.com/kyutai-labs/pocket-tts/blob/3dbee45d343d7dddd0d105468d17f8dcba14db3e/LICENSE) | Technical reference for native inference/preparation; v4.0.1 (`a922b912dfa366975294afb568d815b305acde5d`) also reviewed for the October model configuration. Its license is unchanged. **MIT**; preserve its supplied permission/credits. The pinned license contains no holder/year line; none was invented. | [Pocket-TTS-MIT.txt](licenses/Pocket-TTS-MIT.txt), exact upstream text |
| **MAR**, Tianhong Li | `fe470ac24afbee924668d8c5c83e9fec60af3a73`; [license](https://github.com/LTH14/mar/blob/fe470ac24afbee924668d8c5c83e9fec60af3a73/LICENSE), [referenced sampler](https://github.com/LTH14/mar/blob/fe470ac24afbee924668d8c5c83e9fec60af3a73/models/diffloss.py) | Pocket's sampler explicitly credits this source. Retain the **MIT** attribution, **Copyright (c) 2024 Tianhong Li**, for applicable inherited material; the text is included conservatively for that reference chain. | [MAR-MIT.txt](licenses/MAR-MIT.txt), exact upstream text |
| **PyTorch RNG-derived implementation**, PyTorch/Caffe/Caffe2 contributors | Local reference `2.14.1+cpu`, source commit `5c4886908584029761b579af026dcfb627c84070`; [license](https://github.com/pytorch/pytorch/blob/5c4886908584029761b579af026dcfb627c84070/LICENSE) | `production_noise.cpp` is an altered scalar subset. Relevant root license **BSD-3-Clause**; preserve the complete contributor list, conditions and disclaimer. No LibTorch runtime/wheel is shipped. | [RNG_NOTICES.md — PyTorch](RNG_NOTICES.md#pytorch) |
| **MT19937**, Makoto Matsumoto and Takuji Nishimura | Improved 2002 initialization, through pinned [ATen header](https://github.com/pytorch/pytorch/blob/5c4886908584029761b579af026dcfb627c84070/aten/src/ATen/core/MT19937RNGEngine.h) | Altered native RNG adaptation; **BSD-3-Clause-style** terms. Copyright 1997–2002 authors; Cokus/Bellew/Wada acknowledgements retained. Source and binary notice obligations apply. | [RNG_NOTICES.md — MT19937](RNG_NOTICES.md#mt19937) |
| **avx_mathfun-derived routines**, Giovanni Garberoglio, based on Julien Pommier's sse_mathfun | Copyright 2012; pinned [ATen math header](https://github.com/pytorch/pytorch/blob/5c4886908584029761b579af026dcfb627c84070/aten/src/ATen/native/cpu/avx_mathfun.h) | Altered scalar mathematical routines in native RNG; **Zlib-style** terms. Preserve origin/notice and mark alterations. | [RNG_NOTICES.md — math adaptation](RNG_NOTICES.md#avx_mathfun-scalar-adaptation) |

The native implementation was developed using Pocket TTS as a technical reference;
no completely independent clean-room authorship claim is made. Neither Kyutai nor
MAR is described as owning the entire native engine. Preserve upstream rights while
licensing original project-owned contributions under MIT.

**MAR/DiT distinction:** the historical DiT source in the referenced chain declares
CC BY-NC 4.0. A04.1 found **no identifiable DiT-derived protected expression in the
inspected native FlowSampler** and established no concrete native-code licensing
conflict. That is a bounded technical finding, not a final legal ruling. Out Loud's
original code is not designated CC BY-NC, and no noncommercial restriction is added.
The historical comparison remains in A04.1; no sampler investigation or rewrite was
performed to create these notices.

## Separately licensed model, vocabulary and Unicode data

| Component / publisher | Verified origin | Terms, relationship and attribution | Complete terms / asset notice |
| --- | --- | --- | --- |
| **Pocket TTS model**, Kyutai | Active Out Loud 0.3.0: `kyutai/pocket-tts`, `languages/english/model.safetensors`, immutable revision `db04f0e653473e5c7333c69686ac92b7460b0c1d` (`english_2026-10`); [source card](https://huggingface.co/kyutai/pocket-tts/blob/db04f0e653473e5c7333c69686ac92b7460b0c1d/README.md). Historical conversion/public-payload provenance is retained in the model attribution. | **CC-BY-4.0**, as declared by this checkpoint card. The 169 synthesis tensors are copied as exact BF16 bytes into OLP; 45 preparation-only tensors are excluded. Retain Kyutai/creator credits, hashes and conversion notice. Weights are not MIT or newly trained by Out Loud. The prohibited-use paragraph matches the bundled policy. | [Model attribution](licenses/Pocket-Model-Attribution.md), [full CC legal code](licenses/CC-BY-4.0.txt), [0.3.0 verification](docs/releases/0.3.0.md) |
| **Pocket tokenizer vocabulary**, Kyutai | Separate `kyutai/pocket-tts-without-voice-cloning`, `00eac05ed3d16bdc3f6b5d598874019c34a89214`, `languages/english/tokenizer.json`; [pinned card](https://huggingface.co/kyutai/pocket-tts-without-voice-cloning/raw/00eac05ed3d16bdc3f6b5d598874019c34a89214/README.md) | **CC-BY-4.0** declared; learned vocabulary/scores/configuration converted to native binary. Separate publisher/source/change attribution; vocabulary is not MIT. | [Tokenizer attribution](licenses/Pocket-Tokenizer-Attribution.md), [full CC legal code](licenses/CC-BY-4.0.txt) |
| **Unicode-derived property/case tables**, Unicode, Inc. | Unicode 15.0.0 via Python 3.12; [historical notice](https://github.com/unicode-org/icu/blob/release-72-1/icu4c/LICENSE) preserved in ICU 72.1 | **Unicode-DFS-2016**, copyright 1991–2022 Unicode, Inc. Generated whitespace/uppercase/digit ranges and uppercase mappings are included in the binary tokenizer; preserve notice. No ICU library is shipped. | [Unicode-Data.txt](licenses/Unicode-Data.txt), exact relevant historical section |

**Historical A04.3 decision A: the then-current OLP and binary tokenizer may be bundled with
their required notices.** The [focused verification](docs/MODEL_DISTRIBUTION_A04_3.md)
established matching public payload provenance and reviewed the pinned public
terms. No concrete redistribution prohibition or separate agreement preventing
bundling was identified. Preserve the public card's prohibited-use policy
reference; this decision does not authorize harmful or nonconsensual voice use,
relabel assets MIT, or cancel actual separate agreements. It supersedes the earlier
pending asset conclusion in A04/A04.2 and the unchanged tokenizer attribution.
Out Loud 0.3.0 bundles the newly converted October OLP and unchanged binary tokenizer.
The active source card retains CC BY 4.0 and the same policy paragraph; see the
current model attribution and release notes above. No user-only weight download is required.

## Microsoft hosting material and runtime prerequisites

| Component / author | Verified source or version | Relationship / license qualification | Complete terms and release action |
| --- | --- | --- | --- |
| **.NET apphost**, .NET Foundation and Contributors | [Runtime source license v10.0.12](https://github.com/dotnet/runtime/blob/v10.0.12/LICENSE.TXT); production build pins `Microsoft.NETCore.App.Host.win-x64` **10.0.12** and records the selected apphost path/hash | Framework-dependent WPF `.exe` includes hosting material even without a bundled Desktop Runtime. **MIT**; copyright .NET Foundation and Contributors. | [DotNet-AppHost-MIT.txt](licenses/DotNet-AppHost-MIT.txt), exact pinned runtime MIT notice |
| **.NET 10 Desktop Runtime / WPF**, Microsoft, .NET Foundation and Contributors | [.NET source license](https://github.com/dotnet/runtime/blob/v10.0.12/LICENSE.TXT), [WPF source license](https://github.com/dotnet/wpf/blob/v10.0.0/LICENSE.TXT) | Runtime prerequisite for the framework-dependent frontend. Source licenses are MIT; a selected official binary/runtime package can have additional licenses, Microsoft package terms and ThirdPartyNotices. No runtime package is copied here. | Preserve exact selected package's terms/ThirdPartyNotices if redistributed. This apphost notice is not a complete future Desktop Runtime license inventory. |
| **Microsoft VC release x64 runtimes**, Microsoft | Toolchain-derived runtime floor; [VC guidance](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files?view=msvc-170) | Native engine imports `msvcp140`, `msvcp140_atomic_wait`, `vcruntime140`, `vcruntime140_1`. **Separate Microsoft binary terms**, not project MIT. No VC runtime DLL or redistributable installer is bundled. | Setup uses compatible system-installed prerequisites or offers a consented, hash- and signature-checked download directly from Microsoft. Microsoft package terms remain applicable. |
| **Inno Setup**, Jordan Russell and Martijn Laan | Inno Setup 7; exact compiler version recorded in the installer build record; [upstream license](https://github.com/jrsoftware/issrc/blob/main/license.txt) | Setup/uninstaller built with unmodified Inno components. Copyright (C) 1997-2026 Jordan Russell; portions Copyright (C) 2000-2026 Martijn Laan. Existing compiler-supplied copyright notices and website addresses remain intact. | Inno Setup license; no Inno compiler, SDK or signing utility is bundled as an application file |

Microsoft STL object portions arise from compiler headers identified as
**Apache-2.0 WITH LLVM-exception** (MSVC 14.44.35207); the
[upstream source terms](https://github.com/microsoft/STL/blob/main/LICENSE.txt)
provide the embedded-object exception described in A04. No STL source is vendored;
do not confuse its source license with proprietary VC runtime binary terms.
Windows SAPI/COM, UCRT and ordinary OS DLLs are supplied by Windows, not redistributed
in this installer. XAML refers to installed Windows fonts; no font file is bundled.

SignTool and Windows SDK tools are not bundled. The SDK redistribution grant identified
in A04 is MSIX-purpose-limited and does not establish permission to ship SignTool
for ordinary DLL signing. Installation-owned engine signing uses the native
Windows signing mechanism; it does not redistribute SignTool or publisher-sign setup.

## Excluded developer dependencies and voice content

The 50 pinned Python dependencies reviewed in A04, Python/PyTorch environments,
SDKs, reference fixtures and development checkers are preparation/build/test tooling,
not the public installer payload. Their complete wheel license collections
are not copied here. If a developer-tool bundle is later distributed, its actual
dependencies require a separate notice/package review.

Out Loud distributes **no `.olv` voices, prepared `.safetensors` voices, recordings
or sample/development/test voices**. Users supply/import/export/manage their own
compatible voices. MIT does not grant rights to those files, their source recordings
or speaker likenesses. File validation/conversion does not establish consent or ownership.
The inference model's `.safetensors` source checkpoint is separately identified model
data, not a permission to package prepared voice `.safetensors` files.

## Distribution scope for 0.1.0

The explicit production payload includes the MIT license, this notice, RNG
notices, Sonic's full Apache 2.0 license, Kyutai attribution/CC terms and verbatim
prohibited-use policy, Unicode notice, apphost MIT notice, and generated complete
installation terms. Setup requires interactive license acceptance and separate
acknowledgment of the Kyutai policy before prerequisite or application installation.

No voice data, research fixtures, Python environment, Microsoft runtime package,
runtime DLL, SDK or SignTool is bundled. Required shared Microsoft runtimes are
detected or downloaded directly from Microsoft only after consent, and retain
their own package licenses. The compiler-selected apphost identity and the exact
payload hashes are recorded in the build manifests.

The copyright holder and year for original project material are owner-confirmed.
Third-party license compliance does not grant consent to use another person's
voice or recordings, or override actual separate agreements.
