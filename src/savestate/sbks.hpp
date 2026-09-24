#pragma once

#include "snapshot.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// P6 persistent savestate format (.sbks), version 1. Explicit little-endian
// fields at fixed offsets; no C++ struct is written with fwrite. A file holds
// one InMemorySnapshot plus the identity needed to refuse foreign states. It
// never contains the ROM, host pointers, wall-clock time or native objects.
//
//   [0, 128)             header (fixed fields, checksum in its last 8 bytes)
//   [128, 128 + 48*N)    section table (N entries)
//   [...]                section payloads, exactly tiling the rest of the file
//
// Header (all little-endian):
//    0  u8[8]  magic "SBKS\r\n\x1A\n"
//    8  u16    format_version (1)          10 u16 header_size (128)
//   12  u32    flags (0; unknown bits rejected)
//   16  u16    endian marker 0x1234         18 u8 compression (0 = none)
//   19  u8    reserved (0)                  20 u32 section_count
//   24  u8[16] game_id, ASCII, NUL padded ("snowboardkids")
//   40  u64    ROM identity (XXH3-64 of the validated ROM image)
//   48  u64    build corpus digest          56 u32 build function count
//   60  u32    build HLE count              64 u32 snapshot schema version
//   68  u32    present domain mask          72 u64 uncompressed payload bytes
//   80  u64    stored payload bytes         88 u64 aggregate canonical hash
//   96  u64    XXH3-64 of the section table
//  104  u8[16] reserved (0)
//  120  u64    XXH3-64 of header bytes [0, 120)
//
// Section entry (48 bytes): u32 type, u16 version, u16 flags (bit0 required),
// u64 offset, u64 uncompressed length, u64 stored length, u64 canonical hash
// (the snapshot domain hash; XXH3-64 of the payload for non-domain sections),
// u64 XXH3-64 of the stored bytes.
namespace sbk::savestate::sbks {

constexpr std::array<uint8_t, 8> kMagic = {'S', 'B', 'K', 'S', 0x0D, 0x0A, 0x1A, 0x0A};
constexpr uint16_t kFormatVersion = 1;
constexpr uint16_t kHeaderSize = 128;
constexpr uint32_t kSectionEntrySize = 48;
constexpr uint16_t kEndianMarker = 0x1234;
constexpr char kGameId[] = "snowboardkids";
constexpr size_t kGameIdBytes = 16;
constexpr uint16_t kSectionRequired = 1;
constexpr uint16_t kSectionVersion = 1;

enum class Compression : uint8_t { None = 0, Zstd = 1 /* reserved: not supported by this build */ };

enum class SectionType : uint32_t {
    Memory = 1, Continuations, Scheduler, Time, Vi, Audio, Input, Overlays, Rsp,
    Renderer, FramebufferColor, FramebufferDepth, Metadata
};
const char* section_name(uint32_t type);

// What a file must match to be loaded (and what a writer stamps).
struct Identity {
    std::string game_id = kGameId;
    uint64_t rom_hash = 0;
    BuildIdentity build;
    uint32_t present_mask = 0;      // expected adapter set; 0 = not checked here
};

// Parser bounds. Nothing is allocated from an untrusted length before it is
// checked against the actual input size and these limits.
struct Limits {
    uint64_t max_file_bytes = 256ull << 20;
    uint64_t max_section_bytes = 256ull << 20;
    uint32_t max_sections = 64;
};

enum class Status : uint8_t {
    Ok, NotFound, IoError, TooLarge, CorruptHeader, CorruptSection, Truncated, HashMismatch,
    MissingRequiredSection, WrongGame, WrongRom, UnsupportedVersion, IncompatibleBuild
};
const char* status_name(Status status);     // e.g. "WRONG_ROM"
const char* status_message(Status status);  // short user-readable text

struct Result {
    Status status = Status::Ok;
    std::string detail;                     // technical, for logs
    bool ok() const { return status == Status::Ok; }
};

// Optional, non-canonical, deterministic metadata (no timestamps).
struct Metadata {
    std::string writer;
    std::string slot;
};

// Encodes a structurally valid snapshot whose stored hashes are current.
bool encode(const InMemorySnapshot& snapshot, const Identity& identity, const Metadata& metadata,
    std::vector<uint8_t>& out, std::string& error);

// Validates everything that can be checked without the runtime (header,
// identity, table bounds/overlap/duplicates, checksums, section decoding,
// canonical hashes, snapshot structure) and only then fills `out`.
Result decode(std::span<const uint8_t> file, const Identity& expected, InMemorySnapshot& out,
    const Limits& limits = {}, Metadata* metadata = nullptr);

// Test/tool helpers: header and table checksums over a raw image, so fixtures
// can build deliberately malformed files that still pass the outer checksums.
void reseal(std::vector<uint8_t>& file);
}
