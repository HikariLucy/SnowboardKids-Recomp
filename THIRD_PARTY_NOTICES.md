# Third-party notices and distribution review

This file is an inventory for the current pinned dependency tree, not a legal
opinion or a grant of permission. The archive script includes this file and
copies available top-level N64Recomp and RT64 license texts.

| Component | Evidence in pinned checkout | Distribution status |
| --- | --- | --- |
| Project source | No repository-level LICENSE in baseline repository | BLOCKER: owner decision required |
| N64ModernRuntime | `.deps-runtime/N64ModernRuntime/COPYING` (GPL-3.0) | BLOCKER: binary linking imposes GPL-3.0 copyleft and source disclosure terms |
| N64Recomp | `.deps/N64Recomp/LICENSE` (MIT) | CLEARED: notice included in `licenses/N64Recomp.txt` |
| RT64 | `.deps-renderer/rt64/LICENSE` (MIT) | CLEARED: notice included in `licenses/RT64.txt` |
| RecompFrontend | No top-level LICENSE file in pinned tree | BLOCKER: upstream author must provide license grant |
| SDL2 | Dynamically linked (`libSDL2-2.0.so.0` / `SDL2.dll`) | NOTICE REQUIRED: zlib license terms documented |
| {fmt} | `.deps/N64Recomp/lib/fmt/LICENSE` (MIT) | CLEARED: MIT notice preserved |
| RmlUi | `.deps-renderer/RecompFrontend/recompui/lib/RmlUi/LICENSE.txt` (MIT) | CLEARED: MIT notice preserved |
| toml++ | `.deps/N64Recomp/lib/tomlplusplus/LICENSE` (MIT) | CLEARED: MIT notice preserved |
| Rabbitizer | `.deps/N64Recomp/lib/rabbitizer/LICENSE` (MIT) | CLEARED: MIT notice preserved |
| stb | `.deps-renderer/rt64/src/contrib/stb/LICENSE` (MIT / Public Domain) | CLEARED: permissive terms satisfied |
| Dear ImGui | `.deps-renderer/rt64/src/contrib/imgui/LICENSE.txt` (MIT) | CLEARED: MIT notice preserved |
| nativefiledialog-extended | `.deps-renderer/rt64/src/contrib/nativefiledialog-extended/LICENSE` (Zlib) | CLEARED: notice included in `licenses/nativefiledialog-extended.txt` |
| Theme promptfont | `assets/promptfont/LICENSE.txt` (OFL 1.1) | CLEARED: notice bundled with font |
| Theme fonts (Lato, Fredoka, NotoEmoji) | Unbundled in `assets/` | BLOCKER: OFL Section 2 requires bundled license texts |
| Theme UI artwork (SVGs, PNGs) | Present in theme assets | BLOCKER: author documentation of distribution rights required |
| Game-derived code (CPU corpus, RSP) | Generated from user ROM | BLOCKER: legal distribution status of translated commercial game logic unresolved |

See `docs/LICENSE-AUDIT.md` for complete analysis and component audit.
No ROM or extracted game assets may be included in any distribution archive.
