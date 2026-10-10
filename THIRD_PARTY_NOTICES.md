# Out Loud third-party notices

Out Loud is free and open-source. [LICENSE](LICENSE) contains the MIT license for
original project-owned code and contributions the project has the necessary rights
to license. **Copyright (c) 2026 Nendis.** MIT permits downstream commercial use;
Out Loud's free distribution policy does not narrow that grant.

Third-party code, model weights, vocabulary, Unicode data and vendor binaries retain
their own licenses and ownership. The project MIT notice does not replace them.
This document records the verified material identified by
[A04](docs/LICENSING_AND_REDISTRIBUTION_A04.md) and its focused
[A04.1 follow-up](docs/FLOWSAMPLER_PROVENANCE_A04_1.md). It is not a claim that a
complete public binary distribution has been cleared.

## Reference implementations and native adaptations

| Component / original author | Verified version or revision; authoritative source | Relationship, license and attribution | Complete terms |
| --- | --- | --- | --- |
| **Pocket TTS**, Kyutai | 3.3.0; `3dbee45d343d7dddd0d105468d17f8dcba14db3e`; [upstream license](https://github.com/kyutai-labs/pocket-tts/blob/3dbee45d343d7dddd0d105468d17f8dcba14db3e/LICENSE) | Technical reference for native inference/preparation. **MIT**; preserve its supplied permission/credits. The pinned license contains no holder/year line; none was invented. | [Pocket-TTS-MIT.txt](licenses/Pocket-TTS-MIT.txt), exact upstream text |
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
| **Pocket TTS model**, Kyutai | Historical conversion: `kyutai/pocket-tts`, `983151f13aaeab1b13c1e5e3c2c383d49a9edf3f`, `languages/english/model.safetensors`; [original metadata](https://huggingface.co/api/models/kyutai/pocket-tts/revision/983151f13aaeab1b13c1e5e3c2c383d49a9edf3f?blobs=true). Additional bit-exact public payload source: `kyutai/pocket-tts-without-voice-cloning`, `00eac05ed3d16bdc3f6b5d598874019c34a89214`, same file path; [public metadata](https://huggingface.co/api/models/kyutai/pocket-tts-without-voice-cloning/revision/00eac05ed3d16bdc3f6b5d598874019c34a89214?blobs=true). | **CC-BY-4.0**; 169/169 OLP tensors match the public source exactly. Existing BF16 subset/container and historical source binding are unchanged. Retain Kyutai/creator credits, hashes and change notice. Weights are not MIT or newly trained by Out Loud. | [Model attribution](licenses/Pocket-Model-Attribution.md), [full CC legal code](licenses/CC-BY-4.0.txt), [A04.3 verification](docs/MODEL_DISTRIBUTION_A04_3.md) |
| **Pocket tokenizer vocabulary**, Kyutai | Separate `kyutai/pocket-tts-without-voice-cloning`, `00eac05ed3d16bdc3f6b5d598874019c34a89214`, `languages/english/tokenizer.json`; [pinned card](https://huggingface.co/kyutai/pocket-tts-without-voice-cloning/raw/00eac05ed3d16bdc3f6b5d598874019c34a89214/README.md) | **CC-BY-4.0** declared; learned vocabulary/scores/configuration converted to native binary. Separate publisher/source/change attribution; vocabulary is not MIT. | [Tokenizer attribution](licenses/Pocket-Tokenizer-Attribution.md), [full CC legal code](licenses/CC-BY-4.0.txt) |
| **Unicode-derived property/case tables**, Unicode, Inc. | Unicode 15.0.0 via Python 3.12; [historical notice](https://github.com/unicode-org/icu/blob/release-72-1/icu4c/LICENSE) preserved in ICU 72.1 | **Unicode-DFS-2016**, copyright 1991–2022 Unicode, Inc. Generated whitespace/uppercase/digit ranges and uppercase mappings are included in the binary tokenizer; preserve notice. No ICU library is shipped. | [Unicode-Data.txt](licenses/Unicode-Data.txt), exact relevant historical section |

**A04.3 decision A: the existing OLP and binary tokenizer may be bundled with
their required notices.** The [focused verification](docs/MODEL_DISTRIBUTION_A04_3.md)
established matching public payload provenance and reviewed the pinned public
terms. No concrete redistribution prohibition or separate agreement preventing
bundling was identified. Preserve the public card's prohibited-use policy
reference; this decision does not authorize harmful or nonconsensual voice use,
relabel assets MIT, or cancel actual separate agreements. It supersedes the earlier
pending asset conclusion in A04/A04.2 and the unchanged tokenizer attribution.
No current model/tokenizer conversion or user-only weight download is required.

## Microsoft hosting material and runtime prerequisites

| Component / author | Verified source or version | Relationship / license qualification | Complete terms and release action |
| --- | --- | --- | --- |
| **.NET apphost**, .NET Foundation and Contributors | [Runtime source license v10.0.12](https://github.com/dotnet/runtime/blob/v10.0.12/LICENSE.TXT); current project targets .NET 10 Windows x64 | Framework-dependent WPF `.exe` includes hosting material even without a bundled Desktop Runtime. Source license **MIT**; copyright .NET Foundation and Contributors. Available SDK/runtime versions are not conclusive provenance for every existing/future apphost binary. | [DotNet-AppHost-MIT.txt](licenses/DotNet-AppHost-MIT.txt), exact pinned runtime MIT notice. A03 must pin the actual host pack and applicable additional notices. |
| **.NET 10 Desktop Runtime / WPF**, Microsoft, .NET Foundation and Contributors | [.NET source license](https://github.com/dotnet/runtime/blob/v10.0.12/LICENSE.TXT), [WPF source license](https://github.com/dotnet/wpf/blob/v10.0.0/LICENSE.TXT) | Runtime prerequisite for the framework-dependent frontend. Source licenses are MIT; a selected official binary/runtime package can have additional licenses, Microsoft package terms and ThirdPartyNotices. No runtime package is copied here. | Preserve exact selected package's terms/ThirdPartyNotices if redistributed. This apphost notice is not a complete future Desktop Runtime license inventory. |
| **Microsoft VC release x64 runtimes**, Microsoft | Observed redist tree 14.44.35112; [VS 2022 redist list](https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution) and [VC guidance](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files?view=msvc-170) | Native engine imports `msvcp140`, `msvcp140_atomic_wait`, `vcruntime140`, `vcruntime140_1`. **Separate Microsoft binary terms**, not project MIT. Owner redistribution entitlement is unresolved if bundling is chosen. | A03 must establish an authorized package/grant or use a user-installed official prerequisite; preserve required package terms and vendor signatures. |

Microsoft STL object portions arise from compiler headers identified as
**Apache-2.0 WITH LLVM-exception** (MSVC 14.44.35207); the
[upstream source terms](https://github.com/microsoft/STL/blob/main/LICENSE.txt)
provide the embedded-object exception described in A04. No STL source is vendored;
do not confuse its source license with proprietary VC runtime binary terms.
Windows SAPI/COM, UCRT and ordinary OS DLLs are supplied by Windows, not redistributed
in this documentation task. XAML refers to installed Windows fonts; no font file is bundled.

SignTool and Windows SDK tools are not bundled. The SDK redistribution grant identified
in A04 is MSIX-purpose-limited and does not establish permission to ship SignTool
for ordinary DLL signing. A03 must settle its signing-tool approach and runtime
handling; publishing MIT notices does not authorize copying proprietary tools/DLLs.

## Excluded developer dependencies and voice content

The 50 pinned Python dependencies reviewed in A04, Python/PyTorch environments,
SDKs, reference fixtures and development checkers are preparation/build/test tooling,
not the planned public installer payload. Their complete wheel license collections
are not copied here. If a developer-tool bundle is later distributed, its actual
dependencies require a separate notice/package review.

Out Loud distributes **no `.olv` voices, prepared `.safetensors` voices, recordings
or sample/development/test voices**. Users supply/import/export/manage their own
compatible voices. MIT does not grant rights to those files, their source recordings
or speaker likenesses. File validation/conversion does not establish consent or ownership.
The inference model's `.safetensors` source checkpoint is separately identified model
data, not a permission to package prepared voice `.safetensors` files.

## Remaining release qualifications

- Retain the model/tokenizer attribution, CC terms, Unicode notice and public
  use-policy source reference established in A04.3.
- VC runtime distributor entitlement if binaries are bundled.
- SignTool permission/design if shipped for classic DLL signing.
- Exact final apphost/Runtime package versions, terms and subcomponent notices.
- A production-only installer file allowlist excluding standalone and checker-embedded
  voice/test data. Current development staging is not the public package.
- Contributor/third-party rights for material not actually owned or licensable by Nendis.

The copyright holder and year for original project material are owner-confirmed.
Other rights questions are not silently resolved by that declaration. No installer
has been implemented or public binary release cleared by this notice work.
