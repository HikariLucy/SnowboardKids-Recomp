# Native savestates for Snowboard Kids: Recompiled

Status: proposed for human approval; no implementation authorized or included.
Investigation date: 2026-09-23.

## Decision in brief

Native, restartable `.sbks` files are feasible, but **not as an RDRAM dump or a small VI callback patch**. The pinned runtime has no serializable execution continuation, no coordinated snapshot barrier, and no renderer state export/reset interface. These are prerequisites, not optional hardening.

Recommend explicit, serializable recompiled continuations plus a runtime-owned cooperative checkpoint coordinator. Capture at a precisely defined VI transaction boundary after all previously accepted work has completed. Store game-visible state in portable records; rebuild host machinery. The renderer must export semantic N64 graphics state independently of RT64/Vulkan objects.

There is also a requirements tension: the existing clock, asynchronous event order, live input and SDL audio depth are nondeterministic inputs to the game. Preserving their current behavior permits faithful restoration with identical *supplied external observations*, but does not guarantee that the same controller input alone produces identical future execution. A deterministic execution mode would change timing/audio feedback behavior and needs separate approval. Do not silently include that change or advertise controller-input-only determinism.

The first approval is for this architecture and its feasibility gates, not permission to implement it.

## Scope and inspected baseline

| Component | Inspected revision / integration |
|---|---|
| SnowboardKids-Recomp | `0b518d1fc318916ff90cd32c9858f5649f347d16`; initially clean tracked working tree |
| N64ModernRuntime | `6ccb2e7c2e7f6708257b461097e0aaf03c445e2a`, matching bootstrap pin |
| Runtime's N64Recomp | `81213c1831fab2521a6a5459c67b63437d67e253` |
| RT64 | `6a4166b2cfa952d931a08481d1037da995f28b54` |
| RecompFrontend | `e85b912d9df677b04f9358867dd010c8af27ea05` |
| Game | USA `snowboardkids.n64.us`; normalized ROM XXH3-64 `F384619787B78D4B`; expected SHA-1 `1583bacc9046a360df8ea4d536942155247e154c` |
| Port version supplied to runtime | `0.0.1-boot` |

The runtime checkout already differs from its commit in `ultramodern/src/threads.cpp`, `threadqueue.cpp`, and `include/ultramodern/config.hpp`. The first two contain the project `osStopThread` compatibility fix; the configuration header supplies presentation-mode compatibility. These pre-existing changes were not altered. Runtime identity must include the patch content, not just the upstream commit. The presentation enum modification is not in `patches/n64modernruntime-osstopthread.patch`; making the entire dependency patch set reproducible is a prerequisite for a trustworthy build fingerprint.

Current scope: native boot, unmodified USA game, generated `aspMain` audio microcode, no game mods (`mod_game_id` is empty), `SaveType::None`, player 1 reported connected, no Controller Pak. Do not infer SRAM/EEPROM/Pak support from generic runtime code. Their current absence is a compatibility constraint.

Preserve `PresentationMode::Console` by default, the existing `SBK_PRESENT_MODE` development override, graphics settings, frontend/window lifetime, current input bindings and controller policy, sample conversion and stereo handling, volume, audio latency adjustments, and the noncurrent-thread `osStopThread` fix. Loading does not run `start_game()` or the game entrypoint again.

## Evidence map

Paths below are relative to the project root. Symbol names identify the inspected code even when line numbers change.

| Evidence | Finding |
|---|---|
| `src/main/native_boot.cpp`: `on_vi`, `start_game_on_first_vi`, `update_gfx`, renderer/audio/input callbacks | First VI starts the game; subsequent callbacks update rumble. This is startup sequencing, not a general snapshot lock. SDL event processing belongs to RecompInput. |
| `.deps-runtime/N64ModernRuntime/librecomp/src/recomp.cpp`: `run_thread_function`, `wait_for_game_started`, `start` | Each game thread has a stack-local `recomp_context`; game startup also has a separate context. RDRAM mapping reserves 4 GiB, permits access to 512 MiB. |
| Runtime `N64Recomp/include/recomp.h`: `recomp_context`, `get_cop1_cs`, `set_cop1_cs`; `RecompiledFuncs/funcs_0.c` | Context has registers, no resumable PC/call stack. Generated functions use ordinary C calls, C labels and locals (`hi`, `lo`, `result`, `c1cs`). FP rounding is also host-thread state. |
| Runtime `ultramodern/include/ultramodern/ultra64.h`: `OSThread`; `ultramodern.hpp`: `UltraThreadContext` | Runtime's OSThread layout embeds a native pointer in RDRAM. UltraThreadContext contains a std::thread and two semaphores, not a saved N64 CPU context. |
| Runtime `ultramodern/src/{threads,scheduling,threadqueue,mesgqueue}.cpp` | Cooperative game scheduling suspends native stacks. Run-queue head lives outside RDRAM. External messages are in a host concurrent queue. |
| Runtime `ultramodern/src/timer.cpp` | Detached timer worker; active set and currently selected timer are local variables; actions queued separately; deadlines use host elapsed time. Worker writes recurring timer timestamps in RDRAM. |
| Runtime `ultramodern/src/events.cpp` | Separate VI, graphics and SP workers; local retrace countdown; double-buffered VI state; graphics tasks copied, audio tasks enqueued as pointers. Graphics SP notification precedes `send_dl`, DP follows its return. |
| Runtime `librecomp/src/{rsp,sp,ai,dp}.cpp`, `ultramodern/src/audio.cpp`, `rsp/aspMain.cpp` | Persistent audio DMEM; task-local RSP registers; no persistent audio IMEM implementation. AI status pretends immediate completion; AI length comes from SDL-backed callback. Independent `rdp_state` global. |
| Runtime `librecomp/src/{overlays,heap,pi}.cpp`, `librecomp/include/librecomp/addresses.hpp` | Overlay maps, section addresses, heap offset, ROM and save services outside basic RDRAM. Heap is initialized above 16 MiB even without mods. |
| Runtime `thirdparty/o1heap/o1heap/o1heap.c` | This pinned allocator uses 32-bit fragment offsets, not host pointers, in its arena metadata. Do not apply assumptions from a different O1Heap revision. |
| Runtime `ultramodern/src/{input,extensions}.cpp` | Controller channel count and display-list event registrations outside RDRAM. SI completions use external-message delivery. |
| `.deps-renderer/RecompFrontend/recompui/src/renderer/rt64_render_context.cpp` | Separate graphics DMEM/IMEM and MI/DPC registers. `send_dl` resets HLE RSP and loads microcode, but does not reset all RDP state. |
| `.deps-renderer/rt64/src/hle/{rt64_application,rt64_state,rt64_rdp,rt64_rsp,rt64_workload_queue,rt64_present_queue}.{h,cpp}` | Persistent TMEM/RDP state, framebuffer managers, async workload/present queues and optional render-to-RAM. Queue idle methods are not a complete snapshot protocol. |
| Frontend `recompinput/src/input_events.cpp`, `recompui/src/composites/ui_prompt.cpp` | Event loop lacks a public application hotkey callback. `open_notification` is a prompt without buttons, not an automatically expiring toast. |

