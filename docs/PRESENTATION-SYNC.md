# Presentation synchronization (VSync) — audit

Base: `1c3734b`. Pins: RT64 `6a4166b` (Plume submodule `4d97ba0`),
RecompFrontend `e85b912`, N64ModernRuntime `6ccb2e7` plus the canonical patch
series in `scripts/dependency_lock.py`.

Abbreviations: RT = `.deps-renderer/rt64/src`; PL = `RT/contrib/plume`;
FE = `.deps-renderer/RecompFrontend/recompui`;
UM = `.deps-runtime/N64ModernRuntime/ultramodern`.

VSync here means **host presentation synchronization only**: whether the
renderer's swapchain waits for the display's vertical blank before showing an
image. It is not the guest frame rate, not VI timing and not a frame limiter.

## Presentation path

```
guest (recompiled code)
 └─ UM vi_thread_func           wall-clock 60 Hz VI (UM/src/events.cpp)
     └─ UM gfx thread           send_dl / update_screen, update_config
         └─ FE RT64Context      set_application_user_config, update_config
             └─ RT Application  userConfig, setFullScreen, swapChain
                 ├─ RT WorkloadQueue thread   renders workloads
                 └─ RT PresentQueue thread    acquire → VI blit → present
                     └─ PL RenderSwapChain    Vulkan / D3D12 / Metal
                         └─ SDL window (Linux: SDL_Vulkan surface; Windows: HWND)
```

- The VI rate is produced by `vi_thread_func`, which sleeps until
  `start + n / 60 s`. It never reads the graphics config or waits on the
  renderer.
- Presentation happens only on the RT64 present thread
  (`PresentQueue::threadLoop`). While it processes a present it holds
  `PresentQueue::threadMutex`; swapchain resize and recreation happen there
  and nowhere else.
- The workload thread waits for the matching present ID
  (`WorkloadQueue::threadLoop`), so a blocked present back-pressures rendering
  inside RT64. The present queue already skips presents when newer ones are
  queued (`skipPresent`). Neither path feeds back into VI timing.

## Backends

### Vulkan (Linux primary; Windows optional)

| Item | Finding |
|---|---|
| CURRENT MODE | `VK_PRESENT_MODE_FIFO_KHR`. `VulkanSwapChain` constructor calls `setVsyncEnabled(true)`; nothing else in RT64 or the frontend calls it. The game therefore always runs with VSync On today. |
| API | `plume::RenderSwapChain::setVsyncEnabled(bool)` / `isVsyncEnabled()`. Off selects `VK_PRESENT_MODE_IMMEDIATE_KHR` **only** when the surface reports it (`immediatePresentModeSupported`); otherwise FIFO is kept. `isVsyncEnabled()` reports the mode the live swapchain was created with (FIFO or MAILBOX = synchronized). |
| RUNTIME MUTABILITY | Yes, through recreation. `setVsyncEnabled` only changes `requiredPresentMode`; `needsResize()` then returns true and the next `resize()` creates a new swapchain with `oldSwapchain` set. |
| REQUIRES SWAPCHAIN RECREATE | Yes. RT64 already has the mechanism: the present thread drains the present worker and calls `resize()` whenever `needsResize()` is true (window resize, fullscreen toggle). No renderer teardown is needed. |
| SUPPORTED VALUES | FIFO (On, guaranteed by the spec); IMMEDIATE (Off, surface dependent). MAILBOX and FIFO_RELAXED are detected/defined but no Plume path selects them. |
| LIMITATIONS | Calling `setVsyncEnabled` from another thread races `needsResize()`/`resize()` on the present thread: it must be applied on the present thread. Off must be reported as unavailable when IMMEDIATE is not offered. On Wayland compositors IMMEDIATE is often not offered. |

Local machine (X11, `vulkaninfo`): RADV Renoir offers IMMEDIATE, MAILBOX,
FIFO, FIFO_RELAXED; NVIDIA RTX 3060 Laptop offers FIFO, IMMEDIATE. This is
recorded for evidence, not assumed anywhere in code.

### D3D12 (Windows)

