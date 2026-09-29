# Third-party notices and distribution review

This file records the licensing/provenance status of the dependency tree used by
Snowboard Kids Recompiled `v0.9.0-beta`. It is an inventory and disclosure,
not a substitute for the license texts themselves and not a legal opinion.

The public beta archive includes this file, the project GPL-3.0 license, the
available dependency license texts and `SOURCE-COMPLIANCE.md`.

| Component | Evidence in pinned checkout | Distribution status |
| --- | --- | --- |
| Project-authored source | Root `LICENSE` | GPL-3.0 |
| N64ModernRuntime | `.deps-runtime/N64ModernRuntime/COPYING` | GPL-3.0; corresponding-source/build information published with the release |
| N64Recomp | `.deps/N64Recomp/LICENSE` | MIT; notice included |
| RT64 | `.deps-renderer/rt64/LICENSE` | MIT; notice included |
| RecompFrontend | No top-level LICENSE file in pinned tree | **PENDING UPSTREAM CLARIFICATION**: issue #44 is open; the beta proceeds with this uncertainty explicitly disclosed |
| SDL2 | Dynamically linked (`libSDL2-2.0.so.0` / `SDL2.dll`); Windows `SDL2.dll` 2.26.3 from RT64's pinned `mupen64plus-win32-deps` | zlib license; Windows packages ship `licenses/SDL2.txt` from the pinned `COPYING.txt` |
| DirectX Shader Compiler `dxcompiler.dll` 1.8.2505.32 | Windows only: official Microsoft DXC `v1.8.2505.1` release archive, SHA-256 pinned in `scripts/dxc_redist.py` (not RT64's unsigned contrib build; see `docs/DXC-PROVENANCE.md`); `rt64` links `dxcompiler.lib`, and the same release's `dxc.exe` compiles the shaders at build time | LLVM Release License (University of Illinois/NCSA) per the release notes; Windows packages ship `licenses/DirectXShaderCompiler-LICENSE-LLVM.txt`, `-LICENSE.txt` and `-ThirdPartyNotices.txt` (upstream texts at the release commit). The separately licensed validator `dxil.dll` is not shipped or needed: this release signs shaders itself. Not part of the Linux release |
| Microsoft Visual C++ runtime (`vcruntime140*.dll`, `msvcp140*.dll`) | Windows only, if the engine/module import them | Microsoft Visual C++ Redistributable distributable code, deployed app-local from the build machine's `VCToolsRedistDir` |
| {fmt} | `.deps/N64Recomp/lib/fmt/LICENSE` | MIT |
| RmlUi | `.deps-renderer/RecompFrontend/recompui/lib/RmlUi/LICENSE.txt` | MIT |
| toml++ | `.deps/N64Recomp/lib/tomlplusplus/LICENSE` | MIT |
| Rabbitizer | `.deps/N64Recomp/lib/rabbitizer/LICENSE` | MIT |
| stb | `.deps-renderer/rt64/src/contrib/stb/LICENSE` | MIT / public-domain style terms |
| Dear ImGui | `.deps-renderer/rt64/src/contrib/imgui/LICENSE.txt` | MIT |
| nativefiledialog-extended | `.deps-renderer/rt64/src/contrib/nativefiledialog-extended/LICENSE` | zlib |
| Theme PromptFont | pinned theme `promptfont/LICENSE.txt` | OFL 1.1 |
| Theme LatoLatin | `licenses/LatoLatin-OFL.txt` | OFL 1.1 |
| Theme Fredoka | `licenses/Fredoka-OFL.txt` | OFL 1.1 |
| Theme Noto Emoji | `licenses/NotoEmoji-OFL.txt` | OFL 1.1 |
| Project UI icons/background | `assets/sbk-ui` provenance README | project-owned assets |
| Upstream theme decorative drawings | excluded by the staging allowlist | not shipped |
| Snowboard Kids ROM / extracted commercial assets | release audit forbids ROM extensions/headers and extracted user data | **NOT SHIPPED** |
| Precompiled `SnowboardKidsGame` module | generated from the reviewed recompilation corpus/RSP path; dynamically loaded by the engine | beta compatibility artifact; contains recompiled game logic, no ROM/assets; original-game rights are not claimed by this project |

## RecompFrontend clarification

The pinned RecompFrontend checkout has no top-level license grant. The project
has requested clarification upstream:

https://github.com/N64Recomp/RecompFrontend/issues/44

The `v0.9.0-beta` distribution intentionally does **not** infer or invent a
license for RecompFrontend. Its unresolved status is disclosed here, in the
beta distribution policy and in the GitHub Release notes.

## Corresponding source

The release page must provide the exact source tag/commit and clear directions
to:

https://github.com/HikariLucy/SnowboardKids-Recomp

See `SOURCE-COMPLIANCE.md` for the source/build mapping.

## Game-content boundary

No Snowboard Kids ROM, save file, Controller Pak image, savestate, extracted
texture, audio, music, level data or other original game asset may be included
in the archive.

The user must provide their own supported Snowboard Kids (USA) ROM and the
engine validates it before gameplay.

See `docs/BETA-DISTRIBUTION-POLICY.md` for the public-beta release policy.
