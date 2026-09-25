# Comprehensive License and Distribution Compliance Audit

Date: 2026-09-25
Baseline Branch: `feat/release-distribution`
Git HEAD: `a39ae43ac774c369864fbc11b9741ff962eff871`

---

## 1. Inventory of Distributed Components

| Component | Pinned Commit / Version | License File in Tree | License Identifier | Redistribution Obligation | Status |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Project Source** | HEAD | None in root | Unassigned | Owner must establish terms before public distribution | **OWNER DECISION REQUIRED** |
| **N64ModernRuntime** | `6ccb2e7c2e7f` | `.deps-runtime/N64ModernRuntime/COPYING` | GNU GPL v3.0 | Binary distribution requires complete corresponding source under GPLv3 (§ 6) | **RELEASE BLOCKER** |
| **N64Recomp** | `ffb39cdad1da` | `.deps/N64Recomp/LICENSE` | MIT License (Wiseguy) | Include copyright and permission notice | **CLEARED** |
| **RT64** | `6a4166b2cfa9` | `.deps-renderer/rt64/LICENSE` | MIT License (RT64 Contributors) | Include copyright and permission notice | **CLEARED** |
| **RecompFrontend** | `e85b912d9df6` | None at root | Unspecified by author | Upstream repository lacks top-level license grant | **RELEASE BLOCKER** |
| **SDL2** | 2.30.0 (system / DLL) | Upstream zlib | Zlib License | Retain notices in documentation | **CLEARED** |
| **{fmt}** | Submodule in N64Recomp | `.deps/N64Recomp/lib/fmt/LICENSE` | MIT License | Include copyright and permission notice | **CLEARED** |
| **RmlUi** | Submodule in RecompFrontend | `.../RmlUi/LICENSE.txt` | MIT License | Include copyright and permission notice | **CLEARED** |
| **toml++** | Submodule in N64Recomp | `.../tomlplusplus/LICENSE` | MIT License | Include copyright and permission notice | **CLEARED** |
| **Rabbitizer** | Submodule in N64Recomp | `.../rabbitizer/LICENSE` | MIT License | Include copyright and permission notice | **CLEARED** |
| **stb** | Submodule in RT64 | `.../stb/LICENSE` | MIT / Public Domain | Retain notice under MIT option | **CLEARED** |
| **Dear ImGui** | Submodule in RT64 | `.../imgui/LICENSE.txt` | MIT License | Include copyright and permission notice | **CLEARED** |
| **nativefiledialog-extended** | Submodule in RT64 | `.../nativefiledialog-extended/LICENSE` | Zlib License | Retain origin notice | **CLEARED** |
| **PromptFont** | Theme asset | `assets/promptfont/LICENSE.txt` | SIL Open Font License 1.1 | Retain OFL text (bundled in `licenses/promptfont.txt`) | **CLEARED** |
| **LatoLatin Fonts** | Theme asset | `licenses/LatoLatin-OFL.txt` | SIL Open Font License 1.1 | Retain OFL text (bundled in `licenses/LatoLatin.txt`) | **CLEARED** |
| **Fredoka Font** | Theme asset | `licenses/Fredoka-OFL.txt` | SIL Open Font License 1.1 | Retain OFL text (bundled in `licenses/Fredoka.txt`) | **CLEARED** |
| **NotoEmoji Font** | Theme asset | `licenses/NotoEmoji-OFL.txt` | SIL Open Font License 1.1 | Retain OFL text (bundled in `licenses/NotoEmoji.txt`) | **CLEARED** |
| **Theme UI Navigation Icons** | `0cb9a83a2636` | None in repo | Unspecified by author | Obtain author clarification for redistribution | **RELEASE BLOCKER** |
| **Theme Decorative Art** | `0cb9a83a2636` | None in repo | Unspecified by author | Excluded from distribution package (`board.svg`, `rock.png`) | **NOT SHIPPED** |

---

## 2. RecompFrontend License Audit

An exhaustive forensic search across `cdlewis/RecompFrontend` at commit `e85b912d9df677b04f9358867dd010c8af27ea05` produced the following findings:
- **Repository Tree:** No `LICENSE`, `COPYING`, or license header exists at the root. Submodules (`RmlUi`, `lunasvg`, `freetype-windows-binaries`) have permissive licenses; embedded headers (`lib/SlotMap`, `lib/GamepadMotionHelpers`) carry MIT licenses.
- **Package Metadata:** CMake configuration defines build targets only; no SPDX identifier or licensing metadata.
- **Source Headers:** None of the C++ source or header files in `recompui/` or `recompinput/` contain a copyright notice or license header.
- **Documentation:** `README.md` and `CONTRIBUTING.md` describe module architecture and developer guidelines but do not declare an author license grant.
- **Git History:** The commit history from initial import (`bc2b1d8`) through `e85b912` contains no license grant commit.

