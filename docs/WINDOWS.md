# Windows x86_64 — status and engineering notes

Updated: 2026-09-29

**Windows is in development. It is not a supported platform yet.** The public
`v0.9.0-beta` release is Linux x86_64 only. Windows becomes "supported" only
after a live gameplay validation of a packaged build (see the checklist at the
end of this document).

## Status by level

| Level | Meaning | Status | Evidence |
| --- | --- | --- | --- |
| 1 | Windows renderer/runtime compiles | reached | `renderer-compile.yml` → `windows` job (RT64, RecompFrontend, N64ModernRuntime with clang-cl) |
| 2 | `SnowboardKidsEngine.exe` builds and `--version` works | reached | `renderer-compile.yml` → `windows-engine` job (windows-2022, clang-cl 19): engine-only build, `--version` prints `Snowboard Kids Recompiled 0.9.0` + the HEAD commit, `dumpbin` shows the 62 exported runtime symbols; all 11 engine-only CTests pass |
| 3 | `SnowboardKidsGame.dll` builds and passes ABI validation | mechanism proven, real DLL pending | `module_engine_probe` on Windows: a synthetic DLL built with the real builder code and module harness imports from `SnowboardKidsEngine.exe`, and the real engine loads, validates and initializes it (export binding checked); a DLL needing a symbol the engine lacks is refused (`LoadLibrary` error 127). A real ROM-derived DLL has not been built on Windows yet |
| 4 | complete, audited Windows ZIP | draft only | `package-beta-windows.py --engine-only-draft` in CI; a public package is blocked on the DirectX Shader Compiler license review and on a real game module |
| 5 | ZIP tested on real Windows | not started | requires a person with a Windows PC and their own ROM |

## Architecture

The split runtime is the same on every platform:

```text
SnowboardKidsEngine(.exe)        ROM-free: RT64, RecompFrontend, N64ModernRuntime,
                                 frontend, savestates, Controller Pak
  └─ modules/snowboardkids-us/
       SnowboardKidsGame(.dll)   generated from the reviewed corpus + RSP microcode
```

`SBK_ENGINE_ONLY=ON` builds only the engine. `SnowboardKidsCpu`,
`SnowboardKidsRsp` and `SnowboardKidsRecompiled` are not created and no
ROM-derived source is needed, so hosted CI can build the release engine.

### Module binding: ELF vs PE/COFF

On Linux the engine is linked with `-rdynamic` and the module's undefined
symbols (the `os*_recomp` runtime functions, `recomp::overlays::register_overlays`,
`dmem`) resolve against the executable at `dlopen` time, with ELF interposition.

A Windows DLL cannot bind to an executable implicitly. The engine therefore
exports an explicit, reviewed surface:

- `src/module/engine_exports.inc` lists every runtime function the module
  imports. CMake turns it into the engine's `.def` export table; the module
  builder turns it into `SnowboardKidsEngine.lib` (`NAME SnowboardKidsEngine.exe`)
  with `lib.exe`/`llvm-lib`, which the DLL links against.
- `sbk_engine_register_overlays` is a C-linkage wrapper for the one C++ entry
  point (C++ names are not exported by `.def`).
- RSP DMEM is not imported as data. The RSP microcode unit is compiled with
  `dmem=(*sbk_module_dmem)` and reads the engine buffer passed in
  `SbkEngineApiV1::dmem`.
- The generated corpus declares runtime functions without `dllimport`, so their
  address inside the DLL is a local import thunk. Continuation dispatch compares
  those addresses with the engine's HLE table, so `init()` verifies every export
  with `GetProcAddress` and rebinds the overlay function table to the engine
  addresses collected through `dllimport` in `engine_imports_win32.cpp`.
- Continuation descriptors reach the engine through the module API table
  (`continuations`), the path the ABI always defined; on Linux they currently
  arrive through interposition instead.

The ABI (`SBK_MODULE_ABI_VERSION 1`) is unchanged. `--validate-module` now
also runs the module's `init()`, which is where the Windows binding is checked.

## Building the engine (ROM-free)

From a *x64 Native Tools / Developer* prompt with LLVM (clang-cl), CMake, Ninja
and Python 3:

```powershell
git config --global core.autocrlf false   # the patch checker compares bytes
python scripts/bootstrap.py --only runtime --only rt64 --only frontend --only theme
cmake -S . -B build-engine -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl `
  -DSBK_BUILD_RENDERER_STACK=ON -DSBK_BUILD_NATIVE_BOOT=ON `
  -DSBK_ENGINE_ONLY=ON -DSBK_CONTINUATIONS=ON
cmake --build build-engine --target SnowboardKidsEngine
build-engine\SnowboardKidsEngine.exe --version
ctest --test-dir build-engine --output-on-failure
```

The build copies `SDL2.dll`, `dxcompiler.dll` and `dxil.dll` beside the exe.

