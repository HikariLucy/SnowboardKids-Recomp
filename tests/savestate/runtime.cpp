// P4-B/P4-C non-live integration fixture. Actual patched ultramodern scheduler,
// message queues, timer thread and thread lifetimes; actual continuation
// dispatcher/HLE/owner registry; actual P2 coordinator. The guest program is a
// hand-written continuation corpus (no ROM, renderer or SDL).
#include "continuation/dispatch.hpp"
#include "continuation/execution.hpp"
#include "continuation/hle.hpp"
#include "continuation/runtime_owner.hpp"
#include "quiescence/quiescence.hpp"
#include "quiescence/renderer_state.hpp"
#include "savestate/host_audio.hpp"
#include "savestate/runtime_domains.hpp"
#include "savestate/sbks.hpp"
#include "savestate/service.hpp"
#include "savestate/storage.hpp"
#include "ultramodern/savestate.hpp"
#include "ultramodern/ultramodern.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace std::chrono_literals;
namespace q = sbk::quiescence;
namespace c = sbk::continuation;
namespace s = sbk::savestate;
#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #value); std::fflush(stderr); std::_Exit(1); } } while (false)

std::atomic_bool exited{false}; // runtime worker ABI

// ---- guest layout ----------------------------------------------------------
constexpr uint64_t kExtent = 16ull * 1024 * 1024;
static uint8_t* ram = nullptr;
constexpr int32_t IDLE = int32_t(0x80001000), PRODUCER = int32_t(0x80001100), CONSUMER = int32_t(0x80001200);
constexpr int32_t SLEEPER = int32_t(0x80001300), STOPPER = int32_t(0x80001400), WORKER = int32_t(0x80001500);
constexpr int32_t DATAQ = int32_t(0x80003000), DATAMSG = int32_t(0x80003100);
constexpr int32_t TICKQ = int32_t(0x80003200), TICKMSG = int32_t(0x80003300);
constexpr int32_t MARKQ = int32_t(0x80003400), MARKMSG = int32_t(0x80003500);
constexpr int32_t TIMER = int32_t(0x80003800);
constexpr int32_t PRODUCED = int32_t(0x80006000), CONSUMED = int32_t(0x80006004), CHECKSUM = int32_t(0x80006008);
constexpr int32_t ERROR_FLAG = int32_t(0x8000600C), LAST = int32_t(0x80006010), WORKER_RUNS = int32_t(0x80006014);
constexpr int32_t STOPPER_RUNS = int32_t(0x80006018), TICK_DEST = int32_t(0x80006020), RECV_DEST = int32_t(0x80006030);
constexpr int32_t CREATE_SP = int32_t(0x80006100), CREATE_PRI = int32_t(0x80006104);
constexpr int32_t GATE = int32_t(0x80006108); // nonzero: consumer stops receiving
constexpr int32_t EXT_BASE = int32_t(0x80C00000); // beyond 8 MiB: "extended" pages
constexpr uint32_t EXT_PAGES = 32;

int32_t& word(int32_t address) { uint8_t* rdram = ram; return *TO_PTR(int32_t, address); }

// ---- continuation corpus ---------------------------------------------------
using c::Action;
using c::ActionKind;
using c::Frame;
constexpr uint64_t ID_BOOT = 0x1001ull << 32, ID_IDLE = ID_BOOT | 1, ID_PRODUCER = ID_BOOT | 2,
    ID_CONSUMER = ID_BOOT | 3, ID_SLEEPER = ID_BOOT | 4, ID_STOPPER = ID_BOOT | 5, ID_WORKER = ID_BOOT | 6;
constexpr uint32_t FN_IDLE = 0x80100100, FN_PRODUCER = 0x80100200, FN_CONSUMER = 0x80100300,
    FN_SLEEPER = 0x80100400, FN_STOPPER = 0x80100500, FN_WORKER = 0x80100600;

extern "C" void osRecvMesg_recomp(uint8_t*, recomp_context*) { std::abort(); }
extern "C" void osSendMesg_recomp(uint8_t*, recomp_context*) { std::abort(); }
extern "C" void osStartThread_recomp(uint8_t* rdram, recomp_context* ctx) { osStartThread(rdram, int32_t(ctx->r4)); }
extern "C" void osStopThread_recomp(uint8_t* rdram, recomp_context* ctx) { osStopThread(rdram, int32_t(ctx->r4)); }
extern "C" void osCreateThread_recomp(uint8_t* rdram, recomp_context* ctx) {
    osCreateThread(rdram, int32_t(ctx->r4), OSId(ctx->r5), int32_t(ctx->r6), int32_t(ctx->r7),
        *TO_PTR(int32_t, CREATE_SP), OSPri(*TO_PTR(int32_t, CREATE_PRI)));
}
uint64_t hle(recomp_func_t* token) { auto id = c::hle_id_for_token(token); CHECK(id); return id; }
Action call_hle(recomp_func_t* token) { return {ActionKind::Hle, hle(token), false}; }
Action recv(recomp_context* ctx, int32_t queue, int32_t dest) {
    ctx->r4 = uint64_t(int64_t(queue)); ctx->r5 = uint64_t(int64_t(dest)); ctx->r6 = OS_MESG_BLOCK;
    return call_hle(osRecvMesg_recomp);
}
Action send(recomp_context* ctx, int32_t queue, int32_t value) {
    ctx->r4 = uint64_t(int64_t(queue)); ctx->r5 = uint64_t(int64_t(value)); ctx->r6 = OS_MESG_BLOCK;
    return call_hle(osSendMesg_recomp);
}