| Item | Finding |
|---|---|
| CURRENT MODE | `D3D12SwapChain::vsyncEnabled = true` (default, never changed). Flip-model swapchain; `DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING` is set at creation when `DXGI_FEATURE_PRESENT_ALLOW_TEARING` is supported. |
| API | Same Plume interface. `present()` uses `SyncInterval = (vsync && !enablePresentWait) ? 1 : 0` and passes `DXGI_PRESENT_ALLOW_TEARING` only when VSync is off **and** tearing is supported. RT64 enables present wait when the device supports it, so with present wait the sync interval is 0 in both states and the only difference is the tearing flag. |
| RUNTIME MUTABILITY | Yes. It is a flag read in `present()`; no recreation. It must still be written on the present thread (plain `bool`). |
| REQUIRES SWAPCHAIN RECREATE | No. |
| SUPPORTED VALUES | On (sync interval 1, or present-wait pacing without tearing); Off (tearing present) only when tearing is supported. Without tearing support Off would not differ visibly from On. |
| LIMITATIONS | Not an exact equivalent of Vulkan IMMEDIATE: with DWM composition a tearing present only tears in independent-flip (e.g. borderless fullscreen). Not validated live in this project. |

### Metal (macOS)

`CAMetalLayer.displaySyncEnabled` via `setVsyncEnabled`. Not built or tested in
this project; treated as **On only**.

## Capability classification

| Mode | Classification | Reason |
|---|---|---|
| VSync On | SUPPORTED | Current behavior on every backend (FIFO / sync interval 1 or present-wait / display sync). |
| VSync Off | BACKEND-SPECIFIC | Vulkan: only if the surface offers IMMEDIATE. D3D12: only with tearing support; not validated live. Metal: not exposed. |
| Adaptive (FIFO_RELAXED / sync interval -1) | NOT SUPPORTED | No Plume path selects FIFO_RELAXED; D3D12 has no adaptive path. |
| Mailbox / low latency | NOT SUPPORTED | Plume detects MAILBOX but never requests it; exposing it would need a Plume change and separate validation. |

P1 therefore exposes **On / Off** only, with Off gated by the capability of
the live swapchain.

## Source of truth

The only persisted value is the graphics config (`graphics.json`, key
`vsync`), parsed by the same librecomp `Config` as every other graphics
option and copied into `ultramodern::renderer::GraphicsConfig::vsync_option`.
The renderer receives it through the existing `update_config` path on the gfx
thread. No environment variable, second config file or static flag carries
the setting.

## Binding design

1. `n64modernruntime-vsync.patch`: `enum class VSync { On, Off }`, new
   `GraphicsConfig::vsync_option`, JSON names `"On"` / `"Off"`. A
   value-initialized config is On.
2. `rt64-vsync-presentation.patch`: `Application::setVsyncEnabled` stores a
   request in `PresentQueue`; the present thread consumes it under
   `threadMutex` right before its existing `needsResize()` check, calls
   `RenderSwapChain::setVsyncEnabled`, and lets the existing resize path
   recreate the swapchain. After that it publishes the effective state
   (`isVsyncEnabled()`). `Application::vsyncOffSupported()` reports IMMEDIATE
   (Vulkan) or tearing (D3D12) support; Metal reports false.
3. `recompfrontend-vsync.patch`: Graphics option “VSync” (On / Off),
   applied at startup and on Apply through `RT64Context::update_config`;
   Off is disabled in the UI when the live swapchain cannot provide it.

Results and validation are recorded in the sections below.

## Implemented behavior

