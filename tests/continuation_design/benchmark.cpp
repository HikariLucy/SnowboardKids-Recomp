// Audit-only benchmark of the existing P1 backend. No full-game extrapolation.
#include "runtime.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>
static p1::Machine* active;
void osSendMesg_recomp(uint8_t*, recomp_context* ctx) {
    active->send(uint32_t(ctx->r4), uint32_t(ctx->r5));
    ctx->r2 = 0;
    ++active->sends;
}
void osRecvMesg_recomp(uint8_t*, recomp_context* ctx) {
    p1::check(active->receive(uint32_t(ctx->r4), uint32_t(ctx->r5), uint32_t(ctx->r6)), "unexpected park");
}
int main() {
    constexpr unsigned iterations = 200000;
    p1::Machine machine;
    active = &machine;
    machine.frames.reserve(16);
    const auto initial = machine.ctx;
    auto reset = [&] {
        machine.ctx = initial;
        machine.rebind();
        machine.frames.clear();
        machine.blocked = {};
        machine.sends = machine.receives = 0;
        for (auto q : {p1::request_queue, p1::reply_queue}) {
            machine.word(q + 8, 0); machine.word(q + 12, 0);
        }
        machine.word(0x8000040C, 0); machine.word(0x8000042C, 0);
        machine.deliver(0xA5B6C7D8);
    };
    auto run = [&](bool continuation) {
        reset();
        if (continuation) {
            machine.frames.push_back({p1::root_id});
            p1::check(machine.run(), "unexpected suspension");
        } else {
            set_cop1_cs(machine.rounding);
            p1::reference_root(machine.memory.data(), &machine.ctx);
        }
    };
    run(false);
    auto reference = machine.encode();
    run(true);
    p1::check(machine.encode() == reference, "benchmark semantic mismatch");
    std::vector<double> native, explicit_frames;
    for (unsigned round = 0; round < 9; ++round) {
        // Alternate order to reduce systematic warmup/frequency bias.
        for (unsigned side = 0; side < 2; ++side) {
            bool continuation = (side ^ (round & 1)) != 0;
            auto start = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < iterations; ++i) run(continuation);
            double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / iterations;
            (continuation ? explicit_frames : native).push_back(ns);
        }
    }
    std::sort(native.begin(), native.end());
    std::sort(explicit_frames.begin(), explicit_frames.end());
    std::cout << "iterations_per_sample=" << iterations << " samples=9\n"
              << "native_ns_min_median_max=" << native.front() << ',' << native[4] << ',' << native.back() << '\n'
              << "p1_ns_min_median_max=" << explicit_frames.front() << ',' << explicit_frames[4] << ',' << explicit_frames.back() << '\n'
              << "median_ratio=" << explicit_frames[4] / native[4] << '\n'
              << "host_frame_bytes=" << sizeof(p1::Frame) << '\n';
}
