#include <cstdint>

#include "recomp.h"

// Compatibility shims for libultra functions that N64Recomp intentionally
// excludes from generated output but that can remain referenced by generated
// translation units.
//
// The pinned N64ModernRuntime currently models Controller Pak/PFS as absent:
// its public osPfs* reimplementations return PFS_ERR_NOPACK (1). Mirror that
// policy for the lower-level PFS/SI helpers so linking and behavior are
// consistent until real Controller Pak persistence is implemented.

namespace {
constexpr gpr kPfsErrNoPak = 1;
}

extern "C" void __osPfsSelectBank_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = kPfsErrNoPak;
}

extern "C" void __osContRamRead_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = kPfsErrNoPak;
}

extern "C" void __osContRamWrite_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = kPfsErrNoPak;
}

// Snowboard Kids' matching decomp defines rmonPrintf as an empty debug
// routine. Preserve that behavior on the host.
extern "C" void rmonPrintf_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    (void)ctx;
}
