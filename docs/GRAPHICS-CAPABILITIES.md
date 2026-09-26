# Graphics capabilities — source audit

Base: `4ce33da18bdf9f95392cdf1015c9978371123c34`. This describes the pinned,
locally patched source tree, not current upstream releases. The resulting
implementation (exposed options, hidden framerate, VSync, tests)
is recorded in `UI-IMPLEMENTATION.md`.

Abbreviations: FE = .deps-renderer/RecompFrontend/recompui;
RT = .deps-renderer/rt64/src; UM = .deps-runtime/N64ModernRuntime/ultramodern.
SUPPORTED means an implemented path exists, not that this audit tested it live.
NOT SUPPORTED refers to the current end-to-end SBK stack unless qualified.

| Capability | Classification | Evidence and UI decision |
|---|---|---|
| Display resolution selector | NOT SUPPORTED | GraphicsConfig has no monitor/mode dimensions or mode enumeration binding. Do not confuse internal resolution presets with display modes. OS/window determines drawable size. |
| Internal render resolution | SUPPORTED | FE/include/recompui/resolution.h → set_application_user_config in FE/src/renderer/rt64_render_context.cpp → RT64 userConfig resolutionMultiplier. |
| Resolution scale | SUPPORTED | Discrete 1×, 2×, 3×, 4.5×, 6×, 9× and window-dependent Auto already exist. Preserve serialized keys; label as internal scale. |
| Windowed | SUPPORTED | wm_option → RT64 Application::setFullScreen(false). |
| Fullscreen | SUPPORTED | Existing Fullscreen enum selects the platform fullscreen path. It does not establish exclusive fullscreen. |
| Borderless fullscreen | SUPPORTED | RT/hle/rt64_application_window.cpp uses SDL_WINDOW_FULLSCREEN_DESKTOP on SDL and borderless monitor-sized window styles on Windows. Label accordingly on these platforms. macOS uses its native toggle and needs separate validation. |
| Exclusive fullscreen | NOT SUPPORTED | No independent enum/backend binding in this integration. Do not add a third identical choice. |
| Monitor selector | NOT SUPPORTED | No persisted display selection or enumeration in GraphicsConfig. Windows fullscreen follows the window's monitor; that is not a selector. |
| Aspect ratio | SUPPORTED | Original/Expand → RT64 userConfig.aspectRatio. Original is default. Preserve GRAPHICS-ASPECT-01 geometry tests. |
| Widescreen | PARTIAL | Expand adjusts projection/presentation; this is not proof of correct game framing/HUD everywhere. Label “Expand to window,” retain original 4:3. |
| Ultrawide | PARTIAL | Expand can target window aspect; no SBK-wide ultrawide acceptance evidence. No ultrawide promise or preset. |
| VSync | SUPPORTED (On/Off) | Implemented by VSYNC-P1: Graphics → VSync, `graphics.json` `vsync`, default On, live apply on the RT64 present thread. Vulkan On = FIFO, Off = IMMEDIATE only when the surface offers it; D3D12 Off = tearing present only with tearing support (compile-checked, not live-verified); Metal On only. Off is disabled when unavailable. Adaptive/Mailbox not exposed. See `PRESENTATION-SYNC.md`. |
| Frame pacing | PARTIAL | RT64 presentation queue plus Console/SkipBuffering/PresentEarly project override; default Console. Not a safe general settings control. Keep guest timing unchanged. |
| Anti-aliasing / MSAA | SUPPORTED | msaa_option → antialiasing/updateMultisampling; UI None/2×/4×, gated by programmable sample positions and maximum MSAA. 8× enum existence alone is not justification to expose it. |
| Downsampling | SUPPORTED | ds_option 0/2/4; supported at Original/Original2x. Explicit higher scales ignore ds. Harden numeric JSON override before exposing broadly. |
| Output filtering | SUPPORTED BUT NOT EXPOSED | RT userConfig.filtering is consumed by PresentQueue; Nearest/Linear/AntiAliasedPixelScaling. This is presentation filtering, not a universal texture sampler override. |
| Texture filtering | PARTIAL | RT userConfig.threePointFiltering affects render state; no FE/UM binding. A Nearest/Original/Bilinear texture dropdown would misrepresent the current available semantics. Leave unexposed pending scoped validation. |
| Anisotropic filtering | PARTIAL | RT shader-library sampler plumbing exists; no user setting/config chain identified. Do not expose a multiplier from sampler implementation alone. |
| Internal scaler | SUPPORTED | Manual/window-integer render scaling and presentation filtering; no external SDK needed. |
| FSR / DLSS / XeSS | PARTIAL | Symbols/debug RT settings exist in RT64, including ray-tracing upscaler paths. No stable SBK frontend/runtime config path established. Do not expose or integrate SDKs. |
| Display refresh selection | NOT SUPPORTED | RefreshRate enum is render output targeting, not OS monitor mode selection. |
| High FPS | UNSAFE FOR SBK (unvalidated) | Inherited UI exposes Original/Display/Manual, defaults Display, includes 20–240 slider and a blanket gameplay-safety claim. Remove from user-facing options and adopt Original for this mission; HIGH-FPS-P1 must validate timing/physics/audio separately. |
| HDR output | NOT SUPPORTED | HighPrecisionFramebuffer controls internal format. usesHDR is not proof of HDR monitor output, metadata or calibration. Keep hidden. |
| UI scale | PARTIAL | Automatic height/1080 dp ratio exists; no persisted custom scale. Keep Auto and verify layouts instead of adding an inert control. |

