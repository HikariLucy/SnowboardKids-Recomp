# DirectX Shader Compiler binaries: provenance

Updated: 2026-09-29

RT64 links `dxcompiler.lib`, so `SnowboardKidsEngine.exe` imports
`dxcompiler.dll`. On D3D12, RT64 also *uses* it at run time: it compiles
generated shader text as `lib_6_3` and links it with library shaders embedded
at build time (`RT64::RasterShader`). This document records where the shipped
binary comes from, why no `dxil.dll` ships, and the evidence, all re-checkable.

## Summary

| File | Role | From | SHA-256 | PE FileVersion | Signature | License text |
| --- | --- | --- | --- | --- | --- | --- |
| `dxcompiler.dll` | shipped beside the engine | Microsoft DXC `v1.8.2505.1` release archive `dxc_2025_07_14.zip`, `bin/x64/` | `5888d3e590f5bfd8743484e7f31313996734adec5741506271a5c6368cbe8ba9` | `1.8.2505.32` | Microsoft Corporation (Microsoft Code Signing PCA 2011) | `LICENSE-LLVM.txt` (+ source `LICENSE.TXT`, `ThirdPartyNotices.txt`) |
| `dxc.exe` | build tool only, not shipped | same archive, `bin/x64/` | `ce11bb02f6027055e0b9f8c062d13b14f373c876a16557a4fa744314117d31e7` | `1.8.2505.32` | Microsoft Corporation | — |
| `dxil.dll` | **not extracted, not shipped, not needed** | — | — | — | — | — |

Archive: <https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2505.1/dxc_2025_07_14.zip>,
SHA-256 `9ad895a6b039e3a8f8c22a1009f866800b840a74b50db9218d13319e215ea8a4`,
release tag `v1.8.2505.1` = commit `b106a961d09221b3c5bdb37be45b679257da08b8`.

`python scripts/bootstrap.py --only dxc` (`scripts/dxc_redist.py`) downloads
the archive, checks its hash and the hash of every extracted file, and installs
only `dxcompiler.dll`, `dxc.exe`, `LICENSE-LLVM.txt` and `ReleaseNotes.md` in
`.deps-renderer/dxc-redist/v1.8.2505.1/`. Every Windows build then compiles
**all** RT64 and RecompFrontend shaders with that `dxc.exe`
(`patches/rt64-dxc-executable.patch` + `RT64_DXC_EXECUTABLE`), and the engine
gets that `dxcompiler.dll`. The packager, artifact audit and readiness gate
refuse other bytes and refuse any `dxil.dll`.

Signatures: the certificate chain in each PE was read with
`openssl pkcs7 -print_certs`; the Authenticode digest itself was not verified
(no `signtool` on the Linux host). The primary evidence is the SHA-256 match with
the asset of Microsoft's GitHub release.

## Why dxil.dll was needed before, and why it is not now

`dxil.dll` is DXC's validator. It carries separate Microsoft license terms
(`LICENSE-MS.txt`), unlike the LLVM-licensed compiler.

**DXC ≤ 1.7 (the previous pin, `v1.7.2308`).** `dxcompiler.dll` does not import
`dxil.dll`; it loads it with `LoadLibrary` from the DLL search path (application
directory, System32, `PATH`) to validate and sign ("hash") DXIL. Measured:

- Without it, output is **unsigned**: official `v1.7.2308` Linux compiler without
  `libdxil.so` → zero hash plus *"DXIL signing library (dxil.dll,libdxil.so) not
  found. Resulting DXIL will not be signed for use in release environments."*
  D3D12 rejects unsigned DXIL.
- With whatever `dxil.dll` it finds, results depend on that file. In CI, with the
  Windows SDK 10.0.26100 `dxil.dll` on `PATH`, RT64's run-time link **failed**
  every variant (`Container part 'Runtime Data (RDAT)' does not match expected
  for module`, `PSVRuntimeInfoSize` mismatch); with SDK 10.0.17763's it passed.
  RT64's build-time libraries came from RT64's contrib `dxc.exe`, a different
  compiler again.

