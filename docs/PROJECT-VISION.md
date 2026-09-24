# Project Vision

Updated: 2026-09-24

## Goal

SnowboardKids-Recomp aims to become a complete, polished, reproducible and publicly distributable native PC port of **Snowboard Kids (Nintendo 64, USA)**.

The project is no longer measured only by whether the game boots, renders and completes a race. The product target now includes full-game compatibility, modern controls, multiplayer, mods, native savestates, PC graphics features, automated builds, packaging and public-release quality.

The strongest current benchmark for repository/product completeness is the Snowboard Kids 2 recomp project. Snowboard Kids 1 must still be treated independently: game-specific assumptions are not copied unless they are validated against SBK1.

## Global progress metric

**Approximate full-product maturity: ~52%.**

The previous ~94% figure referred to a narrower technical/native-port roadmap and must not be used as the project-wide completion percentage.

The current progress denominator includes all product areas listed below.

## Product areas

### Core native port

Target:

- reproducible matching decompilation baseline;
- native CPU/RSP recompilation;
- N64ModernRuntime execution;
- stable RT64 rendering;
- robust audio;
- frontend/input integration;
- reproducible dependency patching.

Current maturity: **high**.

### Full-game compatibility

Target coverage includes:

- all characters;
- all courses;
- items;
- AI;
- Training;
- replay flows;
- secondary menus;
- progression/unlocks;
- ending/credits;
- Controller Pak;
- original save behavior;
- clean shutdown.

The existing first-race path is validated, but this broader regression matrix is not complete.

### Controls and multiplayer

Target:

- keyboard;
- Xbox/XInput-style controllers;
- DualShock/DualSense;
- Switch Pro;
- generic SDL controllers;
- remapping;
- deadzones;
- rumble;
- correct button prompts;
- local multiplayer, including all original supported player counts.

Current controller support is only partially validated. Digital Xbox inputs have worked in testing, while analog rider control/remapping remain unfinished.

### Graphics and PC enhancements

Target:

- original 4:3;
- HD/4K internal rendering;
- automatic resolution scaling;
- true widescreen;
- possible ultrawide support;
- HUD/menu adaptations where needed;
- texture-pack support;
- higher-framerate research without changing gameplay timing.

Validated today:

- original 4:3;
- HD/4K internal-resolution presets;
- automatic integer scaling;
- stable console-faithful presentation.

True widescreen/ultrawide/high-FPS support remains research work.

### Audio

Target:

- stable music/SFX throughout the complete game;
- correct restoration across native savestates;
- no progressive corruption or timing drift.

Current baseline audio is functional during normal gameplay.

### Mods

Target:

- documented hooks/events/exports;
- reusable mod template;
- packaged mod format/workflow;
- UI installation/management;
- texture packs;
- sample mods.

The runtime/frontend framework is present, but SBK1-specific end-to-end mod support still needs proof.

### Native savestates

Target:

- faithful restoration of complete native runtime state;
- semantic rather than raw host-object serialization;
- renderer restoration;
- audio/timing restoration;
- transactional load/rollback;
- versioned .sbks files;
- F5/F8 quick-save/load UX.

Current frontier:

- P1 PASS;
- P1.5 PASS;
- P2 PASS;
- P3 PASS;
- P3.1 PASS;
- P4-A code-side complete, final manual race-finish gate pending;
- P4-B and later phases blocked until P4-A passes.

### Release engineering

Target:

- Windows builds;
- Linux builds;
- macOS builds;
- automated GitHub Actions;
- PR build artifacts;
- release pipeline;
- semantic versioning;
- changelog;
- checksums;
- no ROM/assets bundled;
- possible ARM64/Flatpak follow-up.

This area is still early.

### Open-source/public readiness

Target:

- accurate README/status/roadmap;
- build documentation;
- issue templates;
- contribution guide;
- license audit;
- protected default branch/tags;
- clear legal/ROM policy;
- reproducible development environment.

The repository should remain private until the technical and documentation baseline is mature enough for useful public collaboration.

## Current phase model

### Phase A — Complete native savestate R&D

Finish P4-A through P7 without weakening normal gameplay behavior.

### Phase B — Full-game compatibility

Systematically validate all original game modes/content/save flows.

### Phase C — Controls and multiplayer

Complete modern controller support, remapping, rumble and multiplayer.

### Phase D — Release engineering

Automate clean builds, artifacts and releases across supported desktop platforms.

### Phase E — Mods

Prove the first real SBK1 code mod, publish hooks/templates and document authoring.

### Phase F — PC enhancements

Widescreen, ultrawide, texture packs and carefully validated higher-FPS work.

## Engineering principles

1. Preserve original game behavior before adding enhancements.
2. Never distribute the ROM or copyrighted game assets.
3. Keep upstream pins explicit.
4. Keep project-specific dependency changes reproducible.
5. Prefer semantic state over raw host-object serialization.
6. Keep compatibility fixes separate from optional PC features.
7. Require real runtime evidence before marking a feature complete.
8. Treat automated tests and manual gameplay validation as complementary.
9. Do not let old milestone percentages survive after the project scope changes.
