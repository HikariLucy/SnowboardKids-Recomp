#include "service.hpp"
#include "quiescence/quiescence.hpp"

#include <chrono>
#include <cstdio>
#include <exception>

namespace sbk::savestate {
namespace q = sbk::quiescence;
namespace {
using Clock = std::chrono::steady_clock;
uint64_t micros_since(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
}
// Continuations first (memory normalization needs the owner list), memory last.
constexpr DomainId capture_order[] = {
    DomainId::Continuations, DomainId::Scheduler, DomainId::Time, DomainId::Vi,
    DomainId::Overlays, DomainId::Rsp, DomainId::Audio, DomainId::Input,
    DomainId::Renderer, DomainId::Memory};
// Memory before everything that reads guest structures; owners are rebuilt
// only once memory, time, queues and overlays describe the restored timeline.
constexpr DomainId install_order[] = {
    DomainId::Memory, DomainId::Time, DomainId::Scheduler, DomainId::Vi,
    DomainId::Overlays, DomainId::Rsp, DomainId::Continuations,
    DomainId::Renderer, DomainId::Audio, DomainId::Input};
FaultPoint fault_after(DomainId id) {
    switch (id) {
    case DomainId::Memory: return FaultPoint::AfterMemory;
    case DomainId::Time: return FaultPoint::AfterTime;
    case DomainId::Scheduler: return FaultPoint::AfterScheduler;
    case DomainId::Vi: return FaultPoint::AfterVi;
    case DomainId::Overlays: return FaultPoint::AfterOverlays;
    case DomainId::Rsp: return FaultPoint::AfterRsp;
    case DomainId::Continuations: return FaultPoint::AfterContinuations;
    case DomainId::Renderer: return FaultPoint::AfterRenderer;
    case DomainId::Audio: return FaultPoint::AfterAudio;
    default: return FaultPoint::None;
    }
}
template<class F> bool guarded(F&& body, std::string& error) {
    try {
        return body();
    } catch (const std::exception& e) {
        error = e.what();
    } catch (...) {
        error = "unknown exception";
    }
    return false;
}
}

const char* fault_name(FaultPoint fault) {
    static constexpr const char* names[] = {
        "none", "after-retire", "after-memory", "after-time", "after-scheduler", "after-vi",
        "after-overlays", "after-rsp", "after-continuations", "before-renderer", "after-renderer",
        "after-audio", "post-validate", "during-rollback"};
    auto index = static_cast<size_t>(fault);
    return index < static_cast<size_t>(FaultPoint::Count) ? names[index] : "invalid";
}

void SnapshotService::add(Domain* domain) {
    if (find(domain->id())) throw std::logic_error("duplicate savestate domain");
    domains_.push_back(domain);
}

Domain* SnapshotService::find(DomainId id) const {
    for (auto* domain : domains_) if (domain->id() == id) return domain;
    return nullptr;
}

uint32_t SnapshotService::available() const {
    uint32_t mask = 0;
    for (auto* domain : domains_) mask |= domain_bit(domain->id()) | domain->extra_domains();
    return mask;
}

void SnapshotService::step(const char* name) {
    if (on_step) on_step(name);
}

bool SnapshotService::capture_all(InMemorySnapshot& out, Result& result, std::string& error) {
    out = {};
    out.build = build_;
    out.present = available();
    for (DomainId id : capture_order) {
        auto* domain = find(id);
        if (!domain) continue;
        const auto start = Clock::now();
        std::string why;
        if (!guarded([&] { return domain->capture(out, why); }, why)) {
            error = std::string("capture ") + domain_name(id) + ": " + why;
            return false;
        }
        result.phases.push_back({std::string("capture-") + domain_name(id), micros_since(start)});
    }
    const auto start = Clock::now();
    out.hashes = compute_hashes(out);
    result.phases.push_back({"hash", micros_since(start)});
    if (auto* memory = dynamic_cast<MemoryDomain*>(find(DomainId::Memory))) last_memory_metrics = memory->metrics;
    return true;
}

Result SnapshotService::capture(uint64_t generation, InMemorySnapshot& out) {
    Result result;
    const auto start = Clock::now();
    if (!q::begin_transaction(generation, q::State::Capture)) {
        result.error = "capture requires the matching Frozen generation";
        return result;
    }
    for (auto* domain : domains_) domain->begin(Transaction::Capture);
    step("capture-begin");
    InMemorySnapshot staged;
    std::string error;
    const bool ok = guarded([&] { return capture_all(staged, result, error); }, error);
    step("capture-end");
    for (auto* domain : domains_) domain->end(Transaction::Capture, false);
    q::end_transaction(generation);
    result.total_micros = micros_since(start);
    if (!ok) {
        result.error = error; // previous snapshot, if any, stays intact
        return result;
    }
    result.hashes = staged.hashes;
    result.memory = last_memory_metrics;
    result.payload_bytes = payload_bytes(staged);
    out = std::move(staged);
    result.ok = true;
    return result;
}

bool SnapshotService::install_timeline(const InMemorySnapshot& snapshot, FaultPoint fault, Result& result, std::string& error) {
    auto injected = [&](FaultPoint point) {
        if (fault == FaultPoint::None || fault != point) return false;
        error = std::string("injected fault: ") + fault_name(point);
        return true;
    };
    auto start = Clock::now();
    step("retire");
    for (auto* domain : domains_) {
        std::string why;
        if (!guarded([&] { return domain->retire(why); }, why)) {
            error = std::string("retire ") + domain_name(domain->id()) + ": " + why;
            return false;
        }
    }
    result.phases.push_back({"retire-owners", micros_since(start)});
    if (injected(FaultPoint::AfterRetire)) return false;
    for (DomainId id : install_order) {
        auto* domain = find(id);
        if (!domain) continue;
        if (id == DomainId::Renderer && injected(FaultPoint::BeforeRenderer)) return false;
        start = Clock::now();
        step(domain_name(id));
        std::string why;
        if (!guarded([&] { return domain->install(snapshot, why); }, why)) {
            error = std::string("install ") + domain_name(id) + ": " + why;
            return false;
        }
        result.phases.push_back({std::string("install-") + domain_name(id), micros_since(start)});
        if (injected(fault_after(id))) return false;
    }
    start = Clock::now();
    step("post-validate");
    InMemorySnapshot live;
    Result scratch;
    if (!capture_all(live, scratch, error)) {
        error = "post-install export failed: " + error;
        return false;
    }
    const auto mismatch = first_mismatch(snapshot.hashes, live.hashes, snapshot.present);
    if (mismatch != DomainId::Count) {
        // P6-XPROC-02 forensics: every diverging domain with snapshot!=live hashes.
        error = std::string("post-install hash mismatch in ") + domain_name(mismatch);
        for (size_t i = 0; i < size_t(DomainId::Count); ++i) {
            const auto id = static_cast<DomainId>(i);
            if (!(snapshot.present & domain_bit(id)) || snapshot.hashes.domain[i] == live.hashes.domain[i]) continue;
            char buf[64];
            std::snprintf(buf, sizeof(buf), " %s:%016llx!=%016llx", domain_name(id),
                (unsigned long long)snapshot.hashes.domain[i], (unsigned long long)live.hashes.domain[i]);
            error += buf;
        }
        return false;
    }
    result.phases.push_back({"post-validate", micros_since(start)});
    result.hashes = live.hashes;
    if (injected(FaultPoint::PostValidate)) return false;
    return true;
}

Result SnapshotService::restore(uint64_t generation, const InMemorySnapshot& snapshot, FaultPoint fault) {
    Result result;
    const auto start = Clock::now();
    if (!q::begin_transaction(generation, q::State::Restore)) {
        result.error = "restore requires the matching Frozen generation";
        return result;
    }
    for (auto* domain : domains_) domain->begin(Transaction::Restore);
    auto abort_intact = [&](std::string why) {
        // Nothing was mutated: release deferred arrivals into this timeline.
        for (auto* domain : domains_) domain->end(Transaction::Restore, false);
        q::end_transaction(generation);
        result.error = std::move(why);
        result.total_micros = micros_since(start);
        return result;
    };

    // 1. Validate everything before touching live state.
    auto phase = Clock::now();
    std::string error;
    if (!validate_snapshot(snapshot, error)) return abort_intact("invalid snapshot: " + error);
    if (snapshot.build.corpus_digest != build_.corpus_digest ||
        snapshot.build.function_count != build_.function_count ||
        snapshot.build.hle_count != build_.hle_count)
        return abort_intact("snapshot belongs to a different build/corpus");
    if (snapshot.present != available()) return abort_intact("snapshot domain set differs from available adapters");
    for (auto* domain : domains_) {
        std::string why;
        if (!guarded([&] { return domain->validate(snapshot, why); }, why))
            return abort_intact(std::string("validate ") + domain_name(domain->id()) + ": " + why);
    }
    result.phases.push_back({"validate", micros_since(phase)});

    // 2. Rollback snapshot of the current timeline (also the staging allocation).
    phase = Clock::now();
    step("rollback-capture");
    InMemorySnapshot rollback;
    {
        Result scratch;
        if (!guarded([&] { return capture_all(rollback, scratch, error); }, error))
            return abort_intact("rollback capture failed (timeline intact): " + error);
    }
    result.rollback_bytes = payload_bytes(rollback);
    result.phases.push_back({"rollback-capture", micros_since(phase)});

    // 3..15. Retire, install, reconstruct, validate.
    const FaultPoint primary_fault = fault == FaultPoint::DuringRollback ? FaultPoint::AfterMemory : fault;
    if (install_timeline(snapshot, primary_fault, result, error)) {
        step("commit");
        for (auto* domain : domains_) domain->end(Transaction::Restore, true);
        q::end_transaction(generation);
        result.ok = true;
        result.payload_bytes = payload_bytes(snapshot);
        result.total_micros = micros_since(start);
        return result;
    }

    // Rollback under the same closed barrier.
    result.error = error;
    phase = Clock::now();
    step("rollback");
    std::string rollback_error;
    Result rollback_result;
    const FaultPoint rollback_fault = fault == FaultPoint::DuringRollback ? FaultPoint::AfterMemory : FaultPoint::None;
    if (install_timeline(rollback, rollback_fault, rollback_result, rollback_error)) {
        for (auto* domain : domains_) domain->end(Transaction::Restore, false);
        q::end_transaction(generation);
        result.rolled_back = true;
        result.hashes = rollback_result.hashes;
        result.phases.push_back({"rollback", micros_since(phase)});
        result.total_micros = micros_since(start);
        return result;
    }
    // Unrecoverable: stay inside Restore with admissions sealed. resume() cannot
    // release this generation; frontend polling and shutdown cancel still work.
    result.unrecoverable = true;
    result.rollback_error = rollback_error;
    result.total_micros = micros_since(start);
    return result;
}

bool MemoryDomain::capture(InMemorySnapshot& out, std::string&) {
    capture_memory(base_, extent_, slots_ ? slots_(out) : std::vector<NormalizedSlot>{}, out.memory, &metrics);
    return true;
}

bool MemoryDomain::validate(const InMemorySnapshot& in, std::string& error) const {
    if (in.memory.extent != extent_) { error = "memory extent differs"; return false; }
    return true;
}

bool MemoryDomain::install(const InMemorySnapshot& in, std::string&) {
    install_memory(base_, in.memory);
    return true;
}
}
