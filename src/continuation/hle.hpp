#pragma once
#include "execution.hpp"
namespace sbk::continuation {
enum class HleResult { Complete, WaitNext, CheckQueue };
// No scheduler park occurs inside this call. The dispatcher performs the
// returned scheduler action, then calls again to finish or retry.
HleResult advance_hle(uint8_t* rdram, Execution& execution);
uint64_t hle_id_for_token(recomp_func_t* token);
bool hle_active();
}
