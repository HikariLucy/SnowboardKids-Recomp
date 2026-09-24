#pragma once
#include "dispatch.hpp"
#include <cfenv>
namespace sbk::continuation {
enum class BlockedPhase : uint8_t { None, Begin, Waiting, Committed };
struct BlockedOperation {
    uint64_t hle_id = 0;
    BlockedPhase phase = BlockedPhase::None;
    uint64_t args[4]{};
    int32_t result = 0;
    bool tail = false;
};
struct Execution {
    recomp_context cpu{};
    std::vector<Frame> frames;
    BlockedOperation blocked;
    int rounding_mode = FE_TONEAREST;
    uint64_t dispatch_count = 0;
    bool started = false;
    bool finished = false;
};
Execution* current_execution();
void run_execution(uint8_t* rdram, Execution& execution, uint64_t root);
uint64_t total_dispatch_count();
bool startup_is_retired();
}
