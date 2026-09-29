# dxil.dll redistribution decision

Decision: PENDING

This file records the project maintainer's decision about redistributing
`dxil.dll` in public Windows packages. `scripts/package-beta-windows.py` and
`scripts/check_release_readiness.py` refuse a public package that contains
`dxil.dll` until the line above reads exactly:

```text
Decision: ACCEPTED (<maintainer>, <YYYY-MM-DD>)
```

Only a maintainer may change it, after reading the terms below. This file is a
record of a decision, not legal advice, and nothing in this repository asserts
that the decision is legally sufficient.

## What is being distributed

`dxil.dll` 101.7.2308.12 (Microsoft-signed), from the official
DirectXShaderCompiler `v1.7.2308` release archive, SHA-256
`9cccc7ef419da73fa314fdaecae831c6c20206ae70732c9093f95193378ced10`.
Provenance: [DXC-PROVENANCE.md](DXC-PROVENANCE.md).

`dxcompiler.dll` loads it to validate and sign the DXIL shaders RT64 compiles
at run time. Without it, DXC `v1.7.2308` emits unsigned DXIL, which D3D12
rejects, so the D3D12 renderer depends on it (not yet observed on real
hardware for this project: part of the Level 5 test).

## Which license applies

The release archive's `README.md` states: "LICENSE-MS.txt — dxil.dll (if
included in package)". That text is vendored byte-exact at
[`licenses/DirectXShaderCompiler/LICENSE-MS.txt`](../licenses/DirectXShaderCompiler/LICENSE-MS.txt)
and ships in the package as `licenses/DirectXShaderCompiler-dxil-LICENSE-MS.txt`.
It is the *Microsoft Software License Terms — Microsoft DirectX Shader
Compiler*, not an open-source license.

## Terms the maintainer must review (quoted from LICENSE-MS.txt)

> General. Subject to the terms of this agreement, you may install and use any
> number of copies of the software, and solely for use on Windows.

> Distributables. You may copy and distribute the object code form of the
> software listed in the distributables file list in the software

> Distribution Requirements. For any code you distribute, you must:
>
> add significant primary functionality to it in your applications;
>
> i. require distributors and external end users to agree to terms that
> protect it and Microsoft at least as much as this agreement; and
>
> ii. indemnify, defend, and hold harmless Microsoft from any claims, including
> attorneys' fees, related to the distribution or use of your applications,
> except to the extent that any claim is based solely on the unmodified
> distributable code.

> Distribution Restrictions. You may not: use Microsoft's trademarks or trade
> dress in your application in any way that suggests your application comes
> from or is endorsed by Microsoft; or modify or distribute the source code of
> any distributable code so that any part of it becomes subject to any license
> that requires that the distributable code, any other part of the software, or
> any of Microsoft's other intellectual property be disclosed or distributed in
> source code form, or that others have the right to modify it.

Read the full text before deciding; the excerpts are not a substitute.

## Open questions the decision must answer

1. **Distributables file list.** The terms allow distributing software "listed
   in the distributables file list in the software". The `v1.7.2308` archive
   contains no file with that name; the only statement of what is redistributable
   is the README ("This package contains a copy of the DirectX Shader Compiler
   redistributable").
2. **End-user terms (requirement i).** Shipping `LICENSE-MS.txt` in `licenses/`
   discloses the terms; whether that is enough for "require ... external end
   users to agree to terms", or whether an explicit acceptance step is needed,
   is the maintainer's call.
3. **Indemnification (requirement ii)** is an obligation of whoever distributes
   the package.
4. **Relationship with GPL-3.0.** The engine is GPL-3.0 (N64ModernRuntime).
   `dxil.dll` is a separate, unmodified, dynamically loaded Microsoft binary that
   our code never links (only `dxcompiler.dll` loads it). Whether shipping it in
   the same archive is compatible with the GPL-3.0 and with the restriction
   above is a legal question this repository does not answer.

## Alternatives if the decision is not ACCEPTED

- Ship no `dxil.dll` and document that D3D12 needs it (the renderer cannot sign
  shaders without it), e.g. by telling users to obtain the official release
  themselves.
- Ship only a Vulkan-capable configuration (the Windows engine currently
  creates a D3D12 window; this would need engineering work).

Engine-only draft archives built in CI leave `dxil.dll` out while the decision
is PENDING, because CI artifacts are downloadable.
