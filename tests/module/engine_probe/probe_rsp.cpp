// Synthetic RSP unit: compiled exactly like the generated aspMain.cpp, so the
// DMEM accessors must build against the engine buffer on every platform.
#include "librecomp/rsp.hpp"

RspExitReason aspMain(uint8_t*, uint32_t) {
    RSP_MEM_B(0, 0) = static_cast<uint8_t>(RSP_MEM_B(1, 0) + 1);
    return RspExitReason::Broke;
}
