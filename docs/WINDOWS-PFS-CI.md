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

- `assert()` stays active under `NDEBUG` (`#undef NDEBUG`): the asserts wrap
  the PFS operations themselves.
- Every scratch path contains spaces; `run.py` prints the compiler it used.
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

On the hosted runner `c++` resolves to MinGW-w64 g++ 14.2, which targets
Win32 (`_WIN32`), so the Win32 persistence path is what runs. The PFS
backend is not yet compiled by MSVC/clang-cl in a test.

## Result

At `3ce7ce3`, with no test exclusions:

| Workflow run | Windows | Linux |
|---|---|---|
| ROM-free CI 36248017480 | 13/13 PASS (controller_pak PASS) | 13/13 PASS |
| Renderer stack compile 36248017470 | 13/13 PASS, D3D12/VSync stack compiles | — |
