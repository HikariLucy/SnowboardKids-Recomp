#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

// Cooperative in-process barrier. P4 snapshot transactions (Capture/Restore)
// run strictly inside a Frozen generation; no second freeze architecture.
namespace sbk::quiescence {
// Capture/Restore are appended so the P2 numeric values stay unchanged.
enum class State { Idle, Requested, ParkGame, CloseVI, DrainDevices, Frozen, Resume, Capture, Restore };
enum class Device { Rsp, Graphics };
enum class Participant { VI, Timer, Rsp, Graphics, Audio };
struct Status {
    State state;
    uint64_t generation;
    uint64_t rsp_pending, graphics_pending;
    size_t owners, active_owners;
    bool vi, timer, rsp, graphics, audio;
    std::string failure;
};
// Where a dormant execution owner is blocked. Parked owners hold the guest
// run token; Sleeping owners wait on their scheduler semaphore.
enum class Site : uint8_t { Active, Sleeping, ParkedSafepoint, ParkedWake, ParkedRestored };
struct OwnerStatus { uint64_t token, guest; bool active; Site site; bool signaled, retired; };
std::vector<OwnerStatus> owner_status();
struct Trace {
    uint64_t sequence, generation;
    State state;
    std::string participant, operation;
    uint64_t detail;
};
// Enable before starting workers. Disabled by default; request requires ready().
void enable();
bool enabled();
void ready();
uint64_t request(); // 0 = busy/not ready. Never waits.
Status status();
std::vector<Trace> trace();
const char* name(State state);
void poll(); // frontend: advances barriers, never waits for workers
bool resume(uint64_t generation); // stale generations cannot release a barrier
void cancel(const char* reason); // also used on shutdown; never fabricates Frozen
void set_audio_callback(void (*callback)(bool paused));
} // namespace sbk::quiescence
namespace ultramodern::renderer { class RendererContext; }
namespace sbk::quiescence {
void set_renderer_context(ultramodern::renderer::RendererContext* context);
ultramodern::renderer::RendererContext* get_renderer_context();

// Every native game execution owner has a unique lifetime token. Park does not
// signal a scheduler semaphore, change a guest queue, or change owner identity.
void owner_enter(uint64_t guest_id);
void owner_leave();
void owner_sleep();
void owner_wake();
void game_safepoint();
// Scheduler is about to signal this guest thread's semaphore. Clears on wake.
void owner_signal(uint64_t guest_id);
// A reconstructed owner that held the run token at capture parks here until
// the restored generation is released.
void restored_owner_park();
// Owners dormant with no semaphore signal in flight (safe to classify).
bool owners_settled();
struct Owner {
    explicit Owner(uint64_t guest_id) { owner_enter(guest_id); }
    ~Owner() { owner_leave(); }
    Owner(const Owner&) = delete;
    Owner& operator=(const Owner&) = delete;
};
void vi_boundary(); // outside the entire VI transaction, including callbacks
bool draining();
void timer_boundary(size_t canonical_timer_count); // no selected timer/actions
bool try_accept_config();
void accepted(Device device);
void completed(Device device);
// drain callback executes with NO coordinator/scheduler/audio/renderer locks.
void device_boundary(Device device, bool (*drain)() = nullptr);
void note(const char* participant, const char* operation, uint64_t detail = 0);
std::chrono::high_resolution_clock::duration excluded_wall_time();
std::chrono::high_resolution_clock::time_point logical_now();

// ---- P4 snapshot transaction support (all require the matching generation) ----
// Frozen -> Capture|Restore. Producers stay parked; no guest may run.
bool begin_transaction(uint64_t generation, State kind);
// Capture|Restore -> Frozen. The caller then resumes or stays Frozen.
bool end_transaction(uint64_t generation);
// Restore only: every current owner leaves via the registered retire thrower
// (it must unwind without touching guest memory). Returns owners retired.
size_t retire_owners();
size_t owner_count();
// Called on a retired owner's own thread; default throws owner_retired.
struct owner_retired {};
void set_retire_thrower(void (*thrower)());
// Only while the logical clock is paused (closed VI boundary). Places logical
// "now" at the given point; later release excludes the frozen interval as usual.
bool rebase_logical_clock(std::chrono::high_resolution_clock::time_point logical);
}
