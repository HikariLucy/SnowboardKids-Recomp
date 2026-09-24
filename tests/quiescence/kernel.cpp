// Integration fixture using the actual patched scheduler, queues and timers.
// No ROM/renderer required. Process exit owns the baseline detached workers.
#include "quiescence/quiescence.hpp"
#include "ultramodern/ultramodern.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace std::chrono_literals;
namespace q = sbk::quiescence;
std::atomic_bool exited{false}; // runtime worker ABI
static std::atomic_uint stage{0}, received{0};
static std::atomic_int expected{0};
static std::atomic_bool timer_case{false}, allow_consumer{false}, kernel_done{false};
static std::vector<uint8_t> ram(1024 * 1024);
constexpr int32_t queue = int32_t(0x80003000), messages = int32_t(0x80004000);
constexpr int32_t timer_addr = int32_t(0x80005000), result_addr = int32_t(0x80006000);
#define CHECK(value) do { if (!(value)) { std::fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#value); std::_Exit(1); } } while (false)
extern "C" void yield_self_1ms(uint8_t*);
void run_thread_function(uint8_t* rdram, uint64_t addr, uint64_t, uint64_t) {
    if (addr == 1) {
        for (;;) yield_self_1ms(rdram);
    }
    if (addr == 3) {
        while (!allow_consumer) yield_self_1ms(rdram);
        CHECK(osRecvMesg(rdram, queue, result_addr + 4, OS_MESG_BLOCK) == 0);
        CHECK(*TO_PTR(OSMesg, result_addr + 4) == 71);
        CHECK(osRecvMesg(rdram, queue, result_addr + 4, OS_MESG_BLOCK) == 0);
        CHECK(*TO_PTR(OSMesg, result_addr + 4) == 72);
        osSendMesg(rdram, queue + 0x100, 99, OS_MESG_BLOCK);
        for (;;) osRecvMesg(rdram, queue + 0x200, result_addr + 4, OS_MESG_BLOCK);
    }
    for (unsigned i = 1; i <= 40; ++i) {
        bool timed = (i % 2) == 0;
        expected = i;
        timer_case = timed;
        if (timed) osSetTimer(rdram, timer_addr, 46875 * 40, 0, queue, i);
        stage = i;
        CHECK(osRecvMesg(rdram, queue, result_addr, OS_MESG_BLOCK) == 0);
        CHECK(*TO_PTR(OSMesg, result_addr) == int(i));
        received = i;
    }
    // Real full-queue drop/retry/jam behavior. Retries retain order and identity.
    for (int v : {11, 12, 13, 14}) CHECK(osSendMesg(rdram, queue, v, OS_MESG_NOBLOCK) == 0);
    ultramodern::enqueue_external_message(queue, 21, false, false); // dropped when full
    ultramodern::enqueue_external_message(queue, 22, false, true);
    ultramodern::enqueue_external_message(queue, 23, true, true);
    for (int v : {11, 12, 23, 13, 14, 22}) {
        CHECK(osRecvMesg(rdram, queue, result_addr, OS_MESG_NOBLOCK) == 0);
        CHECK(*TO_PTR(OSMesg, result_addr) == v);
    }
    CHECK(osRecvMesg(rdram, queue, result_addr, OS_MESG_NOBLOCK) == -1);
    osCreateMesgQueue(rdram, queue, messages, 1);
    osCreateThread(rdram, int32_t(0x80007000), 3, 3, 0, int32_t(0x800D0000), 5);
    osStartThread(rdram, int32_t(0x80007000));
    CHECK(osSendMesg(rdram, queue, 71, OS_MESG_BLOCK) == 0);
    stage = 100;
    CHECK(osSendMesg(rdram, queue, 72, OS_MESG_BLOCK) == 0); // block with pending value
    CHECK(osRecvMesg(rdram, queue + 0x100, result_addr, OS_MESG_BLOCK) == 0);
    CHECK(*TO_PTR(OSMesg, result_addr) == 99);
    kernel_done = true;
    for (;;) {
        osRecvMesg(rdram, queue, result_addr, OS_MESG_BLOCK);
    }
}
template<class P> void until(P predicate) {
    auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!predicate()) { q::poll(); CHECK(std::chrono::steady_clock::now() < deadline); std::this_thread::sleep_for(100us); }
}
int main(int argc, char**) {
    const bool freeze = argc == 1;
    if (freeze) q::enable();
    auto* rdram = ram.data();
    osCreateMesgQueue(rdram, queue, messages, 4);
    osCreateMesgQueue(rdram, queue + 0x100, messages + 0x100, 1);
    osCreateMesgQueue(rdram, queue + 0x200, messages + 0x200, 1);
    ultramodern::init_timers(rdram);
    std::thread([] { for (;;) { q::vi_boundary(); std::this_thread::sleep_for(100us); } }).detach();
    std::thread([] { for (;;) { q::device_boundary(q::Device::Rsp); std::this_thread::sleep_for(100us); } }).detach();
    std::thread([] { for (;;) { q::device_boundary(q::Device::Graphics); std::this_thread::sleep_for(100us); } }).detach();
    constexpr int32_t idle = int32_t(0x80001000), worker = int32_t(0x80002000);
    // Both threads start stopped. Bootstrap follows the runtime's own handoff.
    osCreateThread(rdram, idle, 1, 1, 0, int32_t(0x800F0000), 0);
    osCreateThread(rdram, worker, 2, 2, 0, int32_t(0x800E0000), 10);
    ultramodern::schedule_running_thread(rdram, idle);
    osStartThread(rdram, worker);
    q::ready();
    for (unsigned i = 1; i <= 40; ++i) {
        until([&] {
            if (stage < i) return false;
            if (!freeze) return true;
            for (auto owner : q::owner_status()) {
                if (owner.guest == uint32_t(worker) && !owner.active) return true;
            }
            return false;
        });
        auto gen = freeze ? q::request() : 0;
        if (freeze) { CHECK(gen); until([&] { return q::status().state == q::State::Frozen; }); }
        const auto before = freeze ? ram : std::vector<uint8_t>{};
        auto count = osGetCount(); auto time = osGetTime();
        if (freeze) {
            CHECK(TO_PTR(OSMesgQueue, queue)->validCount == 0);
            CHECK(TO_PTR(OSMesgQueue, queue)->blocked_on_recv == worker);
        }
        // Queuing a new external observation while Frozen must not deliver it
        // into guest RAM, wake a blocked guest, or alter the scheduler queue.
        if (i % 2 != 0) ultramodern::enqueue_external_message(queue, i, false, true);
        if (freeze) {
            std::this_thread::sleep_for(60ms); // exceeds the 40ms guest deadline
            CHECK(before == ram);
            CHECK(osGetCount() == count && osGetTime() == time);
            CHECK(received < i);
            CHECK(q::resume(gen));
        }
        until([&] { return q::status().state == q::State::Idle; });
        until([&] { return received >= i; });
    }
    until([&] {
        if (stage != 100) return false;
        if (!freeze) return true;
        for (auto owner : q::owner_status()) {
            if (owner.guest == uint32_t(worker) && !owner.active) return true;
        }
        return false;
    });
    auto gen = freeze ? q::request() : 0;
    if (freeze) {
        CHECK(gen);
        until([&] { return q::status().state == q::State::Frozen; });
        CHECK(TO_PTR(OSMesgQueue, queue)->blocked_on_send == worker);
        auto before = ram;
        std::this_thread::sleep_for(60ms);
        CHECK(before == ram);
    }
    allow_consumer = true;
    if (freeze) CHECK(q::resume(gen));
    until([&] { return q::status().state == q::State::Idle; });
    until([&] { return kernel_done.load(); });
    std::printf("%s: expected guest message transcript matched\n", freeze ? "FROZEN" : "BASELINE");
    if (freeze) std::puts("PASS: actual runtime scheduler/blocked receive/external events/one-shot timer expiry: 41 cycles, blocking send, FIFO/drop/retry/jam, memory+osGetCount+osGetTime unchanged over 60ms freezes");
    std::fflush(stdout);
    std::_Exit(0);
}
