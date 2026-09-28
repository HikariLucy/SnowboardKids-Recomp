# GRAPHICS-ASPECT-01: title screen grows and shrinks under Expand

Status: fixed by `patches/rt64-aspect-coverage.patch`.

## Symptom

Between the boot logos and the main menu, the whole image grows and shrinks
about every 0.37 s. Gameplay is stable. It happens only with aspect ratio
`Expand` and a window wider than 4:3.

## What does not change

Change-only instrumentation of the presented frames recorded these values, and
none of them changed during the pulse:

| Value | Observed |
| --- | --- |
| VI | `width=320 h=108-748 v=37-511 xscale=512 yscale=1024`, fbSize 320x240 |
| Framebuffers | 0x38E800 / 0x3B4000 / 0x3D9800 (triple buffer), 320 wide |
| Workload aspect | source 1.333, target 1.778, scale 1.333 |
| resolutionScale | 4.0 x 3.0 (window 1280x720, Auto) |
| Presentation rect | viewport 0,0 1280x720, scissor 0,0-1280,720 |
| Configured aspect | Expand for the whole run (no config reload) |

The outer presentation rectangle, the VI and the user setting are not involved.

## What changes

RT64 decides per framebuffer pair whether a 3D projection widens under Expand.
The test is whether the projection's viewport covers the pair's horizontal
extent. That extent was the union of every scissor state in the pair.

On the title screen the game draws the letterboxed attract scene with scissor
16..304 x 32..208 (`appendViewportDisplayLists`, per-viewport `gDPSetScissor`).
"PUSH START BUTTON" blinks. When it is visible it is drawn into the same pair
with a full-screen scissor 0..320 x 0..240:

| Frame | Pair scissor union | Scene covers it | Scene on screen |
| --- | --- | --- | --- |
| text hidden | 16..304 | yes | widened, x 64..1215 (1152 px) |
| text shown | 0..320 | no | 4:3, x 207..1071 (864 px) |

The value correlated with the pulse is `fbPair.scissorRect`. It alternates in
step with the text blink, 61-62 flips in 36 s. The projection processor and the
framebuffer renderer both read it, so the projection matrix and the viewport
switched together. There was no distortion: the image changed width.

`Original` does not pulse. aspectRatioScale is 1, and the flip has no geometric
effect (constant 864 px).

## Guest vs host

- Guest: legitimately draws a letterbox scene and a blinking full-scissor
  overlay. The VI is constant. A real N64 shows a stable 4:3 image.
- Host: RT64's Expand heuristic judged coverage against scissor *state*, which
  a small overlay inflates, instead of against what was drawn.
- Not related to savestates, the P3/P3.1 renderer import, or the 8312740
  present handshake. It reproduces from a cold boot with no restore.

## Fix

`FramebufferPair::projectionCoversWidth()` compares against
`fbPair.drawColorRect`: the union of scissor ∩ primitive bounds, which RT64
already tracks for framebuffer write-back. It falls back to the scissor union
when nothing was drawn. Both call sites (projection processor and framebuffer
renderer, including the RT path) use it, so matrix and viewport stay consistent.

Unchanged on purpose:

- Rectangle coverage stays on the scissor union. Otherwise a lone centered logo
  drawn with a full scissor would count as covering and would stretch.
- The screen-shape test (`adjustRatio`, scissor ratio vs VI ratio) is
  unchanged, so the blinking text keeps its 4:3 proportions.
- The scissor union itself, `drawColorRect`, framebuffer write-back and hashes
  are unchanged.

There is no game-specific code, no timing, and no config change. A projection
that covered the scissor union still covers the drawn extent, because the drawn
extent is always inside the scissor union. The only new widening is a projection
that spans everything drawn but not a scissor that nothing used.

## Evidence after the fix (Expand, 1280x720)

- Title/attract: 675 frames, 0 flips, content width constantly 1152 px. The
  text keeps its proportions.
- Gameplay (quickload): same pair layout and widening as before the fix.
- Main menu: previously boxed at 4:3 (864 px) because its 2D layer uses a
  full-screen scissor. Now the 3D scene is widened (1152 px) and the 2D logo and
  text keep their 4:3 proportions, as in gameplay. This is a visible change, and
  it removes the size jump between the attract loop and the menu.
- Original: constant 864 px, as before.

## Coverage

`python3 tests/renderer_geometry/run.py [--sanitize]` links the real RT64
decisions (no GPU). With the old rule it fails on the A,B,A,B blink sequence.
