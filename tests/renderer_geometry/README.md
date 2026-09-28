# Presentation geometry regression (GRAPHICS-ASPECT-01)

```sh
python3 tests/renderer_geometry/run.py            # needs a built build-renderer-stack
python3 tests/renderer_geometry/run.py --sanitize # ASan + UBSan
```

No ROM, window or GPU. The test links `rt64.a` and drives the real decisions:

- `FramebufferPair::projectionCoversWidth`: whether a 3D projection widens
  under Expand. The title screen blink sequence (scene only / scene + full
  scissor text, A,B,A,B) must classify identically every frame. The old
  scissor-union rule is evaluated alongside to prove the sequence reproduces
  the pulse.
- `WorkloadQueue::threadConfigurationUpdate`: user aspect (Original, Expand)
  and resolution to aspect target and resolution scale.
- `VIRenderer::getViewportAndScissor`: the outer presentation rectangle across
  VI changes, window resizes and resolution presets.
- `reset_shared_presentation_history` (RecompFrontend): a renderer restore
  keeps the user aspect, resolution and presentation rectangle.

Built with clang++ by default because `rt64.a` is. Override with `CXX`.
