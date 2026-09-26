#pragma once

#include <cstdio>
#include <cstdlib>

// The checks wrap the PFS operations themselves, so they must evaluate in
// every build type (unlike assert under NDEBUG) and fail with a non-zero exit.
#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            std::fflush(stderr);                                               \
            std::exit(1);                                                      \
        }                                                                      \
    } while (0)
