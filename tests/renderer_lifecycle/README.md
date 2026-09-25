# Renderer restore lifecycle (P6-XPROC-01)

```sh
python3 tests/renderer_lifecycle/run.py             # no GPU
python3 tests/renderer_lifecycle/run.py --sanitize  # ASan + UBSan
python3 tests/renderer_lifecycle/run_live.py        # ROM + display + GPU
```

`run.py` needs a built `build-renderer-stack`. It links the real
`RT64::SharedQueueResources` and runs the production reset from
`rt64_presentation_history.h` on warm, fresh and repeated states. It checks
the RT64 present/workload handshake: the restored frame's present must publish
its id on both the interpolation and plain paths, and the workload queue must be
able to reuse either counter set. A control case confirms the old reset
(zeroed counters with stale color images) fails. The script first checks that
the pinned RT64 queues still contain the modeled rules.

`run_live.py` drives the real game through the DEVELOPMENT ONLY control file,
with no keyboard input:

- same-process: dev capture, then 8 restores;
- cross-process: process A quicksaves slot `lifecycle-gate` and exits; fresh
  processes B quickload it after 30–60 s of attract loop.

After each restore the next freeze must reach Frozen, which means Graphics
drained. Guest dispatches and the frontend heartbeat must keep advancing.
The slot file is removed at the end. This is not the human gate: image,
input and closing the window still need a person to check them.
