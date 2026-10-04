# License and Distribution Audit

Updated: 2026-10-03
Release target: `v0.9.0-beta.1` and later compatible beta refreshes

This document records the engineering evidence used for release packaging. It
does not replace legal advice and does not invent rights for third-party code
whose upstream license is unstated.

## 1. Current component status

| Component | Evidence | Current release treatment |
| --- | --- | --- |
| Project-authored source | root `LICENSE` | GPL-3.0 |
| N64ModernRuntime | pinned `COPYING` | GPL-3.0; exact source/build directions published |
| N64Recomp | pinned `LICENSE` | MIT; notice bundled |
| RT64 | pinned `LICENSE` | MIT; notice bundled |
| RecompFrontend | no top-level license in pinned tree | **PENDING** upstream clarification; explicitly disclosed |
| SDL2 | system/runtime dependency; Windows `SDL2.dll` 2.26.3 from RT64's pinned `mupen64plus-win32-deps` | zlib terms; Windows packages ship `licenses/SDL2.txt` |
| DirectX Shader Compiler `dxcompiler.dll` (Windows) | official Microsoft `v1.8.2505.1` release, SHA-256 pinned, Microsoft-signed; section 7 | LLVM/NCSA; `LICENSE-LLVM`, source `LICENSE.TXT` and `ThirdPartyNotices.txt` bundled |
| DirectX Shader Compiler `dxil.dll` (Windows) | not needed by the pinned release; section 7 | not shipped; the audit rejects it |
| Microsoft Visual C++ runtime (Windows) | build machine `VCToolsRedistDir` | Visual Studio redistributable terms, app-local |
| RmlUi | submodule `LICENSE.txt` | MIT |
| {fmt} | submodule `LICENSE` | MIT |
| toml++ | submodule `LICENSE` | MIT |
| Rabbitizer | submodule `LICENSE` | MIT |
| nativefiledialog-extended | pinned license | zlib |
| Lato/Fredoka/Noto Emoji/PromptFont | OFL files | notices bundled |
| Project UI SVGs / app icon | `assets/sbk-ui/README.md` | project-owned/reviewed |
| Upstream theme drawings | excluded by staging allowlist | not shipped |
| Snowboard Kids ROM / extracted assets | artifact scanner | not shipped |
| `SnowboardKidsGame` beta module | reviewed generated module | disclosed separately; contains recompiled game logic, no ROM/assets |

## 2. RecompFrontend

The pinned RecompFrontend revision is
`e85b912d9df677b04f9358867dd010c8af27ea05`.

Review of the repository tree and history found no top-level `LICENSE`,
`COPYING` or equivalent project-wide grant. Licenses inside its submodules do
not establish a license for RecompFrontend's own `recompui` and
`recompinput` sources.

Clarification request:

https://github.com/N64Recomp/RecompFrontend/issues/44

The public beta does not label RecompFrontend as MIT/GPL or otherwise infer a
license. Instead, the unresolved state is disclosed in the release archive,
release notes and `docs/BETA-DISTRIBUTION-POLICY.md`.

Other public N64: Recompiled projects distribute binaries using
RecompFrontend; that is a practical precedent, not a substitute for an
explicit upstream license grant.

## 3. Project GPL-3.0 and source availability

Project-authored source now has a root GPL-3.0 license. N64ModernRuntime is
GPL-3.0 and is statically linked into `SnowboardKidsEngine`.

The release page must link to the exact source tag corresponding to the distributed binary. The
repository retains:

- project source;
- build scripts;
- exact dependency pins;
- the canonical N64ModernRuntime patch series;
- release/package scripts;
- third-party notices.

`SOURCE-COMPLIANCE.md` records the source/build mapping that accompanies the
binary package.

## 4. UI asset provenance

The release does not ship the unlicensed decorative drawings from the pinned
Snowboard Kids theme checkout.

