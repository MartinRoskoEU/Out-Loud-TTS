# WinUI production packaging

Use [the production installer guide](../PRODUCTION.md) for the complete supported
workflow. Run from the repository root:

```powershell
powershell.exe -NoProfile -File installer/inno/build-installer.ps1 `
  -Compiler 'C:\path\to\Inno Setup 7\ISCC.exe' `
  -VcRedistributable 'C:\path\to\vc_redist.x64.exe'
```

The compiler must be Inno Setup 7.0.2 or later. Supply the exact official VC x64
package pinned in `runtime-packages.ini`; its size, digest and Microsoft signature
are checked. The application version comes from `ProductVersion.props`.

The builder compiles Release components, publishes WinUI to a fresh stage,
verifies the reviewed payload and original dependency notices, and writes
`release/OutLoud-Setup.exe` plus its SHA-256 file. `-SkipComponentBuild` reuses
existing native Release output but still publishes WinUI. `-PrepareOnly` prepares
an unsigned maintenance package without compiling Setup or installing anything.

Setup uses the existing native maintenance lifecycle for ownership, signing,
registration and checked cleanup. Do not use `regsvr32`, manually copy installed
DLLs or replace receipts. See the production guide for same-version development
updates, confirmation-based uninstall and failure recovery.