**DXC ≥ 1.8.2502.** Official release notes, `v1.8.2502`: *"The validator binaries
will no longer be required for "signed" (hashed) shaders when using this release
of the compiler, however they will still be loaded and used if found."* (with
*"DXIL Validator Hash is open sourced"*, microsoft/DirectXShaderCompiler#6846).
`v1.8.2505`: *"The compiler will now always use the internal validator instead
of searching for an external DXIL.dll. The (hidden) `-select-validator` option
has been removed."*

Source of the pinned release (`b106a961`) confirms the mechanism:

- `tools/clang/tools/dxcompiler/dxcutil.cpp` `CreateValidator`: in the default
  (`Auto`) mode `CreateDxcValidator` — the **internal** validator — validates
  and hashes every container; `dxil.dll` would only be used for an explicit
  external selection, which RT64 never requests.
- `DXCompiler.cpp` `DllMain` → `DxilLibInitialize()` (`dxillib.cpp`) still calls
  `LoadLibrary("dxil.dll")` once, eagerly, if one is findable. So a `dxil.dll`
  a user has installed (e.g. a Windows SDK on `PATH`) may appear in the process;
  it is not what signs the shaders, and nothing requires it.

## Evidence (ROM-free, in CI on every push)

`tests/renderer/dxc_runtime_probe.cpp` (`SnowboardKidsDxcRuntimeProbe`, CTest
`renderer_dxc_runtime`) runs from a directory holding `dxcompiler.dll` and no
`dxil.dll`. It uses RT64's own `ShaderCompiler` and
`RasterShader::generateShaderText` to compile and link run-time shaders exactly
like `RasterShader` (4 variants: MSAA × smooth shading), plus trivial shaders
for D3D12 on the WARP adapter.

`renderer-compile.yml` → `windows-engine` (first green run: 36582382176):

