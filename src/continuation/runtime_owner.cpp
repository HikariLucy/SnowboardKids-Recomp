#include "runtime_owner.hpp"
#include <mutex>
#include <unordered_map>

namespace sbk::continuation {
namespace {
struct RuntimeOwner {
    OwnerKey key;
    OSThread* guest;
    UltraThreadContext synchronization{};
    Execution execution{};
};
std::mutex owners_mutex;
uint64_t last_lifetime = 0;
std::unordered_map<uint32_t, RuntimeOwner*> live;
// Includes detached owners until their native workers have been joined.
std::unordered_map<UltraThreadContext*, std::unique_ptr<RuntimeOwner>> storage;
thread_local RuntimeOwner* worker = nullptr;
bool is_current(const RuntimeOwner* owner) {
    if (!owner) return false;
    auto it = live.find(owner->key.address);
    return it != live.end() && it->second->key == owner->key;
}
RuntimeOwner* for_guest(OSThread* guest) {
    for (auto& [address, owner] : live) {
        if (owner->guest == guest) return owner;
    }
    return nullptr;
}
void detach(RuntimeOwner* owner) {
    live.erase(owner->key.address);
    owner->guest->context = nullptr; // cache write, never ownership authority
}
}
UltraThreadContext* create_owner(uint32_t address, OSThread* guest) {
    std::lock_guard lock{owners_mutex};
    if (!address || !guest) throw std::invalid_argument("Invalid execution owner");
    if (live.contains(address)) throw std::logic_error("Thread already owned");
    if (last_lifetime == UINT64_MAX) throw std::overflow_error("Owner lifetime exhausted");
    auto owner = std::make_unique<RuntimeOwner>();
    owner->key = {address, ++last_lifetime};
    owner->guest = guest;
    auto* value = owner.get();
    auto* context = &value->synchronization;
    storage.emplace(context, std::move(owner));
    try { live.emplace(address, value); }
    catch (...) { storage.erase(context); throw; }
    guest->context = context;
    return context;
}
void bind_worker(UltraThreadContext* context) {
    std::lock_guard lock{owners_mutex};
    worker = storage.at(context).get();
}
Execution* current_execution() { return worker ? &worker->execution : nullptr; }
UltraThreadContext* current_context() {
    if (!worker) throw std::logic_error("No current execution owner");
    return &worker->synchronization;
}
OwnerKey current_owner_key() {
    if (!worker) throw std::logic_error("No current execution owner");
    return worker->key;
}
bool owner_is_current() {
    std::lock_guard lock{owners_mutex};
    return is_current(worker);
}
UltraThreadContext* context_for(OSThread* guest) {
    std::lock_guard lock{owners_mutex};
    auto* owner = for_guest(guest);
    if (!owner) throw std::logic_error("Thread has no live execution owner");
    return &owner->synchronization;
}
UltraThreadContext* detach_owner(OSThread* guest) {
    std::lock_guard lock{owners_mutex};
    auto* owner = for_guest(guest);
    if (!owner) return nullptr;
    detach(owner);
    return &owner->synchronization;
}
bool finish_current_owner() {
    std::lock_guard lock{owners_mutex};
    if (!is_current(worker)) return false;
    detach(worker);
    return true;
}
void release_joined_owner(UltraThreadContext* context) {
    std::lock_guard lock{owners_mutex};
    auto it = storage.find(context);
    if (it == storage.end() || is_current(it->second.get()) || context->host_thread.joinable())
        throw std::logic_error("Cannot release an unjoined or live execution owner");
    storage.erase(it);
}
size_t live_owner_count() {
    std::lock_guard lock{owners_mutex};
    return live.size();
}
uint64_t total_registered_owners() {
    std::lock_guard lock{owners_mutex};
    return last_lifetime;
}
}
