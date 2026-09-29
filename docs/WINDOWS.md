# Windows x86_64 — status and engineering notes

Updated: 2026-09-29

**Windows has not been publicly distributed yet.** The public `v0.9.0-beta`
release is Linux x86_64 only. A Windows release candidate has been built,
audited and validated on a physical PC, from source and from a clean-folder
ZIP (see [Verified on physical Windows](#verified-on-physical-windows)). Windows
is pending only the production and publication of the definitive beta
artifact ([open items](#open-items-before-a-public-windows-package)).

The ROM, the private module inputs bundle, saves, savestates, Controller Pak
images and any other ROM-derived data must never be distributed or attached
anywhere by accident.

## Status by level

| Level | Meaning | Status | Evidence |
| --- | --- | --- | --- |
| 1 | Windows renderer/runtime compiles | reached | `renderer-compile.yml` → `windows` job (RT64, RecompFrontend, N64ModernRuntime with clang-cl) |
| 2 | `SnowboardKidsEngine.exe` builds and `--version` works | reached | `renderer-compile.yml` → `windows-engine` job (windows-2022, clang-cl 19): engine-only build, `--version` prints `Snowboard Kids Recompiled 0.9.0` + the HEAD commit, `dumpbin` shows the 62 exported runtime symbols; all 12 engine-only CTests pass (also 12/12 on a physical Windows PC, native clang-cl build) |
| 3 | `SnowboardKidsGame.dll` builds and passes ABI validation | reached | `module_engine_probe` on Windows: a synthetic DLL built with the real builder code and module harness imports from `SnowboardKidsEngine.exe`, and the real engine loads, validates and initializes it (export binding checked); a DLL needing a symbol the engine lacks is refused (`LoadLibrary` error 127). The real-module path (module inputs bundle → `build-game-module.py --inputs`) is in place and produces an engine-validated module on Linux; a real `SnowboardKidsGame.dll` was built on physical Windows from a legitimate ROM + private bundle and passes `--validate-module` (`MODULE_VALID: corpus=0x76260CB8F0E080D7 funcs=1981 hle=56`); see [Verified on physical Windows](#verified-on-physical-windows) |
| 4 | complete, audited Windows ZIP | reached | public mode of `package-beta-windows.py` requires the engine built from HEAD, a real engine-validated `SnowboardKidsGame.dll`, reviewed assets, only reviewed runtime DLLs (DXC hash-pinned to the official Microsoft `v1.8.2505.1` release, no `dxil.dll`), their license texts, `RUNTIME-DLLS.txt`, the artifact audit and the readiness gate. CI builds the engine-only draft with the pinned DXC and checks its contents. The release-candidate ZIP was built from a clean tree at `5545c3df9156`, passed every readiness gate, and was extracted and run outside the checkout both with `SBK_USER_DATA_DIR` and with a clean `%APPDATA%`; it is a validation artifact (`0.9.0-dev`), not the public one |
| 5 | live gameplay on real Windows | reached | real gameplay on a physical PC: ROM selector, ROM validation, title, playable race, audio, keyboard, controller, rumble, F5/F8, official save persisting across close and reopen, user data in `%APPDATA%\SnowboardKids`; also from the packaged ZIP; [checklist](#checklist) |

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

From a *Developer PowerShell for VS 2022* with clang-cl, CMake, Ninja and
Python 3 (the `cmd.exe` equivalent, with tool setup, is in
[Real Windows validation](#real-windows-validation-level-3--level-5)):

```powershell
git config --global core.autocrlf false   # the patch checker compares bytes
python scripts/bootstrap.py --only runtime --only rt64 --only frontend --only theme --only dxc
cmake -S . -B build-engine -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl `
  -DSBK_BUILD_RENDERER_STACK=ON -DSBK_BUILD_NATIVE_BOOT=ON `
  -DSBK_ENGINE_ONLY=ON -DSBK_CONTINUATIONS=ON
cmake --build build-engine
build-engine\SnowboardKidsEngine.exe --version
ctest --test-dir build-engine --output-on-failure
```

The build compiles every RT64/RecompFrontend shader with the pinned release's
`dxc.exe` and copies `SDL2.dll` and its `dxcompiler.dll` beside the exe; no
`dxil.dll` is needed or copied (configuration fails if `--only dxc` was not
bootstrapped). CTest `renderer_dxc_runtime` exercises RT64's run-time shader
compile+link and D3D12 (WARP) acceptance ([DXC-PROVENANCE.md](DXC-PROVENANCE.md)).

### Runtime dependencies observed in CI (`dumpbin /dependents`)

| DLL | Needed by | Handling |
| --- | --- | --- |
| `SDL2.dll` (2.26.3) | engine | bundled, `licenses/SDL2.txt` |
| `dxcompiler.dll` 1.8.2505.32 | engine (RT64 links `dxcompiler.lib`; compiles and links D3D12 shaders at run time) | bundled from the SHA-256-pinned official Microsoft `v1.8.2505.1` release (`scripts/dxc_redist.py`), not RT64's unsigned contrib build; `licenses/DirectXShaderCompiler-LICENSE-LLVM.txt`, `-LICENSE.txt`, `-ThirdPartyNotices.txt` ([DXC-PROVENANCE.md](DXC-PROVENANCE.md)) |
| `dxil.dll` | nothing: this DXC signs shaders with its built-in validator | never shipped; staging, audit and readiness reject it. A `dxil.dll` a user has installed (e.g. a Windows SDK on `PATH`) may be loaded by `dxcompiler.dll` at startup and is harmless (tested) |
| `VCRUNTIME140.dll`, `VCRUNTIME140_1.dll`, `MSVCP140.dll`, `MSVCP140_ATOMIC_WAIT.dll` | engine, module, DXC | bundled app-local from `VCToolsRedistDir` |
| `KERNEL32`, `USER32`, `GDI32`, `SHELL32`, `ole32`, `OLEAUT32`, `ADVAPI32`, `IMM32`, `SETUPAPI`, `VERSION`, `WINMM`, `msvcrt`, `d3d12`, `dxgi`, `D3DCOMPILER_47`, `api-ms-win-*` | engine, SDL2, DXC | Windows system DLLs, never bundled |

The engine does not import the Vulkan loader (`vulkan-1.dll`), and the Windows
window is created without `SDL_WINDOW_VULKAN`, so D3D12-only systems can start it.

## Building SnowboardKidsGame.dll (needs your ROM)

The module is compiled from two ROM-derived inputs that are never committed,
uploaded or packaged:

- the **CPU continuation corpus** (`build-tools/production-continuation/corpus`,
  43 generated sources), produced from the matching
  [snowboardkids-decomp](https://github.com/cdlewis/snowboardkids-decomp) ELF;
- the **RSP audio microcode** (`aspMain.cpp`), recompiled from your ROM by RSPRecomp.

The decomp toolchain (MIPS binutils, IDO, splat) only runs on Linux/macOS, so
the corpus cannot be generated natively on Windows. Generate it on Linux or in
**WSL2**, export it with your RSP microcode as one checksummed *module inputs
bundle*, and compile the DLL natively on Windows:

```text
Linux / WSL2                                   Windows (Developer prompt)
ROM ─► decomp ELF ─► corpus ─┐
ROM ─► RSPRecomp ─► aspMain ─┴► sbk-module-inputs.zip ─► build-game-module.py ─► SnowboardKidsGame.dll
                                                          (ROM SHA-1 must match)     └► engine --validate-module
```

The bundle records the SHA-1 of the ROM it came from and a SHA-256 for every
file. `build-game-module.py --inputs` refuses a bundle made from another ROM,
a damaged or line-ending-converted copy, and any unexpected file. On Windows the
module must pass `SnowboardKidsEngine.exe --validate-module` (which runs the
module's `init()` and the PE import binding); there is no weaker fallback.

The same bundle built on Linux produces a module that the Linux engine validates
with the reviewed corpus digest `0x76260CB8F0E080D7` (1981 functions, 56 HLE).
The same module was then compiled natively on Windows and reports the same digest (see [Verified on physical Windows](#verified-on-physical-windows)).

## Real Windows validation (Level 3 → Level 5)

### A. On Linux or WSL2: export the module inputs

In a checkout of the same branch, with your ROM:

```bash
# Only if the corpus was never generated in this checkout:
#   (cd ../snowboardkids-decomp && make extract && make)   # matching ELF, see its README
#   bash scripts/bootstrap-n64recomp.sh                    # N64Recomp + RSPRecomp
#   bash scripts/build-continuation-generator.sh
#   python3 tests/production_continuation/generate_corpus.py
python3 scripts/export-module-inputs.py ~/path/to/snowboardkids.z64 --out ~/sbk-module-inputs.zip
```

It prints the bundle's SHA-256. Copy the `.zip` to the Windows PC as a file
(USB stick, network share, or `\\wsl$\...`); do not unzip it on the way. It
refuses to write inside the repository unless the path is git-ignored.

### B. On Windows x86_64: tools (once)

- Git and Python 3.10+:
  `winget install Git.Git` and `winget install Python.Python.3.12`
- Visual Studio 2022 Build Tools with the C++ workload and Clang (clang-cl, as in CI):
  `winget install Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --add Microsoft.VisualStudio.Component.VC.Llvm.Clang --add Microsoft.VisualStudio.Component.VC.Llvm.ClangToolset"`
- If `cmake` or `ninja` is missing from the Developer prompt:
  `winget install Kitware.CMake` and `winget install Ninja-build.Ninja`

Open **x64 Native Tools Command Prompt for VS 2022** (a `cmd.exe` prompt; all
commands below are for it) and check:

```bat
where git python cmake ninja clang-cl lib
```

### C. On Windows: build the engine, build and install the module, play

```bat
git config --global core.autocrlf false
git clone --branch feat/windows-release https://github.com/HikariLucy/SnowboardKids-Recomp.git C:\src\SnowboardKids-Recomp
cd /d C:\src\SnowboardKids-Recomp

rem 1. pinned dependencies + canonical patches + the pinned Microsoft DXC release (ROM-free)
python scripts\bootstrap.py --only runtime --only rt64 --only frontend --only theme --only dxc

rem 2. ROM-free engine (+ assets, SDL2/DXC DLLs beside it)
cmake -S . -B build-engine -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl ^
  -DSBK_BUILD_RENDERER_STACK=ON -DSBK_BUILD_NATIVE_BOOT=ON ^
  -DSBK_ENGINE_ONLY=ON -DSBK_CONTINUATIONS=ON
cmake --build build-engine
build-engine\SnowboardKidsEngine.exe --version
ctest --test-dir build-engine --output-on-failure

rem 3. SnowboardKidsGame.dll from YOUR ROM + the bundle; validated by the engine,
rem    installed to %APPDATA%\SnowboardKids\modules\snowboardkids-us\
python scripts\build-game-module.py C:\path\to\snowboardkids.z64 --inputs C:\path\to\sbk-module-inputs.zip > build-module.log 2>&1
type build-module.log

rem 4. validate the installed module again, explicitly
build-engine\SnowboardKidsEngine.exe --validate-module "%APPDATA%\SnowboardKids\modules\snowboardkids-us\SnowboardKidsGame.dll"

rem 5. play (the console stays open with the logs)
build-engine\SnowboardKidsEngine.exe
```

Step 3 must end with `BUILD SUCCESS!` and `Validation: engine`; step 4 prints a
`MODULE_VALID: ... corpus=0x76260CB8F0E080D7 funcs=1981 hle=56` line. The engine
in `build-engine\` uses `%APPDATA%\SnowboardKids` for user data (unless a
`build-engine\runtime-data` folder exists or `SBK_USER_DATA_DIR` is set) and
loads the module installed there before any bundled one.

Builder exit codes: 1 wrong ROM / bundle from another ROM, 2 no clang-cl/MSVC or
`lib.exe`, 3 bad bundle, 4 compile, 5 link, 6 engine validation, 7 file permissions.

### D. After the Level 5 checklist passes: public ZIP and clean-folder test

Same prompt and checkout, clean tree, engine built from the current HEAD.

```bat
rem 7. public Windows package (every gate must pass; nothing is uploaded)
git status --short
python scripts\package-beta-windows.py ^
  --engine build-engine\SnowboardKidsEngine.exe ^
  --game-module "%APPDATA%\SnowboardKids\modules\snowboardkids-us\SnowboardKidsGame.dll" > package.log 2>&1
type package.log
type dist\SHA256SUMS.txt

rem 8. extract into a clean folder, with fresh user data so only the bundled module can load
rmdir /s /q C:\SBK-zip-test 2>nul
mkdir C:\SBK-zip-test
tar -xf dist\SnowboardKidsRecompiled-0.9.0-dev-Windows-x86_64.zip -C C:\SBK-zip-test
set SBK_USER_DATA_DIR=C:\SBK-zip-test\userdata
C:\SBK-zip-test\SnowboardKidsRecompiled\SnowboardKidsEngine.exe > zip-run.log 2>&1

rem 9. repeat the smoke test from the ZIP: ROM selector, title, race, audio, input, F5/F8, save, reopen
rem    then once more without SBK_USER_DATA_DIR, after moving the development data aside:
set SBK_USER_DATA_DIR=
move "%APPDATA%\SnowboardKids" "%APPDATA%\SnowboardKids.dev-backup"
C:\SBK-zip-test\SnowboardKidsRecompiled\SnowboardKidsEngine.exe > zip-run-appdata.log 2>&1
dir "%APPDATA%\SnowboardKids" /s /b
```

Step 7 must end with `Public beta package gates passed` and `Release candidate
archive:`; `package.log` lists every runtime DLL as `system` or `bundled`.
The ZIP carries `RUNTIME-DLLS.txt` (SHA-256, version, provenance and license
files of each bundled DLL) and `BUILD-INFO.txt` (`Package: public-beta`).
Step 8/9 prove the package runs without the checkout and keeps its data in
`%APPDATA%\SnowboardKids`. Restore your development data afterwards:

```bat
rmdir /s /q "%APPDATA%\SnowboardKids"
move "%APPDATA%\SnowboardKids.dev-backup" "%APPDATA%\SnowboardKids"
```

### Verified on physical Windows

Results from a physical Windows PC (AMD Radeon Vega 8), with a legitimate ROM
and a private module inputs bundle. Validated commit: `5545c3df9156`.

Engine and module:

- `SnowboardKidsEngine.exe` recompiled natively with clang-cl from `5545c3df9156`.
- `--version` prints `Snowboard Kids Recompiled 0.9.0` and `commit 5545c3df9156`.
- 12/12 CTests PASS.
- A real `SnowboardKidsGame.dll` built from the legitimate ROM + private bundle
  and validated: `MODULE_VALID magic=0x3130444F4D4B4253 abi=1
  corpus=0x76260CB8F0E080D7 funcs=1981 hle=56`.
- The first start had found a bug: RT64 required `dxil.dll` at start-up. Making
  it optional (`NameRequiredPair("dxil.dll", false)`, in
  `patches/rt64-dxc-executable.patch`) lets the game start with the pinned
  Microsoft DXC and no `dxil.dll`. The fix now comes from the repository's
  canonical patch, and CI is green and guards the patched line.

Gameplay from the checkout build:

- D3D12/RT64 renders on the AMD Radeon Vega 8.
- ROM selector OK; ROM validation Good; correct title and game; a playable race.
- Audio, keyboard, physical controller and rumble OK.
- F5 quick-save and F8 quick-load OK.
- The official in-game save persists after closing and reopening the game.
- Close and reopen OK; runtime data lives in `%APPDATA%\SnowboardKids`.

Package candidate (validation artifact, not the public one):

- `SnowboardKidsRecompiled-0.9.0-dev-Windows-x86_64.zip`, generated from a clean
  tree at `5545c3df9156`.
- SHA-256: `a9f2971cad2591767023229d4c5f4d567624e11b5b5f99cb8139c2bd667407bc`
- `package-beta-windows.py` exited 0 and printed `Public beta package gates
  passed.` Every readiness gate passed: `dependency_provenance`,
  `promptfont_license`, `bundled_font_licenses`,
  `recompfrontend_pending_disclosed`, `project_license`,
  `dependency_gpl_compliance`, `theme_asset_icons`, `game_module_disclosure`,
  `windows_runtime_redistribution`. No `dxil.dll` is distributed.
- Clean-folder test 1: the ZIP was extracted to `C:\SBK-zip-test`, outside the
  checkout, and run with `SBK_USER_DATA_DIR=C:\SBK-zip-test\userdata`. Exit 0; it
  loaded exactly the packaged module
  (`...\SnowboardKidsRecompiled\modules\snowboardkids-us\SnowboardKidsGame.dll`).
  ROM, D3D12, audio, gameplay, F5 and F8 OK.
- Clean-folder test 2: run without `SBK_USER_DATA_DIR`, with a temporary clean
  `%APPDATA%`. Exit 0; it again loaded the module from the ZIP and created its
  new data under `%APPDATA%\SnowboardKids`. The original development
  `%APPDATA%` was restored afterwards.

This `0.9.0-dev` ZIP from `5545c3d` was the validation artifact. It is **not**
the definitive public artifact, which must be produced from the final HEAD (see
[open items](#open-items-before-a-public-windows-package)).

### Checklist

Record each result (OK / FAIL + log) in the PR or an issue. Levels 3–5 are
claimed only from these results; nothing here is inferred from CI.

| # | Check | Level |
| --- | --- | --- |
| [x] | `SnowboardKidsEngine.exe --version` prints `0.9.0` and the checkout commit | 2 |
| [x] | `build-game-module.py --inputs` → `BUILD SUCCESS!`, `Validation: engine` | 3 |
| [x] | `--validate-module` prints `MODULE_VALID` (corpus `0x76260CB8F0E080D7`) | 3 |
| [x] | `SnowboardKidsGame.dll` loads (`[MODULE] Loaded game module from: ...`) | 3 |
| [x] | ROM selector opens | 5 |
| [x] | your ROM validates | 5 |
| [x] | title screen reached | 5 |
| [x] | a race starts | 5 |
| [x] | D3D12 rendering works without observed corruption | 5 |
| [x] | audio works | 5 |
| [x] | keyboard works | 5 |
| [x] | physical controller works | 5 |
| [x] | rumble works | 5 |
| [x] | F5 quick-save works | 5 |
| [x] | F8 quick-load works | 5 |
| [x] | native in-game save persists after close and reopen | 5 |
| [x] | close and reopen works; settings and progress restored (F5/F8 state and the official save) | 5 |
| [x] | runtime user data observed in `%APPDATA%\SnowboardKids` | 5 |
| [x] | a packaged ZIP runs from a fresh folder with no development checkout (with `SBK_USER_DATA_DIR` and with a clean `%APPDATA%`) | 4/5 |

`[x]` = verified on physical Windows. The ZIP row was verified with the
`0.9.0-dev` validation package described above.

### If something fails: capture logs

```bat
rem environment
where git python cmake ninja clang-cl lib > sbk-env.txt 2>&1
clang-cl --version >> sbk-env.txt 2>&1
python --version >> sbk-env.txt 2>&1
git rev-parse HEAD >> sbk-env.txt

rem module build with intermediate files kept
python scripts\build-game-module.py C:\path\to\snowboardkids.z64 --inputs C:\path\to\sbk-module-inputs.zip --keep-temp -j 1 > build-module.log 2>&1

rem engine run with all console output
build-engine\SnowboardKidsEngine.exe > sbk-run.log 2>&1

rem engine logs (first-run, builder)
dir "%APPDATA%\SnowboardKids" /s /b > sbk-userdata.txt
type "%APPDATA%\SnowboardKids\logs\*.log"

rem missing-DLL / import problems
dumpbin /nologo /dependents build-engine\SnowboardKidsEngine.exe
dumpbin /nologo /dependents "%APPDATA%\SnowboardKids\modules\snowboardkids-us\SnowboardKidsGame.dll"

rem native crashes (the engine has no crash handler on Windows yet)
wevtutil qe Application /c:5 /rd:true /f:text /q:"*[System[Provider[@Name='Application Error']]]" > sbk-crash.txt

rem GPU / driver
dxdiag /t %CD%\sbk-dxdiag.txt

rem run-time shader compiler (RT64's D3D12 compile+link path, WARP checks)
build-engine\dxc-probe\SnowboardKidsDxcRuntimeProbe.exe dxc-probe-out --control-load > dxc-probe.log 2>&1
```

### What to send back

| Failure at | Send |
| --- | --- |
| tools / clone / bootstrap | `sbk-env.txt`, the full console output of the failing command |
| engine build | the first error block of `cmake --build build-engine`, `sbk-env.txt` |
| module build (step 3) | `build-module.log` (rerun with `--keep-temp -j 1`), `sbk-env.txt` |
| `--validate-module` (step 4) | its full output, both `dumpbin /dependents` outputs |
| launch / gameplay (step 5) | `sbk-run.log`, `%APPDATA%\SnowboardKids\logs\*.log`, `sbk-crash.txt`, `sbk-dxdiag.txt`, `dxc-probe.log` (rendering problems), which checklist row failed |
| package (step 7) | `package.log` |
| ZIP run (steps 8–9) | `zip-run.log` / `zip-run-appdata.log`, the `dir` listing, `RUNTIME-DLLS.txt` from the ZIP |

Before sharing logs, check them for personal paths (`C:\Users\<name>`). Never
attach the ROM, the bundle, the DLL, the ZIP, saves or savestates.

## Packaging

Two modes of `scripts/package-beta-windows.py`, deliberately separate:

| | Engine-only draft (`--engine-only-draft`) | Public package (default) |
| --- | --- | --- |
| Purpose | CI review of the ROM-free engine | the Windows beta ZIP |
| Game module | none | required, validated by the engine (`--validate-module`) |
| Engine | may skip running (`--no-run`) | must run and report the project version and HEAD |
| Working tree | any | must be clean |
| Runtime DLLs | from PE imports; DXC must be the pinned release bytes | same |
| DLLs whose license text is not bootstrapped | left out, with a warning | refused |
| `dxil.dll` | never (unreviewed import; audit rejects it) | never |
| License texts, `RUNTIME-DLLS.txt`, audit | required | required |
| Readiness gate | not run | required (`check_release_readiness.py --public-beta`) |
| Identification | `...-engine-draft.zip`, `Package: draft (not a release candidate; do not distribute)` | `Package: public-beta` |

Public package (see [step 7](#d-after-the-level-5-checklist-passes-public-zip-and-clean-folder-test)):

```bat
python scripts\package-beta-windows.py ^
  --engine build-engine\SnowboardKidsEngine.exe ^
  --game-module "%APPDATA%\SnowboardKids\modules\snowboardkids-us\SnowboardKidsGame.dll"
```

Output:

```text
dist/SnowboardKidsRecompiled-<version>-Windows-x86_64.zip
dist/SHA256SUMS.txt
```

Layout:

```text
SnowboardKidsRecompiled/
├── SnowboardKidsEngine.exe
├── SDL2.dll, dxcompiler.dll, VC++ runtime   (only DLLs the PE imports require; no dxil.dll)
├── modules/snowboardkids-us/SnowboardKidsGame.dll
├── assets/
├── licenses/   (incl. DirectXShaderCompiler-LICENSE-LLVM/-LICENSE/-ThirdPartyNotices,
│                SDL2)
├── scripts/    (optional local module builder)
├── RUNTIME-DLLS.txt   (per bundled DLL: SHA-256, version, provenance, license files)
├── LICENSE, SOURCE-COMPLIANCE.md, THIRD_PARTY_NOTICES.md
├── BETA-DISTRIBUTION-POLICY.md, RUNNING.md, BUILD-INFO.txt
```

System DLLs are never bundled; an import that is neither a system DLL nor a
reviewed redistributable fails staging. The audit rejects ROMs, saves,
Controller Pak images, savestates, logs, `.pdb/.lib/.exp/.ilk`, DLLs outside
the reviewed list, a bundled DLL without its license texts, a
`RUNTIME-DLLS.txt` whose hashes differ from the archived DLLs, and personal
paths such as `C:\Users\<name>`. The readiness gate additionally checks the
DXC bytes against the pinned release and rejects `dxil.dll`.

## Open items before a public Windows package

Gameplay, the ROM selector, rumble, save persistence and the clean-folder ZIP
run are verified (see [Verified on physical Windows](#verified-on-physical-windows));
they are no longer open. What remains is release work:

1. **Definitive artifact**: build the Windows `0.9.0-beta.1` package from the final
   HEAD / merge commit. The validated ZIP was `0.9.0-dev` from `5545c3d`, so it
   must not be published.
2. **Smoke test** of the definitive artifact: a short run from a clean folder
   (ROM selector, title, race, F5/F8), plus a check of its SHA-256.
3. **Console window**: the engine is a console-subsystem executable, so a
   console window opens next to the game. Acceptable for a beta (logs stay
   visible); it may be revisited before a stable release.

Declared beta exception, not hidden: the top-level license clarification for
RecompFrontend is still pending. It is disclosed in the package and checked by
the `recompfrontend_pending_disclosed` readiness gate.

Never publish or attach the ROM, the private module inputs bundle, saves,
savestates, Controller Pak images or other derived data.