This was source inspection, not a runtime determinism experiment. Renderer semantic export coverage and resumable code generation remain feasibility gates; no existing support for either is claimed.

## Approaches considered

| Approach | Assessment |
|---|---|
| Explicit execution continuations and semantic state | Recommended. Restartable files without native addresses; supports validation and future schema evolution. Requires N64Recomp changes as well as runtime and frontend/renderer integration. |
| Native thread/fiber stack images or process checkpoint | Reject for `.sbks`. Stack addresses, return addresses, compiler ABI, TLS, locks, renderer/device resources and ASLR make this process-specific. Fibers alone do not make execution portable or serializable. |
| Replay from boot using a recorded event/input log | Possible research fallback, not the requested native savestate. Requires recording time, audio observations, event order and input, can have unbounded load time, and inherits existing nondeterminism. Game-specific restart-at-loop patches are similarly insufficient unless every continuation is proven reconstructable. |

## State classification

Classes: **1 — mandatory semantic data to serialize**, **2 — reconstruct from saved data or the exact compatible build**, **3 — transient host state; never serialize its object representation**. A component can contain all three; the rows deliberately distinguish them. Data already in the memory payload is not serialized twice except as validation metadata. Unsupported nonempty state must reject a checkpoint, not be silently dropped.

### Memory, execution and scheduler

| State | Class | Representation / load rule |
|---|---|---|
| Game RDRAM: globals, BSS, stacks, heap, RNG seeds, scheduler structures, display/audio lists and samples | 1 | All bytes in 0–8 MiB, including unused bytes; normalize runtime pointer fields as described below. |
| Extended writable runtime memory | 1 | Cover the entire 512 MiB logical span, including PI handles near `0x80800000`, patch area, heap metadata/payload from `0x81000000`, and any other nonzero pages. Sparse zero pages are allowed; omitted pages must become zero on load. Do not serialize the inaccessible remainder of the 4 GiB reservation. |
| RDRAM host base and mapping/protection handles | 3 | Allocate/bind using the existing runtime; guest addresses remain guest addresses. Never require the save process's host base. |
| Every live CPU context, including suspended/stopped threads and any live boot/start context | 1 | GPRs r0–r31, raw FPR bits f0–f31, HI/LO, status and FR mode, effective FP rounding, explicit continuation records. Save exact generated local state where it differs from architectural registers. |
| `ctx.f_odd` | 2 | Recompute from FR mode and the address of the restored context's FPR storage; never copy pointer bits. |
| Native return addresses, C stack frames, TLS storage, fenv object | 3 | Replace execution dependence with class-1 continuations; install the saved semantic FP mode on dispatch. No memcpy of stacks, `jmp_buf`, `ucontext_t` or `fenv_t`. |
| OSThread `next`, `priority`, `queue`, `flags`, `state`, `id`, guest `sp` | 1 | Memory payload plus thread registry cross-check. Track all created, not-started, stopped and blocked threads; queue membership alone misses stopped threads. |
| OSThread native `context` field | 2 | Zero in serialized RDRAM, regenerate from a guest-address + lifetime-generation registry. Null/destroyed status is semantic data. Never trust file-supplied native pointers. |
| Thread creation entrypoint, argument, lifetime generation, started/terminated status, blocked operation | 1 | Registry fields; entrypoint and continuation use stable code IDs/guest addresses. Host `_thread_func` arguments currently retain information not recoverable from OSThread alone. |
| `running_queue_impl`, selected/current thread, priority ordering and equal-priority tie order | 1 | Save run head and exact ordered membership, active guest thread ID and scheduling phase. `running_queue == -1` is a tagged sentinel, not a dereferenceable pointer. |
| Queue links inside RDRAM | 1 | Preserve exact order. Do not rebuild by sorting priorities; equal-priority insertion order affects execution. Existing state flags do not always fully describe actual queue/blocking state. |
| TLS `thread_self`, `is_game_thread`, `is_main_thread` | 2 | Derive from restored execution ownership; never treat arbitrary worker threads as game threads. |
| `UltraThreadContext`, std::threads, semaphores, cleaner queue and join state | 3 | Recreate empty host machinery. Retire old workers completely before replacing memory; old destructors must not touch restored OSThreads or schedule another thread. |

The RDRAM encoding for v1 is explicit 32-bit word-swapped runtime bytes, declared in the header and accepted only on the supported little-endian ABI. Named integer records are little-endian. This avoids incorrectly re-swapping runtime C structures containing subword fields. Cross-endian portability is not promised. Native-pointer slots are sanitized using the thread registry, including retired slots not overwritten by guest data; preferably the future runtime moves ownership entirely to a side table while preserving guest structure offsets. Never find pointers by scanning for address-looking bit patterns.

### Messaging, devices and time

