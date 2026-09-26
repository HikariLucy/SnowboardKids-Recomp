# Dependency Provenance and Pin Integrity Audit

Date: 2026-09-25
Baseline Branch: `feat/release-distribution`
Git HEAD: `a39ae43ac774c369864fbc11b9741ff962eff871`

---

## 1. Executive Summary: Dependency Pin Drift Incident

An anomaly was reported where the **RELEASE-P1** verification report declared:
- **N64ModernRuntime:** `8549921b7ff3b10b0e515fa016b801452df3b05f`
- **RecompFrontend:** `be653f538fa8da1e25dca9ba7bc126dbdf39e551`

These differed from the known historical pins:
- **N64ModernRuntime:** `6ccb2e7c2e7f6708257b461097e0aaf03c445e2a`
- **RecompFrontend:** `e85b912d9df677b04f9358867dd010c8af27ea05`

### Root Cause Investigation Findings

An exhaustive forensic analysis across all local Git repositories, submodules, object databases, remote GitHub repositories (`cdlewis/*` and `N64Recomp/*`), and historical logs demonstrates:

1. **Non-Existent Git Objects:**
   Neither `8549921b7ff3b10b0e515fa016b801452df3b05f` nor `be653f538fa8da1e25dca9ba7bc126dbdf39e551` exist as Git commits, trees, blobs, or tags anywhere in the project or upstream repositories.
2. **Untouched Checkouts:**
   The Git reflogs of `.deps-runtime/N64ModernRuntime` and `.deps-renderer/RecompFrontend` show zero checkout transitions to those SHAs. The working trees have always resided on `6ccb2e7` and `e85b912` respectively.
3. **Strict Validation Enforcement:**
   `scripts/dependency_lock.py` and `scripts/dependency_patches.py` validate the checkout HEAD against the lockfile with `git rev-parse HEAD`. Had any other commit been checked out, the patch application would have aborted with a hard runtime error.
4. **Source of the Discrepancy:**
   The rogue hashes originated exclusively as narrative text in the final markdown summary of conversation `da0113da` (step 547). They were never present in any code, config, lockfile, or build output.
5. **Release Candidate Impact:**
   **ZERO.** The built Linux and Windows release artifacts were compiled strictly against the authentic canonical pins (`6ccb2e7` and `e85b912`). No accidental pin drift or binary rebuild is required.

---

## 2. Canonical Dependency Inventory

| DEPENDENCY | ORIGINAL PIN | CURRENT PIN | CHANGE COMMIT | REASON | PATCH SERIES | LICENSE SOURCE | STATUS |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **N64Recomp** | `ffb39cdad1da` | `ffb39cdad1da` | None (initial pin) | Baseline static recompiler core | `n64recomp-continuations.patch` | `.deps/N64Recomp/LICENSE` (MIT) | **CLEARED** |
| **N64ModernRuntime** | `6ccb2e7c2e7f` | `6ccb2e7c2e7f` | None (initial pin) | Native N64 runtime support (`gamemodes` branch) | 6 patches (quiescence, savestate, etc.) | `.deps-runtime/N64ModernRuntime/COPYING` (GPLv3) | **CLEARED PIN** (GPL obligations remain) |
| **RT64** | `6a4166b2cfa9` | `6a4166b2cfa9` | None (initial pin) | Hardware rendering backend | `rt64-quiescence.patch`, `rt64-aspect-coverage.patch`, `rt64-vsync-presentation.patch` | `.deps-renderer/rt64/LICENSE` (MIT) | **CLEARED** |
| **RecompFrontend** | `e85b912d9df6` | `e85b912d9df6` | None (initial pin) | Input and UI frontend subsystem | 3 patches (resolution, quiescence, input) | Lacks root license file | **CLEARED PIN** (License blocker remains) |
| **recomp-theme** | `0cb9a83a2636` | `0cb9a83a2636` | None (initial pin) | UI theme styling and font assets | None | Lacks root license file | **CLEARED PIN** (Asset audit required) |

---

## 3. Patch Series Verification & Tree Digest State

Each dependency's canonical patch series applies cleanly on top of its pinned Git base commit. `scripts/dependency_patches.py` verifies bit-for-bit equality against an in-memory reconstructed baseline:

$$\text{Base Commit} + \sum \text{Canonical Patches} = \text{Verified Tree State}$$

### Patch Sequence Breakdown

1. **N64Recomp (`ffb39cdad1da5de07eaaa48bd1db4a89a7986771`)**:
   - `n64recomp-continuations.patch`: Generates continuation frames and dispatch tables for asynchronous suspension.
   - Status: Clean match.

2. **N64ModernRuntime (`6ccb2e7c2e7f6708257b461097e0aaf03c445e2a`)**:
   - `n64modernruntime-osstopthread.patch`: Fixes thread stopping lifecycle.
   - `n64modernruntime-quiescence.patch`: Frame boundary synchronization and quiescence barrier.
   - `n64modernruntime-continuations.patch`: Runtime support for serializable continuation descriptors.
   - `n64modernruntime-savestate.patch`: State capture and memory restore hooks.
   - `n64modernruntime-shutdown.patch`: Orderly audio/video subsystem teardown.
   - `n64modernruntime-input-neutral.patch`: Neutral controller state injection.
   - `n64modernruntime-config-shape.patch`: Wrong-shape config files load defaults.
   - `n64modernruntime-vsync.patch`: `GraphicsConfig::vsync_option` (On/Off, JSON `vsync`).
   - Status: Clean match across the full series.

3. **RT64 (`6a4166b2cfa952d931a08481d1037da995f28b54`)**:
   - `rt64-quiescence.patch`: Frame queue drain and renderer state capture.
   - `rt64-aspect-coverage.patch`: Widescreen viewport coverage extension.
   - `rt64-vsync-presentation.patch`: Present-thread VSync requests, effective state, Off capability, opt-in pacing summary.
   - Status: Clean match across the full series.

4. **RecompFrontend (`e85b912d9df677b04f9358867dd010c8af27ea05`)**:
   - `recompfrontend-resolution.patch`: Resolution scaling presets and configuration hooks.
   - `recompfrontend-quiescence.patch`: UI thread synchronization with frame quiescence.
   - `recompfrontend-input.patch`: Extended controller mapping and virtual controller routing.
   - `recompfrontend-graphics-options.patch`, `recompfrontend-theme-focus.patch`: Graphics option scope and themeable focus.
   - `recompfrontend-vsync.patch`: Graphics “VSync” option bound to RT64 presentation.
   - Status: Clean match across the full series.

---

## 4. Release Identity and Cryptographic Digests

The release distribution artifact is cryptographically bound to the following hashes:

- **SnowboardKids-Recomp Commit:** `a39ae43ac774c369864fbc11b9741ff962eff871`
- **Dependency-Lock-Digest:** `61f14bb8fc456f05712c87ec5b5295246d8c97f944e547880f32a4428f7b0ee2`
  *(SHA-256 over canonical dependency names, URLs, and Git commit pins)*
- **Dependency-Patch-Digest:** `e04844e2638b45b0ec4e4adf709736c3e55b4c258fefa8c20ffb0fb1720ea012`
  *(SHA-256 over ordered patch contents)*
- **Combined-State-Digest:** `a55b8140a380de2089be830b47c6306b2af1bd90c39400e0b93648ed549f8415`

Every packaged release ZIP embeds `BUILD-INFO.txt` recording both the build commit and `Dependency-Lock-Digest`, ensuring full reproducibility and auditability.
