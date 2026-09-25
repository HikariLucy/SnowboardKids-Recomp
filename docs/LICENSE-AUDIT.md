# License and Distribution Audit

Date: 2026-09-25  
Baseline: `feat/release-distribution` (`e430e84c8bad`)

This document performs an exhaustive audit of all components statically linked,
bundled, or derived in `SnowboardKids-Recomp`. Every finding is sourced strictly
from actual license files and repository headers in the pinned checkout.

---

## 1. Inventory of Distributed Components

| Component | Pinned Version / Commit | License File in Tree | License Identifier | Redistribution Obligation | Status |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Project Source** | `e430e84c8bad` | None in root | Unassigned | Owner must establish terms before public distribution | **OWNER DECISION REQUIRED** |
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
| **PromptFont** | Theme asset | `assets/promptfont/LICENSE.txt` | SIL Open Font License 1.1 | Retain OFL license text alongside font | **CLEARED** |
| **LatoLatin Fonts** | Theme assets | Unbundled | SIL Open Font License 1.1 | OFL § 2 requires accompanying license text | **RELEASE BLOCKER** |
| **Fredoka Font** | Theme asset | Unbundled | SIL Open Font License 1.1 | OFL § 2 requires accompanying license text | **RELEASE BLOCKER** |
| **NotoEmoji Font** | Theme asset | Unbundled | SIL Open Font License 1.1 | OFL § 2 requires accompanying license text | **RELEASE BLOCKER** |
| **Theme UI SVGs/PNGs** | `0cb9a83a2636` | None in repo | Unspecified by author | Document provenance and redistribution rights | **RELEASE BLOCKER** |

---

## 2. Project License Compatibility Analysis

The choice of license for `SnowboardKids-Recomp` is constrained by its dependency graph:

1. **N64ModernRuntime is GNU GPL v3.0:**
   Because `SnowboardKidsRecompiled` statically links `librecomp` and `ultramodern`, the resulting combined executable is a covered work under GPLv3. If binary artifacts of the complete game are distributed publicly, the entire combined work must be licensed under GPLv3 (or a GPLv3-compatible license), and complete corresponding source code must be made available.

2. **Permissive Dependencies:**
   N64Recomp, RT64, {fmt}, RmlUi, toml++, Rabbitizer, stb, ImGui, and nativefiledialog-extended are licensed under permissive terms (MIT, Zlib, Public Domain). These licenses permit static linking into a GPLv3 binary provided their respective copyright notices are preserved.

3. **Status:**
   The repository author has not yet committed a root `LICENSE` file. Selecting a license is solely the prerogative of the repository owner (`HikariLucy`).

---

## 3. Game-Derived Material and Copyright Separation

Software code licenses apply to copyrightable source code written by project developers or third parties. They do **not** convey intellectual property rights to material derived from proprietary commercial video games.

The repository contents are categorized as follows:

| Category | Components | Legal / Redistribution Assessment |
| :--- | :--- | :--- |
| **Original Project Code** | `src/` (runtime wrappers, PFS/Pak, quiescence, savestates) | Author-owned original code; licensable at owner's discretion |
| **Third-Party Open Source** | `.deps/`, `.deps-runtime/`, `.deps-renderer/` | Governed by upstream licenses detailed in Section 1 |
| **Generated from User ROM** | `RecompiledFuncs/`, `rsp/aspMain.cpp` | Machine-translated from commercial ROM bytecode. Distribution of compiled binaries containing these translations carries proprietary copyright infringement risk. |
| **Game-Derived Static Metadata** | `us.toml`, `aspMain.us.toml`, symbol addresses | Reverse-engineering factual metadata (entrypoints, hardware offsets). Generally fair use / functional interoperability data under many jurisdictions. |
| **Commercial ROM / Assets** | None in repository | Explicitly excluded from repository and distribution packages. |

**Conclusion:**  
Even if all software licenses are formally cleared, the public redistribution of compiled binaries containing recompiled game logic remains a distinct legal question. For this reason, the release engineering architecture enforces a clean separation:
- CI runners compile only ROM-free verification targets.
- Local release builds require the user to supply their own legally obtained ROM and matching locally compiled ELF.
- Binary packages must not be uploaded to public GitHub Releases until this distribution posture is resolved.

---

## 4. Font and Theme Asset Actions

To clear the font blockers before public distribution:
1. Locate the exact SIL Open Font License 1.1 texts for `LatoLatin` (Łukasz Dziedzic), `Fredoka` (Milena Brandao), and `NotoEmoji` (Google).
2. Place corresponding `LICENSE.txt` files in `assets/fonts/` or ensure they are bundled in `licenses/` in the packaged zip.
3. Obtain confirmation from `snowboardkids-recomp-theme` upstream regarding the redistribution license of the SVG and PNG artwork (`board.svg`, `rock.png`, etc.).

---

## 5. Summary of Release Blockers

1. `BLOCKER project_license_missing`: Repository owner must establish and commit a project license.
2. `BLOCKER gpl_compliance`: Distribution of binaries linking `N64ModernRuntime` must adhere to GPLv3 source disclosure and licensing requirements.
3. `BLOCKER upstream_recompfrontend_license`: RecompFrontend lacks an explicit top-level license grant.
4. `BLOCKER theme_font_notices`: OFL license files for Lato, Fredoka, and NotoEmoji must be bundled.
5. `BLOCKER theme_asset_provenance`: Redistribution rights for theme artwork must be documented.
6. `BLOCKER game_derived_code_distribution`: Legal distribution position for recompiled game code must be established.
