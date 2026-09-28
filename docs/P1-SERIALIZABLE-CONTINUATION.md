# P1: serializable continuation feasibility prototype

Result (2026-09-23): **the isolated continuation proof passes**. Three N64Recomp-generated frames park at a blocking `osRecvMesg`, survive destruction of their execution owner and process, and resume in a new process. The final canonical register state and every byte of 8 MiB RDRAM equal an uninterrupted native-call baseline. This establishes feasibility for the exercised subset, not coverage of every game function or restoration of a running game session.

## Run

From the repository root, with the existing pinned N64Recomp toolchain built and the matching sibling decomp ELF available:

```sh
python3 tests/continuation/run.py
```

Dependencies are the existing `.deps/N64Recomp` headers, `build-tools/n64recomp` static libraries, a C++20 compiler, Python 3, and `../snowboardkids-decomp/build/snowboardkids.elf`. No network or new package installation is required. The script builds a separate generator and executable in ignored `build-tools/continuation/`; it does not replace the installed N64Recomp CLI, generated game files, or runtime libraries.

Sanitizer run in the Codex ptrace sandbox:

```sh
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  python3 tests/continuation/run.py --sanitize
```

AddressSanitizer and UndefinedBehaviorSanitizer pass. LeakSanitizer's default run fails with “LeakSanitizer does not work under ptrace”; leak checking was disabled for the successful sandbox run. The existing N64Recomp static libraries themselves are not rebuilt with instrumentation. Outside a ptrace environment, `python3 tests/continuation/run.py --sanitize` can also attempt leak checking.

The toolchain inspected is N64Recomp `ffb39cdad1da5de07eaaa48bd1db4a89a7986771`. The normal game's runtime embeds another N64Recomp revision (`81213c1831fab2521a6a5459c67b63437d67e253`); this experiment does not silently substitute its ABI into that runtime.

## What actually executes

`extract.py` reads the user's big-endian ELF32 and extracts the 68-byte `requestControllerPakFreeSpaceUpdate` at `0x80001858`. No game instructions, generated game C, ROM, or captured RDRAM are checked into Git. The original function sends `0xC0` to queue `0x800E4B78`, then blocks receiving a reply on `0x800E4BB0` into its guest stack.

`generate.cpp` constructs two synthetic MIPS test callers above that real game function. All three functions are compiled twice from the same instructions:

- Ordinary `N64Recomp::recompile_function` / `CGenerator`: native C++ calls, local generated temporaries.
- Experimental `ContinuationGenerator` through `recompile_function_custom`: ordinary instruction emission delegates to the same CGenerator, while calls/returns become explicit dispatch actions.

The synthetic callers deliberately retain different, nonzero `hi`, `lo`, and `c1cs` values across nested calls. After returning they write these values, completion counters, and odd-FPR reads into guest memory. Their JAL delay slots increment a guest register. This makes lost locals, wrong FR binding, repeated calls, and repeated delay-slot execution observable. `result` is conservatively stored too; it is nonzero and checked at park/import, but is not semantically live after these particular calls.

This is one logical recompiled thread executing on the test process's main native thread. It does not start a game session or attach to an existing ultramodern worker. The two callers are test scaffolding, not real game callers of the selected leaf. The message adapter models the fixed queues' FIFO operations with guest addresses and the runtime's queue field offsets, but does not instantiate the full ultramodern scheduler or external event workers.

## Demonstration sequence

1. Enter generated root → generated middle → real generated game leaf.
2. Send the request once, then encounter an empty blocking receive. The leaf's after-call continuation is recorded; the receive's queue, destination, and flags are captured separately. `run()` returns to the harness with no generated native activation remaining.
3. Repeatedly poll while empty and verify that canonical semantic state does not change.
4. Encode the owned context, three frames, blocked operation, queue/RDRAM state, and diagnostic completion counters. Destroy the owner and its memory before writing the test transport bytes, then exit the exporter process.
5. Launch a separate importer process and validate/reconstruct new context, memory, and frame containers. Rebind `f_odd`; native object storage and function dispatch addresses come from the new process.
6. Deliver the same scripted reply (`0xA5B6C7D8`). A successful receive writes the guest destination, consumes one queue item, sets `v0`, and clears the blocked operation before dispatch resumes.
7. Resume at the label immediately following the emitted receive call, then unwind both generated callers. Receive count and both caller completion counts must be exactly one; another `run()` must be inert.
8. Compare the final canonical encoding against a separate uninterrupted baseline process. The baseline keeps its ordinary native generated call chain intact while its HLE adapter delivers the same scripted message at the empty receive. Comparison includes all named GPRs/FPR bits, context HI/LO, status/FR, rounding, counters, and **all 8 MiB RDRAM**, including queue ring storage and guest stack bytes.

Both paths share the queue helper, so this is a continuation differential test, not an independent proof of ultramodern queue correctness or MIPS emulation accuracy.

## Representation and identity

Each frame contains only `{function: u64, continuation: u32, hi: u64, lo: u64, result: u64, c1cs: i32}`. Generated locals bind directly to owned frame fields. A generated step returns a callee ID; the dispatcher pushes a new zero-initialized frame or pops on return. References to vector elements are not retained across pushes/pops.

Function IDs are `(fixed main-section namespace 1 << 32) | guest entry PC`; continuation IDs are guest JAL instruction PCs scoped by their function. Entry uses continuation zero. They are independent of native addresses, symbol discovery order, vector indices, and emission order.

