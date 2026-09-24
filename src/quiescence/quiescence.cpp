#include "quiescence.hpp"
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <stdexcept>

namespace sbk::quiescence {
namespace {
using Clock = std::chrono::high_resolution_clock;
std::atomic_bool on{false};
std::mutex mutex;
std::condition_variable changed;
State state = State::Idle;
uint64_t generation = 0, sequence = 0, next_owner = 0;
size_t waiters = 0;
struct ExecutionOwner { uint64_t guest; bool active; };
std::map<uint64_t, ExecutionOwner> owners;
thread_local uint64_t owner = 0;
std::array<uint64_t, 2> pending{};
std::array<bool, 5> ack{};
std::deque<Trace> records;
bool runtime_ready = false, clock_paused = false, audio_paused = false;
Clock::time_point pause_start;
Clock::duration excluded{};
std::string failure;
void (*audio_callback)(bool) = nullptr;
void record(const char* who, const char* operation, uint64_t detail = 0) {
    // Bounded diagnostic ring, not a replay log or snapshot.
    if (records.size() == 32768) records.pop_front();
    records.push_back({++sequence, generation, state, who, operation, detail});
}
void transition(State next) {
    state = next;
    record("coordinator", name(next));
    changed.notify_all();
}
bool busy() { return state != State::Idle && state != State::Resume; }
void park_wait(std::unique_lock<std::mutex>& lock) {
    ++waiters;
    changed.wait(lock, [] { return !busy(); });
    --waiters;
}
void release(std::unique_lock<std::mutex>& lock) {
    // Keep owners parked until clocks/audio are rebased. Resume is observable
    // in trace but the mutex prevents any owner from escaping early.
    // SDL pause callback runs outside our lock. Owners remain parked until
    // audio is resumed and the logical clock is rebased, then Resume releases
    // this generation. Idle waits for all parked calls to leave.
    auto callback = audio_callback;
    bool unpause = audio_paused;
    audio_paused = false;
    lock.unlock();
    if (unpause && callback) callback(false);
    lock.lock();
    if (clock_paused) {
        excluded += Clock::now() - pause_start;
        clock_paused = false;
    }
    transition(State::Resume);
}
}
const char* name(State s) {
    constexpr const char* names[]{"Idle", "Requested", "ParkGame", "CloseVI", "DrainDevices", "Frozen", "Resume"};
    return names[static_cast<unsigned>(s)];
}
void enable() { on = true; }
bool enabled() { return on.load(std::memory_order_relaxed); }
void ready() { std::lock_guard lock(mutex); runtime_ready = true; }
static ultramodern::renderer::RendererContext* global_renderer_context = nullptr;
void set_audio_callback(void (*callback)(bool)) { std::lock_guard lock(mutex); audio_callback = callback; }
void set_renderer_context(ultramodern::renderer::RendererContext* ctx) { std::lock_guard lock(mutex); global_renderer_context = ctx; }
ultramodern::renderer::RendererContext* get_renderer_context() { std::lock_guard lock(mutex); return global_renderer_context; }
uint64_t request() {
    if (!enabled()) return 0;
    std::lock_guard lock(mutex);
    if (state != State::Idle || !runtime_ready || owners.empty()) return 0;
    ++generation; ack.fill(false); failure.clear();
    transition(State::Requested);
    for (auto [id, entry] : owners) record("game", entry.active ? "requested" : "dormant-ack", id);
    transition(State::ParkGame);
    return generation;
}
Status status() {
    std::lock_guard lock(mutex);
    size_t active = 0;
    for (auto [id, entry] : owners) active += entry.active;
    return {state, generation, pending[0], pending[1], owners.size(), active,
        ack[0], ack[1], ack[2], ack[3], ack[4], failure};
}
std::vector<OwnerStatus> owner_status() {
    std::lock_guard lock(mutex);
    std::vector<OwnerStatus> result;
    for (auto [token, entry] : owners) result.push_back({token, entry.guest, entry.active});
    return result;
}
std::vector<Trace> trace() { std::lock_guard lock(mutex); return {records.begin(), records.end()}; }
void note(const char* who, const char* operation, uint64_t detail) {
    if (!enabled()) return;
    std::lock_guard lock(mutex); record(who, operation, detail);
}
void poll() {
    if (!enabled()) return;
    std::unique_lock lock(mutex);
    if (state == State::Resume && waiters == 0) transition(State::Idle);
    if (state == State::ParkGame) {
        for (auto [id, entry] : owners) if (entry.active) return;
        transition(State::CloseVI);
    }
    if (state == State::CloseVI && ack[0]) {
        // The in-flight VI must use advancing time. Stopping earlier repeats
        // its VI index and overfeeds audio on every freeze.
        pause_start = Clock::now(); clock_paused = true;
        if (!audio_paused) {
            auto callback = audio_callback;
            lock.unlock();
            if (callback) callback(true);
            lock.lock();
            audio_paused = true;
            ack[4] = true;
            record("audio", "ack");
        }
        transition(State::DrainDevices);
    }
    if (state == State::DrainDevices && ack[1] && ack[2] && ack[3] && ack[4]) {
        if (pending[0] || pending[1]) throw std::logic_error("P2 drain acknowledged with pending tasks");
        transition(State::Frozen);
    }
}
bool resume(uint64_t gen) {
    std::unique_lock lock(mutex);
    if (state != State::Frozen || generation != gen) return false;
    release(lock); return true;
}
void cancel(const char* reason) {
    std::unique_lock lock(mutex);
    if (state == State::Idle) return;
    failure = reason; record("coordinator", "cancel"); release(lock);
}
void owner_enter(uint64_t guest) {
    if (!enabled()) return;
    std::unique_lock lock(mutex);
    if (owner) throw std::logic_error("nested P2 execution owner");
    if (busy() && state != State::ParkGame)
        throw std::logic_error("P2 owner created after game admission closed");
    owner = ++next_owner;
    owners.emplace(owner, ExecutionOwner{guest, false});
    record("game", "register", owner);
    record("game", "guest-address", guest);
    // Creation runs before osCreateThread releases its initialized handshake.
    // The creating owner remains active until this owner reaches its semaphore.
    owners.at(owner).active = true;
}
void owner_leave() {
    if (!owner) return;
    std::lock_guard lock(mutex);
    record("game", "retire", owner); owners.erase(owner); owner = 0;
}
void owner_sleep() {
    if (!owner) return;
    std::lock_guard lock(mutex);
    owners.at(owner).active = false; record("game", "scheduler-wait", owner);
}
void owner_wake() {
    if (!owner) return;
    std::unique_lock lock(mutex);
    if (busy()) park_wait(lock);
    owners.at(owner).active = true; record("game", "scheduler-wake", owner);
}
void game_safepoint() {
    if (!owner) return;
    std::unique_lock lock(mutex);
    if (!busy()) return;
    owners.at(owner).active = false;
    record("game", "park-ack", owner);
    park_wait(lock);
    owners.at(owner).active = true;
    record("game", "same-owner-resumed", owner);
}
void vi_boundary() {
    if (!enabled()) return;
    std::unique_lock lock(mutex);
    if (!busy()) return;
    ack[0] = true; record("vi", "transaction-closed");
    if (busy()) park_wait(lock);
}
bool draining() {
    if (!enabled()) return false;
    std::lock_guard lock(mutex);
    return state == State::DrainDevices;
}
void timer_boundary(size_t count) {
    if (!enabled()) return;
    std::unique_lock lock(mutex);
    if (state != State::DrainDevices) return;
    ack[1] = true; record("timer", "canonical-ack", count);
    if (busy()) park_wait(lock);
}
bool try_accept_config() {
    if (!enabled()) return true;
    std::lock_guard lock(mutex);
    if (state != State::Idle) return false;
    record("graphics", "config-accepted", ++pending[1]);
    return true;
}
void accepted(Device device) {
    if (!enabled()) return;
    std::lock_guard lock(mutex);
    if (state == State::Frozen || state == State::DrainDevices)
        throw std::logic_error("P2 task admitted after producers closed");
    auto index = static_cast<size_t>(device);
    record(index ? "graphics" : "rsp", "accepted", ++pending[index]);
}
void completed(Device device) {
    if (!enabled()) return;
    std::lock_guard lock(mutex);
    auto index = static_cast<size_t>(device);
    if (!pending[index]) throw std::logic_error("P2 unmatched completion");
    record(index ? "graphics" : "rsp", "completed", --pending[index]);
}
void device_boundary(Device device, bool (*drain)()) {
    if (!enabled()) return;
    std::unique_lock lock(mutex);
    auto index = static_cast<size_t>(device);
    if (state != State::DrainDevices || pending[index]) return;
    auto gen = generation;
    lock.unlock();
    bool success = !drain || drain();
    lock.lock();
    if (generation != gen || state != State::DrainDevices) return;
    if (!success) { failure = "renderer drain unsupported"; return; }
    ack[index + 2] = true; record(index ? "graphics" : "rsp", "drain-ack");
    if (busy()) park_wait(lock);
}
Clock::time_point logical_now() {
    if (!enabled()) return Clock::now();
    std::lock_guard lock(mutex);
    return (clock_paused ? pause_start : Clock::now()) - excluded;
}
Clock::duration excluded_wall_time() {
    if (!enabled()) return Clock::duration::zero();
    std::lock_guard lock(mutex);
    return excluded + (clock_paused ? Clock::now() - pause_start : Clock::duration::zero());
}
}
