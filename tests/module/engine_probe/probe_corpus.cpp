// Synthetic stand-ins for the ROM-derived corpus symbols game_module_entry.cpp
// needs. They are never executed by --validate-module.
#include <cstdlib>

#include "recomp.h"

extern "C" void sbk_probe_local_function(uint8_t*, recomp_context*) {
    std::abort();
}

extern "C" void recomp_entrypoint(uint8_t*, recomp_context*) {
    std::abort();
}

gpr get_entrypoint_address() {
    return static_cast<gpr>(static_cast<int32_t>(0x80000400u));
}
