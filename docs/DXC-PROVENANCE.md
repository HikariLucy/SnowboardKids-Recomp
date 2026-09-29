# DirectX Shader Compiler binaries: provenance

Updated: 2026-09-29

RT64 links `dxcompiler.lib`, so `SnowboardKidsEngine.exe` imports
`dxcompiler.dll`; `dxcompiler.dll` in turn loads `dxil.dll` to validate and
sign DXIL. Both must ship with a Windows package. This document records where
the shipped binaries come from and why, using evidence anyone can re-check.

## Summary

| DLL | Shipped from | SHA-256 | PE FileVersion | Signature | License text (per release README) |
| --- | --- | --- | --- | --- | --- |
| `dxcompiler.dll` | Microsoft DXC `v1.7.2308` release archive `dxc_2023_08_14.zip`, `bin/x64/` | `570a1a7357893615417edf5ab356625b5b6b721a131bc7331bf17289d4928ed7` | `1.7.2308.7` | Microsoft Corporation (Microsoft Code Signing PCA 2011) | `LICENSE-LLVM.txt` |
| `dxil.dll` | same archive, `bin/x64/` | `9cccc7ef419da73fa314fdaecae831c6c20206ae70732c9093f95193378ced10` | `101.7.2308.12` | Microsoft Corporation (Microsoft Code Signing PCA 2011) | `LICENSE-MS.txt` |

Archive: <https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.7.2308/dxc_2023_08_14.zip>,
SHA-256 `01d4c4dfa37dee21afe70cac510d63001b6b611a128e3760f168765eead1e625`,
release tag `v1.7.2308` = commit `69e54e29086b7035acffb304cec57a350225f8b0`.

`python scripts/bootstrap.py --only dxc` (or `scripts/dxc_redist.py`) downloads
that archive, checks the archive hash and the hash of every extracted file, and
installs them in `.deps-renderer/dxc-redist/v1.7.2308/`. CMake copies the two
DLLs beside the engine from there; `package-beta-windows.py`, the artifact
audit and the readiness gate refuse any other bytes.

Signatures: the certificate chain embedded in each PE (security directory) was
read with `openssl pkcs7 -print_certs`; the Authenticode digest itself was not
verified (no Windows `signtool` on the Linux host). The primary evidence is the
SHA-256 match with the release asset downloaded from Microsoft's GitHub release.

## Why not RT64's copies

RT64 (pinned `cdlewis/rt64@6a4166b2`) vendors DXC as the submodule
`src/contrib/dxc` = `https://github.com/rt64/dxc-bin` at
`cc15e715ee378a4f675b335bd1071ff105873fc8`. That repository has no license
file; its description is "For storing a binary of the latest DXC released".
Its Windows files were added in its initial commit `2581d424` (2024-02-24);
the later "v1.8.2403.2" commits only touched Linux/macOS files.

| File in `rt64/dxc-bin@cc15e715` | SHA-256 | Evidence | Result |
| --- | --- | --- | --- |
| `bin/x64/dxil.dll` | `2c70d034d38b06c6a1161efb9246cce296be248ff52eff6c1b587ce150e5f36d` | byte-identical to `bin/x64/dxil.dll` of the official `v1.7.2212` archive `dxc_2022_12_16.zip`; FileVersion `101.7.2212.14`; Microsoft Authenticode signature | official Microsoft binary |
| `bin/x64/dxcompiler.dll` | `15304a82c8a61db615a83961d8967a5c468ae7ce77b86d8e9b3dec60f7ae2166` | **no Authenticode signature**; FileVersion `1.7.0.4147`, ProductVersion `1.7.0.4147 (0dc8d9060)`; PE timestamp 2023-10-09 21:07 UTC. `0dc8d9060` is DXC commit `0dc8d9060df41901dbfa5ad7190da03e33d09518` ("Pull in llvm-project's clang-format check action (#5834)", 2023-10-09 19:55 UTC), not a release. Matches no x64 `dxcompiler.dll` of `v1.7.2212`, `v1.7.2212.1` or `v1.7.2308` | development build of unknown origin |

Every official release checked (`v1.7.2212`, `v1.7.2212.1`, `v1.7.2308`) ships
Microsoft-signed `dxcompiler.dll`/`dxil.dll` with release version numbers
(`1.7.YYMM.n`). RT64's `dxcompiler.dll` is unsigned and uses development
versioning, so who built it, from which exact tree and with what
configuration cannot be shown. Treating it as "LLVM-licensed because DXC is"
would be an assumption, so it is not redistributed.

