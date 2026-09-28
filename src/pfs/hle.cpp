#include "hle.hpp"
#include "controller_pak.hpp"

#include "recomp.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

namespace sbk::pfs {
namespace {
std::unique_ptr<ControllerPak> device;

// All game pointers are KSEG0 RDRAM addresses. The generated corpus uses
// word-swapped host RDRAM; byte and halfword accesses retain N64 order here.
bool guest_range(uint32_t addr, size_t size) {
    return addr >= 0x80000000u && size <= 0x800000u &&
           uint64_t(addr) + size <= 0x80800000ull;
}
uint8_t byte_at(uint8_t* rdram, uint32_t addr) {
    return rdram[(addr - 0x80000000u) ^ 3u];
}
void put_byte(uint8_t* rdram, uint32_t addr, uint8_t value) {
    rdram[(addr - 0x80000000u) ^ 3u] = value;
}
uint32_t word_at(uint8_t* rdram, uint32_t addr) {
    uint32_t result;
    std::memcpy(&result, rdram + addr - 0x80000000u, 4);
    return result;
}
void put_word(uint8_t* rdram, uint32_t addr, uint32_t value) {
    std::memcpy(rdram + addr - 0x80000000u, &value, 4);
}
void put_half(uint8_t* rdram, uint32_t addr, uint16_t value) {
    std::memcpy(rdram + ((addr - 0x80000000u) ^ 2u), &value, 2);
}
void copy_from_guest(uint8_t* rdram, uint32_t addr, uint8_t* output, size_t size) {
    for (size_t i = 0; i < size; ++i) output[i] = byte_at(rdram, addr + uint32_t(i));
}
void copy_to_guest(uint8_t* rdram, uint32_t addr, const uint8_t* input, size_t size) {
    for (size_t i = 0; i < size; ++i) put_byte(rdram, addr + uint32_t(i), input[i]);
}
uint32_t arg(uint8_t* rdram, recomp_context* ctx, int index) {
    if (index < 4) return uint32_t((&ctx->r4)[index]);
    const uint32_t stack = uint32_t(ctx->r29);
    const uint64_t address = uint64_t(stack) + uint64_t(index) * 4;
    return address <= UINT32_MAX && guest_range(uint32_t(address), 4)
        ? word_at(rdram, uint32_t(address)) : 0;
}
bool identity(uint8_t* rdram, uint32_t name, uint32_t extension,
              uint16_t company, uint32_t game, Identity& out) {
    if (!guest_range(name, 16) || !guest_range(extension, 4)) return false;
    out.company_code = company;
    out.game_code = game;
    copy_from_guest(rdram, name, out.game_name.data(), 16);
    copy_from_guest(rdram, extension, out.ext_name.data(), 4);
    return true;
}
Error checked_port(uint8_t* rdram, uint32_t pfs, int& port) {
    if (!guest_range(pfs, 104) || !device) return Invalid;
    if (!(word_at(rdram, pfs) & 1u)) return Invalid;
    port = int32_t(word_at(rdram, pfs + 8));
    return port >= 0 && port < 4 ? Ok : Invalid;
}
void result(recomp_context* ctx, Error error) { ctx->r2 = int(error); }
} // namespace

void configure(std::filesystem::path config_directory) {
    device = std::make_unique<ControllerPak>(std::move(config_directory));
}
void set_port_present(int port, bool present) {
    if (device) device->set_present(port, present);
}
} // namespace sbk::pfs

extern "C" void osPfsInitPak_recomp(uint8_t* rdram, recomp_context* ctx) {
    using namespace sbk::pfs;
    const uint32_t pfs = uint32_t(ctx->r5);
    const int port = int32_t(ctx->r6);
    if (!guest_range(pfs, 104) || !device) return result(ctx, Invalid);
    put_word(rdram, pfs, 0);
    put_word(rdram, pfs + 4, uint32_t(ctx->r4));
    put_word(rdram, pfs + 8, uint32_t(port));
    const Error status = device->init(port);
    if (status != Ok) {
        return result(ctx, status);
    }
    std::array<uint8_t, 32> id{}, label{};
    const Error contents = device->id_and_label(port, id, label);
    if (contents != Ok) return result(ctx, contents);
    put_word(rdram, pfs, 1); // PFS_INITIALIZED
    put_word(rdram, pfs + 4, uint32_t(ctx->r4));
    put_word(rdram, pfs + 8, uint32_t(port));
    copy_to_guest(rdram, pfs + 12, id.data(), id.size());
    copy_to_guest(rdram, pfs + 44, label.data(), label.size());
    put_word(rdram, pfs + 76, id[27]);
    put_word(rdram, pfs + 80, directory_entries);
    put_word(rdram, pfs + 84, 8);
    put_word(rdram, pfs + 88, 16);
    put_word(rdram, pfs + 92, 24);
    put_word(rdram, pfs + 96, first_data_page);
    put_byte(rdram, pfs + 100, 1);
    put_byte(rdram, pfs + 101, 0);
    result(ctx, Ok);
}

extern "C" void osPfsFindFile_recomp(uint8_t* rdram, recomp_context* ctx) {
    using namespace sbk::pfs;
    int port;
    const auto status = checked_port(rdram, uint32_t(ctx->r4), port);
    if (status != Ok) return result(ctx, status);
    const uint32_t output = arg(rdram, ctx, 5);
    if (!guest_range(output, 4)) return result(ctx, Invalid);
    Identity id;
    if (!identity(rdram, uint32_t(ctx->r7), arg(rdram, ctx, 4),
                  uint16_t(ctx->r5), uint32_t(ctx->r6), id))
        return result(ctx, Invalid);
    int file = -1;
    const Error error = device->find(port, id, file);
    if (error == Ok || error == Invalid) put_word(rdram, output, uint32_t(file));
    result(ctx, error);
}