| State | Class | Representation / load rule |
|---|---|---|
| OSMesgQueue receive/send wait heads, `validCount`, `first`, `msgCount`, guest buffer pointer, all ring slots | 1 | In RDRAM. Validate count/index/range and linked-list consistency. Preserve guest message values verbatim. |
| Blocked send/jam/recv continuation | 1 | Queue address, message/destination, flags, jam bit, progress phase and guest return continuation. A message queue does not contain a blocked sender's pending value or receiver's return destination. |
| External messages not delivered to guest queues | 1 | Ordered records `{sequence, source, mq, msg, jam, requeue_if_blocked}`. Includes PI/SI, timers, SP/DP, VI/AI and extension events. Queue-full retry/drop semantics must remain as today. |
| Concurrent queue storage/tokens and message mutex | 3 | Rebuild queues from records. Add a sequenced admission point; the existing multi-producer queue is not a serializable total-order log. |
| Event registrations SP, DP, AI, SI; VI registration in each VI state | 1 | Guest queue addresses and message values, not host pointers. |
| Timer structs | 1 | Guest address, interval, timestamp, mq/msg already in memory. Store host-side membership too. |
| Timer active set, currently selected timer, unapplied add/remove actions | 1 | Freeze worker at an acknowledged transaction boundary; fold all accepted actions into a canonical active set and reinsert the selected timer. Then action queue/in-flight transaction is empty by invariant. Preserve tie order by guest timer address for equal deadlines. |
| Timer set container, worker thread, wait operation | 3 | Rebuild active set/wait from canonical timer records. Replace the detached worker with a cooperatively parkable lifecycle. |
| Logical count/time epoch, `ostime_offset`, fractional tick conversion phase, timer deadlines, VI deadline phase | 1 | Save elapsed logical ticks and relative scheduling phase, including osGetCount wrap behavior. Do not store `high_resolution_clock::time_point` bytes. |
| Host clock origin (`start_time`) and sleep deadlines | 2 | Rebase to the host's current monotonic elapsed source on release. Time spent saving/loading must not cause timer or VI catch-up bursts. Preserve separate osGetTime offset semantics. |
| VI `states[2]`, `cur_state`, `field`, current/pending framebuffer, state/control flags and retrace counts | 1 | Save both states; do not collapse current and next. Encode mode pointers as guest offsets or the tagged immutable dummy-mode ID. |
| VI `regs`, `update_screen_regs`, `total_vis`, local `remaining_retraces` | 1 | Save both register snapshots and phase. They describe different points in presentation; neither can safely be inferred from the other. |
| VI mode host pointers | 2 | Rebind into restored RDRAM or known constant dummy mode. Startup dummy odd/even toggle is irrelevant to running-game saves; v1 rejects preboot saves. |
| Pending screen updates / accepted task actions | 2 | Empty after the canonical drain; reconstruct future work from restored execution. Their effects and completion messages must already be captured. If any accepted action cannot drain, fail checkpoint. |
| `rdp_state` from librecomp `dp.cpp` | 1 | Independent game-visible status bits; not the renderer's DPC globals. |
| PI ROM DMA | 2 | Synchronous memory transfer is finished before safepoint; preserve any not-yet-delivered completion as class 1. No fictional pending DMA engine to snapshot. |
| ROM bytes and function code | 2 | Load existing validated ROM/build; compare hashes. Never embed ROM or machine code in `.sbks`. |
| `max_controllers`, game-facing input observations and pending read/query status | 1 | Channel count plus semantic controller results at a captured read phase. Existing frontend polls live state; add an explicit observation record so a paused read does not accidentally sample the future. |
| SDL keyboard/controller objects, instance IDs, mouse/UI events, device handles | 3 | Preserve current host connections and profiles; resample only at the next normal game poll. Clear only operation-specific hotkey latches, never purge unrelated events globally. |
| Rumble intent | 1 | Semantic on/off per guest controller if enabled. Currently no Pak is advertised; retain that policy. Stop obsolete host vibration during load; reapply supported saved intent afterwards. |
| Rumble envelope, device update timestamps, input-latency measurements | 3 | Restart host output/measurement; do not restore hardware handles or old wall times. |

### RSP, audio and graphics

