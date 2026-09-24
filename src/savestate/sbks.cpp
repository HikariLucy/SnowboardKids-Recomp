#include "sbks.hpp"
#include "hash.hpp"
#include "quiescence/renderer_state.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

#define XXH_INLINE_ALL
#include "xxHash/xxhash.h"

namespace sbk::savestate::sbks {
namespace {
using sbk::quiescence::SemanticFramebufferHeader;
using sbk::quiescence::SemanticRdpState;
using sbk::quiescence::SemanticStateHeader;
using sbk::quiescence::SemanticViState;

uint64_t checksum(const uint8_t* data, size_t size) { return XXH3_64bits(data, size); }

template<class T> void put_at(std::vector<uint8_t>& out, size_t offset, T value) {
    std::memcpy(out.data() + offset, &value, sizeof(T)); // host is little-endian (hash.hpp)
}
template<class T> T get_at(std::span<const uint8_t> in, size_t offset) {
    T value;
    std::memcpy(&value, in.data() + offset, sizeof(T));
    return value;
}

// Header field offsets.
enum : size_t {
    kOffVersion = 8, kOffHeaderSize = 10, kOffFlags = 12, kOffEndian = 16, kOffCompression = 18,
    kOffReserved0 = 19, kOffSectionCount = 20, kOffGameId = 24, kOffRom = 40, kOffCorpus = 48,
    kOffFunctions = 56, kOffHle = 60, kOffSchema = 64, kOffPresent = 68, kOffUncompressed = 72,
    kOffStored = 80, kOffAggregate = 88, kOffTableChecksum = 96, kOffReserved1 = 104, kOffHeaderChecksum = 120
};
// Section entry field offsets.
enum : size_t {
    kEntType = 0, kEntVersion = 4, kEntFlags = 6, kEntOffset = 8, kEntUncompressed = 16,
    kEntStored = 24, kEntCanonical = 32, kEntChecksum = 40
};

constexpr uint32_t kKnownSections = static_cast<uint32_t>(SectionType::Metadata);
constexpr std::pair<DomainId, SectionType> kDomainSections[] = {
    {DomainId::Memory, SectionType::Memory}, {DomainId::Continuations, SectionType::Continuations},
    {DomainId::Scheduler, SectionType::Scheduler}, {DomainId::Time, SectionType::Time},
    {DomainId::Vi, SectionType::Vi}, {DomainId::Audio, SectionType::Audio},
    {DomainId::Input, SectionType::Input}, {DomainId::Overlays, SectionType::Overlays},
    {DomainId::Rsp, SectionType::Rsp}, {DomainId::Renderer, SectionType::Renderer},
    {DomainId::Color, SectionType::FramebufferColor}, {DomainId::Depth, SectionType::FramebufferDepth}};

// Bounds-checked little-endian reader over untrusted bytes. Any overrun
// latches `ok = false`; counts are checked against the bytes that remain.
class Reader {
public:
    explicit Reader(std::span<const uint8_t> bytes) : bytes_(bytes) {}
    bool ok = true;
    template<class T> T get() {
        T value{};
        if (!ok || bytes_.size() - pos_ < sizeof(T)) { ok = false; return value; }
        std::memcpy(&value, bytes_.data() + pos_, sizeof(T));
        pos_ += sizeof(T);
        return value;
    }
    uint8_t u8() { return get<uint8_t>(); }
    uint16_t u16() { return get<uint16_t>(); }
    uint32_t u32() { return get<uint32_t>(); }
    uint64_t u64() { return get<uint64_t>(); }
    int32_t i32() { return get<int32_t>(); }
    int64_t i64() { return get<int64_t>(); }
    bool boolean() {
        const uint8_t value = u8();
        if (value > 1) ok = false;
        return value == 1;
    }
    // Element count, each element at least `min_bytes` long.
    uint64_t count(size_t min_bytes) {
        const uint64_t value = u64();
        if (ok && value > remaining() / std::max<size_t>(min_bytes, 1)) ok = false;
        return ok ? value : 0;
    }
    std::span<const uint8_t> bytes() {
        const uint64_t size = u64();
        if (!ok || size > remaining()) { ok = false; return {}; }
        auto view = bytes_.subspan(pos_, size_t(size));
        pos_ += size_t(size);
        return view;
    }
    size_t remaining() const { return bytes_.size() - pos_; }
    bool finished() const { return ok && pos_ == bytes_.size(); }
private:
    std::span<const uint8_t> bytes_;
    size_t pos_ = 0;
};

// Lower bounds of encoded element sizes (count validation only).
constexpr size_t kCpuBytes = 32 * 8 * 2 + 8 + 8 + 4 + 1 + 1;
constexpr size_t kThreadMinBytes = 4 + 8 + 4 + 4 + 1 + 1 + 1 + kCpuBytes + 8 + (8 + 1 + 4 * 8 + 4 + 1);
constexpr size_t kFrameMinBytes = 8 * 5 + 4 + 4 + 8;
constexpr size_t kFbHeaderBytes = 4 * 3 + 3 + 4;

void read_cpu(Reader& r, CpuState& cpu) {
    for (auto& value : cpu.gpr) value = r.u64();
    for (auto& value : cpu.fpr) value = r.u64();
    cpu.hi = r.u64(); cpu.lo = r.u64(); cpu.status = r.u32(); cpu.fr = r.u8(); cpu.rounding = r.u8();
}

void read_vi_mode(Reader& r, ViModeState& m) {
    m.mode = r.u32(); m.framebuffer = r.i32(); m.mq = r.i32(); m.msg = r.u32();
    m.state = r.u32(); m.control = r.u32(); m.retrace_count = r.i32();
}

// Inverse of the canonical domain encoding in snapshot.cpp (explicit field
// order, fixed widths, explicit lengths). Hash equality after decoding proves
// the inverse is exact.
bool decode_domain(std::span<const uint8_t> payload, DomainId id, InMemorySnapshot& snap, std::string& error) {
    Reader r(payload);
    if (r.u8() != static_cast<uint8_t>(id) || !r.boolean()) { error = "domain tag mismatch"; return false; }
    switch (id) {
    case DomainId::Memory: {
        auto& m = snap.memory;
        m.extent = r.u64(); m.page_size = r.u32();
        if (!r.ok || m.page_size < 512 || m.page_size > 65536 || (m.page_size & (m.page_size - 1)) ||
            m.extent % m.page_size || m.extent > (uint64_t(1) << 32)) {
            error = "invalid memory geometry";
            return false;
        }
        const uint64_t pages = r.count(4 + 8 + size_t(m.page_size));
        if (pages > m.extent / m.page_size) r.ok = false;
        m.pages.reserve(size_t(pages));
        m.data.reserve(size_t(pages) * m.page_size);
        for (uint64_t i = 0; i < pages && r.ok; ++i) {
            m.pages.push_back(r.u32());
            const auto page = r.bytes();
            if (!r.ok || page.size() != m.page_size) { r.ok = false; break; }
            m.data.insert(m.data.end(), page.begin(), page.end());
        }
        const uint64_t slots = r.count(8);
        for (uint64_t i = 0; i < slots && r.ok; ++i) {
            const uint32_t offset = r.u32();
            const uint32_t size = r.u32();
            m.normalized.push_back({offset, size});
        }
        break;
    }
    case DomainId::Continuations: {
        auto& c = snap.continuations;
        c.logical_lifetime_counter = r.u64();
        const uint64_t threads = r.count(kThreadMinBytes);
        for (uint64_t i = 0; i < threads && r.ok; ++i) {
            ThreadState t;
            t.address = r.u32(); t.logical_lifetime = r.u64(); t.entrypoint = r.u32(); t.argument = r.u32();
            t.run = static_cast<RunState>(r.u8()); t.pending = r.u8(); t.started = r.boolean();
            read_cpu(r, t.cpu);
            const uint64_t frames = r.count(kFrameMinBytes);
            for (uint64_t f = 0; f < frames && r.ok; ++f) {
                FrameState frame;
                frame.function = r.u64(); frame.continuation = r.u64(); frame.hi = r.u64(); frame.lo = r.u64();
                frame.result = r.u64(); frame.c1cs = r.i32(); frame.indirect_target = r.u32();
                const uint64_t scratch = r.count(8);
                frame.scratch.reserve(size_t(scratch));
                for (uint64_t k = 0; k < scratch && r.ok; ++k) frame.scratch.push_back(r.u64());
                t.frames.push_back(std::move(frame));
            }
            t.blocked.hle_id = r.u64(); t.blocked.phase = r.u8();
            for (auto& arg : t.blocked.args) arg = r.u64();
            t.blocked.result = r.i32(); t.blocked.tail = r.boolean();
            c.threads.push_back(std::move(t));
        }
        break;
    }
    case DomainId::Scheduler: {
        snap.scheduler.running_queue_head = r.i32();
        const uint64_t count = r.count(4 + 4 + 1 + 1);
        for (uint64_t i = 0; i < count && r.ok; ++i) {
            InboxMessage m;
            m.mq = r.i32(); m.msg = r.u32(); m.jam = r.boolean(); m.requeue_if_blocked = r.boolean();
            snap.scheduler.inbox.push_back(m);
        }
        break;
    }
    case DomainId::Time: {
        snap.time.logical_ns = r.i64(); snap.time.ostime_offset = r.i64();
        const uint64_t count = r.count(4);
        for (uint64_t i = 0; i < count && r.ok; ++i) snap.time.active_timers.push_back(r.i32());
        break;
    }
    case DomainId::Vi: {
        auto& v = snap.vi;
        v.cur_state = r.i32(); v.field = r.i32();
        for (auto& m : v.states) read_vi_mode(r, m);
        for (auto& reg : v.regs) reg = r.u32();
        for (auto& reg : v.update_screen_regs) reg = r.u32();
        v.total_vis = r.u64(); v.remaining_retraces = r.i32(); v.dummy_odd = r.boolean();
        for (auto* e : {&v.sp, &v.dp, &v.ai, &v.si}) { e->mq = r.i32(); e->msg = r.u32(); }
        break;
    }
    case DomainId::Audio: {
        auto& a = snap.audio;
        a.guest_frequency = r.u32(); a.host_present = r.boolean(); a.host_input_rate = r.u32();
        a.host_output_rate = r.u32(); a.host_output_channels = r.u32();
        const uint64_t history = r.count(4);
        for (uint64_t i = 0; i < history && r.ok; ++i) a.host_history.push_back(r.u32());
        const auto backlog = r.bytes();
        a.host_backlog.assign(backlog.begin(), backlog.end());
        break;
    }
    case DomainId::Input:
        snap.input.host_latches = r.u32();
        break;
    case DomainId::Overlays: {
        const uint64_t loaded = r.count(8);
        for (uint64_t i = 0; i < loaded && r.ok; ++i) {
            LoadedSection section;
            section.section_table_index = r.u32(); section.ram_addr = r.i32();
            snap.overlays.loaded.push_back(section);
        }
        const uint64_t addresses = r.count(4);
        for (uint64_t i = 0; i < addresses && r.ok; ++i) snap.overlays.section_addresses.push_back(r.i32());
        break;
    }
    case DomainId::Rsp: {
        const auto dmem = r.bytes();
        snap.rsp.dmem.assign(dmem.begin(), dmem.end());
        break;
    }
    default:
        error = "not a canonical-payload domain";
        return false;
    }
    if (!r.finished()) { error = "malformed or trailing payload bytes"; return false; }
    return true;
}

// --- renderer: P3 'SBK3' blob split into semantic header / color / depth ----
// The P3 structs are #pragma pack(1) with fixed-width fields: an explicit
// packed layout, stored as length-checked opaque blocks.
struct SplitRenderer {
    std::vector<uint8_t> semantic, color, depth;
};

template<class T> void put(std::vector<uint8_t>& out, T value) {
    uint8_t raw[sizeof(T)];
    std::memcpy(raw, &value, sizeof(T));
    out.insert(out.end(), raw, raw + sizeof(T));
}
void put_bytes(std::vector<uint8_t>& out, const uint8_t* data, size_t size) {
    put<uint64_t>(out, size);
    out.insert(out.end(), data, data + size);
}

bool split_renderer(const std::vector<uint8_t>& blob, SplitRenderer& out, std::string& error) {
    if (blob.size() < sizeof(SemanticStateHeader)) { error = "renderer blob shorter than its header"; return false; }
    SemanticStateHeader header;
    std::memcpy(&header, blob.data(), sizeof(header));
    put<uint32_t>(out.semantic, header.magic);
    put<uint32_t>(out.semantic, header.version);
    // The quiescence generation is a host operation counter: normalized.
    put<uint64_t>(out.semantic, 0);
    put_bytes(out.semantic, reinterpret_cast<const uint8_t*>(&header.rdp), sizeof(header.rdp));
    put_bytes(out.semantic, reinterpret_cast<const uint8_t*>(&header.vi), sizeof(header.vi));
    put<uint32_t>(out.semantic, header.framebuffer_count);
    uint64_t colors = 0, depths = 0;
    std::vector<uint8_t> color, depth;
    size_t offset = sizeof(header);
    for (uint32_t i = 0; i < header.framebuffer_count; ++i) {
        SemanticFramebufferHeader fb;
        if (blob.size() - offset < sizeof(fb)) { error = "truncated framebuffer header"; return false; }
        std::memcpy(&fb, blob.data() + offset, sizeof(fb));
        offset += sizeof(fb);
        if (blob.size() - offset < fb.pixel_bytes) { error = "truncated framebuffer pixels"; return false; }
        put<uint32_t>(out.semantic, fb.address); put<uint32_t>(out.semantic, fb.width);
        put<uint32_t>(out.semantic, fb.height); put<uint8_t>(out.semantic, fb.siz);
        put<uint8_t>(out.semantic, fb.fmt); put<uint8_t>(out.semantic, fb.type);
        put<uint32_t>(out.semantic, fb.pixel_bytes);
        auto& plane = fb.type == 1 ? color : depth;
        (fb.type == 1 ? colors : depths) += 1;
        put_bytes(plane, blob.data() + offset, fb.pixel_bytes);
        offset += fb.pixel_bytes;
    }
    if (offset != blob.size()) { error = "trailing renderer blob bytes"; return false; }
    put<uint64_t>(out.color, colors);
    out.color.insert(out.color.end(), color.begin(), color.end());
    put<uint64_t>(out.depth, depths);
    out.depth.insert(out.depth.end(), depth.begin(), depth.end());
    return true;
}

bool join_renderer(std::span<const uint8_t> semantic, std::span<const uint8_t> color, std::span<const uint8_t> depth,
    std::vector<uint8_t>& blob, std::string& error) {
    Reader s(semantic), c(color), d(depth);
    SemanticStateHeader header{};
    header.magic = s.u32();
    header.version = s.u32();
    header.generation = s.u64();
    const auto rdp = s.bytes();
    const auto vi = s.bytes();
    header.framebuffer_count = s.u32();
    if (!s.ok || rdp.size() != sizeof(SemanticRdpState) || vi.size() != sizeof(SemanticViState)) {
        error = "renderer semantic block shape";
        return false;
    }
    if (header.magic != sbk::quiescence::SEMANTIC_STATE_MAGIC || header.version != sbk::quiescence::SEMANTIC_STATE_VERSION) {
        error = "renderer blob magic/version";
        return false;
    }
    std::memcpy(&header.rdp, rdp.data(), sizeof(header.rdp));
    std::memcpy(&header.vi, vi.data(), sizeof(header.vi));
    if (header.framebuffer_count > s.remaining() / kFbHeaderBytes) { error = "framebuffer count exceeds payload"; return false; }
    uint64_t colors = c.u64(), depths = d.u64();
    blob.resize(sizeof(header));
    std::memcpy(blob.data(), &header, sizeof(header));
    for (uint32_t i = 0; i < header.framebuffer_count; ++i) {
        SemanticFramebufferHeader fb{};
        fb.address = s.u32(); fb.width = s.u32(); fb.height = s.u32();
        fb.siz = s.u8(); fb.fmt = s.u8(); fb.type = s.u8(); fb.pixel_bytes = s.u32();
        if (!s.ok || (fb.type != 1 && fb.type != 2)) { error = "framebuffer header"; return false; }
        Reader& plane = fb.type == 1 ? c : d;
        uint64_t& left = fb.type == 1 ? colors : depths;
        if (!left) { error = "framebuffer plane missing"; return false; }
        --left;
        const auto pixels = plane.bytes();
        if (!plane.ok || pixels.size() != fb.pixel_bytes) { error = "framebuffer plane size"; return false; }
        const auto* raw = reinterpret_cast<const uint8_t*>(&fb);
        blob.insert(blob.end(), raw, raw + sizeof(fb));
        blob.insert(blob.end(), pixels.begin(), pixels.end());
    }
    if (!s.finished() || !c.finished() || !d.finished() || colors || depths) {
        error = "renderer sections carry extra planes or bytes";
        return false;
    }
    return true;
}

std::vector<uint8_t> encode_metadata(const Metadata& metadata) {
    std::vector<uint8_t> out;
    const std::pair<const char*, const std::string*> entries[] = {{"slot", &metadata.slot}, {"writer", &metadata.writer}};
    put<uint64_t>(out, std::size(entries));
    for (const auto& [key, value] : entries) {
        put_bytes(out, reinterpret_cast<const uint8_t*>(key), std::strlen(key));
        put_bytes(out, reinterpret_cast<const uint8_t*>(value->data()), value->size());
    }
    return out;
}

bool decode_metadata(std::span<const uint8_t> payload, Metadata& metadata) {
    Reader r(payload);
    const uint64_t count = r.count(16);
    for (uint64_t i = 0; i < count && r.ok; ++i) {
        const auto key = r.bytes();
        const auto value = r.bytes();
        const std::string k(key.begin(), key.end()), v(value.begin(), value.end());
        if (k == "slot") metadata.slot = v;
        else if (k == "writer") metadata.writer = v; // unknown keys: ignored (optional section)
    }
    return r.finished();
}

Result fail(Status status, std::string detail) { return {status, std::move(detail)}; }

bool add_overflows(uint64_t a, uint64_t b, uint64_t& sum) {
    if (a > std::numeric_limits<uint64_t>::max() - b) return true;
    sum = a + b;
    return false;
}
}

const char* section_name(uint32_t type) {
    static constexpr const char* names[] = {
        "invalid", "MEMORY", "CONTINUATIONS", "SCHEDULER", "TIME", "VI", "AUDIO", "INPUT", "OVERLAYS",
        "RSP", "RENDERER", "FRAMEBUFFER_COLOR", "FRAMEBUFFER_DEPTH", "METADATA"};
    return type <= kKnownSections ? names[type] : "UNKNOWN";
}

const char* status_name(Status status) {
    switch (status) {
    case Status::Ok: return "OK";
    case Status::NotFound: return "NOT_FOUND";
    case Status::IoError: return "IO_ERROR";
    case Status::TooLarge: return "TOO_LARGE";
    case Status::CorruptHeader: return "CORRUPT_HEADER";
    case Status::CorruptSection: return "CORRUPT_SECTION";
    case Status::Truncated: return "TRUNCATED";
    case Status::HashMismatch: return "HASH_MISMATCH";
    case Status::MissingRequiredSection: return "MISSING_REQUIRED_SECTION";
    case Status::WrongGame: return "WRONG_GAME";
    case Status::WrongRom: return "WRONG_ROM";
    case Status::UnsupportedVersion: return "UNSUPPORTED_VERSION";
    case Status::IncompatibleBuild: return "INCOMPATIBLE_BUILD";
    }
    return "UNKNOWN";
}

const char* status_message(Status status) {
    switch (status) {
    case Status::Ok: return "OK";
    case Status::NotFound: return "No saved state exists";
    case Status::IoError: return "The saved state could not be read";
    case Status::TooLarge:
    case Status::CorruptHeader:
    case Status::CorruptSection:
    case Status::Truncated:
    case Status::HashMismatch:
    case Status::MissingRequiredSection: return "The saved state is corrupted";
    case Status::WrongGame: return "The saved state belongs to a different game";
    case Status::WrongRom: return "The saved state was made with a different ROM";
    case Status::UnsupportedVersion: return "The saved state needs a newer version";
    case Status::IncompatibleBuild: return "The saved state is incompatible with this version";
    }
    return "Unknown error";
}

bool encode(const InMemorySnapshot& snapshot, const Identity& identity, const Metadata& metadata,
    std::vector<uint8_t>& out, std::string& error) {
    if (!validate_snapshot(snapshot, error)) { error = "refusing to encode: " + error; return false; }
    if (identity.game_id.empty() || identity.game_id.size() > kGameIdBytes) { error = "invalid game id"; return false; }
    struct Section { SectionType type; uint16_t flags; uint64_t canonical; std::vector<uint8_t> payload; };
    std::vector<Section> sections;
    const auto hashes = compute_hashes(snapshot);
    for (const auto& [domain, type] : kDomainSections) {
        if (!(snapshot.present & domain_bit(domain))) continue;
        if (domain == DomainId::Renderer || domain == DomainId::Color || domain == DomainId::Depth) continue;
        sections.push_back({type, kSectionRequired, hashes.domain[size_t(domain)], canonical_bytes(snapshot, domain)});
    }
    const uint32_t renderer_bits = domain_bit(DomainId::Renderer) | domain_bit(DomainId::Color) | domain_bit(DomainId::Depth);
    if (snapshot.present & renderer_bits) {
        if ((snapshot.present & renderer_bits) != renderer_bits) { error = "renderer domains must be present together"; return false; }
        SplitRenderer split;
        if (!split_renderer(snapshot.renderer.blob, split, error)) return false;
        sections.push_back({SectionType::Renderer, kSectionRequired, hashes.domain[size_t(DomainId::Renderer)], std::move(split.semantic)});
        sections.push_back({SectionType::FramebufferColor, kSectionRequired, hashes.domain[size_t(DomainId::Color)], std::move(split.color)});
        sections.push_back({SectionType::FramebufferDepth, kSectionRequired, hashes.domain[size_t(DomainId::Depth)], std::move(split.depth)});
    }
    auto meta = encode_metadata(metadata);
    const uint64_t meta_hash = checksum(meta.data(), meta.size());
    sections.push_back({SectionType::Metadata, 0, meta_hash, std::move(meta)});
    std::sort(sections.begin(), sections.end(), [](auto& a, auto& b) { return a.type < b.type; });

    const size_t table = kHeaderSize + sections.size() * kSectionEntrySize;
    uint64_t payload = 0;
    for (const auto& s : sections) payload += s.payload.size();
    out.assign(table, 0);
    out.reserve(table + payload);
    uint64_t offset = table;
    for (size_t i = 0; i < sections.size(); ++i) {
        const auto& s = sections[i];
        const size_t at = kHeaderSize + i * kSectionEntrySize;
        put_at<uint32_t>(out, at + kEntType, static_cast<uint32_t>(s.type));
        put_at<uint16_t>(out, at + kEntVersion, kSectionVersion);
        put_at<uint16_t>(out, at + kEntFlags, s.flags);
        put_at<uint64_t>(out, at + kEntOffset, offset);
        put_at<uint64_t>(out, at + kEntUncompressed, s.payload.size());
        put_at<uint64_t>(out, at + kEntStored, s.payload.size());
        put_at<uint64_t>(out, at + kEntCanonical, s.canonical);
        put_at<uint64_t>(out, at + kEntChecksum, checksum(s.payload.data(), s.payload.size()));
        offset += s.payload.size();
    }
    for (const auto& s : sections) out.insert(out.end(), s.payload.begin(), s.payload.end());
    std::memcpy(out.data(), kMagic.data(), kMagic.size());
    put_at<uint16_t>(out, kOffVersion, kFormatVersion);
    put_at<uint16_t>(out, kOffHeaderSize, kHeaderSize);
    put_at<uint32_t>(out, kOffFlags, 0);
    put_at<uint16_t>(out, kOffEndian, kEndianMarker);
    put_at<uint8_t>(out, kOffCompression, static_cast<uint8_t>(Compression::None));
    put_at<uint32_t>(out, kOffSectionCount, static_cast<uint32_t>(sections.size()));
    std::memcpy(out.data() + kOffGameId, identity.game_id.data(), identity.game_id.size());
    put_at<uint64_t>(out, kOffRom, identity.rom_hash);
    put_at<uint64_t>(out, kOffCorpus, snapshot.build.corpus_digest);
    put_at<uint32_t>(out, kOffFunctions, snapshot.build.function_count);
    put_at<uint32_t>(out, kOffHle, snapshot.build.hle_count);
    put_at<uint32_t>(out, kOffSchema, snapshot.version);
    put_at<uint32_t>(out, kOffPresent, snapshot.present);
    put_at<uint64_t>(out, kOffUncompressed, payload);
    put_at<uint64_t>(out, kOffStored, payload);
    put_at<uint64_t>(out, kOffAggregate, hashes.aggregate);
    reseal(out);
    return true;
}

void reseal(std::vector<uint8_t>& file) {
    if (file.size() < kHeaderSize) return;
    const uint32_t count = get_at<uint32_t>(file, kOffSectionCount);
    const uint64_t table_end = uint64_t(kHeaderSize) + uint64_t(count) * kSectionEntrySize;
    if (table_end <= file.size()) {
        for (uint32_t i = 0; i < count; ++i) {
            const size_t at = kHeaderSize + size_t(i) * kSectionEntrySize;
            const uint64_t offset = get_at<uint64_t>(file, at + kEntOffset);
            const uint64_t stored = get_at<uint64_t>(file, at + kEntStored);
            if (offset <= file.size() && stored <= file.size() - offset)
                put_at<uint64_t>(file, at + kEntChecksum, checksum(file.data() + offset, size_t(stored)));
        }
        put_at<uint64_t>(file, kOffTableChecksum, checksum(file.data() + kHeaderSize, size_t(table_end - kHeaderSize)));
    }
    put_at<uint64_t>(file, kOffHeaderChecksum, checksum(file.data(), kOffHeaderChecksum));
}

Result decode(std::span<const uint8_t> file, const Identity& expected, InMemorySnapshot& out,
    const Limits& limits, Metadata* metadata) {
    // ---- header ----
    if (file.size() > limits.max_file_bytes) return fail(Status::TooLarge, "file exceeds the size limit");
    if (file.size() < kMagic.size()) return fail(Status::Truncated, "shorter than the magic");
    if (std::memcmp(file.data(), kMagic.data(), kMagic.size()) != 0) return fail(Status::CorruptHeader, "not a .sbks file (magic)");
    if (file.size() < kHeaderSize) return fail(Status::Truncated, "shorter than the header");
    // magic/version/header_size are layout-stable across versions; the header
    // checksum always occupies the last 8 bytes of the header.
    if (get_at<uint16_t>(file, kOffHeaderSize) != kHeaderSize) {
        return get_at<uint16_t>(file, kOffVersion) > kFormatVersion
            ? fail(Status::UnsupportedVersion, "header layout from a newer format")
            : fail(Status::CorruptHeader, "header size");
    }
    if (get_at<uint64_t>(file, kOffHeaderChecksum) != checksum(file.data(), kOffHeaderChecksum))
        return fail(Status::CorruptHeader, "header checksum");
    const uint16_t version = get_at<uint16_t>(file, kOffVersion);
    if (version == 0) return fail(Status::CorruptHeader, "format version 0");
    if (version > kFormatVersion) return fail(Status::UnsupportedVersion, "format version " + std::to_string(version));
    if (get_at<uint16_t>(file, kOffEndian) != kEndianMarker) return fail(Status::CorruptHeader, "endianness marker");
    if (get_at<uint32_t>(file, kOffFlags) != 0) return fail(Status::UnsupportedVersion, "unknown header flags");
    if (get_at<uint8_t>(file, kOffCompression) != static_cast<uint8_t>(Compression::None))
        return fail(Status::UnsupportedVersion, "unsupported compression");
    if (get_at<uint8_t>(file, kOffReserved0) != 0) return fail(Status::CorruptHeader, "reserved header byte");
    for (size_t i = kOffReserved1; i < kOffHeaderChecksum; ++i)
        if (file[i]) return fail(Status::CorruptHeader, "reserved header bytes");

    // ---- identity / compatibility ----
    const char* id_bytes = reinterpret_cast<const char*>(file.data() + kOffGameId);
    const size_t id_length = strnlen(id_bytes, kGameIdBytes);
    for (size_t i = id_length; i < kGameIdBytes; ++i)
        if (id_bytes[i]) return fail(Status::CorruptHeader, "game id padding");
    if (std::string(id_bytes, id_length) != expected.game_id)
        return fail(Status::WrongGame, "game id '" + std::string(id_bytes, id_length) + "'");
    if (get_at<uint64_t>(file, kOffRom) != expected.rom_hash) return fail(Status::WrongRom, "ROM identity differs");
    const uint32_t schema = get_at<uint32_t>(file, kOffSchema);
    if (schema > kSnapshotVersion) return fail(Status::UnsupportedVersion, "snapshot schema " + std::to_string(schema));
    if (schema < kSnapshotVersion) return fail(Status::IncompatibleBuild, "snapshot schema " + std::to_string(schema));
    const BuildIdentity build{get_at<uint64_t>(file, kOffCorpus), get_at<uint32_t>(file, kOffFunctions), get_at<uint32_t>(file, kOffHle)};
    if (build.corpus_digest != expected.build.corpus_digest || build.function_count != expected.build.function_count ||
        build.hle_count != expected.build.hle_count)
        return fail(Status::IncompatibleBuild, "continuation corpus differs");
    const uint32_t present = get_at<uint32_t>(file, kOffPresent);
    if (present >> kDomainCount) return fail(Status::UnsupportedVersion, "unknown domain bits");
    if (expected.present_mask && present != expected.present_mask) return fail(Status::IncompatibleBuild, "state domain set differs");

    // ---- section table ----
    const uint32_t count = get_at<uint32_t>(file, kOffSectionCount);
    if (!count || count > limits.max_sections) return fail(Status::CorruptHeader, "section count");
    const uint64_t payload_start = uint64_t(kHeaderSize) + uint64_t(count) * kSectionEntrySize;
    if (payload_start > file.size()) return fail(Status::Truncated, "section table");
    if (get_at<uint64_t>(file, kOffTableChecksum) != checksum(file.data() + kHeaderSize, size_t(payload_start - kHeaderSize)))
        return fail(Status::CorruptHeader, "section table checksum");
    uint64_t stored_total = get_at<uint64_t>(file, kOffStored), file_end = 0;
    if (add_overflows(payload_start, stored_total, file_end)) return fail(Status::CorruptHeader, "stored size overflow");
    if (file_end > file.size()) return fail(Status::Truncated, "payload shorter than declared");
    if (file_end < file.size()) return fail(Status::CorruptHeader, "trailing bytes after the payload");

    struct Entry { uint32_t type; uint16_t version, flags; uint64_t offset, uncompressed, stored, canonical, sum; };
    std::vector<Entry> entries;
    entries.reserve(count);
    uint64_t sum_stored = 0, sum_uncompressed = 0;
    std::array<const Entry*, kKnownSections + 1> known{};
    for (uint32_t i = 0; i < count; ++i) {
        const size_t at = kHeaderSize + size_t(i) * kSectionEntrySize;
        Entry e{get_at<uint32_t>(file, at + kEntType), get_at<uint16_t>(file, at + kEntVersion),
            get_at<uint16_t>(file, at + kEntFlags), get_at<uint64_t>(file, at + kEntOffset),
            get_at<uint64_t>(file, at + kEntUncompressed), get_at<uint64_t>(file, at + kEntStored),
            get_at<uint64_t>(file, at + kEntCanonical), get_at<uint64_t>(file, at + kEntChecksum)};
        const std::string name = std::string("section ") + section_name(e.type);
        if (!e.type || !e.version || (e.flags & ~kSectionRequired)) return fail(Status::CorruptHeader, name + " entry fields");
        if (e.stored > limits.max_section_bytes || e.uncompressed > limits.max_section_bytes)
            return fail(Status::TooLarge, name + " exceeds the section limit");
        if (e.stored != e.uncompressed) return fail(Status::CorruptSection, name + " stored/uncompressed size mismatch");
        uint64_t end = 0;
        if (e.offset < payload_start || add_overflows(e.offset, e.stored, end) || end > file.size())
            return fail(Status::CorruptHeader, name + " outside the payload");
        if (add_overflows(sum_stored, e.stored, sum_stored) || add_overflows(sum_uncompressed, e.uncompressed, sum_uncompressed))
            return fail(Status::CorruptHeader, "size overflow");
        entries.push_back(e);
    }
    for (const auto& e : entries) {
        const std::string name = std::string("section ") + section_name(e.type);
        if (e.type <= kKnownSections) {
            if (known[e.type]) return fail(Status::CorruptHeader, "duplicate " + name);
            if (e.version > kSectionVersion) return fail(Status::UnsupportedVersion, name + " version " + std::to_string(e.version));
            known[e.type] = &e;
        } else if (e.flags & kSectionRequired) {
            return fail(Status::UnsupportedVersion, "unknown required section type " + std::to_string(e.type));
        }
    }
    if (sum_stored != stored_total || sum_uncompressed != get_at<uint64_t>(file, kOffUncompressed))
        return fail(Status::CorruptHeader, "section sizes do not add up to the declared payload");
    // Non-overlapping and (with the sums above) exactly tiling the payload.
    std::vector<const Entry*> by_offset;
    for (const auto& e : entries) by_offset.push_back(&e);
    std::sort(by_offset.begin(), by_offset.end(), [](auto* a, auto* b) { return a->offset < b->offset; });
    for (size_t i = 1; i < by_offset.size(); ++i)
        if (by_offset[i - 1]->offset + by_offset[i - 1]->stored > by_offset[i]->offset)
            return fail(Status::CorruptHeader, "overlapping sections");
    for (const auto& e : entries) {
        if (checksum(file.data() + e.offset, size_t(e.stored)) != e.sum)
            return fail(Status::CorruptSection, std::string("section ") + section_name(e.type) + " checksum");
    }
    // Required domain sections must match the present mask exactly.
    for (const auto& [domain, type] : kDomainSections) {
        const bool wanted = present & domain_bit(domain);
        const Entry* e = known[static_cast<uint32_t>(type)];
        if (wanted && !e) return fail(Status::MissingRequiredSection, std::string("missing section ") + section_name(uint32_t(type)));
        if (!wanted && e) return fail(Status::CorruptHeader, std::string("section without domain bit: ") + section_name(uint32_t(type)));
        if (e && !(e->flags & kSectionRequired)) return fail(Status::CorruptHeader, "domain section not marked required");
    }

    // ---- decode into a staging snapshot (never into `out` on failure) ----
    InMemorySnapshot staged;
    staged.magic = kSnapshotMagic;
    staged.version = schema;
    staged.build = build;
    staged.present = present;
    auto payload = [&](SectionType type) {
        const Entry* e = known[static_cast<uint32_t>(type)];
        return file.subspan(size_t(e->offset), size_t(e->stored));
    };
    for (const auto& [domain, type] : kDomainSections) {
        if (!(present & domain_bit(domain)) || domain == DomainId::Renderer || domain == DomainId::Color || domain == DomainId::Depth)
            continue;
        std::string why;
        if (!decode_domain(payload(type), domain, staged, why))
            return fail(Status::CorruptSection, std::string("section ") + section_name(uint32_t(type)) + ": " + why);
    }
    const uint32_t renderer_bits = domain_bit(DomainId::Renderer) | domain_bit(DomainId::Color) | domain_bit(DomainId::Depth);
    if (present & renderer_bits) {
        if ((present & renderer_bits) != renderer_bits) return fail(Status::CorruptHeader, "partial renderer domain set");
        std::string why;
        if (!join_renderer(payload(SectionType::Renderer), payload(SectionType::FramebufferColor),
                payload(SectionType::FramebufferDepth), staged.renderer.blob, why))
            return fail(Status::CorruptSection, "renderer: " + why);
    }
    Metadata meta;
    if (const Entry* e = known[static_cast<uint32_t>(SectionType::Metadata)]) {
        const auto bytes = payload(SectionType::Metadata);
        if (e->canonical != checksum(bytes.data(), bytes.size()) || !decode_metadata(bytes, meta))
            return fail(Status::CorruptSection, "section METADATA");
    }

    // ---- canonical hashes: every domain and the aggregate ----
    const auto hashes = compute_hashes(staged);
    for (const auto& [domain, type] : kDomainSections) {
        if (!(present & domain_bit(domain))) continue;
        if (hashes.domain[size_t(domain)] != known[static_cast<uint32_t>(type)]->canonical)
            return fail(Status::HashMismatch, std::string("canonical hash of ") + domain_name(domain));
    }
    if (hashes.aggregate != get_at<uint64_t>(file, kOffAggregate)) return fail(Status::HashMismatch, "aggregate canonical hash");
    staged.hashes = hashes;
    std::string why;
    if (!validate_snapshot(staged, why)) return fail(Status::CorruptSection, "snapshot structure: " + why);
    out = std::move(staged);
    if (metadata) *metadata = std::move(meta);
    return {};
}
}