Action step_boot(uint8_t*, recomp_context*, Frame&) { return {ActionKind::Return}; }
Action step_idle(uint8_t*, recomp_context*, Frame&) { return {ActionKind::Pause}; }
Action step_sleeper(uint8_t*, recomp_context*, Frame&) { return {ActionKind::Return}; }
Action step_worker(uint8_t*, recomp_context* ctx, Frame&) {
    ++word(WORKER_RUNS);
    (void)ctx;
    return {ActionKind::Return};
}
Action step_stopper(uint8_t*, recomp_context* ctx, Frame& f) {
    switch (f.continuation) {
    case 0: ++word(STOPPER_RUNS); f.continuation = 1; ctx->r4 = 0; return call_hle(osStopThread_recomp);
    default: f.continuation = 0; return {ActionKind::Yield};
    }
}
// Two values per timer tick into a capacity-1 queue: the second send blocks
// until the lower-priority consumer runs (blocked sender at most freezes).
Action step_producer(uint8_t*, recomp_context* ctx, Frame& f) {
    auto& n = f.scratch[0];
    switch (f.continuation) {
    case 0: f.continuation = 1; return recv(ctx, TICKQ, TICK_DEST);
    case 1:
        n = uint64_t(word(PRODUCED)) + 1;
        word(PRODUCED) = int32_t(n);
        // Walks into new, initially zero pages as play advances.
        word(EXT_BASE + int32_t(((n / 64) % EXT_PAGES) * 4096) + int32_t(n % 64) * 4) = int32_t(n);
        f.continuation = 2; return send(ctx, DATAQ, int32_t(n));
    case 2:
        n = uint64_t(word(PRODUCED)) + 1;
        word(PRODUCED) = int32_t(n);
        f.continuation = 3; return send(ctx, DATAQ, int32_t(n));
    case 3:
        if (n % 16 == 0) {
            word(CREATE_SP) = int32_t(0x800F8000); word(CREATE_PRI) = 12;
            ctx->r4 = uint64_t(int64_t(WORKER)); ctx->r5 = 6; ctx->r6 = uint64_t(int64_t(int32_t(FN_WORKER))); ctx->r7 = n;
            f.continuation = 4; return call_hle(osCreateThread_recomp);
        }
        f.continuation = 5; return {ActionKind::Yield};
    case 4: // address reuse: a finished WORKER lifetime is replaced by a new one
        ctx->r4 = uint64_t(int64_t(WORKER)); f.continuation = 5; return call_hle(osStartThread_recomp);
    default: f.continuation = 0; return {ActionKind::Yield};
    }
}
Action step_consumer(uint8_t*, recomp_context* ctx, Frame& f) {
    switch (f.continuation) {
    case 0: ctx->r4 = uint64_t(int64_t(PRODUCER)); f.continuation = 1; return call_hle(osStartThread_recomp);
    case 1: ctx->r4 = uint64_t(int64_t(STOPPER)); f.continuation = 2; return call_hle(osStartThread_recomp);
    case 2:
        if (word(GATE)) return {ActionKind::Pause}; // keeps delivering external events
        f.continuation = 3; return recv(ctx, DATAQ, RECV_DEST);
    case 3: {
        const int32_t value = word(RECV_DEST);
        if (value != word(LAST) + 1) word(ERROR_FLAG) = value;
        word(LAST) = value;
        ++word(CONSUMED);
        word(CHECKSUM) = int32_t(uint32_t(word(CHECKSUM)) * 31u + uint32_t(value));
        f.continuation = 4;
        return {ActionKind::Yield};
    }
    default: f.continuation = 2; return {ActionKind::Yield};
    }
}
void token_boot(uint8_t*, recomp_context*) {}
void token_idle(uint8_t*, recomp_context*) {}
void token_producer(uint8_t*, recomp_context*) {}
void token_consumer(uint8_t*, recomp_context*) {}
void token_sleeper(uint8_t*, recomp_context*) {}
void token_stopper(uint8_t*, recomp_context*) {}
void token_worker(uint8_t*, recomp_context*) {}
const std::map<uint32_t, uint64_t> roots = {{FN_IDLE, ID_IDLE}, {FN_PRODUCER, ID_PRODUCER},
    {FN_CONSUMER, ID_CONSUMER}, {FN_SLEEPER, ID_SLEEPER}, {FN_STOPPER, ID_STOPPER}, {FN_WORKER, ID_WORKER}};

extern "C" recomp_func_t* get_function(int32_t) { std::fprintf(stderr, "unexpected lookup\n"); std::abort(); }
extern "C" void switch_error(const char*, uint32_t, uint32_t) { std::abort(); }
void run_thread_function(uint8_t* rdram, uint64_t addr, uint64_t sp, uint64_t arg) {
    auto* execution = c::current_execution();
    CHECK(execution);
    auto& ctx = execution->cpu;
    ctx.r29 = sp; ctx.r4 = arg; ctx.mips3_float_mode = 0; ctx.f_odd = &ctx.f0.u32h;
    c::run_execution(rdram, *execution, roots.at(uint32_t(addr)));
}