| State | Class | Representation / load rule |
|---|---|---|
| Audio RSP global `dmem[0x1000]` | 1 | All 4096 bytes. `run_task` reloads 0–0xF7F and OSTask at 0xFC0; do not assume the gap or residual bytes are irrelevant. |
| Audio RSP IMEM | 2 | No persistent IMEM array in this execution path. `aspMain` is compiled code selected by task type; bind the exact microcode/code-generation identity. ROM/RDRAM carries source task data. |
| Audio RSP GPR/vector registers, accumulators, flags, reciprocal pipeline, DMA locals, jump target and PC | 2 | `aspMain` initializes local registers and `RSP rsp{}` on each invocation. At the required completed-task boundary none is live. A mid-task save would require all of these and a continuation; v1 forbids it. |
| Reciprocal/inverse-square-root tables | 2 | Recompute with `constants_init()` from the compatible runtime. |
| OSTask descriptors, audio command lists and microcode data/yield buffers | 1 | Guest memory plus persistent DMEM. Pending host task queue and active task must be empty. Existing `osSpTaskYield` is ignored; do not invent yielded-task support. |
| RSP callbacks and compiled microcode function pointer | 3 | Register from current compatible build. Record identity, never executable addresses. |
| N64 audio engine voices, sequence position, envelopes, reverb/ADPCM history, command and sample buffers, audio-thread stack | 1 | Game-managed RDRAM plus thread continuation and audio DMEM. Resetting SDL must not reset these. |
| Runtime `sample_rate` and AI event state | 1 | Restore frequency before any new samples. Preserve AI-on-VI event model and immediate-status behavior. |
| Logical queued-audio ledger: submitted PCM, remaining frames, consumption fraction and source rate | 1 | New application-owned semantic ledger required for faithful restore. RDRAM buffers can be reused after submission; current SDL bytes cannot be recovered from them later. Snapshot the ledger at the audio barrier. |
| Game-visible `osAiGetLength` observation / adjustment policy | 1 / 2 | Capture residual accounting as class 1. Reconstruct unchanged runtime 0.5-frame adjustment and port 1.0-frame adjustment from the exact build. Raw SDL queue length is not a portable N64 state. See determinism limitation below. |
| Four stereo input-frame history in `duplicated_sample_buffer` | 1 | Save eight raw float values or their exact equivalent pre-conversion samples. This is application resampling history, not an SDL object. Preserve for audio continuity. |
| SDL_AudioCVT, its buf pointer, swap vector allocation, device ID/spec objects, SDL queued output bytes | 3 | Clear/rebuild; feed restored semantic PCM through the same conversion path. Queue contents already delivered to hardware cannot be undone. |
| `discarded_output_frames`, output conversion description | 2 | Derive from restored source frequency and current negotiated host device settings; preserve existing conversion formula/channel ordering/gain. |
| Frontend graphics `DMEM[4096]`, `IMEM[4096]`, MI_INTR and DPC registers | 1 | Separate bank from audio DMEM. Save conservatively as named memory/register records. They are mutable inputs to RT64 Core even if mostly dummy in this HLE path. |
| RT64 HLE RSP state reset on every `send_dl` | 2 | Reinitialize through the existing reset/load-ucode path before next task; verify all fields read by that path are covered. Persist any additional inter-task semantic field found by that audit. |
| Persistent RDP semantic state | 1 | TMEM, texture-image descriptor, tile descriptors, color/depth image addresses/formats/width, other mode, combiner, color/LOD/depth/fill/scissor stacks and indices, convert coefficients and key parameters. Export named scalar/array records, never RDP object memory. |
| RDP partial commands and triangle parser scratch | 2 | Empty at a complete display-list/command boundary. Reject if pending-command byte counts or unsubmitted triangles remain; do not discard them. |
| GPU-only color/depth framebuffer content that survives between tasks or is later sampled/scanned out | 1 | Canonical pixel/depth planes with guest address, format, dimensions, validity/ownership and overlap metadata. Preserve any coverage/precision information needed for equivalent subsequent rendering. Not automatically present in RDRAM. |
| Framebuffer managers, GPU images, texture descriptors, command buffers, fences, swapchain, Vulkan/Plume objects | 3 | Drain and reconstruct from memory and semantic graphics planes; never binary-serialize. Preserve CPU RDRAM and GPU authoritative planes separately when they differ. |
| Interpreter dispatch tables, microcode cache entries, texture/pipeline caches, target maps | 2 | Rebuild from exact build, task identity, restored memory and semantic graphics state. Pointer/iterator/container representations are class 3. |
| Present/workload queues, interpolation/matching history, temporal host frame IDs, profilers | 3 | Finish accepted work, invalidate old history, seed a fresh presentation epoch. These are not guest time counters. No old frame may present after load completes. |

The graphics semantic list above is the required export contract, not evidence of an existing exporter. RT64 also tracks replacement-texture metadata and extended state. V1 supports the current vanilla task path only; nondefault extensions/replacements must either receive explicit schemas or cause a compatibility rejection. Removing a field from the mandatory contract requires proving it is overwritten before any subsequent read. Full renderer output bit identity across GPUs, precision settings or temporal interpolation histories is outside v1; such configurations cannot claim exact visual continuation without additional canonical data.

Do not force render-to-RAM to make capture easier. The inspected `State::fullSync()` makes RAM writeback conditional, so unconditional readback into live game memory would change normal behavior. Export GPU-authoritative content to the snapshot, separately from the CPU-visible memory image; restore equivalent ownership into new targets. A cold renderer and “wait for the next frame” alone are insufficient for menus, framebuffer reuse or effects reading previous targets.

### Other globals and compatibility state

| State | Class | Representation / load rule |
|---|---|---|
| Loaded overlay records and section load addresses | 1 | Stable section identity + guest RAM location, ordered as required. Cannot infer current function dispatch solely from RDRAM bytes. |
| Function maps, export tables, patch symbol pointers, ROM-to-section maps | 2 | Rebuild from serialized loaded-section state and compatible static tables. Do not call boot initialization that overwrites restored RAM. |
| `heap_offset` | 1 | Save and validate; arena contents are in extended RDRAM. Do not call `init_heap` on restored memory. |
| Display-list extension `pending_events` | 1 | Ordered `{mq, mesg, displaylist, event_type}` records, including registrations for future lists. Mutex class 3. |
| Current game/mode, save type, execution lifecycle | 1 | Header/runtime records; v1 accepts only the supported running game and normal mode. |
| Game registries, callbacks, config paths, `valid_game_roms`, immutable patch bytes | 2 | Reinitialize from current program/validated files. |
| `game_status` atomics, shutdown flags/semaphores, once-only startup flags | 2 / 3 | Set semantic running state after successful restore; host startup remains completed. Never restore a quit flag or repeat first-VI initialization. |
| Cartridge/Pak backing data | 2 for current port | Reconstruct absent devices and empty save buffer. No save-file rewind or disk write on load. If support is added, device data/protocol state becomes class 1 with an explicit persistence policy and new compatibility version. |
| Mod loader allocations, live-recompiled code, hooks and mutable mod globals | Unsupported in v1 | Require empty game-mod manifest; reject otherwise. Future mods need declared serializers and identity. Merely hashing mod files does not serialize mod globals. |
| Graphics/audio/input preferences, menu state, window dimensions, frontend theme, notification objects | 3 | Keep current host settings/UI. Compatibility metadata can record behavior-affecting values without applying preferences from the file. |
| Display refresh-rate/resolution-scale feedback | 2 | Re-query renderer. If consumed by game/mod logic, treat observations as external input; exact replay must record them. Vanilla profile must be checked for such calls. |
| Diagnostics such as `dump_frame`, crash handler and log/profiler state | 3 | Do not rewind host diagnostics or propagate debug one-shot requests from a state file. |

## Serializable execution is a prerequisite

Neither `ctx.r31` nor the N64 stack reconstructs the generated C call stack. Generated functions can retain locals across calls; runtime blocking operations suspend inside C++ loops. Restoring the same register set into a newly started thread would restart its entrypoint or return to the wrong call site.