| Check | Result |
| --- | --- |
| Every shader command of the build uses the pinned `dxc.exe` (`tests/release/dxc_release_compat.py --expect-pinned`) | 113/113 |
| `dumpbin /dependents`: engine, `dxcompiler.dll`, probe import `dxil.dll` | none |
| `dxil.dll` anywhere in `build-engine/`, the probe directory, `.deps-renderer/dxc-redist/` | none |
| **No `dxil.dll` findable** (every `PATH` entry holding one removed; none in System32; control `LoadLibraryW("dxil.dll")` → *not found on the search path*) | RT64 compile+link: 4/4 variants succeed; `dxil.dll` loaded: no; containers retail-hashed: 11/11 |
| D3D12 on WARP (SM 6.2), same run, debug layer off and on | compiled VS/PS and run-time-linked PS: `S_OK`; same PS with hash zeroed: `E_INVALIDARG`, *"Pixel Shader is unsigned"*; with a wrong hash: `E_INVALIDARG`, *"corrupt or in an unrecognized format"* |
| Windows SDK 10.0.17763 and 10.0.26100 `dxil.dll` first on `PATH` (a user's own install) | loaded eagerly by `dxcompiler.dll`; everything still passes; 22/22 retail-hashed |
| Build-time library shaders (`VERS` part) | `b106a961 / 1.8.2505.32`, the run-time compiler |

`tests/release/dxil_hash.py` ports DXC's `ComputeHashRetail`
(`lib/DxilHash/DxilHash.cpp`); `tests/release/test_dxil_hash.py` pins it to
vectors produced by DXC's own C++ code. Locally it reproduced the signatures
written by `libdxil.so` and classified `v1.7.2308` output without a validator
as unsigned and `v1.8.2502`/`v1.8.2505.1` output without one as signed.

WARP on the runners supports shader model 6.2, so the D3D12 checks use 6.2
shaders (libraries stay `lib_6_3`); RT64's own 6.3 shaders are checked by the
hash port. Rendering on real hardware is part of the Windows Level 5 test.

## Choice of release

Official stable releases considered: `v1.8.2502` (first validator-free signing),
`v1.8.2505.1`, `v1.9.2607` (newest stable).

- `v1.8.2505.1` is a patch release of the first line that *always* uses the
  internal validator, keeps the same imports (system DLLs only, static CRT) and
  exports (`DxcCreateInstance`, `DxcCreateInstance2`) as before, and compiled
  all 58 SPIR-V (Linux) and 113 Windows shader commands of this build.
- `v1.9.2607`'s `dxcompiler.dll` newly imports `MSVCP140.dll`/`VCRUNTIME140*.dll`,
  reports a development-style version (`1.9.0.5402`) and its archive uses
  backslash paths; not needed for anything RT64 uses.

## RT64's vendored copies (not used on Windows)

RT64 (pinned `cdlewis/rt64@6a4166b2`) vendors DXC as the submodule
`src/contrib/dxc` = `https://github.com/rt64/dxc-bin` at
`cc15e715ee378a4f675b335bd1071ff105873fc8` (no license file; description "For
storing a binary of the latest DXC released"). Its Windows files come from its
initial commit `2581d424` (2024-02-24):

| File | SHA-256 | Evidence |
| --- | --- | --- |
| `bin/x64/dxil.dll` | `2c70d034d38b06c6a1161efb9246cce296be248ff52eff6c1b587ce150e5f36d` | byte-identical to the official `v1.7.2212` `dxil.dll`, Microsoft-signed |
| `bin/x64/dxcompiler.dll` | `15304a82c8a61db615a83961d8967a5c468ae7ce77b86d8e9b3dec60f7ae2166` | **unsigned**; `1.7.0.4147 (0dc8d9060)`, a build of DXC commit `0dc8d9060df41901dbfa5ad7190da03e33d09518` (2023-10-09), not a release; matches no x64 `dxcompiler.dll` of `v1.7.2212`, `v1.7.2212.1`, `v1.7.2308` |
| `bin/x64/dxc.exe` | same development build | no longer used on Windows either (`rt64-dxc-executable.patch`) |

## Licenses and notices shipped

The `v1.8.2505.1` release notes' table: `LICENSE-MIT.txt` → `d3d12shader.h`
(not used), `LICENSE-LLVM.txt` → all other files. Vendored byte-exact in
`licenses/DirectXShaderCompiler/` (pinned in `scripts/dxc_redist.py`):

| Package path | Source | SHA-256 |
| --- | --- | --- |
| `licenses/DirectXShaderCompiler-LICENSE-LLVM.txt` | release archive `LICENSE-LLVM.txt` | `729615317e28dd03907e46f0fc3b5e88f7853cee61d1a1471d2749335516b46f` |
| `licenses/DirectXShaderCompiler-LICENSE.txt` | source `LICENSE.TXT` at `b106a961` | `27a49e35d1da96eba18fba54bc882667ff0ff8c0254f16f2b6e165d605ba7df8` |
| `licenses/DirectXShaderCompiler-ThirdPartyNotices.txt` | source `ThirdPartyNotices.txt` at `b106a961` | `19512a5d0a015ef16d167272c164da49a604614a786206212a80ad486ed0be6d` |

`LICENSE-MS.txt` (the validator's terms) is no longer vendored or shipped.
History of that question: [DXIL-REDISTRIBUTION.md](DXIL-REDISTRIBUTION.md).

## Re-checking

```bash
python3 scripts/dxc_redist.py --verify
python3 tests/release/test_windows_package.py
python3 tests/release/test_dxil_hash.py
gh release download v1.8.2505.1 -R microsoft/DirectXShaderCompiler -p dxc_2025_07_14.zip
sha256sum dxc_2025_07_14.zip
```

On Windows, after building (`docs/WINDOWS.md`):

```bat
python tests\release\dxc_release_compat.py --build build-engine --expect-pinned
build-engine\dxc-probe\SnowboardKidsDxcRuntimeProbe.exe probe-out --control-load
```
