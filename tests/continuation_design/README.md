# P1.5 continuation design checks

These isolated checks extend the evidence for the existing P1 prototype. They do
not implement full-game continuation support or a production savestate format.
Run from the repository root:

```sh
python3 -m unittest discover -s tests/continuation_design -p test_schema.py -v
python3 tests/continuation_design/flow_run.py
python3 tests/continuation/run.py
python3 tests/continuation_design/test_cross_build.py
python3 tests/continuation_design/run_benchmark.py
```

The C++ probes require the same local pinned compiler libraries as
[P1](../../docs/P1-SERIALIZABLE-CONTINUATION.md). The P1 runner and benchmark also
require the matching local ELF. Build products stay under ignored `build-tools/`.
Run these commands sequentially: the P1 runner regenerates files used by the
cross-build check and benchmark.

For instrumented flow execution:

```sh
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  python3 tests/continuation_design/flow_run.py --sanitize
```

Leak checking is disabled for the ptrace sandbox; the linked generator libraries
are not instrumented by this command.

- `schema_model.py` is an executable identity/schema proposal. It distinguishes
  images, section ROM ranges, function offsets, code roles, continuation phases
  and delay-slot owners. Tests check order independence, fresh-process hash-seed
  independence, duplicate rejection, and scalar field restrictions. This model
  is not wired into the P1 codec or runtime and is not an untrusted-input parser.
- `flow_run.py` compares synthetic MIPS compiled through native calls and the P1
  explicit-frame backend. It covers a repeated direct call, a backward branch,
  ordinary and likely branch delay slots on both paths, and live HI/LO/c1cs.
  Frame containers are reconstructed between dispatch steps. This is an
  in-process test, without serialization or a blocking HLE call. It also checks
  that the backend rejects register and lookup calls. Decoder options match
  the pinned N64Recomp CLI.
- `test_cross_build.py` transfers P1 snapshots in both directions between O0 PIE
  and O2 non-PIE binaries for FR=0 and FR=1. Parked bytes must match between
  exporters, and restored results must match the native baseline. Both binaries
  use the same generated semantic build; this does not test version migration.
- `run_benchmark.py` first runs P1 verification, then compares nine samples of
  200,000 executions each with alternating measurement order. Frames are
  preallocated and the reply is available immediately. Timings include fixture
  reset and queue operations, but exclude parking, snapshot encoding and I/O.
  Results are a microbenchmark, not a full-game performance estimate.