The proposed N64Recomp extension emits resumable basic-block/call continuations. Each thread owns a vector of explicit frames with stable function/overlay identity, continuation label and all live generated locals. Registers live in owned storage, not a stack-local `recomp_context`. Calls push frames; returns pop frames. Safe points are inserted at bounded basic-block backedges/calls so a thread that does not call libultra can still park. Safepoints occur after complete guest instruction/delay-slot semantics, never between a branch and its delay slot unless that phase is explicitly represented.

HLE runtime operations become explicit states: ready, blocked send, blocked receive, stopped, awaiting external message, etc. Each operation must record whether side effects happened and where its return value will be written. For example, resuming after `osSendMesg` scheduled another thread must not send the same message twice. A stopped audio thread must retain its prior blocked-operation phase across stop/start. Preserve priority/tie behavior and the existing stop/remove fix.

There must be one owner of the game execution token. A worker can release it only after publishing its context/continuation and only acquire it after the scheduler selects it. Thread creation/destruction and boot execution participate in the same protocol. Avoid asynchronous thread suspension and avoid restoring into sleeping old C stacks.

Required proof before implementing file UX: suspend in nested generated calls and every blocking HLE operation, then resume from serialized records in a fresh process with different addresses. Compare guest state to uninterrupted execution. This requires project-owned changes to the **N64Recomp toolchain**, beyond N64ModernRuntime patches. A runtime-only implementation cannot meet the requested contract.

## VI transaction and snapshot barrier

Define boundary `B(n)` as: VI transaction n has performed its existing screen-update enqueue, mode/register swap, retrace accounting, VI/AI event admission and port callback; no operation belonging to VI n+1 has started. Game threads are parked at explicit continuations; all previously accepted SP/graphics/audio work is completed; timer actions are canonical; pending guest events are retained but not newly consumed. This is a logical VI boundary, not a guarantee that the GPU was idle at the instant the host retrace timer fired.

Proposed phases: `Idle → Requested → ParkGame → CloseVI → DrainDevices → Frozen → Capture/Commit → Resume`. A request has an operation ID and runtime generation. Calls mentioned here are proposed interfaces, not functions already exposed by the runtime.

1. F5/F8 queues an operation; it never reads/writes guest memory. Load I/O, parsing, checksums and most compatibility validation occur in staging while play continues.
2. The coordinator asks the active game execution owner to park at a safe point. Blocked/stopped threads are already stable explicit records. Do not hold message/renderer/audio locks while waiting for acknowledgements. Finish any HLE critical transaction already entered. A watchdog fails the operation if parking cannot complete; never force-suspend a worker.
3. Finish the current VI iteration and park its worker before the next iteration. If necessary take the next complete VI after the CPU acknowledgement. Continue current VI/AI notification behavior up to that boundary. Freeze logical time at this cut; no save/load wall time is charged to the game.
4. Close new guest task admission. Timer worker completes its selected transaction, folds queued add/remove actions, records any expiry already committed and parks. Input observations/rumble updates and audio producers acknowledge their barriers. Threads blocked on empty queues must be wakeable by control messages that do not masquerade as game events.
5. Drain all accepted graphics actions and audio RSP tasks, keeping their normal SP/DP and extension completion semantics. The game stays parked; completions enter the saved external-event ledger, not directly executed game code. Keep dependent workload/present workers running until they have satisfied each other's dependencies. Never pause a present worker while a workload worker is waiting for it.
6. Flush renderer CPU batches/partial workloads without emitting extra guest events, then wait for terminal workload/present IDs, uploads, readbacks and GPU fences. `send_dl` return, DP notification, `waitForIdle`, and a single Vulkan idle wait are individually insufficient. The renderer adapter must acknowledge that no worker can read/write the old memory or present an old frame. Snapshot semantic graphics state only after that acknowledgement.
7. Pause SDL playback and serialize the application-owned audio ledger/history only after game audio producers and the RSP have parked. A source-buffer copy, conversion and queue submission must be one acknowledged transaction. Drain any final admitted external events into the ordered ledger; do not force-deliver them into full guest queues.
8. Coordinator now owns all snapshot-relevant state. Assert zero active game execution, zero pending/active SP tasks, empty graphics action queue, no timer/audio mutation and renderer idle. Preserve pending external events and extension registrations. Snapshot into owned staging buffers.
9. For save: release the barrier and write immutable staged data asynchronously. Rebase clock deadlines so capture latency produces no catch-up. Resume original SDL queue without clearing it. Publish success only after atomic file replacement completes.
10. For load: follow the commit protocol below while still frozen, then release exactly the saved runnable owner/scheduler state. Pending VI n events remain pending; next new VI is n+1. Never generate an extra VI/AI/SP/DP/SI completion to “wake things up.”

Queue draining requires an admission lock plus explicit accepted/completed sequence watermarks. A FIFO marker from one producer in a multi-producer queue is not sufficient proof that every prior producer's work finished. New callbacks include a runtime generation so late completions from an obsolete load generation are discarded before touching memory or emitting events.

Timeout before Frozen: cancel the request, reopen admission and resume the existing timeline. Timeout/device loss while the GPU is still using memory: do not overwrite memory or destroy live resources; report the error and keep the load uncommitted. No busy-wait with locks held. Keep the main SDL event pump responsive, but defer graphics configuration/rebinding operations that would mutate participating state until release.

## Transactional load and host reconstruction