extern "C" void osPfsAllocateFile_recomp(uint8_t* rdram, recomp_context* ctx) {
    using namespace sbk::pfs;
    int port;
    const auto status = checked_port(rdram, uint32_t(ctx->r4), port);
    if (status != Ok) return result(ctx, status);
    const uint32_t output = arg(rdram, ctx, 6);
    if (!guest_range(output, 4)) return result(ctx, Invalid);
    Identity id;
    if (!identity(rdram, uint32_t(ctx->r7), arg(rdram, ctx, 4),
                  uint16_t(ctx->r5), uint32_t(ctx->r6), id))
        return result(ctx, Invalid);
    int file = -1;
    const Error error = device->allocate(port, id, int32_t(arg(rdram, ctx, 5)), file);
    if (error == Ok) put_word(rdram, output, uint32_t(file));
    result(ctx, error);
}

extern "C" void osPfsReadWriteFile_recomp(uint8_t* rdram, recomp_context* ctx) {
    using namespace sbk::pfs;
    int port;
    const auto status = checked_port(rdram, uint32_t(ctx->r4), port);
    if (status != Ok) return result(ctx, status);
    const int file = int32_t(ctx->r5);
    const uint32_t flag = uint32_t(ctx->r6);
    const int offset = int32_t(ctx->r7);
    const int size = int32_t(arg(rdram, ctx, 4));
    const uint32_t buffer = arg(rdram, ctx, 5);
    if ((flag != 0 && flag != 1) || size <= 0 || size > int(image_size) ||
        !guest_range(buffer, size)) return result(ctx, Invalid);
    std::array<uint8_t, image_size> bytes{};
    if (flag == 1) {
        copy_from_guest(rdram, buffer, bytes.data(), size_t(size));
        return result(ctx, device->write(port, file, offset, std::span<const uint8_t>(bytes.data(), size_t(size))));
    }
    const Error error = device->read(port, file, offset, std::span<uint8_t>(bytes.data(), size_t(size)));
    if (error == Ok) copy_to_guest(rdram, buffer, bytes.data(), size_t(size));
    result(ctx, error);
}

extern "C" void osPfsDeleteFile_recomp(uint8_t* rdram, recomp_context* ctx) {
    using namespace sbk::pfs;
    int port;
    const auto status = checked_port(rdram, uint32_t(ctx->r4), port);
    if (status != Ok) return result(ctx, status);
    Identity id;
    if (!identity(rdram, uint32_t(ctx->r7), arg(rdram, ctx, 4),
                  uint16_t(ctx->r5), uint32_t(ctx->r6), id))
        return result(ctx, Invalid);
    result(ctx, device->remove(port, id));
}

extern "C" void osPfsFileState_recomp(uint8_t* rdram, recomp_context* ctx) {
    using namespace sbk::pfs;
    int port;
    const auto status = checked_port(rdram, uint32_t(ctx->r4), port);
    if (status != Ok) return result(ctx, status);
    const uint32_t output = uint32_t(ctx->r6);
    if (!guest_range(output, 32)) return result(ctx, Invalid);
    FileState state;
    const Error error = device->file_state(port, int32_t(ctx->r5), state);
    if (error != Ok) return result(ctx, error);
    put_word(rdram, output, state.size);
    put_word(rdram, output + 4, state.identity.game_code);
    put_half(rdram, output + 8, state.identity.company_code);
    copy_to_guest(rdram, output + 10, state.identity.ext_name.data(), 4);
    copy_to_guest(rdram, output + 14, state.identity.game_name.data(), 16);
    result(ctx, Ok);
}

extern "C" void osPfsFreeBlocks_recomp(uint8_t* rdram, recomp_context* ctx) {
    using namespace sbk::pfs;
    int port;
    const auto status = checked_port(rdram, uint32_t(ctx->r4), port);
    if (status != Ok) return result(ctx, status);
    const uint32_t output = uint32_t(ctx->r5);
    if (!guest_range(output, 4)) return result(ctx, Invalid);
    int bytes = 0;
    const Error error = device->free_blocks(port, bytes);
    if (error == Ok) put_word(rdram, output, uint32_t(bytes));
    result(ctx, error);
}

extern "C" void osPfsNumFiles_recomp(uint8_t* rdram, recomp_context* ctx) {
    using namespace sbk::pfs;
    int port;
    const auto status = checked_port(rdram, uint32_t(ctx->r4), port);
    if (status != Ok) return result(ctx, status);
    const uint32_t maximum = uint32_t(ctx->r5);
    const uint32_t used = uint32_t(ctx->r6);
    if (!guest_range(maximum, 4) || !guest_range(used, 4)) return result(ctx, Invalid);
    int max_value = 0, used_value = 0;
    const Error error = device->num_files(port, max_value, used_value);
    if (error == Ok) {
        put_word(rdram, maximum, uint32_t(max_value));
        put_word(rdram, used, uint32_t(used_value));
    }
    result(ctx, error);
}

extern "C" void osPfsRepairId_recomp(uint8_t* rdram, recomp_context* ctx) {
    using namespace sbk::pfs;
    const uint32_t pfs = uint32_t(ctx->r4);
    if (!guest_range(pfs, 104) || !device) return result(ctx, Invalid);
    const int port = int32_t(word_at(rdram, pfs + 8));
    result(ctx, device->repair_id(port));
}

extern "C" void osPfsChecker_recomp(uint8_t* rdram, recomp_context* ctx) {
    using namespace sbk::pfs;
    int port;
    const auto status = checked_port(rdram, uint32_t(ctx->r4), port);
    result(ctx, status == Ok ? device->init(port) : status);
}
