#include "runtime_domains.hpp"

#include "continuation/execution.hpp"
#include "continuation/hle.hpp"
#include "continuation/runtime_owner.hpp"
#include "quiescence/quiescence.hpp"
#include "ultramodern/savestate.hpp"
#include "ultramodern/ultramodern.hpp"

#include <cfenv>
#include <cstddef>
#include <thread>

namespace sbk::savestate {
namespace q = sbk::quiescence;
namespace c = sbk::continuation;
namespace us = ultramodern::savestate;
namespace {
using namespace std::chrono_literals;

template<class Predicate> bool wait_until(Predicate predicate, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(50us);
    }
    return true;
}

constexpr gpr recomp_context::* gprs[32] = {
    &recomp_context::r0, &recomp_context::r1, &recomp_context::r2, &recomp_context::r3,
    &recomp_context::r4, &recomp_context::r5, &recomp_context::r6, &recomp_context::r7,
    &recomp_context::r8, &recomp_context::r9, &recomp_context::r10, &recomp_context::r11,
    &recomp_context::r12, &recomp_context::r13, &recomp_context::r14, &recomp_context::r15,
    &recomp_context::r16, &recomp_context::r17, &recomp_context::r18, &recomp_context::r19,
    &recomp_context::r20, &recomp_context::r21, &recomp_context::r22, &recomp_context::r23,
    &recomp_context::r24, &recomp_context::r25, &recomp_context::r26, &recomp_context::r27,
    &recomp_context::r28, &recomp_context::r29, &recomp_context::r30, &recomp_context::r31};
constexpr fpr recomp_context::* fprs[32] = {
    &recomp_context::f0, &recomp_context::f1, &recomp_context::f2, &recomp_context::f3,
    &recomp_context::f4, &recomp_context::f5, &recomp_context::f6, &recomp_context::f7,
    &recomp_context::f8, &recomp_context::f9, &recomp_context::f10, &recomp_context::f11,
    &recomp_context::f12, &recomp_context::f13, &recomp_context::f14, &recomp_context::f15,
    &recomp_context::f16, &recomp_context::f17, &recomp_context::f18, &recomp_context::f19,
    &recomp_context::f20, &recomp_context::f21, &recomp_context::f22, &recomp_context::f23,
    &recomp_context::f24, &recomp_context::f25, &recomp_context::f26, &recomp_context::f27,
    &recomp_context::f28, &recomp_context::f29, &recomp_context::f30, &recomp_context::f31};

// Host <cfenv> macro values differ per platform; the snapshot uses MIPS FCSR.RM.
uint8_t to_mips_rounding(int mode) {
    switch (mode) {
    case FE_TONEAREST: return 0;
    case FE_TOWARDZERO: return 1;
    case FE_UPWARD: return 2;
    case FE_DOWNWARD: return 3;
    default: throw std::runtime_error("unknown host rounding mode");
    }
}
int from_mips_rounding(uint8_t mode) {
    constexpr int modes[] = {FE_TONEAREST, FE_TOWARDZERO, FE_UPWARD, FE_DOWNWARD};
    return modes[mode & 3];
}

CpuState export_cpu(const recomp_context& ctx, int rounding) {
    CpuState cpu;
    for (size_t i = 0; i < 32; ++i) {
        cpu.gpr[i] = static_cast<uint64_t>(ctx.*gprs[i]);
        cpu.fpr[i] = (ctx.*fprs[i]).u64;
    }
    cpu.hi = ctx.hi; cpu.lo = ctx.lo;
    cpu.status = ctx.status_reg;
    cpu.fr = ctx.mips3_float_mode;
    cpu.rounding = to_mips_rounding(rounding);
    return cpu; // f_odd is a host pointer: rebound from `fr`, never stored.
}

c::Execution import_execution(const ThreadState& t) {
    c::Execution e{};
    for (size_t i = 0; i < 32; ++i) {
        e.cpu.*gprs[i] = static_cast<gpr>(t.cpu.gpr[i]);
        (e.cpu.*fprs[i]).u64 = t.cpu.fpr[i];
    }
    e.cpu.hi = t.cpu.hi; e.cpu.lo = t.cpu.lo;
    e.cpu.status_reg = t.cpu.status;
    e.cpu.mips3_float_mode = t.cpu.fr;
    e.cpu.f_odd = e.cpu.mips3_float_mode ? &e.cpu.f1.u32l : &e.cpu.f0.u32h; // rebound on bind as well
    e.rounding_mode = from_mips_rounding(t.cpu.rounding);
    for (const auto& f : t.frames) {
        c::Frame frame;
        frame.function = f.function; frame.continuation = f.continuation;
        frame.hi = f.hi; frame.lo = f.lo; frame.result = f.result;
        frame.c1cs = f.c1cs; frame.indirect_target = f.indirect_target;
        frame.scratch = f.scratch;
        e.frames.push_back(std::move(frame));
    }
    e.blocked.hle_id = t.blocked.hle_id;
    e.blocked.phase = static_cast<c::BlockedPhase>(t.blocked.phase);
    for (size_t i = 0; i < 4; ++i) e.blocked.args[i] = t.blocked.args[i];
    e.blocked.result = t.blocked.result;
    e.blocked.tail = t.blocked.tail;
    e.started = t.started;
    e.finished = false;
    e.pending = static_cast<c::PendingOp>(t.pending);
    return e;
}

bool classify(const c::OwnerRecord& owner, const q::OwnerStatus& site, ThreadState& t, std::string& error) {
    const auto& e = owner.execution;
    if (e.finished) { error = "finished execution still owned"; return false; }
    switch (site.site) {
    case q::Site::Sleeping:
        // Its scheduler operation completes once it wakes; nothing to redo.
        t.run = RunState::Sleeping;
        t.pending = 0;
        return true;
    case q::Site::ParkedWake:
        t.run = RunState::Running;
        t.pending = 0;
        return true;
    case q::Site::ParkedSafepoint:
    case q::Site::ParkedRestored:
        t.run = RunState::Running;
        t.pending = static_cast<uint8_t>(e.pending);
        if (e.pending == c::PendingOp::None && e.blocked.phase != c::BlockedPhase::None &&
            e.blocked.phase != c::BlockedPhase::Committed && !c::hle_replayable_from_safepoint(e.blocked.hle_id)) {
            error = "owner parked inside a non-replayable native HLE";
            return false;
        }
        return true;
    case q::Site::Active:
        break;
    }
    error = "owner still active at Frozen";
    return false;
}
}

