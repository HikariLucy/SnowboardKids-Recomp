#include "driver.hpp"

#include "app_domains.hpp"
#include "librecomp/addresses.hpp"
#include "quiescence/quiescence.hpp"
#include "sbks.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace sbk::savestate::driver {
namespace q = sbk::quiescence;
namespace {
using HostClock = std::chrono::steady_clock;
constexpr auto kFreezeTimeout = std::chrono::seconds(10);
constexpr auto kNotReadyTimeout = std::chrono::seconds(5);
constexpr int kCaptureAttempts = 8;
constexpr auto kCaptureRetryDelay = std::chrono::milliseconds(120);

enum class Kind : uint8_t { None, DevCapture, DevRestore, QuickSave, QuickLoad };
enum class Phase : uint8_t { Idle, Reading, Freezing, Writing };

struct Request {
    Kind kind = Kind::None;
    std::string slot;
    FaultPoint fault = FaultPoint::None;
};

// Background file work. The worker owns nothing but this shared record.
struct Job {
    std::atomic_bool done{false};
    std::filesystem::path path;
    InMemorySnapshot snapshot;       // save: input; load: decoded result
    bool ok = false;
    sbks::Status status = sbks::Status::Ok;
    std::string error;
    size_t file_bytes = 0;
    uint64_t encode_us = 0, write_us = 0, read_us = 0, decode_us = 0;
};

bool on = false;
Config config;
uint8_t* memory = nullptr;

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
std::unique_ptr<InMemorySnapshot> dev_snapshot; // DEVELOPMENT ONLY single snapshot

Request active, queued;
Phase phase = Phase::Idle;
uint64_t operation = 0;
HostClock::time_point requested_at, not_before;
int capture_attempts = 0;
std::shared_ptr<Job> job;
bool sealed_failure = false;

uint64_t micros(HostClock::time_point start) {
    return std::chrono::duration_cast<std::chrono::microseconds>(HostClock::now() - start).count();
}

sbks::Identity identity() {
    sbks::Identity id;
    id.rom_hash = config.rom_hash;
    id.build = config.build;
    return id;
}

void notify(Notice notice) {
    std::fprintf(stderr, "SAVESTATE NOTICE %s\n", notice_text(notice));
    if (config.notify) config.notify(notice);
}

void finish() {
    active = {};
    phase = Phase::Idle;
    operation = 0;
    capture_attempts = 0;
    job.reset();
}

void print_hashes(const char* prefix, const char* what, const DomainHashes& hashes, uint32_t present) {
    std::fprintf(stderr, "%s %s HASHES aggregate=%016llx", prefix, what, (unsigned long long)hashes.aggregate);
    for (size_t i = 0; i < kDomainCount; ++i) {
        if (present & (1u << i))
            std::fprintf(stderr, " %s=%016llx", domain_name(static_cast<DomainId>(i)), (unsigned long long)hashes.domain[i]);
    }
    std::fputc('\n', stderr);
}

void print_phases(const char* prefix, const Result& result) {
    std::fprintf(stderr, "%s PHASES", prefix);
    for (const auto& phase : result.phases) std::fprintf(stderr, " %s=%lluus", phase.name.c_str(), (unsigned long long)phase.micros);
    std::fputc('\n', stderr);
}

std::optional<std::filesystem::path> resolve(const std::string& slot) {
    auto path = storage::slot_path(config.directory, slot);
    if (!path) std::fprintf(stderr, "SAVESTATE invalid slot '%s'\n", slot.c_str());
    return path;
}

// ---- background workers -----------------------------------------------------
void start_write(std::unique_ptr<InMemorySnapshot> snapshot, std::filesystem::path path) {
    job = std::make_shared<Job>();
    job->path = std::move(path);
    job->snapshot = std::move(*snapshot);
    std::thread([job = job, id = identity(), metadata = sbks::Metadata{config.writer, active.slot}] {
        auto start = HostClock::now();
        std::vector<uint8_t> bytes;
        job->ok = sbks::encode(job->snapshot, id, metadata, bytes, job->error);
        job->encode_us = micros(start);
        job->file_bytes = bytes.size();
        if (job->ok) {
            start = HostClock::now();
            storage::remove_stale_temporaries(job->path);
            job->ok = storage::write_atomic(job->path, bytes, job->error);
            job->write_us = micros(start);
        }
        job->done.store(true, std::memory_order_release);
    }).detach();
}

void start_read(std::filesystem::path path) {
    job = std::make_shared<Job>();
    job->path = std::move(path);
    std::thread([job = job, id = identity()] {
        auto start = HostClock::now();
        std::vector<uint8_t> bytes;
        const sbks::Limits limits;
        switch (storage::read_bounded(job->path, limits.max_file_bytes, bytes, job->error)) {
        case storage::ReadStatus::Ok: break;
        case storage::ReadStatus::NotFound: job->status = sbks::Status::NotFound; break;
        case storage::ReadStatus::TooLarge: job->status = sbks::Status::TooLarge; break;
        case storage::ReadStatus::IoError: job->status = sbks::Status::IoError; break;
        }
        job->read_us = micros(start);
        job->file_bytes = bytes.size();
        if (job->status == sbks::Status::Ok) {
            start = HostClock::now();
            const auto result = sbks::decode(bytes, id, job->snapshot, limits);
            job->status = result.status;
            job->error = result.detail;
            job->decode_us = micros(start);
        }
        job->ok = job->status == sbks::Status::Ok;
        job->done.store(true, std::memory_order_release);
    }).detach();
}

Notice load_notice(sbks::Status status) {
    switch (status) {
    case sbks::Status::Ok: return Notice::Loaded;
    case sbks::Status::NotFound: return Notice::NoSave;
    case sbks::Status::IoError: return Notice::LoadFailed;
    case sbks::Status::WrongGame:
    case sbks::Status::WrongRom:
    case sbks::Status::UnsupportedVersion:
    case sbks::Status::IncompatibleBuild: return Notice::Incompatible;
    default: return Notice::Corrupted;
    }
}

// ---- Frozen operations ---------------------------------------------------------
// Returns false only when the barrier must stay sealed (unrecoverable restore).
bool run_frozen() {
    if (!runtime) runtime = std::make_unique<Runtime>(memory);
    auto& service = runtime->service;
    switch (active.kind) {
    case Kind::DevCapture:
    case Kind::QuickSave: {
        const bool dev = active.kind == Kind::DevCapture;
        auto staged = std::make_unique<InMemorySnapshot>();
        auto result = service.capture(operation, *staged);
        if (!result.ok) {
            if (dev) {
                std::fprintf(stderr, "P4 DEV CAPTURE FAILED gen=%llu error=%s (previous snapshot %s)\n",
                    (unsigned long long)operation, result.error.c_str(), dev_snapshot ? "kept" : "absent");
            } else {
                std::fprintf(stderr, "SAVESTATE QUICKSAVE capture attempt %d/%d failed gen=%llu error=%s\n",
                    capture_attempts + 1, kCaptureAttempts, (unsigned long long)operation, result.error.c_str());
            }
            staged.reset();
            if (!dev && ++capture_attempts < kCaptureAttempts) {
                // Transient boundary conditions (an owner inside a native HLE,
                // a signal in flight): retry at a later freeze.
                operation = 0;
                not_before = HostClock::now() + kCaptureRetryDelay;
                return true;
            }
            if (!dev) notify(result.error.find("startup") != std::string::npos ? Notice::NotReady : Notice::SaveFailed);
            finish();
            return true;
        }
        std::fprintf(stderr, "%s ok gen=%llu total_us=%llu payload_bytes=%zu mapped_bytes=%llu "
            "resident_bytes=%lld nonzero_page_bytes=%llu nonzero_bytes=%llu threads=%zu inbox=%zu timers=%zu "
            "renderer_bytes=%zu audio_backlog=%zu\n",
            dev ? "P4 DEV CAPTURE" : "SAVESTATE QUICKSAVE CAPTURE",
            (unsigned long long)operation, (unsigned long long)result.total_micros, result.payload_bytes,
            (unsigned long long)result.memory.mapped_bytes, (long long)result.memory.resident_bytes,
            (unsigned long long)result.memory.nonzero_page_bytes, (unsigned long long)result.memory.nonzero_bytes,
            staged->continuations.threads.size(), staged->scheduler.inbox.size(), staged->time.active_timers.size(),
            staged->renderer.blob.size(), staged->audio.host_backlog.size());
        print_hashes(dev ? "P4 DEV" : "SAVESTATE", "CAPTURE", staged->hashes, staged->present);
        print_phases(dev ? "P4 DEV" : "SAVESTATE", result);
        if (dev) {
            dev_snapshot = std::move(staged);
            finish();
        } else {
            // Encoding and the atomic write run after resume, off this thread.
            auto path = resolve(active.slot);
            if (!path) { notify(Notice::SaveFailed); finish(); return true; }
            start_write(std::move(staged), *path);
            phase = Phase::Writing;
        }
        return true;
    }
    case Kind::DevRestore:
    case Kind::QuickLoad: {
        const bool dev = active.kind == Kind::DevRestore;
        const InMemorySnapshot& snapshot = dev ? *dev_snapshot : job->snapshot;
        const char* prefix = dev ? "P4 DEV" : "SAVESTATE";
        auto result = service.restore(operation, snapshot, dev ? active.fault : FaultPoint::None);
        const char* outcome = result.ok ? "ok" : result.rolled_back ? "ROLLED_BACK" : result.unrecoverable ? "UNRECOVERABLE" : "REJECTED";
        std::fprintf(stderr, "%s %s gen=%llu total_us=%llu rollback_bytes=%zu fault=%s error=%s rollback_error=%s "
            "deferred_admitted=%llu deferred_superseded=%llu\n",
            dev ? "P4 DEV RESTORE" : "SAVESTATE QUICKLOAD RESTORE", outcome, (unsigned long long)operation,
            (unsigned long long)result.total_micros, result.rollback_bytes, fault_name(dev ? active.fault : FaultPoint::None),
            result.error.c_str(), result.rollback_error.c_str(),
            (unsigned long long)runtime->scheduler.admitted, (unsigned long long)runtime->scheduler.superseded);
        if (result.ok || result.rolled_back) print_hashes(prefix, result.ok ? "RESTORED" : "ROLLBACK", result.hashes, snapshot.present);
        print_phases(prefix, result);
        if (result.unrecoverable) {
            // Never release partially imported state. Frontend pumping continues;
            // quitting cancels the barrier through the normal shutdown path.
            std::fprintf(stderr, "%s RESTORE UNRECOVERABLE: game stays frozen and sealed; please quit.\n", prefix);
            if (!dev) notify(Notice::LoadFailed);
            finish();
            return false;
        }
        if (!dev) {
            if (result.ok) notify(Notice::Loaded);
            else if (result.rolled_back) notify(Notice::LoadFailed);
            else if (result.error.find("runtime not ready") != std::string::npos) notify(Notice::NotReady);
            else notify(Notice::Incompatible); // rejected before any mutation
        }
        finish();
        return true;
    }
    case Kind::None: break;
    }
    finish();
    return true;
}

void submit(Request request) {
    if (!on || sealed_failure) return;
    if (active.kind == Kind::None) {
        active = std::move(request);
        return;
    }
    if (queued.kind != Kind::None) {
        std::fprintf(stderr, "SAVESTATE busy: request ignored\n");
        if (request.kind == Kind::QuickSave || request.kind == Kind::QuickLoad) notify(Notice::Busy);
        return;
    }
    queued = std::move(request);
}

void begin_active() {
    switch (active.kind) {
    case Kind::QuickSave:
        if (!resolve(active.slot)) { notify(Notice::SaveFailed); finish(); return; }
        notify(Notice::Saving);
        phase = Phase::Freezing;
        break;
    case Kind::QuickLoad: {
        auto path = resolve(active.slot);
        if (!path) { notify(Notice::LoadFailed); finish(); return; }
        notify(Notice::Loading);
        start_read(*path);
        phase = Phase::Reading;
        break;
    }
    case Kind::DevRestore:
        if (!dev_snapshot) {
            std::fprintf(stderr, "P4 DEV RESTORE rejected: no in-memory snapshot (capture first)\n");
            finish();
            return;
        }
        phase = Phase::Freezing;
        break;
    case Kind::DevCapture:
        phase = Phase::Freezing;
        break;
    case Kind::None: break;
    }
    requested_at = HostClock::now();
}

void drive_freeze() {
    if (!memory) { // guest memory not handed over yet (still booting)
        if (HostClock::now() - requested_at > kNotReadyTimeout) {
            std::fprintf(stderr, "SAVESTATE runtime not ready (no guest memory yet); request dropped\n");
            if (active.kind == Kind::QuickSave || active.kind == Kind::QuickLoad) notify(Notice::NotReady);
            finish();
        }
        return;
    }
    const auto status = q::status();
    if (!operation) {
        if (HostClock::now() < not_before) return;
        if (status.state != q::State::Idle) return;
        operation = q::request(); // 0 while booting/busy: retried next frame
        if (!operation) {
            if (HostClock::now() - requested_at > kNotReadyTimeout) {
                std::fprintf(stderr, "SAVESTATE runtime not ready for a freeze; request dropped\n");
                if (active.kind == Kind::QuickSave || active.kind == Kind::QuickLoad) notify(Notice::NotReady);
                finish();
            }
            return;
        }
        requested_at = HostClock::now();
        std::fprintf(stderr, "%s requested gen=%llu\n",
            active.kind == Kind::DevCapture ? "P4 DEV capture" : active.kind == Kind::DevRestore ? "P4 DEV restore"
            : active.kind == Kind::QuickSave ? "SAVESTATE quicksave" : "SAVESTATE quickload",
            (unsigned long long)operation);
        return;
    }
    if (status.state == q::State::Frozen && status.generation == operation) {
        const bool dev = active.kind == Kind::DevCapture || active.kind == Kind::DevRestore;
        std::fprintf(stderr, "%s frozen gen=%llu freeze_acquire_us=%llu\n", dev ? "P4 DEV" : "SAVESTATE",
            (unsigned long long)operation, (unsigned long long)micros(requested_at));
        const uint64_t generation = operation;
        if (!run_frozen()) { sealed_failure = true; return; }
        q::resume(generation);
        return;
    }
    if (HostClock::now() - requested_at > kFreezeTimeout) {
        std::fprintf(stderr, "SAVESTATE TIMEOUT gen=%llu state=%s active=%zu failure=%s; cancelling\n",
            (unsigned long long)operation, q::name(status.state), status.active_owners, status.failure.c_str());
        q::cancel("savestate freeze timeout");
        if (active.kind == Kind::QuickSave) notify(Notice::SaveFailed);
        if (active.kind == Kind::QuickLoad) notify(Notice::LoadFailed);
        finish();
    }
}

void drive_job() {
    if (!job || !job->done.load(std::memory_order_acquire)) return;
    if (phase == Phase::Writing) {
        if (job->ok) {
            std::fprintf(stderr, "SAVESTATE QUICKSAVE ok slot=%s path=%s file_bytes=%zu encode_us=%llu write_us=%llu\n",
                active.slot.c_str(), job->path.string().c_str(), job->file_bytes,
                (unsigned long long)job->encode_us, (unsigned long long)job->write_us);
            notify(Notice::Saved);
        } else {
            std::fprintf(stderr, "SAVESTATE QUICKSAVE FAILED slot=%s path=%s error=%s (previous file untouched)\n",
                active.slot.c_str(), job->path.string().c_str(), job->error.c_str());
            notify(Notice::SaveFailed);
        }
        finish();
        return;
    }
    // Reading: the file was fully validated before any freeze.
    if (!job->ok) {
        std::fprintf(stderr, "SAVESTATE QUICKLOAD REJECTED slot=%s path=%s status=%s error=%s (game not frozen)\n",
            active.slot.c_str(), job->path.string().c_str(), sbks::status_name(job->status), job->error.c_str());
        notify(load_notice(job->status));
        finish();
        return;
    }
    std::fprintf(stderr, "SAVESTATE QUICKLOAD file ok slot=%s path=%s file_bytes=%zu read_us=%llu decode_us=%llu\n",
        active.slot.c_str(), job->path.string().c_str(), job->file_bytes,
        (unsigned long long)job->read_us, (unsigned long long)job->decode_us);
    phase = Phase::Freezing;
    requested_at = HostClock::now();
}
}