1. Validate the whole file into bounded staging memory: identity, every section checksum, counts, address ranges, queue/thread/continuation consistency, and required capabilities. Reject unsupported files before parking where possible. Preallocate context/event/graphics import resources before mutation.
2. Enter the same barrier on the current timeline. Retain a rollback semantic snapshot until imported host resources are ready. Increment generation only at the commit stage; retire all old callbacks/workers safely.
3. Clear the old SDL output queue while paused. Reconstruct or reset renderer state only after old work and UI GPU use have stopped. Do not call global `SDL_Quit`, destroy the user's window, or reinitialize game frontend/config/input profiles.
4. Replace memory (including zeroing omitted sparse pages), rebuild overlay dispatch and heap binding, install CPU/continuation records, fix host pointers, restore scheduler/queues, timer membership/time, VI/event registers, DMEM/device state and pending events. Do not call `osCreateThread`, `osCreateMesgQueue`, `osSetTimer` or boot routines in ways that repeat game side effects.
5. Import renderer semantic state and framebuffer planes into fresh targets, invalidate old texture/FB address caches and interpolation history, rebind Core pointers to restored memory/registers, restore VI scanout state. Keep existing presentation configuration; in normal operation it remains Console. Present the restored framebuffer after import without replaying old display lists or advancing guest time.
6. Rebuild SDL conversion for the saved source rate and current output device. Restore four-frame conversion history and logical PCM backlog, queue regenerated output once, then unpause at release. Rebuild backlog in original buffer order with recorded consumption offsets and history; if exact conversion boundaries cannot be reproduced, reject exact-audio capability rather than pretending a queue-depth scalar suffices.
7. Restore pending game-facing input observations; subsequent polls use current devices. Stop obsolete rumble and apply saved intent under the unchanged controller/Pak policy. Leave unrelated frontend events/settings intact.
8. Rebase wall deadlines, activate restored scheduler and admit new events; only now announce “Quick state loaded.” Release rollback state after success.

On import failure, restore the rollback state under the same barrier and resume only if reconstruction succeeds. If device loss prevents recovery, remain paused with an error and offer normal exit/restart; never run a half-restored game. Validate IDs against generated tables rather than jumping to file-provided addresses. A malformed state is data, never executable code.

### Renderer integration contract

Extend the runtime's `RendererContext` with capability discovery and proposed operations equivalent to `quiesce`, `export_n64_state`, `reset_for_load`, `import_n64_state`, `resume`. Keep format-specific encoding outside RT64. Implement adapters in project-owned RecompFrontend/RT64 patches or a maintained project adapter; the runtime must not reach into RT64 internals.

Current `RT64Context::shutdown()` calls `Application::end()`, a full destruction path, not a savestate reset. Current `State::reset()` does not invalidate every GPU cache or persist framebuffer data. Reusing either alone is unsafe. Prefer a dedicated reset/import path retaining the window, device and frontend resources. Full RT64 application reconstruction is a fallback only after proving render hooks/UI resources detach and reattach correctly; it still needs semantic state export/import first.

For v1, retain current graphics settings and reject unsupported exact-restoration combinations. Console buffering order and both current/next framebuffers remain part of the restoration contract. Do not call `enable_instant_present()` as part of saving, flushing or loading.

### SDL verification

Context7 was used to resolve the official SDL wiki, but its query returned SDL3 APIs despite the SDL2 query. Those results were not applied to this SDL2 port. Official SDL2 documentation confirms that `SDL_ClearQueuedAudio` empties pending output but cannot retract audio already passed to hardware, and handles its own locking. `SDL_PauseAudioDevice(dev, pause_on)` controls playback processing; neither call stops the application's RSP/game producers. Hence the separate producer barrier above. Sources: [SDL_ClearQueuedAudio](https://wiki.libsdl.org/SDL2/SDL_ClearQueuedAudio), [SDL_PauseAudioDevice](https://wiki.libsdl.org/SDL2/SDL_PauseAudioDevice).

## Determinism contract and unresolved requirement conflict

The proposed state must be complete enough that resuming twice with the same sequence of external observations yields the same guest memory, continuations, event order and generated PCM. Verification hashes exclude class-3 objects and normalized host pointers. It must also restore in a new process; same-session-only checkpoints are not accepted.

However, today's runtime derives osGetCount/osGetTime, timer expiry, VI catch-up and `osAiGetLength` from host time/audio progress, and lets host producers race to submit events. Reconstructing the same save cannot guarantee identical future values from those sources. Flushing SDL without a restored logical audio backlog immediately changes an input used by the game. Saving one length value is insufficient for subsequent calls.

Recommended preservation-first v1 keeps current live behavior, adds clock pause/rebase and semantic audio bookkeeping, and makes no stronger claim than the contract above. For the deterministic test harness, inject the same recorded clock reads, timer/event admissions, audio-consumption observations and controller results at the same execution points. This tests completeness without changing the default gameplay mode.

If the desired product guarantee is “same controller inputs always produce identical execution,” additionally require a deterministic logical clock, defined external-event ordering/completion points, and AI consumption paced by that clock rather than SDL queue depth. Preserve sample generation/conversion and Console presentation, but acknowledge that scheduling and audio feedback semantics change. That extension is a separate approval decision and cannot honestly be represented as preserving all current behavior. Snapshot support must not silently enable it.

For that optional mode, use a versioned integer logical-tick policy advanced by generated guest execution, with idle fast-forward to the next scheduled event. Give VI a rational 60 Hz deadline, timers absolute logical deadlines and address-based ties, and AI an integer sample-consumption accumulator. Assign task-completion visibility at defined scheduler transitions independent of host worker finish time; wait for the worker if necessary without advancing guest time. Order equal-tick event sources by a documented fixed order and per-source sequence. Sample supplied controller input at logical poll points. Save tick/remainder accumulators, next deadlines and all sequence counters. This is deterministic scheduling of the existing HLE device model, not cycle-accurate N64 emulation. Its tick-cost table and event ordering become part of the state ABI; they must be chosen and validated in a separate timing design before claiming the stronger guarantee.

## `.sbks` v1 format

Use a chunked, explicitly encoded container. No native structs, `size_t`, C++ enum layout, host pointers or implicit compiler padding on disk. Integers are little-endian; floating values are explicit IEEE-754 bit patterns. The header records memory byte encoding and continuation ABI. V1 initially supports the current little-endian execution profile and exact compatible build, not arbitrary ports or future binaries.

### Fixed prefix (144 bytes)

