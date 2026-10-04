# Launcher Integration Contract

This document describes the stable integration boundary for third-party
launchers and catalogs. It is intentionally launcher-neutral and does not grant
redistribution rights beyond the licenses and notices that apply to each
component.

Portcove integration tracking: https://github.com/boburning/portcove/issues/1354

## 1. Direct upstream

- Repository: `HikariLucy/SnowboardKids-Recomp`
- Release channel: GitHub Releases
- Current public beta reviewed here: `v0.9.0-beta.1`
- Supported release platforms:
  - Linux x86-64
  - Windows x86-64

Current beta assets:

| Platform | Asset | SHA-256 |
| --- | --- | --- |
| Linux x86-64 | `SnowboardKidsRecompiled-0.9.0-beta.1-Linux-x86_64.zip` | `5f325afc55c597c7108567ccfc2007eac06c664c7e7d979f6b39d38bd76bef3c` |
| Windows x86-64 | `SnowboardKidsRecompiled-0.9.0-beta.1-Windows-x86_64.zip` | `7c0b49ed8513efc80044198b7941a78d1397e4e882ffb49be4a4398d7a0bcf38` |

A release-level `SHA256SUMS.txt` is published alongside the archives. Future
launchers should prefer the digest attached to the selected GitHub release or
its checksum sidecar rather than hard-coding the beta.1 digests above.

## 2. Installation and executable layout

A launcher-managed installation must preserve the extracted archive tree.

Linux:

```text
SnowboardKidsRecompiled/
├── SnowboardKidsEngine
├── modules/
│   └── snowboardkids-us/
│       └── SnowboardKidsGame.so
└── assets/...
```

Windows:

```text
SnowboardKidsRecompiled/
├── SnowboardKidsEngine.exe
├── modules/
│   └── snowboardkids-us/
│       └── SnowboardKidsGame.dll
├── assets/...
└── reviewed runtime DLLs...
```

Launch the engine from the package root. Setting the process working directory
to the package root is recommended. Do not move the bundled module away from
`modules/snowboardkids-us/` or flatten the internal asset tree.

Normal players do not compile anything. The public package already contains the
reviewed game module.

## 3. First-run source contract

On first run the engine asks the player to select their own Snowboard Kids
(USA) ROM. The engine performs local validation and remembers the selected
path.

Accepted source identity and byte-order normalization are defined in
[ROM-IDENTITY.md](ROM-IDENTITY.md).

The launcher must not provide, fetch, upload, bundle, patch, or silently replace
the commercial ROM.

## 4. Ownership and persistence boundary

Treat these three areas independently:

### Launcher-owned package tree

If the launcher downloaded/extracted the release, it may own only that extracted
package tree. Updates may replace that tree after normal integrity and safety
checks.

### User-owned ROM

The player's ROM is an external source. Merely selecting it does not transfer
ownership to the launcher or to Snowboard Kids Recompiled. Uninstall, update,
repair, rollback, or library removal must not delete or move it.

### User-owned mutable data

Default locations:

- Linux: `$XDG_DATA_HOME/SnowboardKids`, falling back to
  `~/.local/share/SnowboardKids`
- Windows: `%APPDATA%\SnowboardKids`
- Explicit override: `SBK_USER_DATA_DIR`

The entire user-data root is mutable persistent state and should be preserved
by default across package updates and uninstall/removal.

It can contain, among other data:

- configuration and controller-profile files;
- `.mpk` Controller Pak persistence;
- `.sbks` savestates;
- remembered source/configuration state;
- locally generated or user-installed game modules.

A launcher that offers backup/restore should treat the declared user-data root
as the persistence boundary unless it has more specific versioned knowledge.

## 5. Module precedence

The release carries a reviewed bundled game module at:

```text
modules/snowboardkids-us/SnowboardKidsGame.so
modules/snowboardkids-us/SnowboardKidsGame.dll
```

A module installed under the user's data directory intentionally takes
precedence over the bundled module. This is a supported developer/user override,
not evidence that the launcher-owned package was modified.

Launchers that display integrity state should distinguish:

- immutable/reviewed files in the installed release tree; and
- mutable user-owned override modules outside that tree.

Do not delete user-data modules as part of ordinary package repair or uninstall.

## 6. Safe minimum launcher operations

A conservative integration can offer only:

1. acquire or register an exact release package;
2. verify the release artifact;
3. launch the engine;
4. let the engine perform its own ROM selection and validation.

If a launcher manages updates, replace only the launcher-owned package tree and
preserve the external ROM and user-data root.

If a launcher cannot lawfully redistribute the binary package, a non-owning
route can instead register a user-prepared exact release and launch it in place.
That route must preserve the external files and remove only the launcher's
registration when deregistered.

## 7. Save semantics

Controller Pak files and savestates are intentionally separate persistence
domains.

- `.mpk`: original Controller Pak-style persistent storage.
- `.sbks`: temporary execution-state snapshots.
- Loading an `.sbks` does not rewind or replace the `.mpk` file.

A backup/rollback system must not assume that restoring a savestate also
restores Controller Pak state.

## 8. Licensing and redistribution

Project-authored source is GPL-3.0. N64ModernRuntime is GPL-3.0. Other
dependency licenses and notices are documented in:

- `THIRD_PARTY_NOTICES.md`
- `SOURCE-COMPLIANCE.md`
- `docs/LICENSE-AUDIT.md`
- `docs/BETA-DISTRIBUTION-POLICY.md`

The pinned RecompFrontend revision currently has no top-level project-wide
license grant. Clarification remains open at:

https://github.com/N64Recomp/RecompFrontend/issues/44

Do not infer a RecompFrontend license from this project's GPL-3.0 license.
Launchers planning managed redistribution should perform their own rights review
and may choose a non-owning user-prepared registration route while the upstream
clarification remains unresolved.

## 9. What launchers may rely on

For the current public-beta contract, launchers may rely on:

- explicit Linux x86-64 and Windows x86-64 release assets;
- root engine executables named above;
- bundled module under `modules/snowboardkids-us/`;
- runtime ROM selection with no player compilation step;
- the exact normalized USA source identity in `ROM-IDENTITY.md`;
- external mutable data under the documented user-data root;
- ROM-free release packaging and release-level SHA-256 integrity metadata.

Any broader platform, region, revision, automatic source acquisition, or
destructive save-management behavior requires separate evidence.