| Item | Behavior |
|---|---|
| Setting | Graphics → **VSync**: On / Off. Text: “Synchronize frame presentation with the display.” |
| Storage | `graphics.json` key `vsync`, `"On"` or `"Off"`. Default `"On"` (the FIFO behavior every earlier build had). Missing, null, non-string or unknown values (including `"Adaptive"`, `"Mailbox"`) load `"On"`. Manual editing is not needed. |
| Apply | Live. Apply sends the value through `RT64Context::update_config`; the present thread applies it before its next present. No restart. |
| Vulkan On / Off | FIFO / IMMEDIATE. Switching recreates the swapchain through RT64's existing resize path (same path as a window resize). |
| Off availability | Off is disabled in the UI when the swapchain cannot present unsynchronized (Vulkan: surface lacks IMMEDIATE; D3D12: no tearing support; Metal: always). A saved Off on such a system runs On; the file is not rewritten. |
| Window mode | Windowed ↔ Borderless (menu, F11, Alt+Enter) keeps the VSync value; the resize path reuses the requested present mode. |
| Guest timing | Unchanged. VI stays wall-clock 60 Hz; VSync is not part of guest state or `.sbks`. High FPS stays hidden and forced to Original. |
| Diagnostics | `SBK_PRESENT_TIMING=1` logs one summary per 600 presents / VI updates. Mode changes log one `RT64 presentation sync:` line each. Nothing is logged per frame. |

### D3D12 (source audit; compile-checked in CI, not run live)

Plume `D3D12SwapChain::present` passes `DXGI_PRESENT_ALLOW_TEARING` only when
VSync is off **and** the swapchain was created with
`DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING`, which Plume sets only when
`IDXGIFactory5::CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING)`
succeeds. RT64 always enables present wait on D3D12, so `SyncInterval` is 0
in both states and On is paced by the frame-latency waitable object without
tearing. `Application::vsyncOffSupported` reads that swapchain flag and
`setVsyncEnabled` refuses Off without it. Whether Off visibly tears also
depends on DXGI independent flip (typically borderless fullscreen).

## Validation (Linux, NVIDIA RTX 3060 Laptop, X11, 1920×1080 @ 144 Hz)

Live runs use the Model D module and the P4-A navigation to `race_active`
(internal resolution Original unless noted). All: continuation backend ON,
0 native fallbacks, 0 residual processes, 0 audio warnings, no crash, freeze
or present deadlock. VI rate 60.000 Hz in every run.

| Run | VSync requested → effective | Result |
|---|---|---|
| Windowed, Original 4:3 | On → On | PASS (guest input frame 995 at race_active) |
| Windowed, Original 4:3 | Off → Off | PASS (guest input frame 995 at race_active) |
| Borderless (1920×1080 at 0,0) | On → On | PASS |
| Borderless (1920×1080 at 0,0) | Off → Off | PASS |
| Windowed, Expand | On → On | PASS |
| Windowed, Expand, overlay + resume | Off → Off | PASS; expanded frame, HUD 16:9 |
| Windowed, 1080p-class (4.5×) | On → On | PASS (stability; overlay input not exercised in that run) |
| Mid-race via overlay, On → Off | On → Off | PASS; guest kept advancing (input frame 1258 → 2112) and presenting |
| Mid-race via overlay, Off → On | — | INCONCLUSIVE: the automated focus sequence did not select On; renderer stayed healthy, no regression |

Live apply was also exercised in the ROM-free `--frontend-preview` with
keyboard (focus, Enter, F = Apply) and mouse (tab). Physical controller
input was not tested; the option uses the same frontend actions.

Present timing on this 144 Hz display showed no meaningful On/Off difference
(present blocking about 0.2–0.3 ms in both modes; the game presents at
30 Hz). No latency or performance claim is made.

Vulkan validation layers: NOT AVAILABLE on the test machine (Khronos
validation layer not installed).

## Windows

GitHub Actions `Renderer stack compile (ROM-free)` run 36244065956
(windows-2022, clang-cl + Ninja in the MSVC developer environment):
bootstrap and all canonical patches applied on a fresh checkout, and
RT64 (D3D12 + Vulkan, including the D3D12 VSync capability branch),
RecompFrontend (VSync option) and the runtime config compiled.
**D3D12: COMPILE PASS / LIVE NOT VERIFIED.**

Windows ROM-free CTest: **12/12 PASS** in run 36245526088 (after the
Windows portability fixes to `module_loader_synthetic` and
`module_builder_unit`), with the renderer stack still compiling in the same
run. With `controller_pak` enabled (see `WINDOWS-PFS-CI.md`): **Windows
ROM-free CTest 13/13 PASS**, no exclusions (runs 36248017470, 36248017480).