const char* notice_text(Notice notice) {
    switch (notice) {
    case Notice::Saving: return "Saving state...";
    case Notice::Saved: return "State saved";
    case Notice::Loading: return "Loading state...";
    case Notice::Loaded: return "State loaded";
    case Notice::NoSave: return "No quick save exists";
    case Notice::Incompatible: return "Save incompatible";
    case Notice::Corrupted: return "Save corrupted";
    case Notice::SaveFailed: return "Save failed";
    case Notice::LoadFailed: return "Load failed";
    case Notice::Busy: return "Savestate busy";
    case Notice::NotReady: return "Not ready yet";
    }
    return "";
}

bool notice_is_error(Notice notice) {
    return notice != Notice::Saving && notice != Notice::Saved && notice != Notice::Loading && notice != Notice::Loaded;
}

bool init(const Config& value) {
    config = value;
    q::enable();
    q::set_audio_callback(config.audio_pause);
    on = true;
    std::fprintf(stderr, "SAVESTATE enabled: F5=quick save F8=quick load folder=%s\n", config.directory.string().c_str());
    return true;
}

bool enabled() { return on; }

void set_memory(uint8_t* rdram) { memory = rdram; }

void quick_save(std::string_view slot) { submit({Kind::QuickSave, std::string(slot), FaultPoint::None}); }
void quick_load(std::string_view slot) { submit({Kind::QuickLoad, std::string(slot), FaultPoint::None}); }
void dev_capture() { submit({Kind::DevCapture, {}, FaultPoint::None}); }
void dev_restore(FaultPoint fault) { submit({Kind::DevRestore, {}, fault}); }

void shutdown() {
    if (!on || !job || phase != Phase::Writing) return;
    const auto deadline = HostClock::now() + std::chrono::seconds(5);
    while (!job->done.load(std::memory_order_acquire) && HostClock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    config.notify = nullptr; // the UI may already be torn down: log only
    if (job->done.load(std::memory_order_acquire)) drive_job();
    else std::fprintf(stderr, "SAVESTATE QUICKSAVE still writing at exit; previous file stays valid\n");
}

void poll() {
    if (!on) return;
    q::poll();
    if (sealed_failure) return;
    if (active.kind == Kind::None && queued.kind != Kind::None) {
        active = std::move(queued);
        queued = {};
    }
    if (active.kind == Kind::None) return;
    if (phase == Phase::Idle) {
        begin_active();
        if (active.kind == Kind::None) return;
    }
    switch (phase) {
    case Phase::Reading:
    case Phase::Writing: drive_job(); break;
    case Phase::Freezing: drive_freeze(); break;
    case Phase::Idle: break;
    }
}
}
