# A04.2: MIT license and third-party notice finalization

Date: **2026-10-10**. This task finalizes the requested documentation, not public
binary distribution clearance. The existing A04/A04.1 reports remain unchanged.
Repository operations used the renamed **Out Loud TTS** directory; no build cache,
target, executable, namespace or registration was renamed/regenerated.

## 1. Scope and owner decisions

Owner-confirmed copyright: **Copyright (c) 2026 Nendis**. Original project-owned
code and contributions the project has the necessary rights to license use MIT.
Out Loud is free and open-source; downstream commercial use permitted by MIT
remains intact. No activation, subscription, paid edition or DRM is introduced.

No voices are distributed: no OLV, prepared voice safetensors, recordings, samples,
development/test voices or checker-embedded voice data in the intended installer.
Model/tokenizer and third-party material retain separate terms.

## 2. Files created and modified

| File | Action / purpose |
| --- | --- |
| [LICENSE](../LICENSE) | Created canonical MIT text with the supplied Nendis copyright |
| [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) | Created component, attribution, full-text and qualification map |
| [README.md](../README.md) | Updated only the licensing/status wording and added the License section |
| [Pocket-TTS-MIT.txt](../licenses/Pocket-TTS-MIT.txt) | Exact pinned Pocket license |
| [MAR-MIT.txt](../licenses/MAR-MIT.txt) | Exact pinned MAR license/holder, preserving applicable upstream attribution |
| [Pocket-Model-Attribution.md](../licenses/Pocket-Model-Attribution.md) | Separate model source, creator, conversion, hash and qualification record |
| [Pocket-Tokenizer-Attribution.md](../licenses/Pocket-Tokenizer-Attribution.md) | Separate vocabulary, conversion, hash and Unicode/policy record |
| [Unicode-Data.txt](../licenses/Unicode-Data.txt) | Verbatim historical Unicode data/software license section |
| [CC-BY-4.0.txt](../licenses/CC-BY-4.0.txt) | Full unmodified official legal code |
| [DotNet-AppHost-MIT.txt](../licenses/DotNet-AppHost-MIT.txt) | Exact pinned runtime source MIT notice relevant to apphost material |
| This report | Documentation verification and remaining release work |

Ten files were created; README is the only existing file modified. The pre-existing
untracked audit documents were preserved and not treated as this task's additions.

## 3. Authoritative license-text sources

License text was fetched from the already identified primary sources and compared
with local bytes. No model, runtime, dependency or other production asset was downloaded.
All text files are UTF-8; full upstream files retain their content and original end
of file, including absence of a final newline where applicable.

