#include "runtime.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <string>
namespace p1 {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static size_t offset(uint32_t addr, size_t size = 4) {
    check(addr >= 0x80000000 && addr <= 0x80800000 - size && (addr & 3) == 0, "Invalid guest address");
    return addr - 0x80000000;
}
uint32_t Machine::word(uint32_t addr) const {
    uint32_t value;
    std::memcpy(&value, memory.data() + offset(addr), 4);
    return value;
}
void Machine::word(uint32_t addr, uint32_t value) { std::memcpy(memory.data() + offset(addr), &value, 4); }
void Machine::rebind() { ctx.f_odd = ctx.mips3_float_mode ? &ctx.f1.u32l : &ctx.f0.u32h; }
Machine::Machine(bool initialize, uint32_t fr): memory(memory_size) {
    check(std::endian::native == std::endian::little, "P1 requires little-endian word-swapped RDRAM");
    if (initialize) {
        check(fr <= 1, "Invalid FR mode");
        ctx.mips3_float_mode = fr;
        ctx.status_reg = fr << 26;
        ctx.r29 = int32_t(0x807FF000);
        ctx.r31 = int32_t(0x80001234);
        ctx.f0.u64 = 0x13579BDF3FA00000ull;
        ctx.f1.u64 = 0xABCDEF002468ACE0ull;
        ctx.f31.u64 = 0xDEADBEEF01234567ull;
        ctx.hi = 0x123456789ABCDEF0ull;
        ctx.lo = 0xFEDCBA9876543210ull;
        rounding = 3;
        frames.push_back({root_id});
        for (auto [queue, buffer] : {std::pair{request_queue, 0x80001000u}, std::pair{reply_queue, 0x80001010u}}) {
            word(queue + 16, 2);
            word(queue + 20, buffer);
        }
    }
    rebind();
}
static void queue_valid(const Machine& m, uint32_t q) {
    check(q == request_queue || q == reply_queue, "Unsupported queue");
    check(m.word(q) == 0 && m.word(q + 4) == 0, "P1 has no native scheduler wait lists");
    check(m.word(q + 16) == 2 && m.word(q + 8) <= 2 && m.word(q + 12) < 2, "Invalid queue shape");
    check(m.word(q + 20) == (q == request_queue ? 0x80001000u : 0x80001010u), "Invalid queue buffer");
}
void Machine::send(uint32_t q, uint32_t value) {
    queue_valid(*this, q);
    uint32_t count = word(q + 8), first = word(q + 12), capacity = word(q + 16);
    check(count < capacity, "Blocking send outside P1 scope");
    word(word(q + 20) + 4 * ((first + count) % capacity), value);
    word(q + 8, count + 1);
}
bool Machine::receive(uint32_t q, uint32_t destination, uint32_t flags) {
    queue_valid(*this, q);
    check(flags <= 1, "Unsupported message flags");
    if (destination) offset(destination);
    uint32_t count = word(q + 8), first = word(q + 12);
    if (!count) {
        if (flags == 0) { ctx.r2 = uint64_t(-1); return true; }
        return false;
    }
    if (destination) word(destination, word(word(q + 20) + 4 * first));
    word(q + 12, (first + 1) % word(q + 16));
    word(q + 8, count - 1);
    ctx.r2 = 0;
    ++receives;
    return true;
}
void Machine::deliver(uint32_t value) { send(reply_queue, value); }
bool Machine::run() {
    rebind();
    set_cop1_cs(rounding);
    if (blocked.queue) {
        if (!receive(blocked.queue, blocked.destination, blocked.flags)) return false;
        blocked = {};
    }
    while (!frames.empty()) {
        Action action = dispatch(memory.data(), &ctx, frames.back());
        rounding = get_cop1_cs();
        if (!action.callee) frames.pop_back();
        else if (action.callee == send_id) {
            check(ctx.r6 == 1, "Expected blocking send");
            send(uint32_t(ctx.r4), uint32_t(ctx.r5));
            ctx.r2 = 0;
            ++sends;
        } else if (action.callee == recv_id) {
            Blocked operation{uint32_t(ctx.r4), uint32_t(ctx.r5), uint32_t(ctx.r6)};
            if (!receive(operation.queue, operation.destination, operation.flags)) {
                blocked = operation;
                return false;
            }
        } else {
            check(frames.size() < 16, "P1 frame limit");
            frames.push_back({action.callee});
        }
    }
    return true;
}
void Machine::validate() const {
    check(ctx.mips3_float_mode <= 1 && ((ctx.status_reg >> 26) & 1) == ctx.mips3_float_mode, "Invalid FR state");
    check(rounding <= 3, "Invalid rounding mode");
    check(ctx.r0 == 0, "Invalid zero register");
    queue_valid(*this, request_queue);
    queue_valid(*this, reply_queue);
    check(frames.size() == 3, "Expected three parked generated frames");
    check(frames[0].function == root_id && frames[1].function == middle_id && frames[2].function == leaf_id, "Invalid frame chain");
    for (size_t i = 0; i < frames.size(); ++i) {
        check(frames[i].c1cs == 0 || frames[i].c1cs == 1, "Invalid cop1 condition");
        check(pending_callee(frames[i]) == (i + 1 < frames.size() ? frames[i+1].function : recv_id), "Invalid call edge");
    }
    check(blocked.queue == reply_queue && blocked.flags == 1, "Invalid blocked operation");
    check(ctx.r29 == uint64_t(int64_t(int32_t(0x807FEFB0))), "Invalid parked stack footprint");
    check(blocked.destination == uint32_t(ctx.r29) + 0x1C, "Invalid receive destination");
    offset(blocked.destination);
    check(sends == 1 && receives == 0 && word(reply_queue + 8) == 0, "Invalid receive phase");
}
struct Writer {
    std::vector<uint8_t> data;
    void integer(uint64_t value, unsigned bytes) { for (unsigned i = 0; i < bytes; ++i) data.push_back(uint8_t(value >> (8*i))); }
};
struct Reader {
    std::span<const uint8_t> data;
    size_t pos = 0;
    uint64_t integer(unsigned bytes) {
        check(bytes <= data.size() - pos, "Truncated continuation");
        uint64_t value = 0;
        for (unsigned i = 0; i < bytes; ++i) value |= uint64_t(data[pos++]) << (8*i);
        return value;
    }
};
std::vector<uint8_t> Machine::encode() const {
    Writer w;
    w.integer(0x54433150, 4); // P1CT, test transport only
    w.integer(1, 4);
    for (char ch : std::string(build_identity)) w.integer(uint8_t(ch), 1);
    w.integer(ctx.r0, 8);
    w.integer(ctx.r1, 8);
    w.integer(ctx.r2, 8);
    w.integer(ctx.r3, 8);
    w.integer(ctx.r4, 8);
    w.integer(ctx.r5, 8);
    w.integer(ctx.r6, 8);
    w.integer(ctx.r7, 8);
    w.integer(ctx.r8, 8);
    w.integer(ctx.r9, 8);
    w.integer(ctx.r10, 8);
    w.integer(ctx.r11, 8);
    w.integer(ctx.r12, 8);
    w.integer(ctx.r13, 8);
    w.integer(ctx.r14, 8);
    w.integer(ctx.r15, 8);
    w.integer(ctx.r16, 8);
    w.integer(ctx.r17, 8);
    w.integer(ctx.r18, 8);
    w.integer(ctx.r19, 8);
    w.integer(ctx.r20, 8);
    w.integer(ctx.r21, 8);
    w.integer(ctx.r22, 8);
    w.integer(ctx.r23, 8);
    w.integer(ctx.r24, 8);
    w.integer(ctx.r25, 8);
    w.integer(ctx.r26, 8);
    w.integer(ctx.r27, 8);
    w.integer(ctx.r28, 8);
    w.integer(ctx.r29, 8);
    w.integer(ctx.r30, 8);
    w.integer(ctx.r31, 8);
    w.integer(ctx.f0.u64, 8);
    w.integer(ctx.f1.u64, 8);
    w.integer(ctx.f2.u64, 8);
    w.integer(ctx.f3.u64, 8);
    w.integer(ctx.f4.u64, 8);
    w.integer(ctx.f5.u64, 8);
    w.integer(ctx.f6.u64, 8);
    w.integer(ctx.f7.u64, 8);
    w.integer(ctx.f8.u64, 8);
    w.integer(ctx.f9.u64, 8);
    w.integer(ctx.f10.u64, 8);
    w.integer(ctx.f11.u64, 8);
    w.integer(ctx.f12.u64, 8);
    w.integer(ctx.f13.u64, 8);
    w.integer(ctx.f14.u64, 8);
    w.integer(ctx.f15.u64, 8);
    w.integer(ctx.f16.u64, 8);
    w.integer(ctx.f17.u64, 8);
    w.integer(ctx.f18.u64, 8);
    w.integer(ctx.f19.u64, 8);
    w.integer(ctx.f20.u64, 8);
    w.integer(ctx.f21.u64, 8);
    w.integer(ctx.f22.u64, 8);
    w.integer(ctx.f23.u64, 8);
    w.integer(ctx.f24.u64, 8);
    w.integer(ctx.f25.u64, 8);
    w.integer(ctx.f26.u64, 8);
    w.integer(ctx.f27.u64, 8);
    w.integer(ctx.f28.u64, 8);
    w.integer(ctx.f29.u64, 8);
    w.integer(ctx.f30.u64, 8);
    w.integer(ctx.f31.u64, 8);
    w.integer(ctx.hi, 8); w.integer(ctx.lo, 8);
    w.integer(ctx.status_reg, 4); w.integer(ctx.mips3_float_mode, 1);
    // f_odd, native padding, native fenv, and container layouts are NOT encoded.
    w.integer(rounding, 4); w.integer(sends, 4); w.integer(receives, 4);
    w.integer(blocked.queue, 4); w.integer(blocked.destination, 4); w.integer(blocked.flags, 4);
    w.integer(frames.size(), 4);
    for (const Frame& f : frames) {
        w.integer(f.function, 8); w.integer(f.continuation, 4);
        w.integer(f.hi, 8); w.integer(f.lo, 8); w.integer(f.result, 8); w.integer(uint32_t(f.c1cs), 4);
    }
    w.data.insert(w.data.end(), memory.begin(), memory.end());
    return w.data;
}
std::unique_ptr<Machine> Machine::decode(std::span<const uint8_t> data) {
    Reader r{data};
    check(r.integer(4) == 0x54433150 && r.integer(4) == 1, "Unknown continuation schema");
    for (char ch : std::string(build_identity)) check(r.integer(1) == uint8_t(ch), "Incompatible generated build");
    auto m = std::make_unique<Machine>(false);
    auto& ctx = m->ctx;
    ctx.r0 = r.integer(8);
    ctx.r1 = r.integer(8);
    ctx.r2 = r.integer(8);
    ctx.r3 = r.integer(8);
    ctx.r4 = r.integer(8);
    ctx.r5 = r.integer(8);
    ctx.r6 = r.integer(8);
    ctx.r7 = r.integer(8);
    ctx.r8 = r.integer(8);
    ctx.r9 = r.integer(8);
    ctx.r10 = r.integer(8);
    ctx.r11 = r.integer(8);
    ctx.r12 = r.integer(8);
    ctx.r13 = r.integer(8);
    ctx.r14 = r.integer(8);
    ctx.r15 = r.integer(8);
    ctx.r16 = r.integer(8);
    ctx.r17 = r.integer(8);
    ctx.r18 = r.integer(8);
    ctx.r19 = r.integer(8);
    ctx.r20 = r.integer(8);
    ctx.r21 = r.integer(8);
    ctx.r22 = r.integer(8);
    ctx.r23 = r.integer(8);
    ctx.r24 = r.integer(8);
    ctx.r25 = r.integer(8);
    ctx.r26 = r.integer(8);
    ctx.r27 = r.integer(8);
    ctx.r28 = r.integer(8);
    ctx.r29 = r.integer(8);
    ctx.r30 = r.integer(8);
    ctx.r31 = r.integer(8);
    ctx.f0.u64 = r.integer(8);
    ctx.f1.u64 = r.integer(8);
    ctx.f2.u64 = r.integer(8);
    ctx.f3.u64 = r.integer(8);
    ctx.f4.u64 = r.integer(8);
    ctx.f5.u64 = r.integer(8);
    ctx.f6.u64 = r.integer(8);
    ctx.f7.u64 = r.integer(8);
    ctx.f8.u64 = r.integer(8);
    ctx.f9.u64 = r.integer(8);
    ctx.f10.u64 = r.integer(8);
    ctx.f11.u64 = r.integer(8);
    ctx.f12.u64 = r.integer(8);
    ctx.f13.u64 = r.integer(8);
    ctx.f14.u64 = r.integer(8);
    ctx.f15.u64 = r.integer(8);
    ctx.f16.u64 = r.integer(8);
    ctx.f17.u64 = r.integer(8);
    ctx.f18.u64 = r.integer(8);
    ctx.f19.u64 = r.integer(8);
    ctx.f20.u64 = r.integer(8);
    ctx.f21.u64 = r.integer(8);
    ctx.f22.u64 = r.integer(8);
    ctx.f23.u64 = r.integer(8);
    ctx.f24.u64 = r.integer(8);
    ctx.f25.u64 = r.integer(8);
    ctx.f26.u64 = r.integer(8);
    ctx.f27.u64 = r.integer(8);
    ctx.f28.u64 = r.integer(8);
    ctx.f29.u64 = r.integer(8);
    ctx.f30.u64 = r.integer(8);
    ctx.f31.u64 = r.integer(8);
    ctx.hi = r.integer(8); ctx.lo = r.integer(8);
    ctx.status_reg = r.integer(4); ctx.mips3_float_mode = r.integer(1);
    m->rounding = r.integer(4); m->sends = r.integer(4); m->receives = r.integer(4);
    m->blocked.queue = r.integer(4); m->blocked.destination = r.integer(4); m->blocked.flags = r.integer(4);
    auto count = r.integer(4);
    check(count == 3, "Invalid frame count");
    for (size_t i = 0; i < count; ++i) {
        Frame f{};
        f.function = r.integer(8); f.continuation = r.integer(4);
        f.hi = r.integer(8); f.lo = r.integer(8); f.result = r.integer(8); f.c1cs = int32_t(r.integer(4));
        m->frames.push_back(f);
    }
    check(data.size() - r.pos == memory_size, "Incorrect memory length");
    std::copy(data.begin() + r.pos, data.end(), m->memory.begin());
    m->rebind();
    m->validate();
    return m;
}
}
