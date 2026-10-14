# Out Loud installer infrastructure

Out Loud **0.1.0** is prepared as a normal public release. The current workflow
builds Release x64, verifies the explicit payload (including Sonic's Apache 2.0
license), compiles Inno Setup and copies the validated executable to the ignored
`release/` directory. It never executes setup or publishes to GitHub. Publisher
signing is intentionally not part of this release. Final manual release approval
is still required; historical validation limits remain recorded in the linked guides.

The [Inno foundation](inno/README.md) stages the production package and owns the
installation UI, consent, prerequisite handling, shortcuts and Inno metadata.
Product identity remains OutLoud / 0.1.0 / Nendis; SAPI identity comes from
`sapi/src/identity.hpp`.

[A03.10 maintenance](inno/MAINTENANCE.md) implements the shared native controller
used by Inno and [development deployment](../docs/DEVELOPMENT_DEPLOYMENT.md).
It owns protected staging, installation-specific machine signing, file publication,
complete inventories, receipts and checked recovery. PowerShell builds candidates;
it does not implement a second transaction.

**Historical implementation record:** A03.12 integrated the initial runnable installer.
See [build and lifecycle integration](inno/BUILD.md). Shared maintenance/uninstall
is enabled; standalone signing/registration commands remain blocked. Focused
checks use private files and simulated certificate/registry operations. A03.13
disposable-machine functional testing remains pending.
[A03.11 complete uninstall](inno/UNINSTALL.md) now uses that same native authority,
with explicit permanent-data confirmation, bound managed-library/log cleanup,
owned token/COM/certificate removal and protected forward recovery. Native work
precedes Inno host removal; final receipts/helper cleanup follows it.
Unbound data, unknown files, offline profile state and locks cause safe refusal.
The setup executable is publisher-unsigned; installation-owned engine signing
serves a different purpose.
