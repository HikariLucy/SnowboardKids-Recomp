#include "dev_trigger.hpp"

#include "app_domains.hpp"
#include "librecomp/addresses.hpp"
#include "quiescence/quiescence.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <optional>
#include <string>

namespace sbk::savestate::dev {
namespace q = sbk::quiescence;
namespace {
using HostClock = std::chrono::steady_clock;
enum class Op { None, Capture, Restore };

bool on = false;
Config config;
uint8_t* memory = nullptr;
std::string control_path;
uint64_t control_seq = 0;
HostClock::time_point next_control_read;
bool capture_was_down = false, restore_was_down = false;

Op pending = Op::None;
FaultPoint pending_fault = FaultPoint::None;
uint64_t operation = 0;
HostClock::time_point requested_at;
bool sealed_failure = false;

struct Runtime {
    SnapshotService service;
    std::unique_ptr<MemoryDomain> memory;
    ContinuationDomain continuations;
    SchedulerDomain scheduler;
    TimeDomain time;
    ViDomain vi;
    AudioDomain audio;
    InputDomain input;
    OverlayDomain overlays;
    RspDomain rsp;
    RendererDomain renderer;
    explicit Runtime(uint8_t* rdram)
        : memory(std::make_unique<MemoryDomain>(rdram, recomp::mem_size, thread_context_slots)),
          continuations(rdram), audio(config.audio) {
        for (Domain* domain : std::initializer_list<Domain*>{memory.get(), &continuations, &scheduler, &time, &vi,
                 &audio, &input, &overlays, &rsp, &renderer})
            service.add(domain);
        service.set_build(config.build);
    }
};
std::unique_ptr<Runtime> runtime;
std::unique_ptr<InMemorySnapshot> snapshot; // the single in-memory snapshot

FaultPoint parse_fault(const std::string& name) {
    for (unsigned i = 0; i < static_cast<unsigned>(FaultPoint::Count); ++i) {
        if (name == fault_name(static_cast<FaultPoint>(i))) return static_cast<FaultPoint>(i);
    }
    std::fprintf(stderr, "P4 DEV unknown fault '%s' ignored\n", name.c_str());
    return FaultPoint::None;
}

void read_control() {
    if (control_path.empty() || HostClock::now() < next_control_read) return;
    next_control_read = HostClock::now() + std::chrono::milliseconds(100);
    std::ifstream input(control_path);
    uint64_t seq = 0;
    std::string command, fault;
    while (input >> seq >> command) {
        std::getline(input, fault);
        if (seq <= control_seq) continue;
        control_seq = seq;
        const auto start = fault.find_first_not_of(" \t");
        fault = start == std::string::npos ? "" : fault.substr(start);
        if (command == "capture") { pending = Op::Capture; pending_fault = FaultPoint::None; }
        else if (command == "restore") { pending = Op::Restore; pending_fault = fault.empty() ? FaultPoint::None : parse_fault(fault); }
        else std::fprintf(stderr, "P4 DEV unknown command '%s' (seq=%llu)\n", command.c_str(), (unsigned long long)seq);
    }
}

void print_hashes(const char* what, const DomainHashes& hashes, uint32_t present) {
    std::fprintf(stderr, "P4 DEV %s HASHES aggregate=%016llx", what, (unsigned long long)hashes.aggregate);
    for (size_t i = 0; i < kDomainCount; ++i) {
        if (present & (1u << i))
            std::fprintf(stderr, " %s=%016llx", domain_name(static_cast<DomainId>(i)), (unsigned long long)hashes.domain[i]);
    }
    std::fputc('\n', stderr);
}

void print_phases(const Result& result) {
    std::fprintf(stderr, "P4 DEV PHASES");
    for (const auto& phase : result.phases) std::fprintf(stderr, " %s=%lluus", phase.name.c_str(), (unsigned long long)phase.micros);
    std::fputc('\n', stderr);
}

bool run_frozen(Op op) {
    if (!runtime) runtime = std::make_unique<Runtime>(memory);
    if (op == Op::Capture) {
        auto staged = std::make_unique<InMemorySnapshot>();
        auto result = runtime->service.capture(operation, *staged);
        if (!result.ok) {
            std::fprintf(stderr, "P4 DEV CAPTURE FAILED gen=%llu error=%s (previous snapshot %s)\n",
                (unsigned long long)operation, result.error.c_str(), snapshot ? "kept" : "absent");
            return true;
        }
        std::fprintf(stderr, "P4 DEV CAPTURE ok gen=%llu total_us=%llu payload_bytes=%zu mapped_bytes=%llu "
            "resident_bytes=%lld nonzero_page_bytes=%llu nonzero_bytes=%llu threads=%zu inbox=%zu timers=%zu "
            "renderer_bytes=%zu audio_backlog=%zu\n",
            (unsigned long long)operation, (unsigned long long)result.total_micros, result.payload_bytes,
            (unsigned long long)result.memory.mapped_bytes, (long long)result.memory.resident_bytes,
            (unsigned long long)result.memory.nonzero_page_bytes, (unsigned long long)result.memory.nonzero_bytes,
            staged->continuations.threads.size(), staged->scheduler.inbox.size(), staged->time.active_timers.size(),
            staged->renderer.blob.size(), staged->audio.host_backlog.size());
        print_hashes("CAPTURE", staged->hashes, staged->present);
        print_phases(result);
        snapshot = std::move(staged);
        return true;
    }
    if (!snapshot) {
        std::fprintf(stderr, "P4 DEV RESTORE rejected: no in-memory snapshot\n");
        return true;
    }
    auto result = runtime->service.restore(operation, *snapshot, pending_fault);
    const char* outcome = result.ok ? "ok" : result.rolled_back ? "ROLLED_BACK" : result.unrecoverable ? "UNRECOVERABLE" : "REJECTED";
    std::fprintf(stderr, "P4 DEV RESTORE %s gen=%llu total_us=%llu rollback_bytes=%zu fault=%s error=%s rollback_error=%s "
        "deferred_admitted=%llu deferred_superseded=%llu\n",
        outcome, (unsigned long long)operation, (unsigned long long)result.total_micros, result.rollback_bytes,
        fault_name(pending_fault), result.error.c_str(), result.rollback_error.c_str(),
        (unsigned long long)runtime->scheduler.admitted, (unsigned long long)runtime->scheduler.superseded);
    if (result.ok || result.rolled_back) print_hashes(result.ok ? "RESTORED" : "ROLLBACK", result.hashes, snapshot->present);
    print_phases(result);
    if (result.unrecoverable) {
        // Never release partially imported state. Frontend pumping continues;
        // quitting cancels the barrier through the normal shutdown path.
        std::fprintf(stderr, "P4 DEV RESTORE UNRECOVERABLE: game stays frozen and sealed; please quit.\n");
        return false;
    }
    return true;
}
}

bool init(const Config& value) {
    const char* flag = std::getenv("SBK_P4_SAVESTATE_DEV");
    if (!flag || std::strcmp(flag, "1") != 0) return false;
    if (std::getenv("SBK_P2_CYCLES")) {
        std::fprintf(stderr, "P4 DEV disabled: SBK_P2_CYCLES probe already drives the barrier\n");
        return false;
    }
    config = value;
    if (const char* path = std::getenv("SBK_P4_SAVESTATE_CONTROL")) control_path = path;
    q::enable();
    q::set_audio_callback(config.audio_pause);
    on = true;
    std::fprintf(stderr, "P4 DEV savestate DEVELOPMENT ONLY: in-memory, single snapshot, no disk. "
        "Ctrl+F6=capture Ctrl+F7=restore control_file=%s\n", control_path.empty() ? "(none)" : control_path.c_str());
    return true;
}

bool enabled() { return on; }

void set_memory(uint8_t* rdram) { memory = rdram; }

void poll(bool capture_key, bool restore_key) {
    if (!on) return;
    q::poll();
    if (sealed_failure) return;
    if (capture_key && !capture_was_down && pending == Op::None) { pending = Op::Capture; pending_fault = FaultPoint::None; }
    if (restore_key && !restore_was_down && pending == Op::None) { pending = Op::Restore; pending_fault = FaultPoint::None; }
    capture_was_down = capture_key;
    restore_was_down = restore_key;
    if (!operation) read_control();
    if (pending == Op::None || !memory) return;
    if (pending == Op::Restore && !snapshot && !operation) {
        std::fprintf(stderr, "P4 DEV RESTORE rejected: no in-memory snapshot (capture first)\n");
        pending = Op::None;
        return;
    }
    const auto status = q::status();
    if (!operation) {
        if (status.state != q::State::Idle) return;
        operation = q::request(); // 0 while booting/busy: retried next frame
        if (!operation) return;
        requested_at = HostClock::now();
        std::fprintf(stderr, "P4 DEV %s requested gen=%llu\n", pending == Op::Capture ? "capture" : "restore",
            (unsigned long long)operation);
        return;
    }
    if (status.state == q::State::Frozen && status.generation == operation) {
        const auto frozen_after = std::chrono::duration_cast<std::chrono::microseconds>(HostClock::now() - requested_at).count();
        std::fprintf(stderr, "P4 DEV frozen gen=%llu freeze_acquire_us=%lld\n", (unsigned long long)operation, (long long)frozen_after);
        const bool release = run_frozen(pending);
        if (!release) { sealed_failure = true; pending = Op::None; return; }
        q::resume(operation);
        pending = Op::None;
        operation = 0;
        return;
    }
    if (HostClock::now() - requested_at > std::chrono::seconds(10)) {
        std::fprintf(stderr, "P4 DEV TIMEOUT gen=%llu state=%s active=%zu failure=%s; cancelling\n",
            (unsigned long long)operation, q::name(status.state), status.active_owners, status.failure.c_str());
        q::cancel("P4 dev savestate timeout");
        pending = Op::None;
        operation = 0;
    }
}
}
