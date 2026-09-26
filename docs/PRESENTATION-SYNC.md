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