| Frame | Function ID | Parked continuation |
|---|---|---|
| Test root | `0x0000000180700000` | `0x8070001C` |
| Test middle | `0x0000000180700100` | `0x8070011C` |
| Game leaf | `0x0000000180001858` | `0x80001884` |

The leaf also has a send continuation at `0x80001870`. Generated metadata validates each parent→child call edge and the final edge to the receive adapter. Generation is repeated after reversing the function discovery vector; emitted bodies, dispatch, and continuation schemas must be identical.

The codec writes individual little-endian integers, including every GPR and raw FPR bits; it never dumps `recomp_context`, frames, or C++ containers as native structs. A source/input/library SHA-256 build identity rejects incompatible test builds. RDRAM uses the runtime's little-endian, word-swapped byte representation. Import validates the fixed parked stack footprint, three-frame chain, IDs, FR state, rounding, message phase, queue shape/buffers, exact memory length, and absence of trailing data before running anything.

`f_odd` is reconstructed from FR mode and the restored context. Host rounding mode is installed from a semantic 0–3 value when dispatch runs. No native return address, stack image, `jmp_buf`, `ucontext`, thread handle, host pointer, native `fenv` representation, or allocator/container bytes are persisted. Guest PCs, guest addresses, and guest stack bytes remain legitimate guest state. No native `OSThread.context` pointer is ever placed in this isolated RDRAM image.

The temporary `P1CT` transport is a test artifact, not a `.sbks` format or user-facing save API. Only this fixture's blocked state is accepted for import. It is not a general untrusted-file loader.

## Verification coverage

The normal and sanitized runners pass:

- Fresh-process export/import versus uninterrupted native generated baseline, FR=0 and FR=1.
- Actual post-resume odd-FPR reads in both FR modes; forced unrelated host rounding before resume.
- Exact scalar codec checks with distinct values in every GPR/FPR and NaN payload bits, context HI/LO, and correct `f_odd` rebinding.
- Simultaneously allocated source/restored owners at different host addresses; identical canonical bytes even after poisoning the source `f_odd` to null.
- Live-local mutation tests: deliberately dropping root `hi`, `lo`, or `c1cs` changes the expected observable result.
- Exactly-once message consumption, caller completion, JAL delay slots, and inert completed dispatch.
- Empty nonblocking receive, null destination, FIFO order, and ring wrap in the isolated adapter.
- Rejection of truncated/trailing payloads, bad magic/version/build identity, unknown function/continuation IDs, invalid frame chains/counts, blocked records, stack footprint, queue counts, FR, and rounding.
- Stable code IDs and schemas after reversing function discovery.

Independent review identified missing stack-footprint validation and insufficient operational FR coverage; both were fixed and included in the successful sanitizer run. No gameplay, renderer, audio, boot, runtime dependency, or application build target was modified.

## N64Recomp changes required for generalization

The existing custom-generator extension is enough for this narrow proof. Shipping support needs a first-class resumable backend and coordinated runtime changes:

1. **Typed instruction/call-site metadata.** Pass source PC, delay-slot phase, section identity, and call kind through the generator API. This prototype obtains JAL PC from the pinned disassembly-comment callback; it must not become a production ABI. Define distinct stable phases for duplicated emissions, branch-likely slots, and linked branches.
2. **Stable section/function manifest.** Assign reproducible identities from executable/overlay identity and guest offsets, independent of enumeration order or relocated load address. The prototype's single fixed section namespace is insufficient for overlays, mods, reference symbols, and replacement functions. Bind schema/code identities to the compatible generated build.
3. **Complete frame schemas and liveness.** Inventory every generated temporary across suspension points, including jump-table addends, conditional/delay-slot intermediates, hook state, and any helper-local values. Spill typed live values or conservatively retain them. Preserve the pinned generator's actual local HI/LO behavior rather than assuming `ctx.hi/lo` alone are sufficient.
4. **All control transfers.** Lower direct and indirect calls, lookups, cross-section references, tail calls, recursion, returns, hooks, and callbacks through the continuation dispatcher. Preserve the precise order of delay-slot side effects and resolve guest targets against the current overlay map. Ordinary calls into unconverted generated functions cannot cross a parking point. P1 explicitly rejects indirect/lookup/reference calls, jump-table generation, syscall/break, pause, and event emission.
5. **Complete HLE continuation ABI.** Convert every blocking HLE operation into semantic state transitions, covering receive/send/jam, scheduler checks before/after the operation, wake/recheck behavior, external message admission, blocked sender wakeups, thread stop/start/destruction, priorities, and queue ordering. This adapter supports a single receiver and a request queue that never fills; it cannot establish multi-thread scheduling fidelity.
6. **Runtime ownership.** Move each game/boot thread's context and generated frames out of `run_thread_function`'s native stack. Let workers carry an owned execution token and return at suspension; recreate workers independently of guest state. Move native pointers in OSThread storage into reconstructable side tables or explicitly normalize them during memory export. Preserve FR bindings and semantic FP mode on worker switches.
7. **Safepoints and validation.** Define legal capture phases beyond this one receive, bound frame depth and guest memory use, validate complete schemas and call edges, reject unsupported live state, and version the continuation ABI. Audit host helpers for callbacks that retain native activations and for native pointers written into guest memory.
8. **Differential coverage at scale.** Extend generated tests to loops, recursive/mutual calls, tail/indirect/overlay calls, branch-likely and delay-slot combinations, jump tables, FP mode changes, other HLE boundaries, and real scheduler contention. Then regenerate all game functions and compare against the unchanged baseline under identical external observations.

P1 does not address renderer capture, audio workers, VI barriers, F5/F8, `.sbks`, full-game import, or user-facing savestates. Those remain separate work.
