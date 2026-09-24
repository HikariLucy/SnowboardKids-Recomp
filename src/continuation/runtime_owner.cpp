#include "runtime_owner.hpp"
#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace sbk::continuation {
namespace {
struct RuntimeOwner {
    OwnerKey key;
    OSThread* guest;
    UltraThreadContext synchronization{};
    Execution execution{};
    uint64_t logical_lifetime = 0;
    uint32_t entrypoint = 0, argument = 0;
    RestoreStart restore_start = RestoreStart::None;
};
std::mutex owners_mutex;
uint64_t last_lifetime = 0;
uint64_t logical_counter = 0;
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
RuntimeOwner* insert_owner(uint32_t address, OSThread* guest) {
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
    return value;
}
}
UltraThreadContext* create_owner(uint32_t address, OSThread* guest, uint32_t entrypoint, uint32_t argument) {
    std::lock_guard lock{owners_mutex};
    if (logical_counter == UINT64_MAX) throw std::overflow_error("Logical lifetime exhausted");
    auto* owner = insert_owner(address, guest);
    owner->logical_lifetime = ++logical_counter;
    owner->entrypoint = entrypoint;
    owner->argument = argument;
    return &owner->synchronization;
}
UltraThreadContext* create_restored_owner(const OwnerRecord& record, OSThread* guest, RestoreStart start) {
    std::lock_guard lock{owners_mutex};
    if (start == RestoreStart::None) throw std::invalid_argument("Restored owner needs a start mode");
    if (!record.logical_lifetime || record.logical_lifetime > logical_counter)
        throw std::logic_error("Restored logical lifetime outside the restored counter");
    auto* owner = insert_owner(record.address, guest);
    owner->logical_lifetime = record.logical_lifetime;
    owner->entrypoint = record.entrypoint;
    owner->argument = record.argument;
    owner->execution = record.execution;
    owner->restore_start = start;
    return &owner->synchronization;
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
std::vector<OwnerRecord> export_owners() {
    std::lock_guard lock{owners_mutex};
    std::vector<OwnerRecord> result;
    result.reserve(live.size());
    for (auto& [address, owner] : live)
        result.push_back({address, owner->logical_lifetime, owner->entrypoint, owner->argument, owner->execution});
    std::sort(result.begin(), result.end(), [](auto& a, auto& b) { return a.address < b.address; });
    return result;
}
uint64_t logical_lifetime_counter() {
    std::lock_guard lock{owners_mutex};
    return logical_counter;
}
void set_logical_lifetime_counter(uint64_t value) {
    std::lock_guard lock{owners_mutex};
    if (!live.empty()) throw std::logic_error("Logical lifetime counter changes only with no live owner");
    logical_counter = value;
}
size_t retire_all_owners() {
    std::lock_guard lock{owners_mutex};
    std::vector<RuntimeOwner*> owners;
    for (auto& [address, owner] : live) owners.push_back(owner);
    for (auto* owner : owners) detach(owner);
    for (auto& [context, owner] : storage) context->running.signal();
    return storage.size();
}
size_t stored_owner_count() {
    std::lock_guard lock{owners_mutex};
    return storage.size();
}
RestoreStart take_restore_start() {
    std::lock_guard lock{owners_mutex};
    if (!worker) throw std::logic_error("No current execution owner");
    return std::exchange(worker->restore_start, RestoreStart::None);
}
uint32_t current_entrypoint() {
    if (!worker) throw std::logic_error("No current execution owner");
    return worker->entrypoint;
}
uint32_t current_argument() {
    if (!worker) throw std::logic_error("No current execution owner");
    return worker->argument;
}
uint32_t address_for(OSThread* guest) {
    std::lock_guard lock{owners_mutex};
    auto* owner = for_guest(guest);
    if (!owner) throw std::logic_error("Thread has no live execution owner");
    return owner->key.address;
}
bool key_is_live(OwnerKey key) {
    std::lock_guard lock{owners_mutex};
    auto it = live.find(key.address);
    return it != live.end() && it->second->key == key;
}
}
