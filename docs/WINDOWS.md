# Windows x86_64 — status and engineering notes

Updated: 2026-09-29

**Windows is in development. It is not a supported platform yet.** The public
`v0.9.0-beta` release is Linux x86_64 only. Windows becomes "supported" only
after a live gameplay validation of a packaged build (see
[Real Windows validation](#real-windows-validation-level-3--level-5)).

## Status by level

| Level | Meaning | Status | Evidence |
| --- | --- | --- | --- |
| 1 | Windows renderer/runtime compiles | reached | `renderer-compile.yml` → `windows` job (RT64, RecompFrontend, N64ModernRuntime with clang-cl) |
| 2 | `SnowboardKidsEngine.exe` builds and `--version` works | reached | `renderer-compile.yml` → `windows-engine` job (windows-2022, clang-cl 19): engine-only build, `--version` prints `Snowboard Kids Recompiled 0.9.0` + the HEAD commit, `dumpbin` shows the 62 exported runtime symbols; all 11 engine-only CTests pass |
| 3 | `SnowboardKidsGame.dll` builds and passes ABI validation | mechanism proven, real DLL pending | `module_engine_probe` on Windows: a synthetic DLL built with the real builder code and module harness imports from `SnowboardKidsEngine.exe`, and the real engine loads, validates and initializes it (export binding checked); a DLL needing a symbol the engine lacks is refused (`LoadLibrary` error 127). The real-module path (module inputs bundle → `build-game-module.py --inputs`) is in place and produces an engine-validated module on Linux; a real DLL has not been built on Windows yet — see [Real Windows validation](#real-windows-validation-level-3--level-5) |
| 4 | complete, audited Windows ZIP | infrastructure ready, artifact pending | public mode of `package-beta-windows.py` requires the engine built from HEAD, a real engine-validated `SnowboardKidsGame.dll`, reviewed assets, only reviewed runtime DLLs (DXC hash-pinned to the official Microsoft `v1.7.2308` release), their license texts, `RUNTIME-DLLS.txt`, the artifact audit and the readiness gate. CI builds the engine-only draft with the pinned DXC and checks its contents. Missing: a real module, and the maintainer decision on `dxil.dll` ([DXIL-REDISTRIBUTION.md](DXIL-REDISTRIBUTION.md)) |
| 5 | live gameplay on real Windows | not started | requires a person with a Windows PC and their own ROM; [checklist](#checklist) |

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

The build copies `SDL2.dll` and the pinned release's `dxcompiler.dll` and `dxil.dll`
beside the exe (configuration fails if `--only dxc` was not bootstrapped).

### Runtime dependencies observed in CI (`dumpbin /dependents`)

| DLL | Needed by | Handling |
| --- | --- | --- |
| `SDL2.dll` (2.26.3) | engine | bundled, `licenses/SDL2.txt` |
| `dxcompiler.dll` 1.7.2308.7 | engine (RT64 links `dxcompiler.lib`) | bundled from the SHA-256-pinned official Microsoft release (`scripts/dxc_redist.py`), not RT64's unsigned contrib build; `licenses/DirectXShaderCompiler-LICENSE-LLVM.txt`, `-LICENSE.txt`, `-ThirdPartyNotices.txt` ([DXC-PROVENANCE.md](DXC-PROVENANCE.md)) |
| `dxil.dll` 101.7.2308.12 | loaded by `dxcompiler.dll` | same release; `licenses/DirectXShaderCompiler-dxil-LICENSE-MS.txt`; shipped only after [the maintainer decision](DXIL-REDISTRIBUTION.md) |
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
**It has not been compiled on Windows yet** — that is the next step (Level 3).

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
The package refuses to build while `docs/DXIL-REDISTRIBUTION.md` still reads
`Decision: PENDING` — that decision is yours (see that file).

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

### Checklist

Record each result (OK / FAIL + log) in the PR or an issue. Levels 3–5 are
claimed only from these results; nothing here is inferred from CI.

| # | Check | Level |
| --- | --- | --- |
| [ ] | `SnowboardKidsEngine.exe --version` prints `0.9.0` and the checkout commit | 2 |
| [ ] | `build-game-module.py --inputs` → `BUILD SUCCESS!`, `Validation: engine` | 3 |
| [ ] | `--validate-module` prints `MODULE_VALID` (corpus `0x76260CB8F0E080D7`) | 3 |
| [ ] | `SnowboardKidsGame.dll` loads (`[MODULE] Loaded game module from: ...`) | 3 |
| [ ] | ROM selector opens | 5 |
| [ ] | your ROM validates | 5 |
| [ ] | title screen reached | 5 |
| [ ] | a race starts | 5 |
| [ ] | D3D12 rendering works (no corruption, correct aspect) | 5 |
| [ ] | audio works | 5 |
| [ ] | keyboard works | 5 |
| [ ] | physical controller works | 5 |
| [ ] | rumble works | 5 |
| [ ] | F5 quick-save works | 5 |
| [ ] | F8 quick-load works | 5 |
| [ ] | native in-game save persists | 5 |
| [ ] | close and reopen works; settings and progress restored | 5 |
| [ ] | user data is in `%APPDATA%\SnowboardKids` (nothing written beside the exe) | 5 |
| [ ] | a packaged ZIP runs from a fresh folder with no development checkout | 4/5 |

The last row needs the Level 4 package (below), which is still blocked on the
DXC license texts.

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
```

### What to send back

| Failure at | Send |
| --- | --- |
| tools / clone / bootstrap | `sbk-env.txt`, the full console output of the failing command |
| engine build | the first error block of `cmake --build build-engine`, `sbk-env.txt` |
| module build (step 3) | `build-module.log` (rerun with `--keep-temp -j 1`), `sbk-env.txt` |
| `--validate-module` (step 4) | its full output, both `dumpbin /dependents` outputs |
| launch / gameplay (step 5) | `sbk-run.log`, `%APPDATA%\SnowboardKids\logs\*.log`, `sbk-crash.txt`, `sbk-dxdiag.txt`, which checklist row failed |
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
| DLLs not cleared (`dxil.dll` while PENDING) | left out, with a warning | refused |
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
├── SDL2.dll, dxcompiler.dll, dxil.dll, VC++ runtime   (only DLLs the PE imports require)
├── modules/snowboardkids-us/SnowboardKidsGame.dll
├── assets/
├── licenses/   (incl. DirectXShaderCompiler-LICENSE-LLVM/-LICENSE/-ThirdPartyNotices,
│                DirectXShaderCompiler-dxil-LICENSE-MS, SDL2)
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
DXC bytes against the pinned release and the `dxil.dll` decision.

## Open items before a public Windows package

1. **A real `SnowboardKidsGame.dll`** built on Windows with your ROM and
   validated by the engine (steps A–C).
2. **Live validation** ([checklist](#checklist)).
3. **`dxil.dll` redistribution decision** by the maintainer
   ([DXIL-REDISTRIBUTION.md](DXIL-REDISTRIBUTION.md)). Provenance and license
   texts for both DXC DLLs are resolved ([DXC-PROVENANCE.md](DXC-PROVENANCE.md));
   what remains is accepting (or not) Microsoft's distributable-code terms.
4. The engine is a console-subsystem executable, so a console window opens
   next to the game. Acceptable for a beta (logs stay visible); revisit before
   a stable release.
