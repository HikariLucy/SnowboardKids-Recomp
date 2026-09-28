# Controller Pak test on Windows CI

## Why it was skipped

`1b8ee66` (“ci(windows): expand portable ROM-free test execution on Windows
runner”, 2026-09-25) widened the Windows ROM-free step from two tests to
`ctest -E "controller_pak"`. No reason was recorded; `RELEASE-ENGINEERING.md`
only says Linux tests the Controller Pak “with pinned runtime headers”.

The Windows job never bootstrapped the runtime, so `tests/controller_pak/run.py`
could not find the pinned N64Recomp headers for its HLE build (it falls back
to `os.environ["SBK_RECOMP_INCLUDE"]`). Nothing in the PFS backend was
Windows-specific broken.

## Evidence

- Diagnostic run with the exclusion removed (bootstrapped job, no test
  changes): run 36246552827, 13/13 PASS.
- The PFS backend was not changed. Its Windows path already writes a
  `CREATE_NEW` temp image in the same directory, `WriteFile`,
  `FlushFileBuffers`, closes the handle, then
  `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)` and deletes the temp on
  failure. Reads use a scoped stream that is closed before any replacement.

## Test changes

- The checks wrap the PFS operations themselves, so they must run under
  `NDEBUG` (first `#undef NDEBUG`, now an always-evaluated `CHECK`).
- Every scratch path contains spaces.
- New in-process non-ASCII case (`Snowboard Kids PFS ó ñ`): persist, replace
  an existing image, reopen and read back. Replacements must leave no
  `.tmp-` images (also checked after the A/B/C run).
- Non-ASCII directories are created inside the test, not passed through
  narrow `argv`; CJK paths are out of scope.

## CI

`ci.yml`'s Windows job now bootstraps the pinned runtime and theme (LF
checkout, Python 3.12) and runs CTest without exclusions. The theme was
also missing on Linux: `ui_assets`, `release_package` and
`release_readiness` stage the reviewed fonts from
`.deps-renderer/recomp-theme` and failed in both ROM-free jobs.

Until `bcc0264`, `run.py` compiled the suite at test time with `c++`, which
on the hosted runner is MinGW-w64 g++ 14.2: the Win32 persistence path ran,
but never built by the engine's compiler.

## Result

At `3ce7ce3`, with no test exclusions:

| Workflow run | Windows | Linux |
|---|---|---|
| ROM-free CI 36248017480 | 13/13 PASS (controller_pak PASS) | 13/13 PASS |
| Renderer stack compile 36248017470 | 13/13 PASS, D3D12/VSync stack compiles | — |

## Project toolchain (`1725f3f`, `bcc0264`)

`SnowboardKidsControllerPakTest` is a CMake target (`CMakeLists.txt`,
`sbk_add_controller_pak_test`) built from the same sources: backend
(`src/pfs/controller_pak.cpp`), HLE adapter (`src/pfs/hle.cpp`) and the
existing fixtures. Includes are `src` and `SBK_RECOMP_INCLUDE` (pinned
N64Recomp headers); no extra defines or libraries. CTest runs the binary
directly; it re-spawns itself once per mode (normal, unicode, A/B/C, seven
corruptions, HLE), so nothing is compiled at test time. `--compiler` prints
the compiler from its predefined macros, and both Windows jobs fail unless it
is MSVC or clang-cl. `SBK_PFS_SANITIZER` (GCC/Clang only) is what
`run.py --sanitize address,undefined` configures; Linux CI runs it.

MSVC `/W4` reports warnings only (C4244, C4310, C4201 in `recomp.h`, C4996
`getenv`); GCC and Clang build it with `-Wall -Wextra -Werror`.

| Workflow run at `bcc0264` | Compiler (binary `--compiler`) | Result |
|---|---|---|
| ROM-free CI 36250976654, Windows | MSVC 19.44.35229 | 13/13 PASS |
| ROM-free CI 36250976654, Linux | GNU 13.3.0 | 13/13 PASS; ASan/UBSan PASS |
| Renderer stack compile 36250976672 | MSVC 19.44.35229 | 13/13 PASS; D3D12/VSync stack compiles |
| same run, clang-cl build | ClangCl 19.1.5 | `controller_pak` PASS |

MinGW is no longer used for the Controller Pak gate.
