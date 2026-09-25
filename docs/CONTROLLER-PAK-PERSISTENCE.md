# Controller Pak persistence

## Provenance and root cause

Audited Snowboard Kids decomp 03088d8c5164644c7697846770b091d5265e53de,
its PR/os_pfs.h and src/ultra/io, and N64ModernRuntime pin
6ccb2e7c2e7f6708257b461097e0aaf03c445e2a. No libultra code was
copied: the backend and HLE adapter are original project code.

All public osPfs functions in the pinned librecomp/src/pak.cpp returned
PFS_ERR_NOPACK (1). The runtime input callback advertised RumblePak on
connected controllers. The stub was the direct save blocker; the accessory
model also had no ControllerPak variant. The game followed its no-Pak menu
path and could still enter gameplay.

## Game call map

All direct game callsites are in
src/menu/main_menu/controller_main_menu_flow.c in the decomp:

| Function | Lines | Arguments and flow | Return codes needed |
| --- | --- | --- | --- |
| osPfsInitPak | 182,184,242,294,403,459,478,503,540 | queue, handle, channel; probe before save/list/delete | 0, NOPACK, INCONSISTENT, ID_FATAL |
| osPfsFindFile | 244,311,405 | handle, identity, output number | 0 or INVALID with -1 |
| osPfsNumFiles | 255,542 | handle, max, used | 0; max 16 |
| osPfsFreeBlocks | 259,541 | handle, free bytes | 0; free pages times 256 |
| osPfsReadWriteFile | 321,434 | handle, number, flag, offset, size, buffer | 0 or range/media error |
| osPfsAllocateFile | 413 | handle, identity, byte size, output number | 0, EXIST, DATA_FULL, DIR_FULL |
| osPfsRepairId | 460 | handle | 0 after explicit redundant-ID repair, otherwise ID_FATAL |
| osPfsFileState | 480 | handle, number, state | 0 or INVALID |
| osPfsDeleteFile | 518 | handle, identity | 0 or INVALID |

osPfsChecker is called inside the original libultra init path, but not
directly by game code. osPfsIsPlug exists in decomp libultra, but no game
callsite was found. The lower __osPfsSelectBank and __osContRamRead/Write
symbols are referenced by excluded libultra routines, not directly by
the game. Their old compatibility shims remain.

## Raw image and storage

The service uses one 32 KiB raw single-bank image per port. The layout
from PR/os_pfs.h and osPfsInitPak is 32-byte blocks, 256-byte pages,
page 0 system/ID blocks, pages 1 and 2 inode/mirror, pages 3 and 4
directory (16 entries), and pages 5..127 data (123 pages, 31,488 bytes).
The game requests 121 pages. On open the backend validates ID checksums
and four copies, inode checksum/mirror, directory chains, page ownership,
and bounds. Standard single-bank mempak tooling should be able to inspect
the raw bytes, but import/export has not been independently tested.
Multi-bank images are unsupported.

The path uses recomp::get_config_path(): runtime-data/controller-paks/
port1.mpk through port4.mpk in this build. Missing images are formatted
on first use. Corrupt or wrong-size images are preserved and return
ID_FATAL or INCONSISTENT. An explicit repair can restore a damaged ID
copy when at least two matching valid redundant copies and the rest of
the filesystem validate. Unrecoverable images remain untouched.
A mutation writes a same-directory temp image,
flushes it, fsyncs it, renames it, then fsyncs the directory on POSIX.
Windows uses FlushFileBuffers and MoveFileEx with write-through. A mutex
serializes PFS operations; guest size validation precedes host allocation.

P1 is present by default. Connected P2-P4 ports get independent images;
disconnected ports return NOPACK. SBK_PFS_ABSENT=1 is a diagnostic P1
absent override. The frontend currently has no permanent Pak selector.
The PC port deliberately permits virtual Pak storage and host rumble at
the same time: osMotorInit still sees RumblePak, allowing SDL rumble.
This differs from a single physical N64 accessory slot.

Errors follow the pinned PR/os_pfs.h: NOPACK 1, NEW_PACK 2,
INCONSISTENT 3, CONTRFAIL 4, INVALID 5, BAD_DATA 6, DATA_FULL 7,
DIR_FULL 8, EXIST 9, ID_FATAL 10, DEVICE 11. NEW_PACK and DEVICE
have no current production path. SBK_PFS_TRACE=1 records operation,
port, file number, offset, length, and result, never payload bytes.

## Snowboard Kids PFS identity

The game constructs company code 'EB' (0x4542), game code 'NSKE'
(0x4e534b45), 16 raw game-name bytes
2c 27 28 30 1b 28 1a 2b 1d 0f 24 22 1d 2c 00 00, and a four-byte
zero extension. It allocates 0x7900 bytes, reads/writes 0x78e0 bytes,
and uses one note per controller. Replay slots/data reside inside that
note. No separate PFS replay file was found. The backend supports
arbitrary identities and 16 directory entries.

## Savestate boundary and future human gate

The Pak service lives outside savestate runtime domains and .sbks.
Every HLE mutation commits synchronously before returning. Restoring a
savestate therefore does not rewind the Pak. The HLE test restores a
captured guest RAM image after a later Pak write and reads the later
payload. The savestate persistence test also encodes and decodes an
actual synthetic .sbks after payload A, writes payload B externally,
and confirms that payload B remains. A live driver-level restore with
the game has not been run.

For a future human gate: boot, create/use an original save, make visible
progress, exit, restart and confirm; make more progress, restart and
confirm again. Then capture a savestate, change the Pak, restore the
savestate, and confirm the newer Pak data remains. No human LIVE PASS is
claimed here. Back up portN.mpk while the game is closed. Deleting it
starts a blank original-game Pak and loses its original save. .sbks
files are separate execution snapshots.
