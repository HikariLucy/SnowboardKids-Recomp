#include "quiescence/quiescence.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
namespace q = sbk::quiescence;
#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #value); std::abort(); } } while (false)

template<class Predicate> void until(Predicate predicate) {
    auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!predicate()) {
        q::poll();
        CHECK(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(50us);
    }
}
std::atomic_bool audio_paused{false};
std::atomic_uint64_t renderer_drains{0};
bool renderer_drain() {
    // Acquiring status inside the callback proves the coordinator doesn't hold
    // its mutex while asking a renderer to drain.
    CHECK(q::status().state == q::State::DrainDevices);
    ++renderer_drains;
    return true;
}
int main() {
    CHECK(q::request() == 0);
    q::enable();
    CHECK(q::request() == 0); // boot/start not ready
    q::set_audio_callback([](bool pause) { (void)q::status(); audio_paused = pause; });
    std::atomic_bool stop{false}, owner_ready{false}, close_vi{false};
    std::atomic_uint64_t mutations{0}, vis{0}, timers{0};
    std::atomic_uint64_t submissions{0}, rsp_done{0}, gfx_done{0};
    std::atomic_uint64_t permits{0};
    std::mutex event_mutex;
    std::vector<uint64_t> completions;
    std::thread game([&] {
        q::Owner owner{0x80001000}; owner_ready = true;
        while (!stop) {
            q::game_safepoint();
            if (permits.load()) {
                --permits;
                q::accepted(q::Device::Rsp);
                q::accepted(q::Device::Graphics);
                ++submissions;
                ++mutations;
            }
            std::this_thread::sleep_for(20us);
        }
    });
    // A second owner represents a stopped/blocked OS thread. It must remain
    // asleep and must never receive a fabricated scheduler wake to ack.
    std::atomic_bool dormant{false};
    std::thread blocked([&] {
        q::Owner owner{0x80002000};
        q::owner_sleep(); dormant = true;
        while (!stop) std::this_thread::sleep_for(100us);
        q::owner_wake();
    });
    std::thread vi([&] {
        while (!close_vi) std::this_thread::sleep_for(50us);
        while (!stop) {
            q::vi_boundary();
            ++vis; // whole transaction, completed before the next boundary
            std::this_thread::sleep_for(70us);
        }
    });
    std::thread timer([&] {
        while (!stop) {
            q::timer_boundary(3); // canonical set, no selected timer
            ++timers;
            std::this_thread::sleep_for(30us);
        }
    });
    auto device = [&](q::Device kind, std::atomic_uint64_t& done) {
        while (!stop) {
            if (done < submissions) {
                auto id = done.load() + 1;
                // Accepted work produces its normal completion before ack.
                { std::lock_guard lock(event_mutex); completions.push_back(id * 2 + (kind == q::Device::Graphics)); }
                ++done;
                q::completed(kind);
            }
            q::device_boundary(kind, kind == q::Device::Graphics ? renderer_drain : nullptr);
            std::this_thread::sleep_for(20us);
        }
    };
    std::thread rsp([&] { device(q::Device::Rsp, rsp_done); });
    std::thread gfx([&] { device(q::Device::Graphics, gfx_done); });
    until([&] { return owner_ready && dormant; }); q::ready();
    auto in_flight = q::request(); CHECK(in_flight);
    until([&] { return q::status().state == q::State::CloseVI; });
    const auto before_close = q::logical_now();
    std::this_thread::sleep_for(2ms);
    CHECK(q::logical_now() > before_close); // never freeze an in-flight VI clock
    close_vi = true;
    until([&] { return q::status().state == q::State::Frozen; });
    CHECK(q::resume(in_flight));
    until([&] { return q::status().state == q::State::Idle; });
    uint64_t previous = in_flight;
    for (unsigned cycle = 0; cycle < 600; ++cycle) {
        permits += cycle % 5 + 1;
        until([&] { return permits == 0; });
        auto gen = q::request(); CHECK(gen > previous); previous = gen;
        CHECK(q::request() == 0);
        CHECK(!q::resume(gen - 1));
        until([&] { return q::status().state == q::State::Frozen; });
        auto snapshot = q::status();
        CHECK(snapshot.active_owners == 0 && snapshot.owners == 2);
        CHECK(snapshot.vi && snapshot.timer && snapshot.rsp && snapshot.graphics && snapshot.audio);
        CHECK(snapshot.rsp_pending == 0 && snapshot.graphics_pending == 0);
        CHECK(rsp_done == submissions && gfx_done == submissions);
        CHECK(audio_paused);
        const auto a = mutations.load(), b = vis.load(), c = timers.load();
        const auto time = q::logical_now();
        std::vector<uint64_t> pending;
        { std::lock_guard lock(event_mutex); pending = completions; }
        std::this_thread::sleep_for(150us);
        CHECK(mutations == a && vis == b && timers == c);
        CHECK(q::logical_now() == time);
        { std::lock_guard lock(event_mutex); CHECK(pending == completions); }
        CHECK(!q::try_accept_config());
        CHECK(!q::resume(gen + 1));
        CHECK(q::resume(gen));
        CHECK(!q::resume(gen));
        until([&] { return q::status().state == q::State::Idle; });
        CHECK(!audio_paused);
    }
    // Cancellation must release all participants and permit a new generation.
    auto gen = q::request(); CHECK(gen);
    q::cancel("test timeout");
    until([&] { return q::status().state == q::State::Idle; });
    CHECK(!q::resume(gen));
    CHECK(q::status().failure == "test timeout");
    gen = q::request(); CHECK(gen);
    until([&] { return q::status().state == q::State::Frozen; });
    q::cancel("test shutdown");
    until([&] { return q::status().state == q::State::Idle; });
    stop = true;
    game.join(); blocked.join(); vi.join(); timer.join(); rsp.join(); gfx.join();
    CHECK(q::status().owners == 0);
    CHECK(renderer_drains >= 601);
    CHECK(completions.size() == submissions * 2);
    std::vector<unsigned> seen(submissions * 2 + 2);
    for (auto id : completions) { CHECK(id < seen.size()); CHECK(++seen[id] == 1); }
    auto records = q::trace();
    uint64_t seq = 0;
    bool saw_frozen = false, saw_dormant = false, saw_same_owner = false;
    for (auto& record : records) {
        CHECK(record.sequence > seq); seq = record.sequence;
        saw_frozen |= record.operation == "Frozen";
        saw_dormant |= record.operation == "dormant-ack";
        saw_same_owner |= record.operation == "same-owner-resumed";
    }
    CHECK(saw_frozen && saw_dormant && saw_same_owner);
    std::printf("PASS: 600 freeze/resume cycles; %llu tasks per device; cancellation, stale generations, dormant owner, clock, callbacks outside lock, exactly-once completions\n", (unsigned long long)submissions.load());
}
