# Release engineering (P0 draft, 2026-09-25)

Base: `f89785ca2e0f21d0bcf7e5111b28ba7a8540bc78` on
`feat/controller-pak-persistence`. This branch is infrastructure work, not a
public release or a claim of full game compatibility.

## Inputs and reproducibility

`scripts/dependency_lock.py` is the sole source of upstream URLs, exact commits
and canonical patch order. `scripts/bootstrap.sh` clones missing trees, rejects
wrong commits or origins, updates submodules and applies the canonical patches.
`scripts/dependency_patches.py` recognizes complete patch prefixes and refuses
partial/local changes. It is intended to be rerunnable. Existing legacy
bootstrap wrappers call this entrypoint.

| Dependency | Pinned commit |
| --- | --- |
| N64Recomp | `ffb39cdad1da5de07eaaa48bd1db4a89a7986771` |
| N64ModernRuntime | `6ccb2e7c2e7f6708257b461097e0aaf03c445e2a` |
| RecompFrontend | `e85b912d9df677b04f9358867dd010c8af27ea05` |
| RT64 | `6a4166b2cfa952d931a08481d1037da995f28b54` |
| Theme | `0cb9a83a263607fbc8ab6176a758a00726e237cc` |

Linux build prerequisites at this baseline: Git, CMake >= 3.20, Ninja,
Clang/Clang++, Python 3, pkg-config, SDL2, FreeType, GTK3 development
packages, a Vulkan-capable system/driver and the pinned dependencies' DXC
binary. A local validation environment used CMake 3.28.3, Clang 18.1.3,
Ninja 1.11.1, Python 3.12/3.13, SDL2 2.30.0, FreeType 26.1.20 as reported
by pkg-config, and GTK3 3.24.41. Node is not required by the build.

**Clean source checkout limit:** `RecompiledFuncs`, the continuation corpus,
`rsp/aspMain.cpp` and the matching decomp ELF are not tracked. The CPU corpus
needs the matching locally built ELF; RSP generation needs the user's verified
USA ROM. `scripts/build-release.sh` accepts `SBK_ELF` and `SBK_ROM` as explicit
external inputs and runs a Release configure/build. It never downloads a ROM.
A hosted public CI runner cannot reproduce the game binary from this source
checkout without those inputs. Do not put ROMs in GitHub secrets or artifacts.
The redistribution status of generated game-derived code is also unresolved.
Until that is resolved, there is no clean-checkout hosted binary artifact.

For a local legal build, supply the two files explicitly:

```sh
export SBK_ROM=/absolute/path/to/your/snowboardkids-usa.z64
export SBK_ELF=/absolute/path/to/your/matching/snowboardkids.elf
bash scripts/build-release.sh
```

The ROM must match USA SHA-1 `1583bacc9046a360df8ea4d536942155247e154c`.
The runtime's `select_rom()` compares the supported XXH3-64 identity
`0xF384619787B78D4B`. Header game code is `NSKE`. Filename alone is never
an identity check. No ROM location or download source is provided.

## Runtime and archive layout

The planned archive root is `SnowboardKidsRecompiled/` with executable,
`assets/`, `RUNNING.md`, `THIRD_PARTY_NOTICES.md` and available license texts.
The app resolves read-only assets from the executable's directory. On Linux,
frontend user data defaults to `~/.config/snowboardkids-recompiled`; Windows
uses Local AppData via the frontend. `SBK_USER_DATA_DIR` overrides it. An
existing development build directory with `CMakeCache.txt` and `runtime-data/`
continues to use that local folder. Controls, general/graphics/sound settings,
`.sbks` savestates and `.mpk` Controller Paks share the registered data path.
Do not place `portable.txt` in an artifact: RecompFrontend interprets it as a
request to write beside the executable. Migration of old development data is
manual and non-destructive.

No-argument launch opens the existing RecompFrontend file dialog. Explicit
ROM paths remain supported. `--help` and `--version` return before SDL/audio,
Vulkan, ROM validation or user-data creation. Invalid arguments return 2;
unsupported ROMs return 1; dialog cancellation returns 0.

`scripts/package_release.py` accepts only explicit binary, asset and shared
library inputs, emits a sorted fixed-timestamp ZIP and SHA256SUMS.txt, then
runs `scripts/audit_release_artifact.py`. The scanner rejects ROM extensions
and headers, saves/Paks, JSON user config, logs, build files, symlinks,
absolute personal paths and suspicious size. A `--draft` flag exists solely
for local synthetic-package tests while `THIRD_PARTY_NOTICES.md` contains
`RELEASE_BLOCKER`; it must never be used for a public upload.

## CI and test classes

`.github/workflows/ci.yml` configures ROM-free CMake targets on Ubuntu and
Windows. Linux also tests Controller Pak with pinned runtime headers; Windows
runs audio and release tooling tests. No GPU, display, audio device or ROM is
needed. `.github/workflows/artifacts.yml` is a manual readiness gate that
fails explicitly until licensing and clean-checkout generated-input blockers
are resolved. It does not upload a misleading binary or create a release.
There is no cache: correctness takes precedence over speed.

- **ROM-free:** audio progress, isolated Controller Pak, P2, savestate codec,
  release scanner/packager, and some controls/renderer fixtures after their
  dependencies are built.
- **Local ROM required:** generated corpus, RSP, full executable and automated
  navigation tests.
- **Human/live:** gameplay finish, original save, physical controller/rumble,
  multiplayer and visual validation. These are outside this release task.

Linux dynamic dependencies observed with `ldd` on the baseline local binary
include SDL2, libatomic, FreeType, GTK3, libc/libstdc++, audio backends and
X11/Wayland support. Do not bundle glibc. Recheck `ldd`, `RPATH` and package
launch from an arbitrary directory on the actual release candidate; that gate
is not yet complete. `RUNNING.md` covers missing runtime libraries, Vulkan,
audio, invalid ROM, unwritable data path and corrupt Pak.

Windows has a ROM-free CI compile path but no game binary build or artifact
claim. The full CMake target and generation scripts contain Clang/GNU flags,
Unix shell tools and Linux package assumptions. A Windows runner compile and
`--help`/`--version` smoke are required before declaring support. macOS is
**NEEDS WORK**: the pinned stack has a DXC/MoltenVK branch, but no build or
runtime evidence for this project.

## Source, assets and licensing

Tracked project files are project source/config/tests. The ignored generated
CPU corpus and RSP source are game-derived; user ROM and ELF are external.
Pinned dependencies and theme are third-party. The source repository has no
project LICENSE at this baseline, and several upstream/theme/font obligations
are not documented; see `THIRD_PARTY_NOTICES.md`. These are distribution
blockers, not a legal conclusion. No public artifact should be uploaded until
they are resolved. No game screenshots, ROM textures or extracted assets are
included by the package script.

## Version policy

Development version starts at `0.1.0` in CMake. `--version` reports that value
and the 12-character source Git commit. No timestamp is embedded. A future
approved release could use `v0.x.y` tags and archives named
`SnowboardKidsRecompiled-<version>-<platform>.zip`; no tag or GitHub Release
is created in this phase.
