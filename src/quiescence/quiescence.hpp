#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

// In-process feasibility barrier only. No continuation or snapshot format.
namespace sbk::quiescence {
enum class State { Idle, Requested, ParkGame, CloseVI, DrainDevices, Frozen, Resume };
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
struct OwnerStatus { uint64_t token, guest; bool active; };
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
}
