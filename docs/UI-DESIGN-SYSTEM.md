# Snowboard Kids PC frontend — proposed design system

Status: architectural design for review, not implemented. Scope: host frontend,
overlay, settings and Model D onboarding. Original in-game menus remain intact.

## Direction

An original snow-sports arcade menu: stepped mountain geometry, opaque snow
panels, dark ink outlines, chunky shadows and warm yellow selection markers.
Use typography for the project title, not an imitation of the commercial logo.
No ROM sprites, textures, screenshots, extracted logos or proprietary icons.
No raster background, glass, RGB lighting, flashing decoration or UI sounds.

Preferred implementation: project-owned C++ FrontendTheme using existing element
widgets and navigation, project SVG geometry, small RCSS support. This covers
programmatic styles, unlike an RCSS-only overlay. A complete custom widget
framework would expand risk without helping this mission and is not proposed.

## Tokens

All dimensions below are reference dp at 1080 drawable height; use the existing
dp ratio. Layout behavior still requires visual checks at each target size.

| Token | Proposed value / role |
|---|---|
| Ink | #172B46, body text and outlines |
| Snow | #F5F8ED, opaque panels |
| Ice | #D5EDF1, secondary panels and hover fill |
| Slope | #235A78, background and header strips |
| Sun | #FFD35C, selected/focused fill with Ink text |
| Warning | #7B351C text on Sun; include warning label/symbol |
| Disabled | #536573 text on #DCE2E4; explicit disabled state |
| Spacing | 4, 8, 12, 16, 24, 32, 48 dp |
| Borders | 3 dp ordinary; 4 dp focus; 6 dp outer panel |
| Corners | 8 dp panels, 4 dp controls; stepped decoration at panel edges |
| Shadow | Static 6 dp solid Ink offset, no blur |
| Text | Lato 24 dp body, 21 dp descriptions; Fredoka 36 dp sections/48 dp page title |
| Targets | Minimum 56 dp control height at reference scale |

Verify actual composited contrast (normal text at least 4.5:1), including hover,
focus, warning and disabled descriptions; token choice is not test evidence.
Avoid opacity-based text over gameplay. Maintain useful body text size at 720p.

## Components and states

- Panels: opaque Snow settings content; Slope title band; minimal decorative
  mountain silhouettes outside text areas. Keep scrollable bodies and fixed
  reachable footer actions within window bounds.
- Buttons: Ink outline and solid offset shadow. Hover adds Ice fill. Focus
  adds a steady Sun marker plus 4 dp outline; no color-only selection.
- Selected tabs/radios: filled Sun with Ink text and a visible square/check
  marker. Selection and focus remain distinct. Preserve existing radio order.
- Disabled controls: dimmed opaque fill and explanatory text when hardware
  prevents a setting. Do not imply a disabled value has taken effect.
- Sliders: thick track, square thumb, numeric current value; keyboard and
  controller left/right use existing step behavior.
- Toggles: explicit On/Off labels plus position/state marker.
- Dropdowns: original caret SVG, full-width value, scrollable choices; back
  closes the list and restores focus to its trigger.
- Modals: opaque framed content over a subdued overlay; clear title/action;
  cancel/back returns to the invoking control. Retain existing input capture.
- Notifications: short readable text, success/error symbol and label, no
  gameplay input capture. Keep current savestate timing and service behavior.

## Navigation and settings

Provide Resume, Settings, Controller, Savestates and Quit where wired to actual
services. Settings retains Graphics, Audio, Controls and General. Preserve the
Mods policy: no tab unless both game ID and initialized subsystem permit it.
Savestates exposes Quick Save (F5) and Quick Load (F8) via the existing request
path with pending/error feedback; no new format, thumbnails or Pak deletion.

