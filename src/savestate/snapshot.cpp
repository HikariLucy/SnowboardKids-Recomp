#include "snapshot.hpp"
#include "hash.hpp"
#include "quiescence/renderer_state.hpp"

#include <algorithm>
#include <cstring>
#ifdef __linux__
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace sbk::savestate {
namespace {
using sbk::quiescence::SemanticFramebufferHeader;
using sbk::quiescence::SemanticStateHeader;

struct ParsedFramebuffer {
    SemanticFramebufferHeader header;
    const uint8_t* pixels;
};
struct ParsedRenderer {
    SemanticStateHeader header;
    std::vector<ParsedFramebuffer> framebuffers;
};

bool parse_renderer(const std::vector<uint8_t>& blob, ParsedRenderer& out, std::string* error) {
    auto fail = [&](const char* why) { if (error) *error = why; return false; };
    if (blob.size() < sizeof(SemanticStateHeader)) return fail("renderer blob shorter than its header");
    std::memcpy(&out.header, blob.data(), sizeof(out.header));
    if (out.header.magic != sbk::quiescence::SEMANTIC_STATE_MAGIC ||
        out.header.version != sbk::quiescence::SEMANTIC_STATE_VERSION)
        return fail("renderer blob magic/version mismatch");
    size_t offset = sizeof(out.header);
    for (uint32_t i = 0; i < out.header.framebuffer_count; ++i) {
        if (offset + sizeof(SemanticFramebufferHeader) > blob.size()) return fail("truncated framebuffer header");
        ParsedFramebuffer fb{};
        std::memcpy(&fb.header, blob.data() + offset, sizeof(fb.header));
        offset += sizeof(fb.header);
        if (fb.header.type != 1 && fb.header.type != 2) return fail("unknown framebuffer plane type");
        if (offset + fb.header.pixel_bytes > blob.size()) return fail("truncated framebuffer pixels");
        fb.pixels = blob.data() + offset;
        offset += fb.header.pixel_bytes;
        out.framebuffers.push_back(fb);
    }
    if (offset != blob.size()) return fail("trailing renderer blob bytes");
    return true;
}

template<class Sink> void encode_fb_header(Sink& s, const SemanticFramebufferHeader& h) {
    s.u32(h.address); s.u32(h.width); s.u32(h.height);
    s.u8(h.siz); s.u8(h.fmt); s.u8(h.type); s.u32(h.pixel_bytes);
}

template<class Sink> void encode_cpu(Sink& s, const CpuState& cpu) {
    for (auto value : cpu.gpr) s.u64(value);
    for (auto value : cpu.fpr) s.u64(value);
    s.u64(cpu.hi); s.u64(cpu.lo); s.u32(cpu.status); s.u8(cpu.fr); s.u8(cpu.rounding);
}

template<class Sink> void encode_vi_mode(Sink& s, const ViModeState& m) {
    s.u32(m.mode); s.i32(m.framebuffer); s.i32(m.mq); s.u32(m.msg);
    s.u32(m.state); s.u32(m.control); s.i32(m.retrace_count);
}

template<class Sink> void encode(Sink& s, const InMemorySnapshot& snap, DomainId id) {
    s.u8(static_cast<uint8_t>(id));
    s.boolean(snap.present & domain_bit(id));
    if (!(snap.present & domain_bit(id))) return;
    switch (id) {
    case DomainId::Memory: {
        const auto& m = snap.memory;
        s.u64(m.extent); s.u32(m.page_size); s.u64(m.pages.size());
        for (size_t i = 0; i < m.pages.size(); ++i) {
            s.u32(m.pages[i]);
            s.bytes(m.data.data() + i * size_t(m.page_size), m.page_size);
        }
        s.u64(m.normalized.size());
        for (auto slot : m.normalized) { s.u32(slot.offset); s.u32(slot.size); }
        break;
    }
    case DomainId::Continuations: {
        const auto& c = snap.continuations;
        s.u64(c.logical_lifetime_counter); s.u64(c.threads.size());
        for (const auto& t : c.threads) {
            s.u32(t.address); s.u64(t.logical_lifetime); s.u32(t.entrypoint); s.u32(t.argument);
            s.u8(static_cast<uint8_t>(t.run)); s.u8(t.pending); s.boolean(t.started);
            encode_cpu(s, t.cpu);
            s.u64(t.frames.size());
            for (const auto& f : t.frames) {
                s.u64(f.function); s.u64(f.continuation); s.u64(f.hi); s.u64(f.lo); s.u64(f.result);
                s.i32(f.c1cs); s.u32(f.indirect_target); s.u64(f.scratch.size());
                for (auto value : f.scratch) s.u64(value);
            }
            s.u64(t.blocked.hle_id); s.u8(t.blocked.phase);
            for (auto arg : t.blocked.args) s.u64(arg);
            s.i32(t.blocked.result); s.boolean(t.blocked.tail);
        }
        break;
    }
    case DomainId::Scheduler: {
        s.i32(snap.scheduler.running_queue_head); s.u64(snap.scheduler.inbox.size());
        for (const auto& m : snap.scheduler.inbox) {
            s.i32(m.mq); s.u32(m.msg); s.boolean(m.jam); s.boolean(m.requeue_if_blocked);
        }
        break;
    }
    case DomainId::Time:
        s.i64(snap.time.logical_ns); s.i64(snap.time.ostime_offset);
        s.u64(snap.time.active_timers.size());
        for (auto timer : snap.time.active_timers) s.i32(timer);
        break;
    case DomainId::Vi: {
        const auto& v = snap.vi;
        s.i32(v.cur_state); s.i32(v.field);
        for (const auto& m : v.states) encode_vi_mode(s, m);
        for (auto r : v.regs) s.u32(r);
        for (auto r : v.update_screen_regs) s.u32(r);
        s.u64(v.total_vis); s.i32(v.remaining_retraces); s.boolean(v.dummy_odd);
        for (const auto* e : {&v.sp, &v.dp, &v.ai, &v.si}) { s.i32(e->mq); s.u32(e->msg); }
        break;
    }
    case DomainId::Audio: {
        const auto& a = snap.audio;
        s.u32(a.guest_frequency); s.boolean(a.host_present); s.u32(a.host_input_rate);
        s.u32(a.host_output_rate); s.u32(a.host_output_channels);
        s.u64(a.host_history.size());
        for (auto bits : a.host_history) s.u32(bits);
        s.bytes(a.host_backlog.data(), a.host_backlog.size());
        break;
    }
    case DomainId::Input:
        s.u32(snap.input.host_latches);
        break;
    case DomainId::Overlays:
        s.u64(snap.overlays.loaded.size());
        for (const auto& l : snap.overlays.loaded) { s.u32(l.section_table_index); s.i32(l.ram_addr); }
        s.u64(snap.overlays.section_addresses.size());
        for (auto address : snap.overlays.section_addresses) s.i32(address);
        break;
    case DomainId::Rsp:
        s.bytes(snap.rsp.dmem.data(), snap.rsp.dmem.size());
        break;
    case DomainId::Renderer:
    case DomainId::Color:
    case DomainId::Depth: {
        ParsedRenderer parsed{};
        if (!parse_renderer(snap.renderer.blob, parsed, nullptr)) { s.u8(0xFF); break; }
        if (id == DomainId::Renderer) {
            // The header's quiescence generation is a host operation counter: excluded.
            s.bytes(&parsed.header.rdp, sizeof(parsed.header.rdp));
            s.bytes(&parsed.header.vi, sizeof(parsed.header.vi));
            s.u32(parsed.header.framebuffer_count);
            for (const auto& fb : parsed.framebuffers) encode_fb_header(s, fb.header);
        } else {
            const uint8_t type = id == DomainId::Color ? 1 : 2;
            uint64_t planes = 0;
            for (const auto& fb : parsed.framebuffers) {
                if (fb.header.type != type) continue;
                ++planes;
                encode_fb_header(s, fb.header);
                s.bytes(fb.pixels, fb.header.pixel_bytes);
            }
            s.u64(planes);
        }
        break;
    }
    case DomainId::Count: break;
    }
}
}

const char* domain_name(DomainId id) {
    static constexpr const char* names[kDomainCount] = {
        "memory", "continuations", "scheduler", "time", "vi", "audio", "input",
        "overlays", "rsp", "renderer", "color", "depth"};
    auto index = static_cast<size_t>(id);
    return index < kDomainCount ? names[index] : "invalid";
}

uint64_t hash_domain(const InMemorySnapshot& snapshot, DomainId id) {
    Hasher hasher;
    encode(hasher, snapshot, id);
    return hasher.finish();
}

std::vector<uint8_t> canonical_bytes(const InMemorySnapshot& snapshot, DomainId id) {
    std::vector<uint8_t> bytes;
    ByteSink sink{&bytes};
    encode(sink, snapshot, id);
    return bytes;
}

DomainHashes compute_hashes(const InMemorySnapshot& snapshot) {
    DomainHashes result{};
    Hasher aggregate;
    aggregate.u32(snapshot.magic); aggregate.u32(snapshot.version);
    aggregate.u64(snapshot.build.corpus_digest);
    aggregate.u32(snapshot.build.function_count); aggregate.u32(snapshot.build.hle_count);
    aggregate.u32(snapshot.present);
    for (size_t i = 0; i < kDomainCount; ++i) {
        result.domain[i] = hash_domain(snapshot, static_cast<DomainId>(i));
        aggregate.u64(result.domain[i]);
    }
    result.aggregate = aggregate.finish();
    return result;
}

DomainId first_mismatch(const DomainHashes& expected, const DomainHashes& actual, uint32_t present) {
    for (size_t i = 0; i < kDomainCount; ++i) {
        if ((present & (1u << i)) && expected.domain[i] != actual.domain[i]) return static_cast<DomainId>(i);
    }
    return DomainId::Count;
}

bool validate_snapshot(const InMemorySnapshot& snap, std::string& error) {
    auto fail = [&](std::string why) { error = std::move(why); return false; };
    if (snap.magic != kSnapshotMagic || snap.version != kSnapshotVersion) return fail("snapshot magic/version mismatch");
    if (snap.present >> kDomainCount) return fail("unknown domain bits present");
    if (snap.present & domain_bit(DomainId::Memory)) {
        const auto& m = snap.memory;
        if (!m.page_size || (m.page_size & (m.page_size - 1)) || m.extent % m.page_size)
            return fail("memory page geometry invalid");
        const uint64_t page_count = m.extent / m.page_size;
        if (m.data.size() != m.pages.size() * size_t(m.page_size)) return fail("memory data size mismatch");
        for (size_t i = 0; i < m.pages.size(); ++i) {
            if (m.pages[i] >= page_count || (i && m.pages[i] <= m.pages[i - 1]))
                return fail("memory pages not ascending within the extent");
        }
        uint64_t last_end = 0;
        for (auto slot : m.normalized) {
            if (slot.offset < last_end || uint64_t(slot.offset) + slot.size > m.extent)
                return fail("normalized slot outside the extent or overlapping");
            last_end = uint64_t(slot.offset) + slot.size;
        }
    }
    if (snap.present & domain_bit(DomainId::Continuations)) {
        const auto& c = snap.continuations;
        for (size_t i = 0; i < c.threads.size(); ++i) {
            const auto& t = c.threads[i];
            if (!t.address || (i && t.address <= c.threads[i - 1].address)) return fail("threads not ascending/unique");
            if (!t.logical_lifetime || t.logical_lifetime > c.logical_lifetime_counter)
                return fail("thread logical lifetime outside the saved counter");
            if (t.run != RunState::Sleeping && t.run != RunState::Running) return fail("invalid run state");
            if (t.pending > 2 || (t.run == RunState::Sleeping && t.pending)) return fail("invalid pending operation");
            if (!t.started && (t.pending || !t.frames.empty())) return fail("unstarted thread carries execution");
            if (t.started && t.frames.empty()) return fail("started thread without frames");
            if (t.blocked.phase > 3) return fail("invalid blocked phase");
            if (t.cpu.rounding > 3 || t.cpu.fr > 1) return fail("invalid FP mode");
        }
    }
    if (snap.present & domain_bit(DomainId::Vi)) {
        if ((snap.vi.cur_state != 0 && snap.vi.cur_state != 1) || snap.vi.field < 0 || snap.vi.field > 1)
            return fail("invalid VI state index");
    }
    if (snap.present & domain_bit(DomainId::Rsp)) {
        if (snap.rsp.dmem.size() != 0x1000) return fail("RSP DMEM must be 4096 bytes");
    }
    if (snap.present & (domain_bit(DomainId::Renderer) | domain_bit(DomainId::Color) | domain_bit(DomainId::Depth))) {
        ParsedRenderer parsed{};
        std::string why;
        if (!parse_renderer(snap.renderer.blob, parsed, &why)) return fail("renderer: " + why);
    }
    if (compute_hashes(snap) != snap.hashes) return fail("snapshot content does not match its canonical hashes");
    return true;
}

size_t payload_bytes(const InMemorySnapshot& snap) {
    size_t total = snap.memory.data.size() + snap.memory.pages.size() * sizeof(uint32_t) +
        snap.memory.normalized.size() * sizeof(NormalizedSlot);
    for (const auto& t : snap.continuations.threads) {
        total += sizeof(ThreadState);
        for (const auto& f : t.frames) total += sizeof(FrameState) + f.scratch.size() * sizeof(uint64_t);
    }
    total += snap.scheduler.inbox.size() * sizeof(InboxMessage) + snap.time.active_timers.size() * sizeof(int32_t);
    total += sizeof(ViState) + sizeof(AudioState) + snap.audio.host_backlog.size() + snap.audio.host_history.size() * 4;
    total += snap.overlays.loaded.size() * sizeof(LoadedSection) + snap.overlays.section_addresses.size() * 4;
    total += snap.rsp.dmem.size() + snap.renderer.blob.size();
    return total;
}

namespace {
bool page_is_zero(const uint8_t* page, uint32_t size) {
    uint64_t acc = 0;
    for (uint32_t i = 0; i < size; i += 64) {
        uint64_t w[8];
        std::memcpy(w, page + i, 64);
        acc |= w[0] | w[1] | w[2] | w[3] | w[4] | w[5] | w[6] | w[7];
        if (acc) return false;
    }
    return true;
}
}

void capture_memory(const uint8_t* base, uint64_t extent, std::vector<NormalizedSlot> slots,
    GuestMemory& out, MemoryMetrics* metrics) {
    constexpr uint32_t page = 4096;
    // Residency is sampled before scanning: reading untouched anonymous pages
    // maps the shared zero page, which mincore would then count as resident.
    int64_t resident_bytes = -1;
#ifdef __linux__
    const long host_page = sysconf(_SC_PAGESIZE);
    if (host_page > 0 && reinterpret_cast<uintptr_t>(base) % host_page == 0) {
        std::vector<unsigned char> residency((extent + host_page - 1) / host_page);
        if (mincore(const_cast<uint8_t*>(base), extent, residency.data()) == 0) {
            int64_t resident = 0;
            for (auto flag : residency) resident += (flag & 1) ? host_page : 0;
            resident_bytes = resident;
        }
    }
#endif
    std::sort(slots.begin(), slots.end(), [](auto a, auto b) { return a.offset < b.offset; });
    out = {};
    out.extent = extent;
    out.page_size = page;
    out.normalized = slots;
    size_t next_slot = 0;
    const uint64_t pages = extent / page;
    for (uint64_t index = 0; index < pages; ++index) {
        const uint64_t begin = index * page, end = begin + page;
        while (next_slot < slots.size() && uint64_t(slots[next_slot].offset) + slots[next_slot].size <= begin) ++next_slot;
        const uint8_t* source = base + begin;
        if (page_is_zero(source, page)) continue;
        const size_t at = out.data.size();
        out.data.insert(out.data.end(), source, source + page);
        bool touched = false;
        for (size_t s = next_slot; s < slots.size() && slots[s].offset < end; ++s) {
            const uint64_t lo = std::max<uint64_t>(slots[s].offset, begin);
            const uint64_t hi = std::min<uint64_t>(uint64_t(slots[s].offset) + slots[s].size, end);
            std::memset(out.data.data() + at + (lo - begin), 0, hi - lo);
            touched = true;
        }
        if (touched && page_is_zero(out.data.data() + at, page)) {
            out.data.resize(at);
            continue;
        }
        out.pages.push_back(static_cast<uint32_t>(index));
    }
    if (!metrics) return;
    metrics->mapped_bytes = extent;
    metrics->nonzero_page_bytes = out.data.size();
    uint64_t nonzero = 0;
    for (uint8_t byte : out.data) nonzero += byte != 0;
    metrics->nonzero_bytes = nonzero;
    metrics->resident_bytes = resident_bytes;
}

void install_memory(uint8_t* base, const GuestMemory& memory) {
    const uint32_t page = memory.page_size;
    const uint64_t pages = memory.extent / page;
    size_t saved = 0;
    for (uint64_t index = 0; index < pages; ++index) {
        uint8_t* target = base + index * page;
        if (saved < memory.pages.size() && memory.pages[saved] == index) {
            std::memcpy(target, memory.data.data() + saved * size_t(page), page);
            ++saved;
        } else if (!page_is_zero(target, page)) {
            std::memset(target, 0, page);
        }
    }
}
}
