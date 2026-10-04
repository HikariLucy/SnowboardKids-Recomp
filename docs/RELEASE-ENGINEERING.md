# Release engineering — v0.9.0-beta.1

Updated: 2026-10-03

This document describes the cross-platform public beta refresh of Snowboard
Kids Recompiled. Linux x86-64 and Windows x86-64 are both validated release
platforms for `v0.9.0-beta.1`.

## Release model

The public archive uses the split runtime architecture:

```text
SnowboardKidsEngine(.exe)
    + reviewed frontend/assets
    + bundled SnowboardKidsGame.so / SnowboardKidsGame.dll
    + license/source notices

user supplies supported Snowboard Kids (USA) ROM
    -> ROM validation
    -> gameplay
```

The archive contains no ROM and no extracted game assets. The bundled game
module exists so normal users do not need Python or a C++ compiler.

A user-installed module takes precedence over the bundled module. The default
locations are `~/.local/share/SnowboardKids/modules/snowboardkids-us/` on
Linux and `%APPDATA%\SnowboardKids\modules\snowboardkids-us\` on Windows.

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

Create the deterministic Linux release package for beta.1:

```bash
SBK_RELEASE_VERSION=0.9.0-beta.1 \
SBK_RELEASE_OUT="$PWD/dist-beta1/linux" \
bash scripts/package-beta-linux.sh
```

Expected outputs:

```text
dist-beta1/linux/SnowboardKidsRecompiled-0.9.0-beta.1-Linux-x86_64.zip
dist-beta1/linux/SHA256SUMS.txt
```

The script refuses dirty source trees, stale binaries, missing modules and
invalid module ABI. It stages only reviewed assets, creates the archive, runs
the artifact audit and executes the disclosed public-beta readiness policy.

## Fresh-user release test

Do not test the archive from the repository directory.

```bash
rm -rf /tmp/sbk-v090-beta1-fresh
mkdir -p /tmp/sbk-v090-beta1-fresh
cd /tmp/sbk-v090-beta1-fresh
unzip ~/proyectos/Recomp/SnowboardKids-Recomp/dist-beta1/linux/SnowboardKidsRecompiled-0.9.0-beta.1-Linux-x86_64.zip
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
├── ROM-IDENTITY.md
├── LAUNCHER-INTEGRATION.md
├── BUILD-INFO.txt
├── RUNNING.md
└── scripts/
    └── rom_identity.py
```

The audit rejects ROM headers/extensions, user data, saves, Controller Pak
images, savestates, logs, build junk, symlinks and personal absolute paths.
The game module is permitted only at the canonical module path.

`ROM-IDENTITY.md` and `LAUNCHER-INTEGRATION.md` are required release metadata.
The readiness gate verifies that the canonical SHA-256 and the external
user-data ownership boundary remain documented. `scripts/rom_identity.py` is
ROM-free tooling: it inspects a user-supplied source locally and never writes or
uploads ROM bytes.

## CI

`.github/workflows/ci.yml` is ROM-free and runs on Linux and Windows.
`.github/workflows/renderer-compile.yml` verifies the Windows clang-cl
RT64/RecompFrontend/runtime compile path.

Neither workflow stores a commercial ROM.

The release artifact itself is assembled from the locally validated Linux
engine/module because the game-module generation inputs are intentionally not
placed in GitHub Actions.

## Tag and GitHub Release

Only after both fresh-user archive tests pass and both archives are built from
the same clean commit:

1. copy the final Windows ZIP next to the Linux artifact;
2. create one release-level `SHA256SUMS.txt` containing both archive hashes;
3. tag the exact tested commit;
4. publish both archives, the combined checksum file and the beta.1 notes.

Example final publication layout:

```text
dist-beta1/release/
├── SnowboardKidsRecompiled-0.9.0-beta.1-Linux-x86_64.zip
├── SnowboardKidsRecompiled-0.9.0-beta.1-Windows-x86_64.zip
└── SHA256SUMS.txt
```

Then:

```bash
git switch main
git pull --ff-only origin main
git tag -a v0.9.0-beta.1 -m "Snowboard Kids Recompiled v0.9.0-beta.1"
git push origin v0.9.0-beta.1

gh release create v0.9.0-beta.1 \
  dist-beta1/release/SnowboardKidsRecompiled-0.9.0-beta.1-Linux-x86_64.zip \
  dist-beta1/release/SnowboardKidsRecompiled-0.9.0-beta.1-Windows-x86_64.zip \
  dist-beta1/release/SHA256SUMS.txt \
  --prerelease \
  --title "Snowboard Kids Recompiled v0.9.0-beta.1" \
  --notes-file docs/releases/v0.9.0-beta.1.md
```

The release notes must retain the RecompFrontend licensing disclosure.

## Windows status

Windows x86-64 is validated for the beta.1 release. Hosted CI builds the
ROM-free `SnowboardKidsEngine.exe` with clang-cl, checks `--version` against
HEAD, verifies the renderer/runtime stack, exercises the synthetic module probe,
and guards the DXC/`dxil.dll` policy. A real `SnowboardKidsGame.dll` has also
been built and validated on physical Windows from a legitimate user ROM and
private module-input bundle.

Physical testing covers the ROM selector, title/race gameplay, D3D12/RT64
rendering, audio, keyboard, controller, rumble, official save persistence,
F5/F8 savestates, close/reopen and clean extracted-package execution. The
Windows release package ships `dxcompiler.dll` from the pinned official
Microsoft DXC v1.8.2505.1 release and does not ship `dxil.dll`.

See `docs/WINDOWS.md` for the detailed evidence and release checklist.