The staging allowlist uses project-owned replacement UI SVGs plus reviewed
fonts. `stage_ui_assets.py` rejects extra files and symlinks, and the release
archive is checked byte-for-byte against the reviewed sources.

The application icon is project-specific and its SHA-256/provenance are
recorded in `assets/sbk-ui/README.md`.

## 5. Game-content boundary

The public beta package contains a dynamically loaded
`SnowboardKidsGame.so` / `SnowboardKidsGame.dll` compatibility module so
normal users do not need a compiler.

That module contains statically recompiled game logic and generated RSP code.
It does **not** contain the user's ROM or extracted textures/audio/levels. The
engine still requires and validates the user's supported original ROM before
gameplay.

The project does not claim ownership of original Snowboard Kids code/content.
This boundary and the decision to ship the module are documented in
`docs/BETA-DISTRIBUTION-POLICY.md`.

## 6. Artifact safeguards

The public package audit rejects:

- `.z64`, `.n64`, `.v64`, generic ROM files and ROM headers;
- Controller Pak files and savestates;
- JSON user configuration;
- logs and build-system outputs;
- symlinks and path traversal;
- personal absolute paths;
- a game module placed outside the canonical `modules/snowboardkids-us/` path.

Required public-beta files include:

- `LICENSE`;
- `THIRD_PARTY_NOTICES.md`;
- `SOURCE-COMPLIANCE.md`;
- `BETA-DISTRIBUTION-POLICY.md`;
- `BUILD-INFO.txt`;
- `RUNNING.md`;
- `ROM-IDENTITY.md`;
- `LAUNCHER-INTEGRATION.md`.

## 7. Windows runtime DLLs

Windows packages bundle only DLLs the engine and module actually import
(`scripts/pe_imports.py`), each listed with SHA-256, version, provenance and
license paths in the package's `RUNTIME-DLLS.txt`; Windows system DLLs are
never bundled (`scripts/windows_runtime.py`).

RT64's vendored `src/contrib/dxc` (`rt64/dxc-bin@cc15e715`, no license file)
holds an official `dxil.dll` (`v1.7.2212`) but an unsigned development
`dxcompiler.dll` (`1.7.0.4147`, DXC commit `0dc8d9060`) that matches no
Microsoft release; none of it is used on Windows. Builds and packages use the
official `v1.8.2505.1` release, pinned by SHA-256 in `scripts/dxc_redist.py`:
its `dxc.exe` compiles every shader at build time and its `dxcompiler.dll` is
the only DXC file shipped, with the license texts the release notes assign to
it vendored byte-exact in `licenses/DirectXShaderCompiler/`.

`dxil.dll`, DXC's separately licensed validator (Microsoft distributable-code
terms), is not shipped: from DXC 1.8.2502 the compiler validates and hashes
DXIL itself, and CI proves RT64's run-time path and D3D12 acceptance with no
`dxil.dll` findable. The audit and readiness gate reject it. No decision about
its terms was taken or is required (`docs/DXIL-REDISTRIBUTION.md`, historical).
Evidence and re-check commands: `docs/DXC-PROVENANCE.md`.

## 8. Launcher redistribution boundary

`LAUNCHER-INTEGRATION.md` separates launcher-owned package bytes from the user's external ROM and mutable user-data root. It also records the non-owning registration path available to launchers that choose not to redistribute the package while RecompFrontend licensing remains unresolved. This documentation is an integration contract, not a new license grant.

`ROM-IDENTITY.md` records the canonical normalized SHA-256 and SHA-1 of the supported USA source without shipping any game data.

## 9. Release decision

For the public beta, the project owner has chosen to proceed with a public
binary beta while the RecompFrontend license clarification remains pending,
provided that the pending status stays explicit and the artifact passes the
release audit.

This changes the previous engineering gate from "wait indefinitely for
upstream" to "disclose the unresolved dependency and continue the beta
process." It does not convert the missing upstream license into a cleared
license.

The release remains a prerelease/beta and can be revised or withdrawn if new
upstream licensing information requires it.