// ---- harness ---------------------------------------------------------------
template<class P> void until(P predicate, std::chrono::seconds limit = 10s) {
    auto deadline = std::chrono::steady_clock::now() + limit;
    while (!predicate()) {
        q::poll();
        CHECK(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(100us);
    }
}
uint64_t freeze() {
    uint64_t gen = 0;
    until([&] { q::poll(); return q::status().state == q::State::Idle && (gen = q::request()) != 0; });
    until([] { return q::status().state == q::State::Frozen; });
    return gen;
}
void thaw(uint64_t gen) {
    CHECK(q::resume(gen));
    until([] { return q::status().state == q::State::Idle; });
}
// Synthetic host audio device behind the real AudioDomain (P5 boundary):
// advancing play submits PCM and the "device" consumes part of it, so every
// capture sees a pending, partially consumed backlog. Test thread only.
struct FakeAudio {
    s::audio::Boundary boundary{1 << 20};
    std::vector<uint8_t> queue;
    std::array<float, 8> history{};
    uint32_t produced = 0;
    s::audio::DeviceFormat format() const { return {ultramodern::savestate::get_audio_frequency(), 48000, 2}; }
    void play(size_t frames) {
        std::vector<uint8_t> pcm(frames * 8);
        for (auto& b : pcm) b = uint8_t(++produced);
        queue.insert(queue.end(), pcm.begin(), pcm.end());
        boundary.submitted(pcm.data(), pcm.size());
        history[produced % 8] = float(produced);
        const size_t consumed = std::min(queue.size(), (frames * 3 / 4) * 8); // whole frames
        queue.erase(queue.begin(), queue.begin() + long(consumed));
    }
    s::HostAudio host() {
        return {[this](s::AudioState& st, std::string& e) { return boundary.capture(format(), history, uint32_t(queue.size()), st, e); },
            [this](const s::AudioState& st, std::string& e) {
                const s::audio::HostQueue ops{[this] { return uint32_t(queue.size()); }, [this] { queue.clear(); },
                    [this](const uint8_t* d, uint32_t n, std::string&) { queue.insert(queue.end(), d, d + n); return true; }};
                return boundary.install(st, format(), history, ops, e);
            },
            [this](const s::AudioState& st, std::string& e) { return boundary.validate(st, format(), history.size(), e); }};
    }
};
FakeAudio fake_audio;

void advance(int items) {
    const int target = word(CONSUMED) + items;
    until([&] { return word(CONSUMED) >= target; });
    fake_audio.play(size_t(40 + items % 13));
}
std::vector<int32_t> queue_contents(int32_t queue) {
    uint8_t* rdram = ram;
    auto* mq = TO_PTR(OSMesgQueue, queue);
    std::vector<int32_t> result;
    for (int i = 0; i < mq->validCount; ++i) result.push_back(TO_PTR(OSMesg, mq->msg)[(mq->first + i) % mq->msgCount]);
    return result;
}
void inject(int32_t value) {
    // A real host producer thread, never a game owner.
    std::thread([value] { ultramodern::enqueue_external_message(MARKQ, value, false, true); }).join();
}
struct Stats {
    std::vector<uint64_t> values;
    void add(uint64_t v) { values.push_back(v); }
    uint64_t pct(double p) {
        auto sorted = values; std::sort(sorted.begin(), sorted.end());
        return sorted[std::min(sorted.size() - 1, size_t(p * (sorted.size() - 1) + 0.5))];
    }
    std::string line() {
        return "median=" + std::to_string(pct(0.5)) + " p95=" + std::to_string(pct(0.95)) + " max=" + std::to_string(pct(1.0));
    }
};
void check_owners(size_t expected, uint64_t stale_below) {
    CHECK(c::live_owner_count() == expected);
    CHECK(q::owner_count() == expected);
    for (uint32_t address : {uint32_t(IDLE), uint32_t(PRODUCER), uint32_t(CONSUMER), uint32_t(SLEEPER), uint32_t(STOPPER), uint32_t(WORKER)})
        for (uint64_t lifetime = 1; lifetime < stale_below; ++lifetime) CHECK(!c::key_is_live({address, lifetime}));
}
bool contains_pattern(const std::vector<uint8_t>& bytes, uint64_t value) {
    if (bytes.size() < 8) return false;
    for (size_t i = 0; i + 8 <= bytes.size(); ++i) {
        uint64_t v; std::memcpy(&v, bytes.data() + i, 8);
        if (v == value) return true;
    }
    return false;
}

// Synthetic renderer blob (P3 'SBK3' layout) for renderer hashing without a GPU.
std::vector<uint8_t> renderer_blob(uint64_t generation, uint8_t color_seed, uint8_t depth_seed) {
    q::SemanticStateHeader header{};
    header.magic = q::SEMANTIC_STATE_MAGIC; header.version = q::SEMANTIC_STATE_VERSION;
    header.generation = generation; header.rdp.fill_color = 0x1234; header.vi.origin = 0x100;
    header.framebuffer_count = 2;
    std::vector<uint8_t> blob(sizeof(header));
    std::memcpy(blob.data(), &header, sizeof(header));
    for (uint8_t type : {uint8_t(1), uint8_t(2)}) {
        q::SemanticFramebufferHeader fb{};
        fb.address = type == 1 ? 0x100000 : 0x200000; fb.width = 16; fb.height = 8; fb.siz = 2; fb.type = type;
        fb.pixel_bytes = 16 * 8 * 2;
        const auto* raw = reinterpret_cast<const uint8_t*>(&fb);
        blob.insert(blob.end(), raw, raw + sizeof(fb));
        for (uint32_t i = 0; i < fb.pixel_bytes; ++i) blob.push_back(uint8_t(i * (type == 1 ? color_seed : depth_seed)));
    }
    return blob;
}

s::sbks::Identity file_identity(const s::SnapshotService& service) {
    s::sbks::Identity id;
    id.rom_hash = 0x5EEDull;          // fixture "ROM" identity
    id.build = {0xC0FFEEull, 7, 61};
    id.present_mask = service.available();
    return id;
}

std::vector<uint8_t> encode_file(const s::InMemorySnapshot& snap, const s::SnapshotService& service) {
    std::vector<uint8_t> bytes;
    std::string error;
    if (!s::sbks::encode(snap, file_identity(service), {"fixture", "quick"}, bytes, error)) {
        std::fprintf(stderr, "encode: %s\n", error.c_str());
        CHECK(false);
    }
    return bytes;
}

// Restore rejected before any mutation: same memory, hashes and owners; the
// barrier stays Frozen and the timeline continues.
void expect_rejected(s::SnapshotService& service, const s::InMemorySnapshot& bad, const char* what) {
    const uint64_t gen = freeze();
    s::InMemorySnapshot before;
    CHECK(service.capture(gen, before).ok);
    const auto memory = std::vector<uint8_t>(ram, ram + kExtent);
    const auto owners = c::live_owner_count();
    const auto total = c::total_registered_owners();
    const auto queue = fake_audio.queue;
    auto r = service.restore(gen, bad);
    CHECK(!r.ok && !r.rolled_back && !r.unrecoverable);
    CHECK(q::status().state == q::State::Frozen);
    CHECK(std::memcmp(memory.data(), ram, kExtent) == 0);
    CHECK(c::live_owner_count() == owners && c::total_registered_owners() == total);
    CHECK(fake_audio.queue == queue);
    s::InMemorySnapshot after;
    CHECK(service.capture(gen, after).ok);
    CHECK(after.hashes == before.hashes);
    thaw(gen);
    advance(10);
    CHECK(word(ERROR_FLAG) == 0);
    std::printf("rejected without mutation: %-26s %s\n", what, r.error.c_str());
}

int main(int argc, char** argv) {
    enum class Mode { Full, Save, Load } mode = Mode::Full;
    std::filesystem::path state_file;
    if (argc == 3 && !std::strcmp(argv[1], "--save")) { mode = Mode::Save; state_file = argv[2]; }
    else if (argc == 3 && !std::strcmp(argv[1], "--load")) { mode = Mode::Load; state_file = argv[2]; }
    else if (argc != 1) { std::fprintf(stderr, "usage: savestate_runtime [--save|--load <file.sbks>]\n"); return 2; }
    ram = static_cast<uint8_t*>(std::aligned_alloc(4096, kExtent));
    std::memset(ram, 0, kExtent);
    uint8_t* rdram = ram;
    q::enable();
    const std::pair<uint64_t, c::Step*> steps[] = {{ID_BOOT, step_boot}, {ID_IDLE, step_idle}, {ID_PRODUCER, step_producer},
        {ID_CONSUMER, step_consumer}, {ID_SLEEPER, step_sleeper}, {ID_STOPPER, step_stopper}, {ID_WORKER, step_worker}};
    recomp_func_t* tokens[] = {token_boot, token_idle, token_producer, token_consumer, token_sleeper, token_stopper, token_worker};
    for (size_t i = 0; i < 7; ++i)
        c::register_function({steps[i].first, 0x80100000u + uint32_t(i) * 0x100, steps[i].first == ID_PRODUCER ? 2u : 0u,
            steps[i].second, tokens[i], "fixture"});
    // Startup continuation runs and permanently retires, as in the real game.
    recomp_context boot{}; boot.f_odd = &boot.f0.u32h;
    c::enter(ID_BOOT, rdram, &boot);
    CHECK(c::startup_is_retired());

    osCreateMesgQueue(rdram, DATAQ, DATAMSG, 1);
    osCreateMesgQueue(rdram, TICKQ, TICKMSG, 4);
    osCreateMesgQueue(rdram, MARKQ, MARKMSG, 16);
    ultramodern::init_thread_cleanup();
    ultramodern::init_timers(rdram);
    std::thread([] { for (;;) { q::vi_boundary(); std::this_thread::sleep_for(200us); } }).detach();
    std::thread([] { for (;;) { q::device_boundary(q::Device::Rsp); std::this_thread::sleep_for(200us); } }).detach();
    std::thread([] { for (;;) { q::device_boundary(q::Device::Graphics); std::this_thread::sleep_for(200us); } }).detach();
    osCreateThread(rdram, IDLE, 1, int32_t(FN_IDLE), 0, int32_t(0x800F0000), 1);
    osCreateThread(rdram, PRODUCER, 2, int32_t(FN_PRODUCER), 0, int32_t(0x800F1000), 10);
    osCreateThread(rdram, CONSUMER, 3, int32_t(FN_CONSUMER), 0, int32_t(0x800F2000), 5);
    osCreateThread(rdram, SLEEPER, 4, int32_t(FN_SLEEPER), 0, int32_t(0x800F3000), 3); // never started
    osCreateThread(rdram, STOPPER, 5, int32_t(FN_STOPPER), 0, int32_t(0x800F4000), 4);
    // Real periodic timer: 3 ms ticks into TICKQ (retries when full).
    osSetTimer(rdram, TIMER, 46875 * 3, 46875 * 3, TICKQ, 7);
    ultramodern::schedule_running_thread(rdram, IDLE);
    osStartThread(rdram, CONSUMER);
    q::ready();
    until([] { return word(CONSUMED) >= 60 && word(WORKER_RUNS) >= 1 && word(STOPPER_RUNS) == 1; });

    s::SnapshotService service;
    s::MemoryDomain memory(ram, kExtent, s::thread_context_slots);
    s::ContinuationDomain continuations(ram);
    s::SchedulerDomain scheduler;
    s::TimeDomain time;
    s::AudioDomain audio(fake_audio.host());
    s::InputDomain input;
    for (s::Domain* d : std::initializer_list<s::Domain*>{&memory, &continuations, &scheduler, &time, &audio, &input}) service.add(d);
    service.set_build({0xC0FFEEull, 7, 61});

    // ---- P6 cross-process: process A saves, a fresh process B loads ----
    if (mode == Mode::Save) {
        advance(100);
        const uint64_t gen = freeze();
        s::InMemorySnapshot saved;
        CHECK(service.capture(gen, saved).ok);
        const auto bytes = encode_file(saved, service);
        std::string error;
        CHECK(s::storage::write_atomic(state_file, bytes, error));
        std::printf("process A: saved consumed=%d checksum=%d aggregate=%016llx bytes=%zu\n", word(CONSUMED), word(CHECKSUM),
            (unsigned long long)saved.hashes.aggregate, bytes.size());
        thaw(gen);
        advance(50);
        std::puts("PASS: process A saved and exits without cleanup");
        std::fflush(stdout);
        std::_Exit(0);
    }
    if (mode == Mode::Load) {
        advance(37); // a different point of a fresh timeline, other host lifetimes
        std::vector<uint8_t> bytes;
        std::string error;
        CHECK(s::storage::read_bounded(state_file, s::sbks::Limits{}.max_file_bytes, bytes, error) == s::storage::ReadStatus::Ok);
        s::InMemorySnapshot loaded;
        const auto decoded = s::sbks::decode(bytes, file_identity(service), loaded);
        if (!decoded.ok()) std::fprintf(stderr, "decode: %s %s\n", s::sbks::status_name(decoded.status), decoded.detail.c_str());
        CHECK(decoded.ok());
        const auto owners_before = c::total_registered_owners();
        const uint64_t gen = freeze();
        auto r = service.restore(gen, loaded);
        if (!r.ok) std::fprintf(stderr, "restore: %s / %s\n", r.error.c_str(), r.rollback_error.c_str());
        CHECK(r.ok && r.hashes == loaded.hashes);
        CHECK(c::logical_lifetime_counter() == loaded.continuations.logical_lifetime_counter);
        check_owners(loaded.continuations.threads.size(), owners_before + 1);
        const int consumed = word(CONSUMED);
        std::printf("process B: restored consumed=%d checksum=%d aggregate=%016llx restore_us=%llu\n", consumed, word(CHECKSUM),
            (unsigned long long)r.hashes.aggregate, (unsigned long long)r.total_micros);
        thaw(gen);
        advance(300);
        CHECK(word(ERROR_FLAG) == 0 && word(CONSUMED) >= consumed + 300);
        std::puts("PASS: process B loaded the .sbks of process A and continued 300 items");
        std::fflush(stdout);
        std::_Exit(0);
    }

    // ---- A/C/D: capture integrity, canonical equality, no host addresses ----
    uint64_t gen = freeze();
    const auto before = std::vector<uint8_t>(ram, ram + kExtent);
    const auto time_before = osGetTime();
    s::InMemorySnapshot snap, again;
    auto result = service.capture(gen, snap);
    if (!result.ok) std::fprintf(stderr, "capture error: %s\n", result.error.c_str());
    CHECK(result.ok);
    CHECK(std::memcmp(before.data(), ram, kExtent) == 0);  // capture mutated nothing
    CHECK(osGetTime() == time_before);                      // no logical time advance
    CHECK(q::status().state == q::State::Frozen);
    std::string why;
    CHECK(s::validate_snapshot(snap, why));
    CHECK(service.capture(gen, again).ok);
    CHECK(again.hashes == snap.hashes);                     // C: same logical state twice
    CHECK(snap.continuations.threads.size() == 5 || snap.continuations.threads.size() == 6);
    std::vector<uint64_t> host_values{uint64_t(uintptr_t(ram)), uint64_t(uintptr_t(&snap))};
    for (int32_t t : {IDLE, PRODUCER, CONSUMER, SLEEPER, STOPPER}) {
        auto* guest = TO_PTR(OSThread, t);
        CHECK(guest->context != nullptr);
        host_values.push_back(uint64_t(uintptr_t(guest->context)));
    }
    for (size_t d = 0; d < s::kDomainCount; ++d) {
        const auto bytes = s::canonical_bytes(snap, s::DomainId(d));
        for (uint64_t value : host_values) CHECK(!contains_pattern(bytes, value)); // D
    }
    bool saw_not_started = false, saw_stopped = false;
    for (const auto& t : snap.continuations.threads) {
        if (t.address == uint32_t(SLEEPER)) { saw_not_started = !t.started && t.run == s::RunState::Sleeping; }
        if (t.address == uint32_t(STOPPER)) {
            saw_stopped = t.started && t.run == s::RunState::Sleeping && TO_PTR(OSThread, STOPPER)->state == STOPPED;
        }
    }
    CHECK(saw_not_started); // not-started owner
    CHECK(saw_stopped);     // F: stopped thread
    const auto snap_consumed = word(CONSUMED), snap_last = word(LAST), snap_worker = word(WORKER_RUNS);
    const auto snap_checksum = word(CHECKSUM);
    std::printf("capture: threads=%zu inbox=%zu timers=%zu payload=%zu mapped=%llu resident=%lld nonzero_pages=%llu nonzero=%llu us=%llu\n",
        snap.continuations.threads.size(), snap.scheduler.inbox.size(), snap.time.active_timers.size(), result.payload_bytes,
        (unsigned long long)result.memory.mapped_bytes, (long long)result.memory.resident_bytes,
        (unsigned long long)result.memory.nonzero_page_bytes, (unsigned long long)result.memory.nonzero_bytes,
        (unsigned long long)result.total_micros);
    CHECK(snap.time.active_timers.size() == 1);
    thaw(gen);

    // ---- B: each category changes its own hash only ----
    {
        auto mutate = [&](auto change, s::DomainId expected) {
            auto copy = snap; change(copy);
            const auto hashes = s::compute_hashes(copy);
            for (size_t d = 0; d < s::kDomainCount; ++d) {
                const bool differs = hashes.domain[d] != snap.hashes.domain[d];
                CHECK(differs == (s::DomainId(d) == expected));
            }
            CHECK(hashes.aggregate != snap.hashes.aggregate);
        };
        mutate([](auto& x) { x.memory.data[17] ^= 1; }, s::DomainId::Memory);
        mutate([](auto& x) { x.continuations.threads[1].cpu.gpr[4] ^= 1; }, s::DomainId::Continuations);
        mutate([](auto& x) { x.continuations.threads[0].cpu.fpr[3] ^= 1; }, s::DomainId::Continuations);
        mutate([](auto& x) { x.continuations.threads[0].logical_lifetime ^= 0; x.continuations.logical_lifetime_counter += 1; }, s::DomainId::Continuations);
        mutate([](auto& x) { x.scheduler.running_queue_head ^= 4; }, s::DomainId::Scheduler);
        mutate([](auto& x) { x.scheduler.inbox.push_back({MARKQ, 9, false, true}); }, s::DomainId::Scheduler);
        mutate([](auto& x) { x.time.logical_ns += 1; }, s::DomainId::Time);
        mutate([](auto& x) { x.time.active_timers.push_back(TIMER + 0x40); }, s::DomainId::Time);
        mutate([](auto& x) { x.audio.guest_frequency += 1; }, s::DomainId::Audio);
        mutate([](auto& x) { x.input.host_latches = 1; }, s::DomainId::Input);
        // I: renderer/color/depth fixtures (present only in this copy)
        auto base = snap;
        base.present |= s::domain_bit(s::DomainId::Renderer) | s::domain_bit(s::DomainId::Color) | s::domain_bit(s::DomainId::Depth);
        base.renderer.blob = renderer_blob(1, 3, 5);
        base.hashes = s::compute_hashes(base);
        CHECK(s::validate_snapshot(base, why));
        auto renderer_case = [&](std::vector<uint8_t> blob, s::DomainId expected) {
            auto copy = base; copy.renderer.blob = std::move(blob);
            const auto hashes = s::compute_hashes(copy);
            for (size_t d = 0; d < s::kDomainCount; ++d)
                CHECK((hashes.domain[d] != base.hashes.domain[d]) == (s::DomainId(d) == expected));
        };
        renderer_case(renderer_blob(1, 4, 5), s::DomainId::Color);
        renderer_case(renderer_blob(1, 3, 6), s::DomainId::Depth);
        auto semantic = renderer_blob(1, 3, 5);
        reinterpret_cast<q::SemanticStateHeader*>(semantic.data())->rdp.fill_color = 0x4321;
        renderer_case(semantic, s::DomainId::Renderer);
        CHECK(s::compute_hashes([&] { auto x = base; x.renderer.blob = renderer_blob(99, 3, 5); return x; }()) == base.hashes);
        auto broken = base; broken.renderer.blob.pop_back(); broken.hashes = s::compute_hashes(broken);
        CHECK(!s::validate_snapshot(broken, why)); // truncated planes rejected before mutation
    }

    // ---- restore after independent advancement (H: zero/extended pages) ----
    advance(200);
    CHECK(word(CONSUMED) > snap_consumed);
    const uint64_t owners_before_restore = c::total_registered_owners();
    gen = freeze();
    {
        // A page that was zero at capture but is written now must return to zero.
        int32_t written_zero_page = -1;
        for (uint32_t p = 0; p < EXT_PAGES; ++p) {
            const uint32_t index = uint32_t(EXT_BASE - int32_t(0x80000000) + p * 4096) / 4096;
            const bool saved = std::binary_search(snap.memory.pages.begin(), snap.memory.pages.end(), index);
            bool nonzero = false;
            for (uint32_t i = 0; i < 1024 && !nonzero; ++i) nonzero = word(EXT_BASE + int32_t(p * 4096 + i * 4)) != 0;
            if (!saved && nonzero) { written_zero_page = int32_t(p); break; }
        }
        auto restored = service.restore(gen, snap);
        if (!restored.ok) std::fprintf(stderr, "restore error: %s / %s\n", restored.error.c_str(), restored.rollback_error.c_str());
        CHECK(restored.ok);
        CHECK(restored.hashes == snap.hashes);
        CHECK(word(CONSUMED) == snap_consumed && word(LAST) == snap_last && word(WORKER_RUNS) == snap_worker);
        CHECK(word(CHECKSUM) == snap_checksum);
        CHECK(written_zero_page >= 0); // H: exercised, not vacuous
        for (uint32_t i = 0; i < 1024; ++i) CHECK(word(EXT_BASE + written_zero_page * 4096 + int32_t(i * 4)) == 0);
        std::printf("restore: us=%llu zero_page_rewritten=%d\n", (unsigned long long)restored.total_micros, written_zero_page);
        check_owners(snap.continuations.threads.size(), owners_before_restore + 1); // G: stale lifetimes invalid
        for (const auto& t : snap.continuations.threads) {
            auto* guest = TO_PTR(OSThread, int32_t(t.address));
            CHECK(guest->context == c::context_for(guest)); // cache rebound from the registry
        }
    }
    thaw(gen);
    advance(300); // continuing play from the restored timeline
    CHECK(word(ERROR_FLAG) == 0);
    CHECK(word(WORKER_RUNS) > snap_worker); // worker address reused after restore

    // ---- Phase 6: external arrivals around capture/restore ----
    {
        until([] { return queue_contents(MARKQ).empty(); });
        service.on_step = [&](const char* step) { if (!std::strcmp(step, "capture-begin")) inject(101); };
        gen = freeze();
        s::InMemorySnapshot marks;
        CHECK(service.capture(gen, marks).ok);
        CHECK(ultramodern::savestate::deferred_external_count() == 0);
        for (auto& m : marks.scheduler.inbox) CHECK(m.msg != 101); // sealed: not in the snapshot
        service.on_step = nullptr;
        thaw(gen);
        inject(102); // after resume
        advance(20);
        CHECK((queue_contents(MARKQ) == std::vector<int32_t>{101, 102}));
        gen = freeze();
        inject(106); // during Frozen, before a transaction: part of the captured state
        s::InMemorySnapshot with_marks;
        CHECK(service.capture(gen, with_marks).ok);
        CHECK(std::count_if(with_marks.scheduler.inbox.begin(), with_marks.scheduler.inbox.end(), [](auto& m) { return m.msg == 106; }) == 1);
        thaw(gen);
        advance(20);
        CHECK((queue_contents(MARKQ) == std::vector<int32_t>{101, 102, 106}));
        inject(103);
        advance(20);
        CHECK((queue_contents(MARKQ) == std::vector<int32_t>{101, 102, 106, 103}));
        const auto superseded_before = scheduler.superseded, admitted_before = scheduler.admitted;
        service.on_step = [&](const char* step) { if (!std::strcmp(step, "memory")) inject(104); };
        gen = freeze();
        auto restored = service.restore(gen, with_marks);
        CHECK(restored.ok);
        service.on_step = nullptr;
        CHECK(scheduler.superseded == superseded_before + 1); // 104: old-timeline arrival, accounted
        CHECK(scheduler.admitted == admitted_before);
        thaw(gen);
        inject(105);
        advance(20);
        // Restored timeline: 101/102 from memory, 106 delivered exactly once
        // from the restored inbox, 103 abandoned with the old timeline, 105 live.
        CHECK((queue_contents(MARKQ) == std::vector<int32_t>{101, 102, 106, 105}));
        gen = freeze();
        uint8_t* r = ram; auto* mq = TO_PTR(OSMesgQueue, MARKQ); (void)r;
        mq->validCount = 0; mq->first = 0; // test-only drain while Frozen
        thaw(gen);
        CHECK(word(ERROR_FLAG) == 0);
    }

    // ---- E: full queue with a blocked sender and blocked receivers ----
    {
        gen = freeze(); word(GATE) = 1; thaw(gen);
        s::InMemorySnapshot blocked;
        for (;;) {
            gen = freeze();
            if (TO_PTR(OSMesgQueue, DATAQ)->blocked_on_send == PRODUCER) break;
            thaw(gen);
        }
        CHECK(service.capture(gen, blocked).ok);
        const auto pending = queue_contents(DATAQ);
        CHECK(pending.size() == 1);
        bool sender_waiting = false;
        for (const auto& t : blocked.continuations.threads) {
            if (t.address == uint32_t(PRODUCER))
                sender_waiting = t.run == s::RunState::Sleeping && t.blocked.phase == uint8_t(c::BlockedPhase::Waiting) &&
                    t.blocked.hle_id == hle(osSendMesg_recomp) && int32_t(t.blocked.args[1]) == pending[0] + 1;
        }
        CHECK(sender_waiting);
        const auto consumed = word(CONSUMED);
        word(GATE) = 0;
        thaw(gen);
        advance(40);
        gen = freeze();
        CHECK(service.restore(gen, blocked).ok);
        CHECK(TO_PTR(OSMesgQueue, DATAQ)->blocked_on_send == PRODUCER && queue_contents(DATAQ) == pending);
        CHECK(word(CONSUMED) == consumed);
        word(GATE) = 0; // harness input into the restored timeline, still Frozen
        thaw(gen);
        advance(40); // the saved blocked send completes exactly once, in order
        CHECK(word(ERROR_FLAG) == 0);
        std::printf("blocked sender: queued=%d pending_send=%d restored and completed\n", pending[0], pending[0] + 1);
    }

    // ---- Phase 9: fault injection, rollback to the pre-restore timeline ----
    for (auto fault : {s::FaultPoint::AfterRetire, s::FaultPoint::AfterMemory, s::FaultPoint::AfterTime,
             s::FaultPoint::AfterScheduler, s::FaultPoint::AfterContinuations, s::FaultPoint::AfterAudio,
             s::FaultPoint::PostValidate}) {
        advance(50);
        gen = freeze();
        s::InMemorySnapshot current;
        CHECK(service.capture(gen, current).ok);
        const auto consumed = word(CONSUMED), checksum = word(CHECKSUM);
        const auto total = c::total_registered_owners();
        auto r = service.restore(gen, snap, fault);
        if (!r.rolled_back) std::fprintf(stderr, "fault %s: %s / %s\n", s::fault_name(fault), r.error.c_str(), r.rollback_error.c_str());
        CHECK(!r.ok && r.rolled_back && !r.unrecoverable);
        CHECK(r.hashes == current.hashes); // previous timeline restored exactly
        CHECK(word(CONSUMED) == consumed && word(CHECKSUM) == checksum);
        check_owners(current.continuations.threads.size(), total + 1);
        CHECK(q::status().state == q::State::Frozen);
        thaw(gen);
        advance(30); // still reachable, continues from the rolled-back timeline
        CHECK(word(CONSUMED) > consumed);
        CHECK(word(ERROR_FLAG) == 0);
        std::printf("fault %-20s rolled back: %s\n", s::fault_name(fault), r.error.c_str());
    }

    // ---- Phase 10: 100 capture/advance/restore cycles ----
    Stats capture_us, restore_us, payload;
    size_t blocked_receivers = 0;
    s::InMemorySnapshot cycle_snapshot;
    int32_t cycle_consumed = 0, cycle_checksum = 0;
    for (int cycle = 0; cycle < 100; ++cycle) {
        if (cycle % 10 == 0) {
            gen = freeze();
            auto r = service.capture(gen, cycle_snapshot);
            CHECK(r.ok);
            capture_us.add(r.total_micros);
            payload.add(r.payload_bytes);
            cycle_consumed = word(CONSUMED); cycle_checksum = word(CHECKSUM);
            for (const auto& t : cycle_snapshot.continuations.threads)
                blocked_receivers += t.blocked.phase == uint8_t(c::BlockedPhase::Waiting) && t.blocked.hle_id == hle(osRecvMesg_recomp);
            thaw(gen);
        }
        advance(10 + cycle % 7);
        const uint64_t total = c::total_registered_owners();
        gen = freeze();
        auto r = service.restore(gen, cycle_snapshot);
        if (!r.ok) std::fprintf(stderr, "cycle %d: %s\n", cycle, r.error.c_str());
        CHECK(r.ok);
        CHECK(r.hashes == cycle_snapshot.hashes);
        CHECK(word(CONSUMED) == cycle_consumed && word(CHECKSUM) == cycle_checksum);
        check_owners(cycle_snapshot.continuations.threads.size(), total + 1);
        restore_us.add(r.total_micros);
        thaw(gen);
        CHECK(word(ERROR_FLAG) == 0);
    }
    advance(100);
    CHECK(word(ERROR_FLAG) == 0);
    CHECK(scheduler.superseded == 1);
    CHECK(blocked_receivers > 0);
    std::printf("stress: cycles=100 capture_us median=%llu p95=%llu max=%llu restore_us median=%llu p95=%llu max=%llu payload_max=%llu\n",
        (unsigned long long)capture_us.pct(0.5), (unsigned long long)capture_us.pct(0.95), (unsigned long long)capture_us.pct(1.0),
        (unsigned long long)restore_us.pct(0.5), (unsigned long long)restore_us.pct(0.95), (unsigned long long)restore_us.pct(1.0),
        (unsigned long long)payload.pct(1.0));

    // ---- Phase 2 robustness: the same snapshot restored 100 times ----
    {
        gen = freeze();
        s::InMemorySnapshot a;
        CHECK(service.capture(gen, a).ok);
        const auto a_consumed = word(CONSUMED), a_checksum = word(CHECKSUM);
        const auto a_queue = fake_audio.queue;
        thaw(gen);
        const auto superseded = scheduler.superseded, admitted = scheduler.admitted;
        for (int i = 0; i < 100; ++i) {
            advance(5 + i % 11);
            const uint64_t total = c::total_registered_owners();
            gen = freeze();
            auto r = service.restore(gen, a);
            if (!r.ok) std::fprintf(stderr, "same-snapshot restore %d: %s\n", i, r.error.c_str());
            CHECK(r.ok && r.hashes == a.hashes);
            CHECK(word(CONSUMED) == a_consumed && word(CHECKSUM) == a_checksum);
            CHECK(fake_audio.queue == a_queue);                     // backlog queued once, never accumulated
            CHECK(c::logical_lifetime_counter() == a.continuations.logical_lifetime_counter); // no leaked lifetimes
            check_owners(a.continuations.threads.size(), total + 1);
            thaw(gen);
            CHECK(word(ERROR_FLAG) == 0);
        }
        CHECK(scheduler.superseded == superseded && scheduler.admitted == admitted); // no events duplicated/lost
        // capture A -> capture B -> restore B; a failed capture never replaces B.
        advance(20);
        gen = freeze();
        s::InMemorySnapshot b;
        CHECK(service.capture(gen, b).ok);
        CHECK(!(b.hashes == a.hashes));
        const auto b_consumed = word(CONSUMED);
        thaw(gen);
        advance(30);
        s::InMemorySnapshot slot = b;
        CHECK(!service.capture(gen, slot).ok);                        // stale generation: rejected
        CHECK(slot.hashes == b.hashes);
        gen = freeze();
        auto r = service.restore(gen, slot);
        CHECK(r.ok && r.hashes == b.hashes && word(CONSUMED) == b_consumed);
        thaw(gen);
        advance(20);
        CHECK(word(ERROR_FLAG) == 0);
        std::puts("repeated restore: 100x same snapshot (0 mismatches, 0 stale owners, 0 leaked lifetimes); A->B->restore B; failed capture keeps slot");

        // Rejections before mutation.
        auto bad = b; bad.magic ^= 1; bad.hashes = s::compute_hashes(bad);
        expect_rejected(service, bad, "invalid magic");
        bad = b; bad.build.corpus_digest ^= 1; bad.hashes = s::compute_hashes(bad);
        expect_rejected(service, bad, "incompatible build");
        bad = b; bad.hashes.domain[size_t(s::DomainId::Time)] ^= 1;
        expect_rejected(service, bad, "corrupt hash");
        bad = b; bad.memory.data.resize(bad.memory.data.size() - 4096); bad.hashes = s::compute_hashes(bad);
        expect_rejected(service, bad, "truncated memory");
        bad = b; bad.memory.extent *= 2; bad.hashes = s::compute_hashes(bad);
        expect_rejected(service, bad, "memory extent");
        bad = b; bad.present &= ~s::domain_bit(s::DomainId::Input); bad.hashes = s::compute_hashes(bad);
        expect_rejected(service, bad, "domain set");
        bad = b; bad.audio.host_output_rate = 44100; bad.hashes = s::compute_hashes(bad);
        expect_rejected(service, bad, "audio device format");
        bad = b; bad.continuations.threads[0].frames.front().scratch.push_back(1); bad.hashes = s::compute_hashes(bad);
        expect_rejected(service, bad, "continuation frame shape");
    }

    // ---- Phase 19: capture -> encode -> decode -> restore (100) and disk (50) ----
    {
        Stats encode_us, decode_us, save_us, load_us, restore_file_us;
        size_t file_bytes = 0;
        const auto dir = std::filesystem::temp_directory_path() / ("sbk-runtime-" + std::to_string(getpid()));
        std::filesystem::create_directories(dir);
        const auto path = *s::storage::slot_path(dir, s::storage::kQuickSlot);
        std::string error;
        for (int cycle = 0; cycle < 150; ++cycle) {
            const bool disk = cycle >= 100;
            advance(8 + cycle % 5);
            gen = freeze();
            s::InMemorySnapshot captured, twice;
            CHECK(service.capture(gen, captured).ok);
            const int consumed = word(CONSUMED), checksum = word(CHECKSUM);
            auto t = std::chrono::steady_clock::now();
            const auto bytes = encode_file(captured, service);
            encode_us.add(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t).count());
            if (cycle % 25 == 0) { CHECK(service.capture(gen, twice).ok); CHECK(encode_file(twice, service) == bytes); } // deterministic
            file_bytes = bytes.size();
            if (disk) {
                t = std::chrono::steady_clock::now();
                CHECK(s::storage::write_atomic(path, bytes, error));
                save_us.add(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t).count());
            }
            thaw(gen);
            advance(10 + cycle % 7);
            std::vector<uint8_t> input = bytes;
            t = std::chrono::steady_clock::now();
            if (disk) CHECK(s::storage::read_bounded(path, s::sbks::Limits{}.max_file_bytes, input, error) == s::storage::ReadStatus::Ok);
            s::InMemorySnapshot loaded;
            CHECK(s::sbks::decode(input, file_identity(service), loaded).ok());
            (disk ? load_us : decode_us).add(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t).count());
            CHECK(loaded.hashes == captured.hashes);
            const uint64_t total = c::total_registered_owners();
            gen = freeze();
            auto r = service.restore(gen, loaded);
            if (!r.ok) std::fprintf(stderr, "file cycle %d: %s\n", cycle, r.error.c_str());
            CHECK(r.ok && r.hashes == captured.hashes);
            CHECK(word(CONSUMED) == consumed && word(CHECKSUM) == checksum);
            CHECK(c::logical_lifetime_counter() == loaded.continuations.logical_lifetime_counter);
            check_owners(loaded.continuations.threads.size(), total + 1);
            if (disk) restore_file_us.add(r.total_micros);
            thaw(gen);
            CHECK(word(ERROR_FLAG) == 0);
        }
        // The same file loaded repeatedly into an advancing timeline.
        std::vector<uint8_t> bytes;
        CHECK(s::storage::read_bounded(path, s::sbks::Limits{}.max_file_bytes, bytes, error) == s::storage::ReadStatus::Ok);
        s::InMemorySnapshot last;
        CHECK(s::sbks::decode(bytes, file_identity(service), last).ok());
        for (int i = 0; i < 10; ++i) {
            advance(15);
            gen = freeze();
            CHECK(service.restore(gen, last).ok);
            thaw(gen);
        }
        advance(50);
        CHECK(word(ERROR_FLAG) == 0);
        size_t leftovers = 0;
        for (auto& e : std::filesystem::directory_iterator(dir)) leftovers += e.path().filename() != "quick.sbks";
        CHECK(leftovers == 0);
        std::filesystem::remove_all(dir);
        std::printf("persistence stress: 100 encode/decode/restore + 50 disk save/load/restore + 10 reloads, fixture file_bytes=%zu\n", file_bytes);
        std::printf("  fixture encode_us %s decode_us %s\n", encode_us.line().c_str(), decode_us.line().c_str());
        std::printf("  fixture save_us %s load_us %s restore_us %s\n", save_us.line().c_str(), load_us.line().c_str(), restore_file_us.line().c_str());
    }

    // ---- Unrecoverable path (last: leaves the barrier sealed in Restore) ----
    gen = freeze();
    auto fatal = service.restore(gen, snap, s::FaultPoint::DuringRollback);
    CHECK(!fatal.ok && !fatal.rolled_back && fatal.unrecoverable);
    CHECK(q::status().state == q::State::Restore);
    CHECK(!q::resume(gen));            // partially applied state can never be released
    CHECK(ultramodern::savestate::external_inbox_sealed());
    std::printf("unrecoverable rollback stays sealed: %s / %s\n", fatal.error.c_str(), fatal.rollback_error.c_str());
    std::puts("PASS: P4-B capture + P4-C transactional restore on the actual runtime scheduler/queues/timer/owners");
    std::fflush(stdout);
    std::_Exit(0);
}