| Offset | Field | Encoding |
|---|---|---|
| 0 | magic | 8 bytes `53 42 4B 53 0D 0A 1A 0A` (`SBKS` + marker) |
| 8 | major, minor | u16 each; initially 1, 0 |
| 12 | endian tag | u32 `0x01020304` |
| 16 | header_bytes | u32; fixed prefix + metadata + chunk directory, padded to 8 bytes |
| 20 | flags | u32; known capability/compression flags only |
| 24 | file_bytes | u64 exact final length |
| 32 | total_stored_payload_bytes | u64 |
| 40 | total_raw_payload_bytes | u64 |
| 48 | timestamp_utc_ns | signed i64; metadata only, never a game clock |
| 56 | saved_vi_index | u64 |
| 64 | slot_id | u32; 0 = quick slot |
| 68 | chunk_count | u32 |
| 72 | normalized_rom_bytes | u64 |
| 80 | normalized_rom_xxh3_64 | u64 |
| 88 | normalized_rom_sha256 | 32 raw bytes |
| 120 | metadata_bytes | u32 |
| 124 | directory_entry_bytes | u32; 48 for v1 |
| 128 | header_xxh3_64 | u64, over entire header with this field zero |
| 136 | reserved | u64 zero |

Metadata uses length-delimited TLV records (u16 type, u16 flags with required bit, u32 byte length, bytes, zero padding to 8-byte alignment). Required metadata: game ID; port semver and source revision; runtime upstream revision; SHA-256 of ordered runtime patch set; N64Recomp revision and continuation-schema digest; generated CPU/RSP code digest; frontend/RT64 revisions and patch digests; runtime state ABI identifier; build compatibility digest; memory extent/encoding; supported-device and graphics capability profile; empty mod manifest digest. Slot label, user description, originating platform and known ROM SHA-1 are optional. V1 quick label is “Quick save.” Record a generation UUID for diagnostics without using it as a host pointer.

The build compatibility digest covers all behavior-affecting patches, generated code, toolchain/FP options and serialization schemas. Semver is descriptive, not proof of compatibility. Hash the normalized big-endian ROM bytes as validated by the runtime; compute SHA-256 when loading the ROM. Keep the known SHA-1 as a secondary baseline identity, not the sole integrity check. No ROM filename or path is an identity.

Each 48-byte directory entry: FourCC u32; schema version u16; required flags u16; codec u32; reserved u32; offset u64; stored bytes u64; raw bytes u64; raw payload XXH3-64 u64. Chunks are 8-byte aligned, ordered and nonoverlapping. V1 uses codec 0 (uncompressed) with sparse memory pages; additional compression codecs require explicit support and bounded decoding. Last required `HASH` chunk contains SHA-256 over all preceding file bytes including header, directory and padding; its directory checksum is defined as zero to avoid a circular dependency. It is always last and 32 bytes. This detects corruption, not malicious tampering/authentication.

### Required chunks

| FourCC | Contents |
|---|---|
| `MEM0` | 512 MiB logical memory extent, 4096-byte page size, sorted nonzero page index/data records; omitted pages defined as zero; pointer fields sanitized |
| `THRD` | Thread registry, CPU registers, generated continuation frames, FP mode, HLE blocked-operation records |
| `SCHD` | Running head/owner and ordered queue validation records; exact scheduling phase |
| `EVNT` | Event registrations and ordered undelivered external events, sequence counters and extension events |
| `TIME` | Logical clock, offsets, VI/timer phase, active timer addresses and deadlines |
| `VI00` | Both VI states, mode references, both register snapshots, retrace/field state |
| `RSP0` | Audio DMEM, completed-task invariant and microcode identity |
| `AUD0` | Source rate, logical PCM backlog, consumed offsets/phase and four-frame history |
| `DEV0` | Controller channel count/observations, rumble intent, logical RDP status and absent-device profile |
| `OVLY` | Loaded section identities/addresses, heap offset and compatible runtime semantic globals |
| `GFX0` | Graphics DMEM/IMEM and registers, persistent semantic RDP state, canonical framebuffer planes/ownership |
| `HASH` | File SHA-256 as defined above |

An optional thumbnail chunk may be added later and is never necessary to load. OSMesgQueue, OSThread guest fields and timer structs reside in `MEM0`; other chunks must agree with them and cannot override discrepancies silently.

Proposed v1 parser limits: exactly 512 MiB expanded `MEM0`, maximum 131072 pages, no duplicates, at most 64 chunks, 64 KiB metadata, 4096 bytes per string, 256 thread records, 4096 continuation frames per thread, 64 MiB total continuation data, 1048576 pending event records, 65536 timers, 64 MiB audio ledger and 256 MiB graphics semantic data. Cap both file size and total expanded payload at 1 GiB; use checked integer arithmetic. These are explicit product limits to validate against stress captures, not claims about existing runtime bounds. Allocation sizes never come unchecked from the file. Reject any save exceeding a limit without modifying the previous slot. Unknown required TLVs/chunks, incompatible major versions, unsupported codecs or state ABIs are hard errors. Optional unknown chunks may be skipped after structural validation.

The conservative 512 MiB memory scan and renderer readback may cause a noticeable pause. Measure capture duration and memory overhead before shipping; staging and rollback must be budgeted together. Sparse encoding saves file space, not scan time. A later dirty-page optimization is valid only if it observes every writer, including generated stores, timer updates, synchronous DMA, RSP and renderer writes. Never optimize to an assumed 8 MiB capture without proving all extended regions reconstructable.

Store `runtime-data/savestates/snowboardkids.n64.us/quick.sbks`, separate from cartridge saves. Write a unique temporary file in the same directory, complete checksums, flush file contents, atomically replace the slot, and flush directory metadata where supported. Retain the previous valid quick file on any failure; remove abandoned temporary files on a later startup. Never report success on staging alone. Slots later use stable slot IDs and independent files; no format redesign is needed.

## Initial UX

F5 requests quick save; F8 requests quick load. Use key-down edges, ignore repeats and require release before another request. Disable during boot/shutdown, unsupported profiles, input-binding capture, text entry or an unrelated modal prompt. Do not divert unrelated keys, change controller bindings or add a second SDL event consumer.

