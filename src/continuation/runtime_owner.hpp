#pragma once
#include "execution.hpp"
#include "owner_registry.hpp"
#include "ultramodern/ultramodern.hpp"

namespace sbk::continuation {
// Only the cooperative guest scheduler creates/detaches owners. Cleanup may run
// concurrently, but may release storage only after joining the exact worker.
UltraThreadContext* create_owner(uint32_t address, OSThread* guest);
void bind_worker(UltraThreadContext* context);
UltraThreadContext* current_context();
OwnerKey current_owner_key();
bool owner_is_current();
UltraThreadContext* context_for(OSThread* guest);
UltraThreadContext* detach_owner(OSThread* guest);
bool finish_current_owner();
void release_joined_owner(UltraThreadContext* context);
size_t live_owner_count();
uint64_t total_registered_owners();
}
