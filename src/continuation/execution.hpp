#pragma once
#include "dispatch.hpp"
#include <cfenv>
#include <cstddef>
namespace sbk::continuation {
void set_player_count_request_callback(void (*callback)(size_t));
enum class BlockedPhase : uint8_t { None, Begin, Waiting, Committed };
struct BlockedOperation {
    uint64_t hle_id = 0;
    BlockedPhase phase = BlockedPhase::None;
    uint64_t args[4]{};
    int32_t result = 0;
    bool tail = false;
};
// Scheduler operation that run_execution started outside any frame. A park at
// its leading safepoint means it must be redone from the beginning; a
// scheduler sleep inside it means it has completed once the owner wakes.
enum class PendingOp : uint8_t { None, CheckQueue, Pause };
struct Execution {
    recomp_context cpu{};
    std::vector<Frame> frames;
    BlockedOperation blocked;
    int rounding_mode = FE_TONEAREST;
    uint64_t dispatch_count = 0;
    bool started = false;
    bool finished = false;
    PendingOp pending = PendingOp::None;
};
Execution* current_execution();
void run_execution(uint8_t* rdram, Execution& execution, uint64_t root);
// Reconstructed owners: redo `pending` from its start, then continue frames.
void resume_execution(uint8_t* rdram, Execution& execution);
uint64_t total_dispatch_count();
bool startup_is_retired();
}
