# Third-party notices and distribution review

This file is an inventory for the current pinned dependency tree, not a legal
opinion or a grant of permission. The archive script includes this file and
copies available top-level N64Recomp and RT64 license texts.

| Component | Evidence in pinned checkout | Distribution status |
| --- | --- | --- |
| N64Recomp | `.deps/N64Recomp/LICENSE` | License file available; nested dependency notices require audit |
| RT64 | `.deps-renderer/rt64/LICENSE` | License file available; nested dependency notices require audit |
| N64ModernRuntime | No top-level LICENSE/NOTICE observed at pinned checkout | RELEASE_BLOCKER: determine applicable source and binary terms |
| RecompFrontend | No top-level LICENSE/NOTICE observed at pinned checkout | RELEASE_BLOCKER: determine applicable source and binary terms |
| snowboardkids-recomp-theme | No top-level LICENSE/NOTICE observed | RELEASE_BLOCKER: establish asset ownership and redistribution rights |
| Theme fonts: LatoLatin, Fredoka, NotoEmoji | Font files in theme assets, without adjacent license files | RELEASE_BLOCKER: find and include exact font license texts |
| Theme promptfont | `assets/promptfont/LICENSE.txt` | Preserve this notice with the font |
| Theme SVG/PNG/RCSS | Source in pinned theme checkout | RELEASE_BLOCKER: document provenance and rights |
| SDL2, RmlUi, fmt, tomlplusplus, Rabbitizer, stb, ImGui, DXC, nfd and other transitive code | Present in upstream dependency trees | RELEASE_BLOCKER: inventory linked or bundled components and notices per artifact |
| Project source | No repository-level LICENSE at baseline | RELEASE_BLOCKER: owner must choose a project license before public distribution |

The generated CPU corpus, RSP translation and binary contain material derived
from a user-supplied game. Their redistribution status is unresolved.
**RELEASE_BLOCKER:** establish a lawful distribution position for these outputs
before publishing executable artifacts. No ROM or extracted game assets may be
included in any archive.