## Existing settings chain and apply behavior

FE/src/config/ui_config_tab_graphics.cpp constructs the schema, registers save
and load callbacks and applies it to UM GraphicsConfig. RT64Context::update_config
updates userConfig and invalidates framebuffers when resolution, aspect,
downsampling or MSAA changes. MSAA additionally calls updateMultisampling.
Window mode invokes setFullScreen. These are live on Apply, not necessarily
on each click: graphics uses temporary config plus Apply/Revert.

Graphics API is selected during renderer setup and requires restart if exposed.
High precision framebuffer is hidden and must not be presented as HDR. There
is no verified timed display confirmation; existing Apply/Revert is a temporary
settings transaction, not a 15-second post-apply recovery mechanism. Retain it;
no display mode enumeration or fragile countdown is planned.

VSync is applied on the RT64 present thread under its present lock (never
from a UI callback): Plume Vulkan changes requiredPresentMode and the existing
needsResize/resize path recreates the swapchain. The live swapchain's mode is
reported as the effective state. VSync controls presentation, never guest VI
timing or game FPS; Off is not offered when Immediate/tearing is unavailable.

## Internal resolution model

| Serialized value | Scale | Nominal 4:3 render size with downsampling off |
|---|---:|---:|
| Original | 1× | 320×240 |
| Original2x | 2× | 640×480 |
| 720p | 3× | 960×720 |
| 1080p | 4.5× | 1440×1080 |
| 1440p | 6× | 1920×1440 |
| 2160p | 9× | 2880×2160 |
| Auto | Window-dependent integer | Depends on drawable/VI |

These nominal dimensions explain existing preset semantics; they are not fixed
output modes or a claim of tested 4K output. Actual rendering depends on VI and
aspect. Avoid “4K” as the 9× label. Higher settings increase allocation/GPU cost.

## Persistence and invalid input gaps

librecomp/src/config.cpp saves under get_path_to_config, and native_boot routes
user data (including SBK_USER_DATA_DIR override). Preserve user paths and keys.
The enum parser falls back to the schema default for unknown strings/types.
The downsampling custom parser bypasses that path and directly calls
j.get<uint32_t>(); scale() only clamps a minimum, not an upper bound. Negative,
large, wrong-type and invalid numeric values need tests and explicit whitelisting
before GPU allocation. Number parsing returns double without clamping in the
shown load loop; validate at the graphics boundary as well.

Required acceptance: process A saves each supported scale/aspect/VSync setting;
process B reads exactly the normalized values using isolated user config.
Exercise malformed/truncated JSON, invalid enums, negative/oversized numbers
and backend limitations. No settings control is accepted without UI → config
→ backend → persistence → tests. Existing source paths alone do not satisfy
that acceptance gate.
