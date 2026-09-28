#pragma once
#include "execution.hpp"
namespace sbk::continuation {
enum class HleResult { Complete, WaitNext, CheckQueue };
// No scheduler park occurs inside this call. The dispatcher performs the
// returned scheduler action, then calls again to finish or retry.
HleResult advance_hle(uint8_t* rdram, Execution& execution);
uint64_t hle_id_for_token(recomp_func_t* token);
bool hle_active();
// True when a park at a safepoint inside this HLE precedes all its side
// effects, so the operation can be replayed from its recorded phase.
bool hle_replayable_from_safepoint(uint64_t hle_id);
bool hle_known(uint64_t hle_id);
}
