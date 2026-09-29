# Release engineering — v0.9.0-beta

Updated: 2026-09-28

This document describes the first public playable beta of Snowboard Kids
Recompiled. The validated release platform is Linux x86-64.

## Release model

The public archive uses the split runtime architecture:

```text
SnowboardKidsEngine
    + reviewed frontend/assets
    + bundled SnowboardKidsGame.so
    + license/source notices

user supplies supported Snowboard Kids (USA) ROM
    -> ROM validation
    -> gameplay
```

The archive contains no ROM and no extracted game assets. The bundled game
module exists so normal users do not need Python or a C++ compiler.

A user-installed module in
`~/.local/share/SnowboardKids/modules/snowboardkids-us/` takes precedence over
the bundled module.

## Exact dependency pins

`scripts/dependency_lock.py` is the source of truth for upstream revisions and
canonical patch order.

| Dependency | Pinned commit |
| --- | --- |
| N64Recomp | `ffb39cdad1da5de07eaaa48bd1db4a89a7986771` |
| N64ModernRuntime | `6ccb2e7c2e7f6708257b461097e0aaf03c445e2a` |
| RecompFrontend | `e85b912d9df677b04f9358867dd010c8af27ea05` |
| RT64 | `6a4166b2cfa952d931a08481d1037da995f28b54` |
| Theme | `0cb9a83a263607fbc8ab6176a758a00726e237cc` |

## License/distribution policy

Project-authored source is GPL-3.0. The engine links N64ModernRuntime
(GPL-3.0); the public release points to the exact source tag and preserves the
project build scripts, dependency pins and runtime patch series.

RecompFrontend does not currently have a top-level license grant in the pinned
checkout. Upstream clarification remains pending at:

https://github.com/N64Recomp/RecompFrontend/issues/44

The beta proceeds with that uncertainty explicitly disclosed in:

- `THIRD_PARTY_NOTICES.md`
- `SOURCE-COMPLIANCE.md`
- `docs/BETA-DISTRIBUTION-POLICY.md`
- the GitHub Release notes

This is an explicit release-policy decision, not an inferred RecompFrontend
license.

## User-data path

- Linux: `$XDG_DATA_HOME/SnowboardKids` or `~/.local/share/SnowboardKids`
- Windows: `%APPDATA%\SnowboardKids`
- Override: `SBK_USER_DATA_DIR`

The release archive must not contain user configuration, saves, Controller Pak
files, savestates or `portable.txt`.

## Building the Linux release candidate

The local validated build tree already contains the generated continuation
corpus required to compile the engine/module. A public hosted runner never
receives a ROM.

Configure/build the current release branch as usual, then make sure the
ROM-free engine and reviewed local module exist:

```bash
cmake --build build-renderer-stack --target SnowboardKidsEngine -j "$(nproc)"

ls -lh build-renderer-stack/SnowboardKidsEngine
ls -lh ~/.local/share/SnowboardKids/modules/snowboardkids-us/SnowboardKidsGame.so

./build-renderer-stack/SnowboardKidsEngine --version
./build-renderer-stack/SnowboardKidsEngine \
  --validate-module ~/.local/share/SnowboardKids/modules/snowboardkids-us/SnowboardKidsGame.so
```

Create the deterministic release package:

```bash
bash scripts/package-beta-linux.sh
```

Expected outputs:

```text
dist/SnowboardKidsRecompiled-v0.9.0-beta-Linux-x86_64.zip
dist/SHA256SUMS.txt
```

The script refuses dirty source trees, stale binaries, missing modules and
invalid module ABI. It stages only reviewed assets, creates the archive, runs
the artifact audit and executes the disclosed public-beta readiness policy.

## Fresh-user release test

Do not test the archive from the repository directory.

```bash
rm -rf /tmp/sbk-v090-fresh
mkdir -p /tmp/sbk-v090-fresh
cd /tmp/sbk-v090-fresh
unzip ~/proyectos/Recomp/SnowboardKids-Recomp/dist/SnowboardKidsRecompiled-v0.9.0-beta-Linux-x86_64.zip
cd SnowboardKidsRecompiled
./SnowboardKidsEngine
```

Validate at minimum:

1. the application opens from the extracted directory;
2. it finds the bundled game module without compiling anything;
3. it asks for the user's ROM;
4. the supported ROM validates;
5. title/menu gameplay starts;
6. controller/keyboard input works;
7. an existing save can be loaded if desired;
8. F5/F8 savestates work;
9. closing and reopening works from the extracted build.

No development directory may be required for this test.

## Artifact layout

```text
SnowboardKidsRecompiled/
├── SnowboardKidsEngine
├── modules/
│   └── snowboardkids-us/
│       └── SnowboardKidsGame.so
├── assets/
├── licenses/
├── LICENSE
├── THIRD_PARTY_NOTICES.md
├── SOURCE-COMPLIANCE.md
├── BETA-DISTRIBUTION-POLICY.md
├── BUILD-INFO.txt
└── RUNNING.md
```

The audit rejects ROM headers/extensions, user data, saves, Controller Pak
images, savestates, logs, build junk, symlinks and personal absolute paths.
The game module is permitted only at the canonical module path.

## CI

`.github/workflows/ci.yml` is ROM-free and runs on Linux and Windows.
`.github/workflows/renderer-compile.yml` verifies the Windows clang-cl
RT64/RecompFrontend/runtime compile path.

Neither workflow stores a commercial ROM.

The release artifact itself is assembled from the locally validated Linux
engine/module because the game-module generation inputs are intentionally not
placed in GitHub Actions.

## Tag and GitHub Release

Only after the fresh-user archive test passes:

```bash
git switch main
git pull --ff-only origin main
git tag -a v0.9.0-beta -m "Snowboard Kids Recompiled v0.9.0-beta"
git push origin v0.9.0-beta

gh release create v0.9.0-beta \
  dist/SnowboardKidsRecompiled-v0.9.0-beta-Linux-x86_64.zip \
  dist/SHA256SUMS.txt \
  --prerelease \
  --title "Snowboard Kids Recompiled v0.9.0-beta" \
  --notes-file docs/releases/v0.9.0-beta.md
```

The release notes must retain the RecompFrontend licensing disclosure.

## Windows status

Windows is **in development**, not supported. Hosted CI builds the ROM-free
`SnowboardKidsEngine.exe` (`SBK_ENGINE_ONLY=ON`, `windows-engine` job), checks
its `--version` against HEAD, lists its DLL dependencies, runs the synthetic
module probe and uploads an engine-only draft archive. The Windows game module
path and packaging (`scripts/package-beta-windows.py`) exist but a real
`SnowboardKidsGame.dll` and a live gameplay test are still missing. The only
DirectX Shader Compiler file shipped is `dxcompiler.dll` from a SHA-256-pinned
official Microsoft release (v1.8.2505.1), with its license texts; the validator
`dxil.dll` is neither needed nor shipped (`docs/DXC-PROVENANCE.md`). See `docs/WINDOWS.md` for the level-by-level status and the live
validation checklist. Do not label Windows as supported until that test passes.
