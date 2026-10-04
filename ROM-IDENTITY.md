# Supported ROM Identity

This document defines the exact user-supplied game source accepted by
Snowboard Kids Recompiled. It is intended for players, launchers, catalog
maintainers, and integration tooling.

No ROM is included, downloaded, or uploaded by this project.

## Canonical source

Snowboard Kids Recompiled currently supports one corpus:

| Field | Canonical value |
| --- | --- |
| Title | Snowboard Kids |
| Region | USA / North America |
| Canonical byte order | Big-endian N64 (Z64) |
| Canonical size | 8,388,608 bytes (8 MiB) |
| Header magic | `80 37 12 40` |
| Internal name | `SNOWBOARD KIDS` |
| Game code | `NSKE` |
| SHA-256 | `58870ea67d49f778e7a7607eb270ad1d3a081a4733b337b2d607de2606dcfb3c` |
| SHA-1 | `1583bacc9046a360df8ea4d536942155247e154c` |
| MD5 | `eb31f4f9c1fe26a3a663f74e9790516e` |
| CRC32 | `020fb906` |
| N64 header CRC1 | `DBF4EA9D` |
| N64 header CRC2 | `333E82C0` |

The SHA-256 and SHA-1 above refer to the same canonical big-endian 8 MiB
image. The SHA-1 is also the identity used by the matching Snowboard Kids
decompilation.

## Accepted input forms

The runtime accepts the three common N64 byte orders and normalizes them in
memory before identity validation:

| Input form | Header magic | Normalization |
| --- | --- | --- |
| `.z64` | `80 37 12 40` | already canonical |
| `.v64` | `37 80 40 12` | swap each 16-bit pair |
| `.n64` | `40 12 37 80` | reverse each 32-bit word |

The filename extension is not trusted as identity. The image must normalize to
exactly 8 MiB and match the canonical cryptographic hashes. Unsupported
regions, revisions, hacks, patches, and modified images are rejected.

## Local verification

From a source checkout:

```bash
python3 scripts/rom_identity.py /path/to/your/SnowboardKids.rom
```

For machine-readable output:

```bash
python3 scripts/rom_identity.py --json /path/to/your/SnowboardKids.rom
```

The tool reads the supplied file locally, normalizes it in memory, prints only
identity metadata, and does not copy, upload, or retain ROM bytes.

A supported source exits with status 0. A valid N64 image with a different
identity exits with status 1. An unreadable or structurally invalid image exits
with status 2.

## Integration contract

Launchers should treat the canonical SHA-256 as the preferred exact source
identity and may retain the SHA-1 for compatibility with existing N64 tooling.
Game code `NSKE`, filename, size, or region label alone are insufficient to
establish compatibility.

Launchers must not download a commercial ROM on behalf of the user. The source
is user-provided and remains outside the release package and outside the
launcher-owned installation unless the user explicitly chooses otherwise.

## Cross-reference evidence

The canonical SHA-1 is published by the matching decompilation at:

- https://github.com/cdlewis/snowboardkids-decomp

The SHA-256/SHA-1/MD5/CRC32 tuple for the canonical big-endian image is also
cross-referenced by public ROM metadata catalogs. The project records the
SHA-256 here so downstream launchers can use a stronger exact-source identity
without redistributing game data.
