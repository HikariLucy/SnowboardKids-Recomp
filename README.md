# SnowboardKids-Recomp

Experimental native recompilation project for **Snowboard Kids (Nintendo 64, USA)**.

The goal is to study the original game, reuse public reverse-engineering knowledge from the matching decompilation, and build a native PC runtime using the modern N64 recompilation ecosystem.

> This repository does **not** distribute the original ROM, copyrighted game assets, or proprietary Nintendo/Atlus/Racdym code.

## Current status

**Phase 0 — reproducible decomp environment: COMPLETE**

Validated locally on Linux:

- Original dump normalized to big-endian `.z64`
- Expected SHA-1 verified
- Matching decompilation cloned and dependencies installed
- Assets extracted successfully
- Upstream project rebuilt successfully
- Rebuilt ROM verified byte-for-byte against the expected SHA-1

Expected USA ROM SHA-1:

```text
1583bacc9046a360df8ea4d536942155247e154c
```

Successful baseline:

```text
[ verify ]  Checking snowboardkids.sha1
build/snowboardkids.z64: OK
```

## Project direction

The intended pipeline is:

```text
Legally obtained Snowboard Kids ROM
        |
        v
Matching decompilation / symbols / asset maps
        |
        v
N64Recomp analysis and configuration
        |
        v
Native recompiled game code
        |
        v
Modern N64 runtime + renderer
        |
        v
Linux / Windows native executable
```

## Upstream research

- Snowboard Kids matching decompilation:
  https://github.com/cdlewis/snowboardkids-decomp
- N64Recomp:
  https://github.com/N64Recomp/N64Recomp

The decompilation project is a research reference and is **not** itself a PC port.

## Development rules

1. Never commit ROM files.
2. Never commit extracted copyrighted assets unless their redistribution is explicitly permitted.
3. Keep generated build outputs out of Git.
4. Document every baseline hash and upstream revision used.
5. Prefer reproducible scripts over manual binary edits.
6. Keep upstream decomp modifications separate from recomp-specific code where possible.

## Documentation

- [Current status](docs/STATUS.md)
- [Roadmap](docs/ROADMAP.md)

## Disclaimer

This is an unofficial preservation and reverse-engineering research project. Snowboard Kids and related trademarks/assets belong to their respective rights holders.
