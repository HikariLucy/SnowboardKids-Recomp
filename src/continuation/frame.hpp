#pragma once
#include <cstdint>
#include <vector>
namespace sbk::continuation {
// Build compatibility additionally requires the generated corpus/manifest digest.
// Function IDs pack section ROM start + 1 and function offset within that section.
struct Frame {
    uint64_t function = 0;
    uint64_t continuation = 0;
    uint64_t hi = 0, lo = 0, result = 0;
    int32_t c1cs = 0;
    uint32_t indirect_target = 0;
    std::vector<uint64_t> scratch;
};
enum class ActionKind : uint8_t { Return, Call, Lookup, Hle, Yield, Pause };
struct Action {
    ActionKind kind = ActionKind::Return;
    uint64_t target = 0;
    bool tail = false;
};
}