### Runtime dependencies observed in CI (`dumpbin /dependents`)

| DLL | Needed by | Handling |
| --- | --- | --- |
| `SDL2.dll` (2.26.3) | engine | bundled, `licenses/SDL2.txt` |
| `dxcompiler.dll` | engine (RT64 links `dxcompiler.lib`) | bundled; license text pending review |
| `dxil.dll` | loaded by `dxcompiler.dll` | bundled companion; license text pending review |
| `VCRUNTIME140.dll`, `VCRUNTIME140_1.dll`, `MSVCP140.dll`, `MSVCP140_ATOMIC_WAIT.dll` | engine, module, DXC | bundled app-local from `VCToolsRedistDir` |
| `KERNEL32`, `USER32`, `GDI32`, `SHELL32`, `ole32`, `OLEAUT32`, `ADVAPI32`, `IMM32`, `SETUPAPI`, `VERSION`, `WINMM`, `msvcrt`, `d3d12`, `dxgi`, `D3DCOMPILER_47`, `api-ms-win-*` | engine, SDL2, DXC | Windows system DLLs, never bundled |

The engine does not import the Vulkan loader (`vulkan-1.dll`), and the Windows
window is created without `SDL_WINDOW_VULKAN`, so D3D12-only systems can start it.

## Building SnowboardKidsGame.dll (needs your ROM and the local corpus)

The module is compiled from the reviewed continuation corpus
(`build-tools/production-continuation/corpus`, generated locally from your ROM;
never committed or uploaded) plus RSP microcode extracted from your ROM:

```powershell
python scripts\build-game-module.py C:\path\to\snowboardkids.z64
```

Requirements: clang-cl (preferred) or MSVC, and `lib.exe`/`llvm-lib` — i.e. a
Developer prompt. MinGW g++ is rejected: the DLL must share the engine's MSVC
ABI and CRT. The result is installed to
`%APPDATA%\SnowboardKids\modules\snowboardkids-us\SnowboardKidsGame.dll` and
validated by `SnowboardKidsEngine.exe --validate-module` when the engine is
found in `build-engine\` (or `SBK_ENGINE` points to it).

## Packaging

```powershell
python scripts\package-beta-windows.py --version 0.9.x-beta
```

The script refuses a dirty tree, checks `--version` against HEAD, validates the
module with the engine, derives runtime DLLs from the real PE import tables
(`scripts/pe_imports.py`) and the reviewed policy (`scripts/windows_runtime.py`),
then runs the shared `package_release.py`, artifact audit and public-beta
readiness gates. Output:

```text
dist/SnowboardKidsRecompiled-<version>-Windows-x86_64.zip
dist/SHA256SUMS.txt
```

Layout:

```text
SnowboardKidsRecompiled/
├── SnowboardKidsEngine.exe
├── SDL2.dll, dxcompiler.dll, dxil.dll   (only DLLs the PE imports require)
├── modules/snowboardkids-us/SnowboardKidsGame.dll
├── assets/
├── licenses/
├── scripts/                              (optional local module builder)
├── LICENSE, SOURCE-COMPLIANCE.md, THIRD_PARTY_NOTICES.md
├── BETA-DISTRIBUTION-POLICY.md, RUNNING.md, BUILD-INFO.txt
```

System DLLs are never bundled; an import that is neither a system DLL nor a
reviewed redistributable fails staging. The audit rejects ROMs, saves,
Controller Pak images, savestates, logs, `.pdb/.lib/.exp/.ilk`, DLLs outside
the reviewed list, and personal paths such as `C:\Users\<name>`.

## Open items before a public Windows package

1. **DirectX Shader Compiler license texts.** `rt64` links `dxcompiler.lib`, so
   `dxcompiler.dll` (and its companion `dxil.dll`) must ship, but the pinned
   RT64 tree contains no license text for these binaries. Public packaging is
   refused until reviewed texts are added to the policy.
2. **A real `SnowboardKidsGame.dll`** built on Windows from the local corpus
   and validated by the engine.
3. **Live validation** (below).
4. The engine is a console-subsystem executable, so a console window opens
   next to the game. Acceptable for a beta (logs stay visible); revisit before
   a stable release.

## Live validation checklist (LEVEL 5)

Use a clean extraction, not the repository:

1. Extract the ZIP to a new folder (e.g. `C:\Games\SBK-test`).
2. Run `SnowboardKidsEngine.exe`; the window opens without missing-DLL errors.
3. Select your own Snowboard Kids (USA) ROM; it validates.
4. The bundled module loads (console: `[MODULE] Loaded game module from: ...`).
5. The title screen and a race start; video renders (D3D12 by default).
6. Audio plays; input works on keyboard and a controller.
7. F5 saves a state, F8 restores it.
8. Close and reopen; settings and saves persist in `%APPDATA%\SnowboardKids`.
