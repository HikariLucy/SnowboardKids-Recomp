# P2 quiescence tests

> **Historical baseline (2026-09-23)**, written for P2. `run.py` now also runs `lifecycle.cpp` (P2 arming at startup); see `docs/P6-PERSISTENT-SAVESTATE.md`.

Apply the pinned dependency patches first:

```sh
bash scripts/apply-runtime-patches.sh
python3 scripts/apply-quiescence-patches.py
python3 tests/quiescence/run.py
```

`main.cpp` tests the production coordinator with concurrent synthetic owners,
VI, timer, RSP and graphics participants. It performs 600 stress cycles plus
in-flight-VI clock, stale-generation, cancellation and shutdown-release cases.
It does not pretend to run menus or races.

`kernel.cpp` compiles the actual patched runtime threads, scheduler, message
queues and timers. Forty cycles alternate external messages and one-shot timer
expiry, holding each freeze longer than the timer deadline. An additional cycle
parks a sender blocked on a full queue. Assertions cover all fixture RAM,
osGetCount/osGetTime, dormant queue membership, FIFO, jam, drop and retry
behavior. The same expected guest message transcript also runs with the
coordinator disabled (`--baseline`). This fixture uses `_Exit` after reporting
results because the pinned runtime has detached timer/guest workers; it does
not claim to test clean runtime shutdown.

CMake alternatives: enable `SBK_BUILD_QUIESCENCE_TESTS`, build
`SnowboardKidsQuiescenceTest` and `SnowboardKidsQuiescenceKernelTest`, then run
`ctest --test-dir <build> --output-on-failure -R runtime_quiescence`.

For the real graphical game, set `SBK_P2_CYCLES` before starting the native
executable. Optional `SBK_P2_START_MS`, `SBK_P2_INTERVAL_MS` and
`SBK_P2_HOLD_MS` control host-time test scheduling. The normal SDL event pump
continues on the frontend thread. `SBK_P2_CONTROL_FILE` optionally names a
text file containing a hexadecimal N64 button mask for test controller 1
(`1000` Start, `8000` A, `4000` B, `0000` release). This is a test input,
not a checkpoint file; updates are read on the frontend and published atomically.
It avoids stealing desktop focus. Omit it for ordinary controller input.

The probe compares 8 MiB of RDRAM, logical clock and SDL queued audio across
each Frozen interval, writes diagnostic records to stderr, and resumes.
`P2 COMPLETE` explicitly does **not** claim full-game baseline equivalence.
There are no snapshot files, save/load hotkeys, or restored memory writes.

See [the P2 report](../../docs/P2-RUNTIME-QUIESCENCE.md) for actual gameplay
results, graphics scope, and limitations.
