// Weak stand-ins for the audited HLE allowlist. The fixture defines the few it
// uses; any other HLE reached by the synthetic guest aborts the test.
#include "recomp.h"
#include <cstdio>
#include <cstdlib>
#define SBK_HLE(name, kind) \
    extern "C" __attribute__((weak)) void name(uint8_t*, recomp_context*) { \
        std::fprintf(stderr, "unexpected HLE %s\n", #name); std::abort(); }
#include "continuation/hle_allowlist.inc"
#undef SBK_HLE