std::vector<NormalizedSlot> thread_context_slots(const InMemorySnapshot& snapshot) {
    std::vector<NormalizedSlot> slots;
    for (const auto& t : snapshot.continuations.threads) {
        slots.push_back({t.address - kGuestBase + static_cast<uint32_t>(offsetof(OSThread, context)),
            static_cast<uint32_t>(sizeof(OSThread::context))});
    }
    return slots;
}

bool ContinuationDomain::capture(InMemorySnapshot& out, std::string& error) {
    if (!c::startup_is_retired()) { error = "startup execution context has not retired"; return false; }
    if (!wait_until([] { return q::owners_settled(); }, timeout)) {
        error = "execution owners did not settle (scheduler signal in flight)";
        return false;
    }
    const auto owners = c::export_owners();
    const auto sites = q::owner_status();
    auto& state = out.continuations;
    state.logical_lifetime_counter = c::logical_lifetime_counter();
    state.threads.clear();
    for (const auto& owner : owners) {
        const q::OwnerStatus* site = nullptr;
        for (const auto& candidate : sites) {
            if (candidate.guest != owner.address || candidate.retired) continue;
            if (site) { error = "guest address reuse in flight"; return false; }
            site = &candidate;
        }
        if (!site) { error = "live owner has no barrier registration"; return false; }
        ThreadState t;
        t.address = owner.address;
        t.logical_lifetime = owner.logical_lifetime;
        t.entrypoint = owner.entrypoint;
        t.argument = owner.argument;
        t.started = owner.execution.started;
        if (!classify(owner, *site, t, error)) return false;
        const auto& e = owner.execution;
        t.cpu = export_cpu(e.cpu, e.rounding_mode);
        for (const auto& f : e.frames) {
            t.frames.push_back({f.function, f.continuation, f.hi, f.lo, f.result, f.c1cs, f.indirect_target, f.scratch});
        }
        t.blocked.hle_id = e.blocked.hle_id;
        t.blocked.phase = static_cast<uint8_t>(e.blocked.phase);
        for (size_t i = 0; i < 4; ++i) t.blocked.args[i] = e.blocked.args[i];
        t.blocked.result = e.blocked.result;
        t.blocked.tail = e.blocked.tail;
        state.threads.push_back(std::move(t));
    }
    return true;
}

bool ContinuationDomain::validate(const InMemorySnapshot& in, std::string& error) const {
    // A load into a fresh process waits until boot has handed over to owners.
    if (!c::startup_is_retired()) { error = "runtime not ready: startup execution context still active"; return false; }
    for (const auto& t : in.continuations.threads) {
        for (const auto& f : t.frames) {
            try {
                if (c::descriptor(f.function).scratch_count != f.scratch.size()) {
                    error = "continuation frame scratch shape differs from this build";
                    return false;
                }
            } catch (const std::exception&) {
                error = "unknown continuation function identity";
                return false;
            }
        }
        if (t.blocked.hle_id && !c::hle_known(t.blocked.hle_id)) { error = "unknown blocked HLE"; return false; }
        if (t.run == RunState::Running && !t.pending && t.blocked.phase != 0 && t.blocked.phase != 3 &&
            !c::hle_replayable_from_safepoint(t.blocked.hle_id)) {
            error = "saved owner parked inside a non-replayable native HLE";
            return false;
        }
    }
    return true;
}

