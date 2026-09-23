#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROM="$ROOT_DIR/../snowboardkids.z64"
XXHASH_DIR="$ROOT_DIR/.deps-runtime/N64ModernRuntime/thirdparty/xxHash"
BUILD_DIR="$ROOT_DIR/build-rom-inspect"
HELPER="$BUILD_DIR/rom_runtime_metadata"

EXPECTED_SHA1="1583bacc9046a360df8ea4d536942155247e154c"

if [[ ! -f "$ROM" ]]; then
    echo "ROM not found: $ROM" >&2
    exit 1
fi

ACTUAL_SHA1="$(sha1sum "$ROM" | awk '{print $1}')"
if [[ "$ACTUAL_SHA1" != "$EXPECTED_SHA1" ]]; then
    echo "ROM SHA-1 mismatch." >&2
    echo "Expected: $EXPECTED_SHA1" >&2
    echo "Actual:   $ACTUAL_SHA1" >&2
    exit 1
fi

if [[ ! -f "$XXHASH_DIR/xxh3.h" ]]; then
    echo "Pinned xxHash source not found under N64ModernRuntime." >&2
    echo "Run: bash scripts/bootstrap-native-runtime.sh" >&2
    exit 1
fi

mkdir -p "$BUILD_DIR"

cat > "$BUILD_DIR/rom_runtime_metadata.cpp" <<'CPP'
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#define XXH_INLINE_ALL
#include "xxh3.h"

static uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) |
           (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8) |
            uint32_t(p[3]);
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: rom_runtime_metadata <rom.z64>\n";
        return 2;
    }

    std::ifstream f(argv[1], std::ios::binary);
    if (!f) {
        std::cerr << "failed to open ROM\n";
        return 2;
    }

    f.seekg(0, std::ios::end);
    const auto size = static_cast<size_t>(f.tellg());
    f.seekg(0, std::ios::beg);

    std::vector<uint8_t> rom(size);
    f.read(reinterpret_cast<char*>(rom.data()), static_cast<std::streamsize>(size));

    if (rom.size() < 0x40) {
        std::cerr << "ROM is too small\n";
        return 2;
    }

    const uint64_t hash = XXH3_64bits(rom.data(), rom.size());

    std::string internal_name(reinterpret_cast<const char*>(rom.data() + 0x20), 20);
    while (!internal_name.empty() &&
           (internal_name.back() == ' ' || internal_name.back() == '\0')) {
        internal_name.pop_back();
    }

    std::string game_code(reinterpret_cast<const char*>(rom.data() + 0x3B), 4);

    std::cout << "Snowboard Kids — N64ModernRuntime ROM metadata\n";
    std::cout << "================================================\n";
    std::cout << "Size          : " << rom.size() << " bytes\n";
    std::cout << "XXH3-64       : 0x"
              << std::hex << std::uppercase << std::setw(16) << std::setfill('0')
              << hash << std::dec << "\n";
    std::cout << "Internal name : " << internal_name << "\n";
    std::cout << "Game code     : " << game_code << "\n";
    std::cout << "CRC1          : 0x"
              << std::hex << std::uppercase << std::setw(8) << std::setfill('0')
              << be32(rom.data() + 0x10) << "\n";
    std::cout << "CRC2          : 0x"
              << std::hex << std::uppercase << std::setw(8) << std::setfill('0')
              << be32(rom.data() + 0x14) << std::dec << "\n";
    std::cout << "Entrypoint    : 0x80000400\n";
    return 0;
}
CPP

clang++     -std=c++20     -O2     -I"$XXHASH_DIR"     "$BUILD_DIR/rom_runtime_metadata.cpp"     -o "$HELPER"

"$HELPER" "$ROM"