RT64's contrib `dxc.exe` (same development build) is still what RT64's build
uses to compile shaders into the engine. That is a build tool: its outputs are
shader bytecode embedded in the engine, and the tool itself is not shipped.

## Choice of release and compatibility

`v1.7.2308` is the newest official release older than commit `0dc8d9060`, so
it is the closest official build to what RT64's runtime compiler was built
from; like that build (and unlike `v1.7.2212`) it defaults to HLSL 2021. It
exports the same entry points (`DxcCreateInstance`, `DxcCreateInstance2`), and
its `dxcompiler.lib` names the same imports as RT64's, so it is a drop-in
replacement at link and load time.

Evidence that it compiles what the renderer compiles:

- `tests/release/dxc_release_compat.py` replays every RT64/RecompFrontend
  shader command of a configured build with the pinned compiler. With the
  official `v1.7.2308` Linux compiler (`linux_dxc_2023_08_14.x86_64.tar.gz`) on
  the Linux renderer build: 58/58 SPIR-V commands compile.
- CI (`renderer-compile.yml` → `windows-engine`) runs the same check with the
  pinned `dxc.exe` on the Windows build, i.e. the DXIL commands, validated and
  signed by the pinned `dxil.dll`.
- Live rendering with these DLLs is part of the Windows Level 5 test
  ([WINDOWS.md](WINDOWS.md#checklist)); it has not been done yet.

## Licenses and notices shipped

The release archive's `README.md` says:

| License file | Applies to |
| --- | --- |
| `LICENSE-MS.txt` | `dxil.dll` (if included in package) |
| `LICENSE-MIT.txt` | `d3d12shader.h` (not shipped) |
| `LICENSE-LLVM.txt` | all other files |

Vendored byte-exact in `licenses/DirectXShaderCompiler/` (pinned by SHA-256 in
`scripts/dxc_redist.py`, checked by tests and the packager):

| Package path | Source | SHA-256 |
| --- | --- | --- |
| `licenses/DirectXShaderCompiler-LICENSE-LLVM.txt` | release archive `LICENSE-LLVM.txt` | `729615317e28dd03907e46f0fc3b5e88f7853cee61d1a1471d2749335516b46f` |
| `licenses/DirectXShaderCompiler-LICENSE.txt` | source tree `LICENSE.TXT` at `69e54e29` (same bytes at `v1.7.2212`) | `9c9393bb14872aca75124c65558174da9cf2aa13f69be4aaffbfb96b29de1910` |
| `licenses/DirectXShaderCompiler-ThirdPartyNotices.txt` | source tree `ThirdPartyNotices.txt` at `69e54e29` | `19512a5d0a015ef16d167272c164da49a604614a786206212a80ad486ed0be6d` |
| `licenses/DirectXShaderCompiler-dxil-LICENSE-MS.txt` | release archive `LICENSE-MS.txt` | `734f72f239fe7b07b4c7203f294c1a7ce27095687278bab7e56d630d7c672963` |

`LICENSE.TXT` is `LICENSE-LLVM.txt` plus the list of third-party components in
the LLVM tree; `ThirdPartyNotices.txt` carries the notices for components DXC
incorporates. Both describe the source revision the release was built from,
so they ship with `dxcompiler.dll`.

## What is and is not settled

- **`dxcompiler.dll`**: provenance proven (official signed release, hash-pinned),
  license texts vendored and shipped. Cleared for packaging.
- **`dxil.dll`**: provenance proven. Its license is Microsoft's proprietary
  distributable-code terms, whose distribution requirements (end-user terms,
  indemnification, source-license restriction) need a maintainer decision:
  [DXIL-REDISTRIBUTION.md](DXIL-REDISTRIBUTION.md). Until it reads
  `Decision: ACCEPTED (...)`, public Windows packages are refused and CI drafts
  leave `dxil.dll` out.

## Re-checking

```bash
python3 scripts/dxc_redist.py --verify           # installed release vs pins
python3 -m unittest tests/release/test_windows_package.py
gh release download v1.7.2308 -R microsoft/DirectXShaderCompiler -p dxc_2023_08_14.zip
sha256sum dxc_2023_08_14.zip
```