bool ContinuationDomain::retire(std::string& error) {
    // Detach and signal first: stale keys and guest lookups fail before any
    // worker wakes; a woken sleeper parks until the barrier retires it.
    c::retire_all_owners();
    q::retire_owners();
    // Workers unwind through thread_terminated without guest access; the
    // runtime cleaner joins each exact worker before releasing its storage.
    if (!wait_until([] { return q::owner_count() == 0 && c::stored_owner_count() == 0; }, timeout)) {
        error = "old execution owners did not retire/join";
        return false;
    }
    return true;
}

bool ContinuationDomain::install(const InMemorySnapshot& in, std::string& error) {
    if (c::live_owner_count() || c::stored_owner_count()) { error = "stale owners remain"; return false; }
    const auto& state = in.continuations;
    c::set_logical_lifetime_counter(state.logical_lifetime_counter);
    uint8_t* rdram = rdram_;
    for (const auto& t : state.threads) {
        c::OwnerRecord record{t.address, t.logical_lifetime, t.entrypoint, t.argument, import_execution(t)};
        const auto start = t.run == RunState::Sleeping ? c::RestoreStart::Sleeping : c::RestoreStart::Running;
        auto* guest = TO_PTR(OSThread, static_cast<int32_t>(t.address));
        auto* context = c::create_restored_owner(record, guest, start);
        us::spawn_restored_thread(rdram, static_cast<int32_t>(t.address), context);
    }
    const size_t expected = state.threads.size();
    if (!wait_until([&] { return q::owner_count() == expected && q::owners_settled(); }, timeout)) {
        error = "reconstructed owners did not reach their saved wait sites";
        return false;
    }
    return true;
}

bool SchedulerDomain::capture(InMemorySnapshot& out, std::string&) {
    out.scheduler.running_queue_head = us::get_running_queue_head();
    out.scheduler.inbox.clear();
    for (const auto& m : us::export_external_inbox())
        out.scheduler.inbox.push_back({m.mq, m.msg, m.jam, m.requeue_if_blocked});
    return true;
}

bool SchedulerDomain::install(const InMemorySnapshot& in, std::string&) {
    us::set_running_queue_head(in.scheduler.running_queue_head);
    std::vector<us::InboxMessage> inbox;
    for (const auto& m : in.scheduler.inbox) inbox.push_back({m.mq, m.msg, m.jam, m.requeue_if_blocked});
    us::import_external_inbox(inbox);
    return true;
}

void SchedulerDomain::begin(Transaction) { us::seal_external_inbox(); }

void SchedulerDomain::end(Transaction, bool committed_restore) {
    const auto result = us::unseal_external_inbox(!committed_restore);
    admitted += result.admitted;
    superseded += result.superseded;
}

bool TimeDomain::capture(InMemorySnapshot& out, std::string& error) {
    const auto state = us::export_time();
    if (state.pending_timer_actions) { error = "timer actions pending at the boundary"; return false; }
    out.time.logical_ns = state.logical_ns;
    out.time.ostime_offset = state.ostime_offset;
    out.time.active_timers.assign(state.active_timers.begin(), state.active_timers.end());
    return true;
}

bool TimeDomain::install(const InMemorySnapshot& in, std::string& error) {
    us::TimeState state{};
    state.logical_ns = in.time.logical_ns;
    state.ostime_offset = in.time.ostime_offset;
    state.active_timers.assign(in.time.active_timers.begin(), in.time.active_timers.end());
    if (!us::import_time(state)) { error = "logical clock not paused or timer set rejected"; return false; }
    return true;
}

bool AudioDomain::capture(InMemorySnapshot& out, std::string& error) {
    out.audio = {};
    out.audio.guest_frequency = us::get_audio_frequency();
    if (!host_.capture) return true;
    out.audio.host_present = true;
    return host_.capture(out.audio, error);
}

bool AudioDomain::validate(const InMemorySnapshot& in, std::string& error) const {
    if (in.audio.host_present != static_cast<bool>(host_.capture)) {
        error = "host audio adapter availability differs from the snapshot";
        return false;
    }
    if (in.audio.host_present && host_.validate) return host_.validate(in.audio, error);
    return true;
}

bool AudioDomain::install(const InMemorySnapshot& in, std::string& error) {
    // Rebuilds the host converter for the current device at the saved rate.
    us::restore_audio_frequency(in.audio.guest_frequency);
    if (!host_.install) return true;
    return host_.install(in.audio, error);
}
}