### Engineering Compliance Action: Upstream Clarification Request
Because licenses cannot be inferred by similarity, this component remains a **RELEASE BLOCKER**. When authorized by project leadership, the following inquiry data must be submitted to upstream:
- **Target Repository:** `https://github.com/cdlewis/RecompFrontend.git` (also upstream `https://github.com/N64Recomp/RecompFrontend.git`)
- **Pinned Commit:** `e85b912d9df677b04f9358867dd010c8af27ea05`
- **Distributed Components:** Compiled static object code of `recompui` (UI components) and `recompinput` (controller mapping).
- **Linkage Mode:** Statically compiled and linked directly into the final game executable.
- **Requested Clarification:** Provision of an explicit permissive open source license grant (e.g. MIT) covering author contributions.

---

## 3. Font Asset Provenance and Resolution

All distributed TrueType fonts have been inspected via their OpenType `name` tables and traced to authoritative upstream repositories. Their exact SIL Open Font License 1.1 texts have been incorporated into `licenses/` and are bundled directly in release packages:

1. **PromptFont (`assets/promptfont/promptfont.ttf`):**
   - Author: PromptFont Project.
   - License: SIL Open Font License 1.1 (`assets/promptfont/LICENSE.txt`).
   - Status: **CLEARED**.
2. **LatoLatin (`assets/LatoLatin-*.ttf` - Regular, Bold, Italic, BoldItalic):**
   - Author: Łukasz Dziedzic (http://www.latofonts.com/), tyPoland.
   - Version: 2.015 (2015-08-06).
   - License Text: SIL Open Font License 1.1, sourced from upstream RmlUi (`recompui/lib/RmlUi/Samples/assets/LICENSE.txt`).
   - Packaging: Bundled as `licenses/LatoLatin-OFL.txt`.
   - Status: **CLEARED**.
3. **Fredoka (`assets/Fredoka.ttf`):**
   - Author: The Fredoka Project Authors (Milena B. Brandão, Ben Nathan).
   - Upstream: `https://github.com/hafontia/Fredoka-One`.
   - License Text: SIL Open Font License 1.1, sourced from official repository `OFL.txt`.
   - Packaging: Bundled as `licenses/Fredoka-OFL.txt`.
   - Status: **CLEARED**.
4. **NotoEmoji (`assets/NotoEmoji-Regular.ttf`):**
   - Author: Google Inc. / Monotype Imaging Inc.
   - Version: 1.05 uh.
   - License Text: SIL Open Font License 1.1, sourced from upstream RmlUi (`recompui/lib/RmlUi/Samples/assets/LICENSE.txt`).
   - Packaging: Bundled as `licenses/NotoEmoji-OFL.txt`.
   - Status: **CLEARED**.

---

## 4. Theme SVG / PNG Asset Classification

Every graphical asset bundled in `.deps-renderer/recomp-theme/assets` was committed by `Chris Lewis <chris.lewis2@gmail.com>` in commit `0cb9a83a263607fbc8ab6176a758a00726e237cc`. The repository contains no license file. Each asset is classified as follows:

| Filename | Type | Author / Origin | Code Reference / Runtime Use | Classification | Recommended Action |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `assets/board.svg` | Vector art | Chris Lewis (Inkscape) | **None.** Unreferenced in C++ and RCSS. | **NOT SHIPPED** | Exclude from packaged zip. |
| `assets/board-selected.svg` | Vector art | Chris Lewis (Inkscape) | **None.** Unreferenced in C++ and RCSS. | **NOT SHIPPED** | Exclude from packaged zip. |
| `assets/rock.png` | Raster art | Chris Lewis | **None.** Unreferenced in C++ and RCSS. | **NOT SHIPPED** | Exclude from packaged zip. |
| `assets/icons/Caret.svg` | UI Icon | Chris Lewis (Figma) | RecompFrontend UI dropdowns | **BLOCKER** | Seek upstream license grant. |
| `assets/icons/Cont.svg` | UI Icon | Chris Lewis (Figma) | Controller toggle UI | **BLOCKER** | Seek upstream license grant. |
| `assets/icons/Keyboard.svg` | UI Icon | Chris Lewis (Figma) | Keyboard mapping UI | **BLOCKER** | Seek upstream license grant. |
| `assets/icons/PlusKeyboard.svg`| UI Icon | Chris Lewis / thecozies | Multiplayer player assignment | **BLOCKER** | Seek upstream license grant. |
| `assets/icons/Question.svg` | UI Icon | Chris Lewis (Figma) | Help and unmapped button hint | **BLOCKER** | Seek upstream license grant. |
| `assets/icons/Quit.svg` | UI Icon | Chris Lewis (Figma) | Quit confirmation dialog | **BLOCKER** | Seek upstream license grant. |
| `assets/icons/RecordBorder.svg`| UI Icon | Chris Lewis (Figma) | Binding capture indicator | **BLOCKER** | Seek upstream license grant. |
| `assets/icons/RecordSpinner.svg`| UI Icon | Chris Lewis (Figma)| Binding capture spinner | **BLOCKER** | Seek upstream license grant. |
| `assets/icons/Reset.svg` | UI Icon | Chris Lewis (Figma) | Default configuration reset | **BLOCKER** | Seek upstream license grant. |
| `assets/icons/Trash.svg` | UI Icon | Chris Lewis (Figma) | Profile deletion button | **BLOCKER** | Seek upstream license grant. |
| `assets/icons/X.svg` | UI Icon | Chris Lewis (Figma) | Modal close button | **BLOCKER** | Seek upstream license grant. |

---

## 5. N64ModernRuntime GPLv3 Compliance

N64ModernRuntime (`.deps-runtime/N64ModernRuntime`) is licensed under the **GNU General Public License v3.0** (`COPYING`).

### Linkage Mode
`SnowboardKidsRecompiled` statically links `libultramodern.a` and `librecomp.a`. Static linking creates a single combined executable work under copyright law.

### Technical Obligations
An engineering compliance requirement based on the license text mandates that if binary executables are distributed:
1. **License Copy:** A complete copy of GNU GPLv3 must accompany the distribution package.
2. **Complete Corresponding Source Availability:** The complete corresponding source code of the entire combined executable—including all original project code (`src/`), build scripts, dependency patches, and N64ModernRuntime source—must be made available under GPLv3 terms (§ 6).
3. **No Additional Restrictions:** Downstream recipients must receive all rights granted by the GPLv3.

---

## 6. Project License Options for Repository Owner

Selection of a project license belongs exclusively to the project owner (`HikariLucy`). The following options are technically plausible under current binary linkage:

| Option | Compatibility with Binary Linkage | Consequences | Open Questions |
| :--- | :--- | :--- | :--- |
| **GNU GPL v3.0** | Fully compatible with static linkage of N64ModernRuntime. | Entire combined binary is governed uniformly under GPLv3. Source disclosure obligations satisfied. | Limits proprietary commercial use; does not solve proprietary game IP distribution. |
| **MIT License** | Incompatible for compiled binary distribution; compatible for original source repo only. | Original code is permissive, but any distributed binary combining it with N64ModernRuntime must be conveyed under GPLv3. | Creates dual-licensing complexity between source repository and distributed binary. |
| **Source-Available / Non-Commercial** | Legally incompatible with GPLv3 binaries. | Cannot distribute binaries linking N64ModernRuntime under non-commercial or proprietary terms (GPLv3 § 10 forbids further restrictions). | Would require complete decoupling from N64ModernRuntime or clean-room reimplementation. |

*Status: OWNER DECISION REQUIRED. Engineer must not select or commit a root LICENSE.*

---

## 7. Technical Inventory of Game-Derived Material

Software licensing applies to original human author contributions. Commercial video game bytecode translated by recompiler tools represents game-derived material with distinct legal implications:

| Category | Produced from ROM? | Required at Runtime? | Committed in Git? | Present in Binary? | Reconstructable from User ROM? | Can Be Generated Locally? | Current Package Contains It? |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **RecompiledFuncs** | **YES** (N64 bytecode translation) | **YES** (core game logic) | **NO** | **YES** (in game binary) | **YES** (via N64Recomp) | **YES** | Excluded in ROM-free CI; present in local game builds. |
| **Symbols / Offsets** | **YES** (reverse-engineered) | **YES** (function tables) | **YES** (`catalog.json`, `us.toml`)| **YES** | **YES** | **YES** | Factual metadata committed in Git. |
| **Config / TOML** | **NO** (authored specs) | **NO** (tool input only)| **YES** (`us.toml`, `aspMain.us.toml`)| **NO** | **N/A** | **YES** | Build config only; not shipped in binary. |
| **RSP / Microcode** | **YES** (extracted task code) | **YES** (audio processing)| **NO** | **YES** (in game binary) | **YES** (via RSP recompiler)| **YES** | Recompiled locally via `scripts/run-rsp-recompiler.sh`. |
| **Static Tables** | **YES** (embedded game data) | **YES** (physics/data) | **NO** | **YES** (in game binary) | **YES** | **YES** | Embedded inside recompiled functions. |
| **Game Audio/Visual Assets** | **YES** (textures, soundbanks)| **YES** | **NO** | **NO** (accessed from ROM) | **YES** (read from user ROM) | **YES** | Never committed or bundled in repository. |
| **Generated Headers** | **NO** (build metadata) | **YES** (version query) | **NO** | **YES** | **YES** | **YES** | `sbk_version.h` generated deterministically. |

---

## 8. User-ROM Distribution Model Feasibility Study

To eliminate copyright infringement risks associated with distributing recompiled commercial game code, four distribution architectures have been evaluated:

| Criterion | Model A: Prebuilt Full Binary | Model B: Source + Local ROM Recompilation | Model C: Launcher / Bootstrap Local Builder | Model D: Split Engine + Local Dynamic Game Module |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Technical Complexity** | Lowest | Moderate (requires full C++ dev toolchain) | High (requires bundling recompiler + lightweight compiler/linker) | High (engine plugin architecture with dynamic library loading) |
| **User Experience** | Turnkey (download and run) | Poor (requires developer setup: git, cmake, ninja, clang) | Good (GUI asks user for ROM, generates game module in background) | Excellent (engine ships prebuilt; user selects ROM; engine compiles/links module) |
| **CI Implications** | CI produces ready-to-run binary (risk of ROM copyright violation) | CI compiles only verification targets (ROM-free) | CI builds engine + toolchain binaries; no ROM needed | CI builds portable engine executable and host runner cleanly |
| **Distributable Material** | Game code + engine combined | Source code only | Engine + recompiler tools only | Prebuilt engine executable + UI theme + licenses |
| **Locally Generated** | None | Entire project from source | Recompiled C + RSP + final executable | `game.so` / `game.dll` compiled from user ROM |

### Most Feasible Technical Approach
**Model D (Split Engine + Local Dynamic Game Module)** or **Model C (Self-Contained ROM-Consuming Launcher)** offer the optimal balance of user accessibility and strict copyright hygiene. Neither model requires uploading or distributing commercial game data.

---

## 9. Strict "No ROM Upload" Guarantee

Any distribution architecture deployed in `SnowboardKids-Recomp` must enforce:
1. **Never upload ROM:** Hosted GitHub Actions and CI workflows must never require, fetch, cache, or process commercial ROMs.
2. **Never store ROM in cloud:** No cloud storage or release artifact hosting may contain game bytecode or copyrighted assets.
3. **Local Conversion:** Recompilation of CPU and RSP microcode must occur entirely on the end user's machine.
4. **Preserve ROM Validation:** Sha-1 / MD5 validation hashes must continue to verify user ROM integrity before generating local code.

---

## 10. Summary of Release Readiness Gates

### Cleared Checks
- `PASS dependency_provenance`: Canonical Git pin provenance verified; no drift.
- `PASS patch_compatibility`: All patch series match clean base commit trees.
- `PASS font_licenses`: OFL 1.1 license texts bundled for PromptFont, LatoLatin, Fredoka, and NotoEmoji.
- `PASS decorative_assets_excluded`: Unused artwork (`board.svg`, `rock.png`) excluded from distribution packages.
- `PASS artifact_identity`: Packages bound to `Dependency-Lock-Digest` and Git HEAD.

### Remaining Distribution Blockers
- `BLOCKER project_license_missing`: Repository owner must declare and commit project license terms.
- `BLOCKER dependency_gpl_compliance`: GPLv3 complete corresponding source disclosure obligations must be implemented before binary distribution.
- `BLOCKER upstream_recompfrontend_license`: Awaiting upstream license clarification from RecompFrontend authors.
- `BLOCKER theme_ui_icons_license`: Awaiting upstream license confirmation for UI navigation icons in `recomp-theme`.
- `BLOCKER game_derived_distribution_model`: Production deployment of a local ROM consumption model (Model C or D) must replace direct game binary distribution.
