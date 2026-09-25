#include "pfs/hle.hpp"
#include "recomp.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

extern "C" void osPfsInitPak_recomp(uint8_t*, recomp_context*);
extern "C" void osPfsFindFile_recomp(uint8_t*, recomp_context*);
extern "C" void osPfsAllocateFile_recomp(uint8_t*, recomp_context*);
extern "C" void osPfsReadWriteFile_recomp(uint8_t*, recomp_context*);
extern "C" void osPfsFreeBlocks_recomp(uint8_t*, recomp_context*);
extern "C" void osPfsRepairId_recomp(uint8_t*, recomp_context*);

constexpr uint32_t base = 0x80000000;
constexpr uint32_t pfs = base + 0x1000;
constexpr uint32_t name = base + 0x2000;
constexpr uint32_t ext = base + 0x2010;
constexpr uint32_t file_number = base + 0x2020;
constexpr uint32_t payload = base + 0x3000;
constexpr uint32_t stack = base + 0x10000;

void put_word(std::vector<uint8_t>& ram, uint32_t address, uint32_t value) {
    std::memcpy(ram.data() + address - base, &value, 4);
}
uint32_t word(const std::vector<uint8_t>& ram, uint32_t address) {
    uint32_t value;
    std::memcpy(&value, ram.data() + address - base, 4);
    return value;
}
void put_byte(std::vector<uint8_t>& ram, uint32_t address, uint8_t value) {
    ram[(address - base) ^ 3] = value;
}
uint8_t byte(const std::vector<uint8_t>& ram, uint32_t address) {
    return ram[(address - base) ^ 3];
}
void fill_identity(std::vector<uint8_t>& ram) {
    for (int i = 0; i < 16; ++i) put_byte(ram, name + i, uint8_t(i + 1));
}
void prepare(recomp_context& ctx) {
    ctx.r29 = stack;
    ctx.r4 = pfs;
    ctx.r5 = 0x4542;
    ctx.r6 = 0x4e534b45;
    ctx.r7 = name;
}

int main(int argc, char** argv) {
    assert(argc == 2);
    auto ram = std::vector<uint8_t>(0x800000);
    sbk::pfs::configure(argv[1]);
    sbk::pfs::set_port_present(0, false);
    recomp_context ctx{};
    ctx.r4 = 0;
    ctx.r5 = pfs;
    ctx.r6 = 0;
    osPfsInitPak_recomp(ram.data(), &ctx);
    assert(ctx.r2 == 1 && !std::filesystem::exists(std::filesystem::path(argv[1]) / "controller-paks/port1.mpk"));
    sbk::pfs::set_port_present(0, true);
    osPfsInitPak_recomp(ram.data(), &ctx);
    assert(ctx.r2 == 0 && word(ram, pfs) == 1 && word(ram, pfs + 80) == 16);
    fill_identity(ram);
    prepare(ctx);
    put_word(ram, stack + 16, ext);
    put_word(ram, stack + 20, file_number);
    osPfsFindFile_recomp(ram.data(), &ctx);
    assert(ctx.r2 == 5 && word(ram, file_number) == uint32_t(-1));
    put_word(ram, stack + 20, 256);
    put_word(ram, stack + 24, file_number);
    osPfsAllocateFile_recomp(ram.data(), &ctx);
    assert(ctx.r2 == 0 && word(ram, file_number) == 0);
    put_word(ram, stack + 20, file_number);
    osPfsFindFile_recomp(ram.data(), &ctx);
    assert(ctx.r2 == 0 && word(ram, file_number) == 0);
    for (int i = 0; i < 256; ++i) put_byte(ram, payload + i, uint8_t(i));
    ctx.r4 = pfs;
    ctx.r5 = 0;
    ctx.r6 = 1;
    ctx.r7 = 0;
    put_word(ram, stack + 16, 256);
    put_word(ram, stack + 20, payload);
    osPfsReadWriteFile_recomp(ram.data(), &ctx);
    assert(ctx.r2 == 0);
    // This stands in for restoring guest RAM. The host Pak remains external.
    const auto captured_guest = ram;
    for (int i = 0; i < 256; ++i) put_byte(ram, payload + i, uint8_t(i + 17));
    osPfsReadWriteFile_recomp(ram.data(), &ctx);
    assert(ctx.r2 == 0);
    ram = captured_guest;
    ctx.r6 = 0;
    std::fill(ram.begin() + 0x3000, ram.begin() + 0x3100, 0);
    osPfsReadWriteFile_recomp(ram.data(), &ctx);
    assert(ctx.r2 == 0);
    for (int i = 0; i < 256; ++i) assert(byte(ram, payload + i) == uint8_t(i + 17));
    const auto pak_path = std::filesystem::path(argv[1]) / "controller-paks/port1.mpk";
    std::fstream image(pak_path, std::ios::in | std::ios::out | std::ios::binary);
    image.seekp(32 + 28);
    image.put(char(0xff));
    image.close();
    sbk::pfs::set_port_present(0, false);
    sbk::pfs::set_port_present(0, true);
    ctx.r4 = 0;
    ctx.r5 = pfs;
    ctx.r6 = 0;
    osPfsInitPak_recomp(ram.data(), &ctx);
    assert(ctx.r2 == 10);
    ctx.r4 = pfs;
    osPfsRepairId_recomp(ram.data(), &ctx);
    assert(ctx.r2 == 0);
    ctx.r4 = 0;
    ctx.r5 = pfs;
    ctx.r6 = 0;
    osPfsInitPak_recomp(ram.data(), &ctx);
    assert(ctx.r2 == 0);
    std::cout << "PASS HLE present/absent, guest ABI, corrupt repair, external media after guest restore\n";
}
