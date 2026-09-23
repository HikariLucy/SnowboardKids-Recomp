#include "runtime.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <functional>
#include <array>
using p1::check;
static p1::Machine* reference_machine = nullptr; // transient baseline adapter only
static constexpr uint32_t message = 0xA5B6C7D8;
void osSendMesg_recomp(uint8_t*, recomp_context* ctx) {
    check(reference_machine && ctx->r6 == 1, "Unexpected reference send");
    reference_machine->send(uint32_t(ctx->r4), uint32_t(ctx->r5));
    ++reference_machine->sends;
    ctx->r2 = 0;
}
void osRecvMesg_recomp(uint8_t*, recomp_context* ctx) {
    check(reference_machine && ctx->r6 == 1, "Unexpected reference receive");
    check(reference_machine->word(p1::reply_queue + 8) == 0, "Reference must reach empty receive");
    // The scripted external event arrives while the native call chain is intact.
    reference_machine->deliver(message);
    check(reference_machine->receive(uint32_t(ctx->r4), uint32_t(ctx->r5), uint32_t(ctx->r6)), "Reference receive failed");
}
static void finished(p1::Machine& m) {
    check(m.frames.empty() && !m.blocked.queue, "Execution not finished");
    check(m.sends == 1 && m.receives == 1, "HLE side effect repeated");
    check(m.ctx.r10 == 2, "JAL delay slot repeated or skipped");
    check(get_cop1_cs() == m.rounding && m.rounding == 3, "Rounding mode was not installed");
    check(m.word(0x8000040C) == 1 && m.word(0x8000042C) == 1, "Generated caller resumed other than once");
    check(m.word(0x80000408) == 1 && m.word(0x80000428) == 1, "Lost live c1cs");
    check(m.word(0x80000400) == 6 && m.word(0x80000420) == 10, "Lost live hi");
    check(m.word(0x80000404) == uint32_t(uint64_t(0xFFFFABCD) * 7) &&
          m.word(0x80000424) == uint32_t(uint64_t(0xFFFFABCD) * 11), "Lost live lo");
    check(m.ctx.r29 == uint64_t(int64_t(int32_t(0x807FF000))), "Guest stack mismatch");
    check(m.word(0x807FEFCC) == message, "Receive destination mismatch");
    uint32_t odd = m.ctx.mips3_float_mode ? 0x2468ACE0 : 0x13579BDF;
    check(m.word(0x80000410) == odd && m.word(0x80000430) == odd, "Resumed odd FPR read used wrong binding");
    auto before = m.encode();
    check(m.run() && before == m.encode(), "Completed continuation ran twice");
}
static void parked(p1::Machine& m) {
    check(!m.run(), "Expected empty receive to park");
    m.validate();
    check(m.frames[0].hi == 6 && m.frames[1].hi == 10 &&
          m.frames[0].result && m.frames[1].result && m.frames[0].c1cs == 1, "Live locals missing at park");
    auto before = m.encode();
    for (int i = 0; i < 3; ++i) check(!m.run(), "Spurious resume without message");
    check(before == m.encode(), "Empty polling changed semantic state");
}
static void write(const char* path, const std::vector<uint8_t>& data) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    check(bool(file), "Write failed");
}
static std::vector<uint8_t> read(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    check(bool(file), "Read failed");
    auto size = file.tellg();
    check(size >= 0 && size <= p1::memory_size + 4096, "Oversized P1 payload");
    file.seekg(0);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
static void selftest() {
    p1::Machine m;
    parked(m);
    auto pristine = m.encode();
    // Exercise every named scalar field, including opaque NaN payload bits.
    constexpr std::array<gpr recomp_context::*, 32> gprs = {
        &recomp_context::r0, &recomp_context::r1, &recomp_context::r2, &recomp_context::r3, &recomp_context::r4, &recomp_context::r5, &recomp_context::r6, &recomp_context::r7,
        &recomp_context::r8, &recomp_context::r9, &recomp_context::r10, &recomp_context::r11, &recomp_context::r12, &recomp_context::r13, &recomp_context::r14, &recomp_context::r15,
        &recomp_context::r16, &recomp_context::r17, &recomp_context::r18, &recomp_context::r19, &recomp_context::r20, &recomp_context::r21, &recomp_context::r22, &recomp_context::r23,
        &recomp_context::r24, &recomp_context::r25, &recomp_context::r26, &recomp_context::r27, &recomp_context::r28, &recomp_context::r29, &recomp_context::r30, &recomp_context::r31
    };
    constexpr std::array<fpr recomp_context::*, 32> fprs = {
        &recomp_context::f0, &recomp_context::f1, &recomp_context::f2, &recomp_context::f3, &recomp_context::f4, &recomp_context::f5, &recomp_context::f6, &recomp_context::f7,
        &recomp_context::f8, &recomp_context::f9, &recomp_context::f10, &recomp_context::f11, &recomp_context::f12, &recomp_context::f13, &recomp_context::f14, &recomp_context::f15,
        &recomp_context::f16, &recomp_context::f17, &recomp_context::f18, &recomp_context::f19, &recomp_context::f20, &recomp_context::f21, &recomp_context::f22, &recomp_context::f23,
        &recomp_context::f24, &recomp_context::f25, &recomp_context::f26, &recomp_context::f27, &recomp_context::f28, &recomp_context::f29, &recomp_context::f30, &recomp_context::f31
    };
    for (uint32_t fr = 0; fr < 2; ++fr) {
        auto seeded = p1::Machine::decode(pristine);
        seeded->ctx.mips3_float_mode = fr;
        seeded->ctx.status_reg = fr << 26;
        for (size_t i = 0; i < 32; ++i) {
            if (i != 0 && i != 29) seeded->ctx.*gprs[i] = 0xFEDCBA9876543200ull + i;
            (seeded->ctx.*fprs[i]).u64 = 0x7FF80000DEADBE00ull + i;
        }
        auto copy = p1::Machine::decode(seeded->encode());
        for (size_t i = 0; i < 32; ++i) {
            check(copy->ctx.*gprs[i] == seeded->ctx.*gprs[i], "GPR codec loss");
            check((copy->ctx.*fprs[i]).u64 == (seeded->ctx.*fprs[i]).u64, "FPR bit codec loss");
        }
        check(copy->ctx.hi == seeded->ctx.hi && copy->ctx.lo == seeded->ctx.lo, "HI/LO codec loss");
        check(copy->ctx.f_odd == (fr ? &copy->ctx.f1.u32l : &copy->ctx.f0.u32h), "Wrong FR pointer binding");
    }

    auto reject = [&](const std::function<void(p1::Machine&)>& mutate) {
        auto bad = p1::Machine::decode(pristine);
        mutate(*bad);
        bool rejected = false;
        try { auto ignored = p1::Machine::decode(bad->encode()); } catch (const std::exception&) { rejected = true; }
        check(rejected, "Invalid semantic continuation accepted");
    };
    reject([](auto& x) { x.frames[0].function ^= 1; });
    reject([](auto& x) { x.frames[0].continuation ^= 4; });
    reject([](auto& x) { std::swap(x.frames[0], x.frames[1]); });
    reject([](auto& x) { x.frames.pop_back(); });
    reject([](auto& x) { x.blocked.destination = 0x12345678; });
    reject([](auto& x) { x.blocked.flags = 0; });
    reject([](auto& x) { x.ctx.r29 = uint64_t(int64_t(int32_t(0x807FFFE0))); x.blocked.destination = 0x807FFFFC; });
    reject([](auto& x) { x.blocked.queue = p1::request_queue; });
    reject([](auto& x) { x.receives = 1; });
    reject([](auto& x) { x.word(p1::reply_queue + 8, 3); });
    reject([](auto& x) { x.ctx.mips3_float_mode = 2; });
    reject([](auto& x) { x.rounding = 4; });
    auto rebuilt = p1::Machine::decode(pristine);
    check(rebuilt->ctx.f_odd != m.ctx.f_odd && rebuilt->memory.data() != m.memory.data(), "Host storage not relocated");
    check(rebuilt->encode() == pristine, "Codec is not canonical");
    // Poison transient pointers: encoding must neither read nor preserve them.
    auto* saved = m.ctx.f_odd;
    m.ctx.f_odd = nullptr;
    check(m.encode() == pristine, "Host pointer leaked into payload");
    m.ctx.f_odd = saved;
    for (int local = 0; local < 3; ++local) {
        auto altered = p1::Machine::decode(pristine);
        if (local == 0) altered->frames[0].hi = 0;
        if (local == 1) altered->frames[0].lo = 0;
        if (local == 2) altered->frames[0].c1cs = 0;
        altered->deliver(message);
        check(altered->run(), "Mutant did not finish");
        bool detected = false;
        try { finished(*altered); } catch (const std::exception&) { detected = true; }
        check(detected, "Test failed to detect lost live local");
    }
    // Message helper boundary behavior used by the one-thread adapter.
    p1::Machine q;
    check(q.receive(p1::reply_queue, 0, 0) && q.ctx.r2 == uint64_t(-1), "Nonblocking empty receive");
    q.deliver(1); q.deliver(2);
    check(q.receive(p1::reply_queue, 0x80000500, 1) && q.word(0x80000500) == 1, "FIFO first");
    q.deliver(3);
    check(q.receive(p1::reply_queue, 0, 1), "Null destination");
    check(q.receive(p1::reply_queue, 0x80000500, 1) && q.word(0x80000500) == 3, "Ring wrap");
    std::cout << "PASS validation, host relocation, live-local mutation, FIFO and idempotence checks\n";
}
int main(int argc, char** argv) try {
    check(argc >= 2, "Expected mode");
    std::string mode = argv[1];
    if (mode == "selftest") { selftest(); return 0; }
    check(argc == 4, "Expected mode input/output FR/output");
    if (mode == "reference") {
        p1::Machine m(true, std::stoul(argv[3]));
        m.frames.clear();
        set_cop1_cs(m.rounding);
        reference_machine = &m;
        p1::reference_root(m.memory.data(), &m.ctx);
        reference_machine = nullptr;
        m.rounding = get_cop1_cs();
        finished(m);
        write(argv[2], m.encode());
        std::cout << "Native generated baseline complete\n";
    } else if (mode == "export") {
        auto m = std::make_unique<p1::Machine>(true, std::stoul(argv[3]));
        parked(*m);
        auto data = m->encode();
        m.reset(); // no native worker/stack/context survives this point
        write(argv[2], data);
        std::cout << "Exported three generated frames at osRecvMesg; execution owner destroyed\n";
    } else if (mode == "import") {
        auto m = p1::Machine::decode(read(argv[2]));
        parked(*m);
        m->deliver(message);
        set_cop1_cs(0); // importing must install semantic FP state, not inherit host state
        check(m->run(), "Did not resume after delivery");
        finished(*m);
        write(argv[3], m->encode());
        std::cout << "Imported in fresh process; receive and caller returns each completed once\n";
    } else throw std::runtime_error("Unknown mode");
} catch (const std::exception& e) { std::cerr << "P1: " << e.what() << '\n'; return 1; }
