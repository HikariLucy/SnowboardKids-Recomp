#include "quiescence/quiescence.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

// LIVE-STARTUP-01: device work queued at startup must be admitted after the
// coordinator is armed. The frontend config load queues a renderer
// UpdateConfigAction long before the graphics worker starts draining; if P2 is
// armed later, that completion is unmatched.
using namespace std::chrono_literals;
namespace q = sbk::quiescence;
#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #value); std::abort(); } } while (false)

enum class Action { Config, Dummy, Screen };
std::mutex queue_mutex;
std::deque<Action> actions;
void push(Action action) { std::lock_guard lock(queue_mutex); actions.push_back(action); }
// Mirrors events.cpp producers: admission is counted before the enqueue.
void trigger_config() { CHECK(q::try_accept_config()); push(Action::Config); }
void vi_transaction(bool started) {
    if (!started) { q::accepted(q::Device::Graphics); push(Action::Dummy); }
    q::accepted(q::Device::Graphics); push(Action::Screen);
}
// Mirrors gfx_thread_func: one completion per dequeued action.
size_t drain() {
    size_t count = 0;
    for (;;) {
        {
            std::lock_guard lock(queue_mutex);
            if (actions.empty()) return count;
            actions.pop_front();
        }
        q::completed(q::Device::Graphics);
        ++count;
    }
}
bool throws(void (*operation)(), const char* message) {
    try { operation(); } catch (const std::logic_error& error) { return std::strcmp(error.what(), message) == 0; }
    return false;
}

// Old order (d6298fe..d177aa7): config queued while disabled, armed afterwards.
int late_enable() {
    CHECK(!q::enabled());
    trigger_config();
    CHECK(throws([] { q::enable(); }, "P2 enabled after unaccounted device work"));
    CHECK(!q::enabled()); // stays disarmed: the queued completion is a no-op
    CHECK(drain() == 1);
    std::puts("P2 lifecycle late-enable: refused to arm after uncounted startup work");
    return 0;
}

int armed_startup() {
    q::enable();
    q::enable(); // idempotent once armed
    for (int cycle = 0; cycle < 50; ++cycle) {
        // Frontend config load and early VIs before the renderer worker exists.
        trigger_config();
        trigger_config();
        for (int vi = 0; vi < 4; ++vi) vi_transaction(false);
        CHECK(q::status().graphics_pending == 10);
        std::this_thread::sleep_for(200us); // renderer/RmlUi init
        size_t done = 0;
        std::thread gfx([&] { done = drain(); });
        for (int vi = 0; vi < 8; ++vi) vi_transaction(cycle % 2);
        gfx.join();
        done += drain();
        CHECK(done == 10 + 8 * (cycle % 2 ? 1 : 2));
        CHECK(q::status().graphics_pending == 0);
    }
    // After the activation boundary every completion still needs its request.
    q::accepted(q::Device::Graphics);
    q::completed(q::Device::Graphics);
    CHECK(throws([] { q::completed(q::Device::Graphics); }, "P2 unmatched completion"));
    CHECK(throws([] { q::completed(q::Device::Rsp); }, "P2 unmatched completion"));
    CHECK(q::status().graphics_pending == 0 && q::status().rsp_pending == 0);
    std::puts("P2 lifecycle armed-startup: 50 startups matched, invalid completion rejected");
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--late-enable") return late_enable();
    return armed_startup();
}
