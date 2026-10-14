# A03.5: licenses and separate Kyutai policy acknowledgment

**Current status:** [A03.12](BUILD.md) supersedes earlier foundation/compiler-gate
statements below. Shared native maintenance/uninstall and Inno lifecycle are
integrated for a controlled-test build. Standalone legacy signing/registration
commands remain blocked. Historical phase evidence below does not establish
live qualification; A03.13 functional testing is still required. Path budgeting
now uses the complete vault layout, rather than the former 100-character root cap.


The installer uses Inno Setup's standard [LicenseFile page](https://jrsoftware.org/ishelp/topic_setup_licensefile.htm)
with explicit accept/decline options. `consent.iss` initializes acceptance false
and decline true. No stored acceptance, command-line acknowledgment, new consent
receipt or custom wizard framework is used. MIT remains **Copyright (c) 2026
Nendis**; existing licenses, attributions, source hashes and historical audits
are unchanged.

## Documents and provenance

[consent.json](consent.json) orders every existing legal document plus the new
offline [Kyutai policy](../../licenses/Pocket-Prohibited-Use-Policy.txt).
The policy paragraph is verbatim from **Prohibited use** in Kyutai's
[pinned public model card](https://huggingface.co/kyutai/pocket-tts-without-voice-cloning/raw/00eac05ed3d16bdc3f6b5d598874019c34a89214/README.md),
revision `00eac05ed3d16bdc3f6b5d598874019c34a89214`, the same source pin verified
by [A04.3](../../docs/MODEL_DISTRIBUTION_A04_3.md). Its UTF-8 paragraph SHA-256,
excluding a final newline, is
`2af16e4028225963083cddc7d9d964e2cd49545653a6d19ef214703d5343651f`.
The copied notice records publisher, revision, URL and exact policy boundary.

The paragraph preserves unlawful/harmful/deceptive/fraudulent/unauthorized activity,
impersonation or cloning without explicit lawful consent, misinformation,
disinformation, fake news/calls, misleading recordings, prohibited content and
upstream liability disclaimer. It adds no new prohibition. The presentation
explicitly separates MIT software licensing, CC BY 4.0 model/tokenizer licensing
and the upstream use policy. The policy is **not** described as a restriction
inside CC BY 4.0. Existing asset licenses are not rewritten or narrowed; separate
applicable agreements remain independent. Acknowledgment grants no voice rights.

## Offline packaging

[consent-common.ps1](consent-common.ps1) renders a UTF-8 text presentation from
[consent-introduction.txt](consent-introduction.txt), the exact policy, LICENSE,
both Pocket asset attributions, full CC legal code, THIRD_PARTY_NOTICES,
RNG_NOTICES and all existing Pocket/MAR/apphost/Unicode notices. Complete source
text is appended, including credits, disclaimer and conversion/provenance details.
No legal source is edited by the renderer. Policy meaning/content is protected
by the pinned paragraph hash. This is a reviewed offline build input, with no
build-time network access or mutable policy lookup.

`build-stage.ps1` validates legal inputs **before building** and creates
`payload/licenses/OutLoud-Installation-Terms.txt`. [package.json](package.json)
and the explicit Inno file inventory add this presentation and the policy: **22
payload files**, with the original 20 retained. Stage verification compares the
agreement with fresh rendering from all current legal sources, not just the stage
manifest. Missing documents, changed policy, truncated presentation and extra
files fail packaging. Hash metadata is not signed release authentication.

`LicenseFile` uses the staged agreement. Both new files install under
`{app}\licenses`; the original LICENSE, THIRD_PARTY_NOTICES, RNG_NOTICES and
seven license/attribution documents keep their destinations. Full legal materials
therefore remain available offline after installation. Audit/exporter links inside
historical attribution remain source context; developer tools are not payload.

## Ordering and enforcement

Order: Welcome -> standard license/policy page -> separate Microsoft runtime
consent, when needed -> later configuration/Ready -> prerequisite preparation.
The prerequisite page is inserted after `wpLicense`. License acceptance does not
select the Microsoft checkbox or authorize runtime installation. Compatible
runtimes skip their page; license acceptance is required regardless.

`NextButtonClick` checks the live standard radio button for every post-Welcome
transition. `PrepareToInstall`, the runtime coordinator and each component's
acquisition entry recheck consent before downloads or vendor execution. Returning
to the license page and declining revokes permission without cached state.
Inno normally disables Next on decline; the explicit checks provide defense in
depth. Decline/cancellation makes no Out Loud installation authorized. Shared
runtimes installed after earlier valid consent can remain if setup later stops,
as documented in [PREREQUISITES.md](PREREQUISITES.md).

`WizardSilent` is rejected at InitializeSetup and later Next/preparation guards.
Both `/SILENT` and `/VERYSILENT` have no consent bypass. No unattended acceptance
flag exists. Inno [simulates Next in silent mode](https://jrsoftware.org/ishelp/topic_scriptevents.htm),
so the later guard is deliberate. Any future unattended consent mechanism needs
separate review; adding a command-line switch is outside A03.5.

The unconditional compiler `#error`, InitializeSetup foundation refusal and
`FoundationLifecycleReady=False` **remain**. No installable executable is
produced. Certificate generation/signing, registration and application file
installation cannot run in this foundation. Later implementations must retain
the preparation consent check before introducing their side effects.

## Controlled validation and limits

```powershell
.\installer\inno\build-stage.ps1 -StageDirectory "$PWD\generated\installer\stages\a035-candidate"
.\installer\inno\verify-stage.ps1 -StageDirectory "$PWD\generated\installer\stages\a035-candidate"
.\installer\inno\test-stage.ps1 -StageDirectory "$PWD\generated\installer\stages\a035-candidate"
.\installer\inno\test-consent.ps1 -StageDirectory "$PWD\generated\installer\stages\a035-candidate"
.\installer\inno\test-prerequisites.ps1 -StageDirectory "$PWD\generated\installer\stages\a035-candidate"
.\installer\inno\test-runtime-acquisition.ps1 -StageDirectory "$PWD\generated\installer\stages\a035-candidate"
```

Consent tests check source contracts and evaluate the actual small production
Boolean predicate through a bounded translation. They cover accepted/declined/
silent decisions, separate consent and ordering, full original texts, upstream
scope and missing/modified inputs. Optional `-SourceCard` compares an already
retrieved pinned card locally; tests never download. Missing staged documents
and altered agreement with refreshed hashes are covered by existing staging
tests. Scratch cleanup checks absolute containment; no production legal file,
registry, runtime or voice library is mutated by negative tests.

These checks are **not** an Inno compiler or wizard execution test. The required
compiler is unavailable locally, and foundation gates are never removed to test.
Standard page rendering, scrolling/readability, Back/decline/Next behavior and
silent exit need separately authorized disposable-VM validation later. Expected
manual outcomes: decline disables Next, acceptance permits later pages subject
to lifecycle gates, Microsoft consent stays unchecked, and either silent mode
exits before acquisition. Never use the active development installation for this.

Relevant staging/build and A03.3/A03.4 regressions accompany these checks.
No inference test suite, benchmarks, actual setup/vendor execution or signing is
part of A03.5. Existing legal documents were reviewed for consistency and retained;
the authoritative policy copy is the only new source legal document.

Changed files: README; installer/inno README, PREREQUISITES, OutLoud.iss,
runtime-install.iss, build-stage.ps1, production-common.ps1, package.json and
test-stage.ps1. Added consent.iss, consent-common.ps1, consent-introduction.txt,
consent.json, test-consent.ps1, this guide and licenses/Pocket-Prohibited-Use-Policy.txt.
Engine, Voice Manager, ABI, model/tokenizer bytes and user voices are unchanged.

Remaining work: live consent/prerequisite VM tests, Microsoft package distribution
entitlement, installation-owned certificates and reviewed signing/tooling,
live production registration validation, locked-resource upgrade/repair/recovery and confirmed
uninstall. Next implementation stage is certificate/signing and signature
verification, retaining foundation gates. Stop after A03.5.