Add a small application hotkey callback to the existing RecompInput event dispatch (or an equivalent project-owned adapter at that single dispatch point). The inspected public header only exposes `handle_events`; `native_boot.cpp` currently delegates all SDL polling there. A second `SDL_PollEvent` loop could consume frontend/controller events and is prohibited. Accepted F5/F8 actions are reserved only in gameplay context; expose conflicts with existing user bindings rather than silently remapping them.

Only one operation is active. Repeated requests receive “Savestate operation already in progress”; do not accumulate a load/save queue with surprising effects. If simultaneous edges occur, process one in event order and reject the second as busy. No confirmation dialog for normal quick save/load; F8 deliberately replaces current play state.

Frontend feedback, dispatched through an application/UI message queue on the frontend's supported execution context:

- “Saving quick state…” while capturing/writing; “Quick state saved” only after durable replacement.
- “Loading quick state…” after validation; “Quick state loaded” after restoration and release.
- “No quick state found,” “State belongs to another ROM/build,” “State file is damaged,” “Could not reach a safe checkpoint,” or concise I/O/device error.

Use RecompFrontend notifications. The existing `open_notification` creates a buttonless prompt and requires `close_prompt`; it is not an auto-dismiss toast. A small noncapturing status component in RecompFrontend is preferred to preserve gameplay input. If the prompt API is used initially, prove that it does not change input capture, track notification ownership and auto-close only that notification after a short host-time duration. Never close an unrelated prompt. UI calls must not execute under scheduler/audio/renderer locks or from the file writer. Multiple numbered/named slots and thumbnails are later UX, not v1 implementation scope.

## Ownership of future changes

| Owner | Required work |
|---|---|
| SnowboardKids-Recomp | Savestate service/coordinator client, file schemas/identity/checksums/atomic persistence, F5/F8 wiring, notification lifecycle, SDL audio ledger/history/reset adapter, game profile and compatibility checks, source/generated-code fingerprints, tests and packaging. Preserve all current port callbacks and PresentationMode selection. |
| Project-owned N64ModernRuntime patches | Explicit thread registry/context ownership and resumable HLE scheduling; cooperative VI/timer/SP/graphics/event barriers; sequenced external events; clock snapshot/rebase; named state export/import for queues, timers, VI, audio rate, input/device globals, extensions, overlays and heap; generation-safe worker lifecycle; abstract renderer checkpoint contract. |
| Project-owned N64Recomp toolchain patch | Resumable CPU code generation, stable continuation IDs and live-local schemas, safepoints, regeneration and version fingerprint. This cannot be hidden inside a runtime-only patch. Audio RSP generator need not support mid-task restore because v1 drains tasks. |
| Project-owned RecompFrontend/RT64 patches or maintained adapter | Hotkey dispatch and noncapturing feedback as needed; graphics semantic export/import, complete CPU/GPU barrier, framebuffer ownership/readback, history/cache reset, UI render-hook lifecycle. No native object serialization. |

Keep each dependency patch in this repository, apply against verified pinned revisions with idempotent checks, and hash the ordered patch set. Do not rely on edits made only inside `.deps-*`. Preserve and regression-test `n64modernruntime-osstopthread.patch`; do not replace it with a self-stop-only implementation.

## Feasibility gates and acceptance tests

1. **Continuation gate:** all game threads and boot lifetime accounted for; nested calls, all blocking HLE return phases, thread stop/start/destroy/reuse, priority changes, FP modes and generated locals survive fresh-process restore. Failure blocks the feature entirely.
2. **Quiescence gate:** stress requests during PI reads, audio generation, RSP writes, VI swaps, timer expiry, full message queues, loading screens and thread destruction. Instrument acknowledgements and reject any mutation after Frozen. Verify no dropped/duplicated events or missed queue retries; stress timeouts and shutdown.
3. **Renderer gate:** prove no outstanding CPU/GPU work or late present after import. Compare framebuffer reuse, color/depth contents, TMEM-dependent draws and first restored scanout. Test cold import, Console presentation, resizing and the existing flicker regression. Export must not modify CPU-visible RAM merely to acquire GPU pixels.
4. **Audio/input gate:** restore during music/effects, rate changes and pending controller reads. Compare generated N64 PCM and guest audio state under recorded external observations. Verify no future-timeline queued audio, preserved conversion history, unchanged stereo/gain/latency formulas, correct key-release behavior and no input capture from success notifications. A small unavoidable hardware audio tail is documented, not mistaken for guest corruption.
5. **Memory/global gate:** include extended heap/PI handles, every stopped thread, pending external messages, overlay reloads, timer selected outside its set and both VI mode slots. Repeated load must clear pages made nonzero only after the save. Verify no native pointer leaks in encoded memory.
6. **Format/failure gate:** truncated/corrupt/oversized/overlapping chunks, wrong ROM and build, unsupported capability/mods, unavailable slot, permission/disk-full failure and interrupted replacement all leave the old slot and running state intact. Device import failure rolls back or stays safely paused. Never execute file-provided machine addresses.
7. **Determinism gate:** run a recorded-observation sequence from a saved boundary twice, including a process restart with ASLR, and compare canonical state hashes and PCM at each VI. Include menus, course/race state, pause, transitions and sustained audio. Compare a save-and-resume path to the corresponding no-save path with pause time excluded. Separately measure ordinary live behavior regressions; do not confuse this with proof of input-only determinism.

No code or tests were executed to claim these gates passed. Implementation should begin with continuation and renderer prototypes only after human approval; shipping F5/F8 before the gates pass would create unreliable state files.

## Approval requested

Approve or revise the explicit-continuation architecture, semantic renderer/audio state requirements, strict v1 build compatibility, VI transaction protocol and quick-slot UX. Decide whether faithful restoration under current live timing is the intended v1 guarantee, or whether a separately approved deterministic timing/audio mode is required. The latter necessarily changes some existing runtime behavior.

This document is the complete design deliverable for this investigation. Source files and dependencies are unchanged by this work.
