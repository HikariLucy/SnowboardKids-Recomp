#include "hle.hpp"
#include "ultramodern/ultramodern.hpp"
#include <stdexcept>
#include <string>
#define SBK_HLE(name, kind) extern "C" void name(uint8_t*, recomp_context*);
#include "hle_allowlist.inc"
#undef SBK_HLE
bool do_send(uint8_t*, int32_t, OSMesg, bool, bool);
bool do_recv(uint8_t*, int32_t, int32_t, bool);
void dequeue_external_messages(uint8_t*);
namespace sbk::continuation {
namespace {
enum class Kind { Leaf, Scheduled, Receive, Send, Jam, Stop };
constexpr uint64_t hash(const char* name) {
    uint64_t id = 14695981039346656037ull;
    while (*name) { id ^= static_cast<unsigned char>(*name++); id *= 1099511628211ull; }
    return id;
}
struct Entry { uint64_t id; recomp_func_t* function; Kind kind; };
const Entry entries[] = {
#define SBK_HLE(name, kind) {hash(#name), name, Kind::kind},
#include "hle_allowlist.inc"
#undef SBK_HLE
};
thread_local bool active = false;
struct ActiveScope {
    ActiveScope() { if (active) throw std::runtime_error("Reentrant continuation HLE"); active = true; }
    ~ActiveScope() { active = false; }
};
}
bool hle_active() { return active; }
bool hle_known(uint64_t id) {
    for (const auto& entry : entries) if (entry.id == id) return true;
    return false;
}
bool hle_replayable_from_safepoint(uint64_t id) {
    // Message HLEs reach their only safepoint in dequeue_external_messages,
    // before any guest-visible effect. Leaf/scheduled handlers may not.
    for (const auto& entry : entries) {
        if (entry.id == id)
            return entry.kind == Kind::Receive || entry.kind == Kind::Send || entry.kind == Kind::Jam;
    }
    return false;
}
uint64_t hle_id_for_token(recomp_func_t* token) {
    for (const auto& entry : entries) if (entry.function == token) return entry.id;
    return 0;
}
HleResult advance_hle(uint8_t* rdram, Execution& execution) {
    auto& op = execution.blocked;
    const Entry* entry = nullptr;
    for (const auto& candidate : entries) if (candidate.id == op.hle_id) { entry = &candidate; break; }
    if (!entry) {
        std::fprintf(stderr, "P4A rejected unclassified continuation HLE: %llu\n", (unsigned long long)op.hle_id);
        throw std::runtime_error("Unclassified continuation HLE " + std::to_string(op.hle_id));
    }
    if (op.phase == BlockedPhase::Committed) {
        op.phase = BlockedPhase::None;
        return HleResult::Complete;
    }
    if (op.phase != BlockedPhase::Begin && op.phase != BlockedPhase::Waiting)
        throw std::runtime_error("Invalid continuation HLE phase");
    ActiveScope scope;
    if (entry->kind == Kind::Receive || entry->kind == Kind::Send || entry->kind == Kind::Jam) {
        const auto mq = static_cast<int32_t>(op.args[0]);
        dequeue_external_messages(rdram);
        const bool success = entry->kind == Kind::Receive
            ? do_recv(rdram, mq, static_cast<int32_t>(op.args[1]), false)
            : do_send(rdram, mq, static_cast<OSMesg>(op.args[1]), entry->kind == Kind::Jam, false);
        if (!success && static_cast<int32_t>(op.args[2]) == OS_MESG_BLOCK) {
            const auto self = ultramodern::this_thread();
            if (!self) throw std::runtime_error("Blocking message operation without guest thread");
            const auto queue = entry->kind == Kind::Receive
                ? GET_MEMBER(OSMesgQueue, mq, blocked_on_recv)
                : GET_MEMBER(OSMesgQueue, mq, blocked_on_send);
            ultramodern::thread_queue_insert(rdram, queue, self);
            TO_PTR(OSThread, self)->state = OSThreadState::BLOCKED;
            op.phase = BlockedPhase::Waiting;
            return HleResult::WaitNext;
        }
        op.result = success ? 0 : -1;
        execution.cpu.r2 = op.result;
        op.phase = BlockedPhase::Committed;
        return HleResult::CheckQueue;
    }
    if (entry->kind == Kind::Stop) {
        const auto requested = static_cast<int32_t>(op.args[0]);
        const auto self = ultramodern::this_thread();
        if (!requested || requested == self) {
            if (!self) throw std::runtime_error("Stop without guest thread");
            TO_PTR(OSThread, self)->state = OSThreadState::STOPPED;
            op.phase = BlockedPhase::Committed;
            return HleResult::WaitNext;
        }
        // Runtime's noncurrent stop removes it from its actual runnable/wait queue.
    }
    entry->function(rdram, &execution.cpu);
    op.phase = BlockedPhase::Committed;
    // Native leaf handlers may enqueue completions. Scheduled handlers defer
    // their priority switch via hle_active(), before any queue pop or signal.
    return HleResult::CheckQueue;
}
}
