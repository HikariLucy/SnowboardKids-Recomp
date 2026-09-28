#pragma once
#include "execution.hpp"
#include "owner_registry.hpp"
#include "ultramodern/ultramodern.hpp"
#include <vector>

namespace sbk::continuation {
// Only the cooperative guest scheduler creates/detaches owners. Cleanup may run
// concurrently, but may release storage only after joining the exact worker.
UltraThreadContext* create_owner(uint32_t address, OSThread* guest,
    uint32_t entrypoint = 0, uint32_t argument = 0);
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

// ---- P4 semantic owner state ----
// Host lifetimes (OwnerKey) are operation generations: monotonic for the whole
// process, never saved, never rewound. Logical lifetimes are guest-timeline
// state: saved in snapshots and restored with them.
struct OwnerRecord {
    uint32_t address = 0;
    uint64_t logical_lifetime = 0;
    uint32_t entrypoint = 0, argument = 0;
    Execution execution;
};
// Live owners sorted by guest address. Callers must hold a Frozen barrier.
std::vector<OwnerRecord> export_owners();
uint64_t logical_lifetime_counter();
void set_logical_lifetime_counter(uint64_t value);
// Detach every live owner (clearing its transient guest context cache) and
// signal every stored worker, including detached-but-unjoined ones, so each
// leaves its scheduler wait. Signalling happens under the registry lock, which
// release_joined_owner also needs: no context can be freed while signalled.
// Returns the number of workers signalled.
size_t retire_all_owners();
// Includes detached owners whose worker has not been joined/released yet.
size_t stored_owner_count();
enum class RestoreStart : uint8_t { None, Sleeping, Running };
// Fresh host owner (new host lifetime) carrying the saved logical identity and
// execution. The restored logical counter must be installed first.
UltraThreadContext* create_restored_owner(const OwnerRecord& record, OSThread* guest, RestoreStart start);
// Worker side, for reconstructed owners only.
RestoreStart take_restore_start();
uint32_t current_entrypoint();
uint32_t current_argument();
bool key_is_live(OwnerKey key);
// Guest OSThread address (barrier owner identity) of a live owner.
uint32_t address_for(OSThread* guest);
}