Graphics uses Display (Windowed/Borderless; VSync only after complete binding),
Rendering (Internal Resolution, Original 4:3/Expand to window, hardware-gated
MSAA), and a compact advanced downsampling section if validation passes.
Do not expose monitor resolution, HDR, high FPS, ultrawide guarantees, unsafe
filtering, SDK upscalers or redundant display choices. Omit presets for P1.
Preserve serialized resolution values while improving displayed names.
Descriptions explain output impact and cost, not renderer implementation terms.
Use existing Apply/Revert; mark only startup-only options as restart-required.

## Original assets and staging

Create the eleven replacement icons plus simple navigation/selection geometry
under assets/sbk-ui. Use a common 32×32 viewBox, clear thick strokes, no external
references or editor metadata. Original static mountain/slope SVG stays outside
reading areas. Record creation purpose and provenance; project-owned provenance
does not itself choose a distribution license for the repository.

Build staging must copy only reviewed fonts/licenses, project RCSS and new SVGs.
Replace the theme implementation target with project-owned source and remove
runtime reliance on old theme drawings. Keep upstream checkout unchanged.
Readiness may clear theme_asset_icons only after byte-level staging/package
checks prove all SVGs are owned replacements. Keep all other license blockers.

## Model D architecture

Current builder runs before frontend initialization through blocking std::system.
This requires an architectural change, not merely recoloring an existing page.
Use a ROM-free host UI initialization path, keeping game entrypoints and module
initialization gated until a validated module is installed and loaded. Do not
fabricate a dummy game module just to open a page.

States: Missing module → Select ROM → Validate → Generate CPU → Generate RSP
→ Compile → Validate module → Install → Ready; recoverable Error returns to
selection or retry. Native ROM picker remains OS-owned. Keep CLI build mode.

Run the builder as an owned child process with an argument vector, asynchronous
stdout/stderr consumption and explicit exit/reap handling. Use structured phase
messages from module_builder.service's real status_callback. Only Compile has a
fraction current/total; other phases show their names without invented percent.
Store detailed logs in user data and expose a simple log action. Return errors
as readable summaries plus log access. On close, stop the child cleanly and
preserve atomic installation semantics. No detached child or blocking UI wait.

Ready is emitted only after install and module ABI validation. Hand off once to
the existing runtime boot path. Test cancellation, retry, failed compilation,
invalid module, existing valid module and process cleanup without a ROM using
synthetic modules and fake child processes.

## Responsive/input acceptance

At 1280×720, 1920×1080, 2560×1440, 3840×2160 and a 4:3 window: verify labels,
radio wrapping, descriptions, dropdown bounds, footer and focus visibility.
Use a stacked layout for narrow settings rather than shrinking text indefinitely.
Check keyboard, mouse, controller, focus order/return, confirm/back, sliders,
tabs and dropdowns. Existing dp scaling is the default; no custom scale toggle.

Capture ROM-free local snapshots for overlay, graphics, controls, savestates and
first-run when a safe host-only path exists. Do not commit screenshots containing
uncertain assets. Human visual review remains required after implementation.

## Implementation sequence and acceptance

1. Commit source inventory and capability/design documents.
2. Implement owned theme, fonts hierarchy, original SVGs and allowlisted staging.
3. Harden graphics parsing; expose only validated controls; remove inherited
   high-FPS UI and enforce the intended Original policy without guest changes.
4. Add VSync only after backend capability/effective-state and thread ownership
   are proven; otherwise retain a documented incomplete mission item.
5. Add Model D host-only state machine, child lifecycle and real progress events.
6. Wire overlay/savestate navigation through existing service APIs.
7. Run ROM-free config/schema/binding, process A/B persistence, invalid input,
   input/navigation, builder, geometry, controls, PFS and savestate regressions.
8. Build the engine; perform responsive snapshots and local module race_active
   smoke with overlay/settings/resume, checking fallback and residual processes.
9. Package a local ROM-free engine draft, audit content and owned-asset provenance;
   update running docs and readiness only to match evidence.
10. Commit cohesive changes; push feat/sbk-frontend-theme. No main merge/tag/release.

Baseline is recorded in UI-FRONTEND-MAP.md. None of the implementation, visual,
live-game or new-package acceptance items above are claimed complete.