| Local text | Authoritative source / comparison | Local SHA-256 |
| --- | --- | --- |
| `LICENSE` | Canonical MIT grant/conditions/disclaimer as in the verified MAR MIT text; only copyright changed to the owner-supplied line | `255d2dc0e0085d09878084649ada4e319925b9d7b749152ddb1fab01b9c9595d` |
| `Pocket-TTS-MIT.txt` | [Exact Pocket revision LICENSE](https://raw.githubusercontent.com/kyutai-labs/pocket-tts/3dbee45d343d7dddd0d105468d17f8dcba14db3e/LICENSE), complete byte match | `23f18e03dc49df91622fe2a76176497404e46ced8a715d9d2b67a7446571cca3` |
| `MAR-MIT.txt` | [Exact MAR revision LICENSE](https://raw.githubusercontent.com/LTH14/mar/fe470ac24afbee924668d8c5c83e9fec60af3a73/LICENSE), complete byte match | `4b430d14668d5458233982e75cfc22fcd8329acf310600d7e29320084bfff662` |
| `CC-BY-4.0.txt` | [Creative Commons official plain-text legal code](https://creativecommons.org/licenses/by/4.0/legalcode.txt), full byte match; includes all sections and accompanying official text | `284a64ad3dd604d72f6f49c2481abf33a63c4213ea874a5c240dac8b13897386` |
| `Unicode-Data.txt` | [ICU release 72.1 LICENSE](https://raw.githubusercontent.com/unicode-org/icu/release-72-1/icu4c/LICENSE), exact initial Unicode data/software section before the separator introducing unrelated third-party software | `018f973915116d09fc697d05f433095d13442b632e6a22be4df3660b577ed3e6` |
| `DotNet-AppHost-MIT.txt` | [Runtime v10.0.12 LICENSE.TXT](https://raw.githubusercontent.com/dotnet/runtime/v10.0.12/LICENSE.TXT), complete byte match | `cfc21f5e8bd655ae997eec916138b707b1d290b83272c02a95c9f821b8c87310` |

No license wording was reconstructed from search summaries. Initial text-file
creation added final newlines to some sources; byte comparison caught this, and
those files were restored to exact fetched contents before final verification.

## 4. MIT copyright and scope

`LICENSE` contains the complete standard permission, conditions and warranty
disclaimer, unchanged apart from the copyright holder/year. Scope and third-party
exceptions are explained in README and the notices map rather than inserting new
conditions into the MIT text. No third-party owner was replaced with Nendis.
The owner declaration does not establish rights to otherwise unowned contributions.

## 5. Third-party notice structure

The root notices map covers Pocket, MAR, the three direct RNG lineages, model,
tokenizer, Unicode, apphost and Microsoft prerequisites. Each entry identifies
its source/version, relationship, retained terms/credits, complete license location
and relevant qualifications. It distinguishes incorporated source from external
runtime prerequisites and excluded development environments.

It does not copy every Python/PyTorch wheel license. No license text is used to
claim ownership over model weights, vocabulary, Unicode or vendor DLLs.

## 6. Pocket and MAR attribution

Pocket 3.3.0 is pinned to `3dbee45d343d7dddd0d105468d17f8dcba14db3e`. Its exact MIT
file has no holder/year line; none was added. Out Loud used it as a technical
reference, without asserting clean-room independence or Kyutai ownership of the
whole engine.

MAR's `fe470ac24afbee924668d8c5c83e9fec60af3a73` license retains **Copyright (c) 2024
Tianhong Li**. Its full MIT notice is included conservatively for the applicable
attributed/inherited reference material. This does not declare every native line
a MAR derivative or require a new provenance investigation.

[A04.1](FLOWSAMPLER_PROVENANCE_A04_1.md) remains the technical record: classification
**A**, no identifiable DiT-derived protected expression in the inspected native
FlowSampler, no concrete conflict established, no rewrite recommended. That finding
is bounded, not a legal ruling. No CC BY-NC designation/noncommercial condition
was added to original Out Loud code.

## 7. RNG notices

[RNG_NOTICES.md](../RNG_NOTICES.md) is unchanged and prominently referenced instead
of duplicated. It retains the verified PyTorch contributor list/BSD terms, MT19937
authors/acknowledgements, Garberoglio/Pommier math origin/Zlib-style terms, and altered
scalar implementation statement. Direct-source notices remain separate from the
licenses of the excluded full Torch wheel.

## 8. Model and tokenizer attribution

The model record identifies Kyutai and the A04-verified author credits, original
`kyutai/pocket-tts` revision `983151f13aaeab1b13c1e5e3c2c383d49a9edf3f`, source file,
checkpoint/OLP/payload hashes and subset/container changes. Retained BF16 weights
are not described as newly trained, independently owned or MIT.

The tokenizer record independently identifies `kyutai/pocket-tts-without-voice-cloning`
revision `00eac05ed3d16bdc3f6b5d598874019c34a89214`, source/output hashes, vocabulary
conversion and generated Unicode data. It does not substitute the model's revision
or a tokenizer software-tool license for the vocabulary's declared **CC-BY-4.0**.

Both documents link the complete official CC legal code and retain explicit
unresolved additional-policy qualifications. Conversion/attribution documentation
does not itself establish overall bundling permission. Asset identities were taken
from the verified A04 record; no fresh numerical/PCM/asset regeneration campaign ran.

## 9. Unicode notice

The copied historical notice retains **Copyright © 1991-2022 Unicode, Inc.** and its
full permission/disclaimer wording. Only the relevant Unicode section was extracted;
unrelated ICU library notices were not copied. The notices/tokenizer record explains
Unicode 15.0.0 generated property/case data, separately licensed Pocket vocabulary,
Python tooling and the absence of shipped ICU libraries. Unicode data was not relabeled MIT.

## 10. Microsoft/.NET handling

The .NET Foundation and Contributors MIT notice is preserved exactly from runtime
v10.0.12. Framework-dependent deployment still contains Microsoft apphost material.
This source notice is not the full license inventory of a future Desktop Runtime
or proprietary VC binary package.

Read-only inspection found an installed win-x64 host pack 10.0.12, but the existing
resolved assets expose no host-pack identity/download-dependency entry. The application
EXE's version resource identifies the application version/revision, not an exact
host-pack provenance receipt. **The exact embedded/final apphost pack is not asserted.**
A03 must pin it and determine applicable additional notices for the chosen payload.

No Microsoft DLL, Desktop Runtime, SDK binary or SignTool was copied. VC runtime
entitlement, package-specific notices and signing-tool permission remain unresolved
where bundling is selected. The SDK grant identified in A04 does not establish
classic-DLL SignTool redistribution permission. No certificate/trust/signing action occurred.

## 11. README updates

The concise License section links actual root/text/attribution files, identifies
Nendis/2026, preserves MIT commercial permission, states third-party exceptions,
CC asset qualifications and Unicode terms, and explains user-provided voice rights.
The former closing statement that no project license had been selected was corrected.
Other architecture/build/validation content was not rewritten. No obsolete absolute
repository path was introduced into new or edited licensing text.

## 12. Remaining licensing uncertainties

- Model prohibited-use policy/access agreement and downstream distribution implications.
- Tokenizer-specific policy scope/effect.
- VC runtime distributor entitlement if its binaries/package are bundled.
- SignTool permission or alternate signing design if classic-DLL signing tools ship.
- Final apphost/Runtime artifact identity, package terms and additional notices.
- Complete production-only installer allowlist, including embedded fixture exclusion.
- Rights to third-party/contributor material not actually owned or licensable by Nendis.

The supplied original-code holder/year is now settled. No other gap was silently
resolved. A04.1 narrowed the native DiT concern as recorded above; this task requires
no upstream correspondence and did not reopen that investigation.

## 13. A03 installer requirements

Carry the scoped MIT license, notices map, unchanged RNG notices and applicable
upstream texts with source/binary releases. When model/tokenizer bundling conditions
are established, carry their separate attribution, full CC text and Unicode notice.
Do not impose MIT-only licensing or new restrictions over those asset grants.

Select exact authorized VC/.NET prerequisites and apphost artifacts, retain their
actual terms/ThirdPartyNotices and preserve vendor signatures. Establish a permitted
signing approach. Use a complete production file allowlist: no voices, checker-embedded
conditioning, developer wheels, fixtures, caches or historical deployment contents.
These are requirements for later work; A03 was not started.

## 14. Documentation verification

Lightweight checks cover file presence, exact authoritative license-text comparisons,
copyright/scope, source URLs and relative links, asset identity transcription,
preservation of RNG/audits, explicit remaining uncertainty, changed-file scope and
Git whitespace. No new OLV or obsolete absolute project path was introduced.
The complete CC license is a verbatim legal-code file, not a shortened summary.

Existing tracked files and the two pre-existing audit reports were hashed before
editing. Final comparison permits only README to differ; the RNG notice, historical
reports and all application/build files remain unchanged. The root MIT grant is
also compared with the canonical source body after only the supplied copyright
substitution. `git diff` and `git diff --check` are reviewed as documentation checks,
not builds or executable correctness tests.

**Executed results:** all eleven intended files exist; all six license-text files
match the expected authoritative bytes (the project MIT copy changes only the
copyright identity). All **23 unique source URLs returned HTTP 200**, all local
relative links resolve, and new/edited documentation has no trailing whitespace,
new OLV file or obsolete absolute repository path. Of **272 existing files** in the
entry hash inventory, only README changed; the other **271**, including RNG notices
and both historical audits, match their entry hashes. Git diff contains only the
intended existing-file licensing edit; `git diff --check` passes. Newly created
documents/texts were also inspected directly because they remain untracked.

## 15. Final status

The requested MIT/notice/attribution documentation is implemented with the owner-
confirmed identity and accurate third-party distinctions. **Public binary and asset
redistribution is not finally cleared**; the specific A03 qualifications remain.

Only the authorized documentation/license paths changed. No executable code, binary,
asset, configuration, installed library, registry, certificate or game state changed.
No builds, tests, CTest, benchmarks, deployment/signing or asset regeneration ran.
Nothing was staged, committed or pushed.
