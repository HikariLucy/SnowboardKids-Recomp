// P5/P6 non-live tests without the runtime: .sbks encoding/decoding, parser
// hardening against untrusted files, atomic storage and the host audio
// boundary. Synthetic snapshots only (no ROM, no game data).
#include "quiescence/renderer_state.hpp"
#include "savestate/host_audio.hpp"
#include "savestate/sbks.hpp"
#include "savestate/storage.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace s = sbk::savestate;
namespace sb = sbk::savestate::sbks;
namespace st = sbk::savestate::storage;
namespace au = sbk::savestate::audio;
namespace q = sbk::quiescence;
namespace fs = std::filesystem;
#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #value); std::exit(1); } } while (false)

namespace {
using Clock = std::chrono::steady_clock;
const s::BuildIdentity kBuild{0xC0FFEEull, 7, 61};
constexpr uint64_t kRom = 0xF384619787B78D4Bull;

sb::Identity identity() {
    sb::Identity id;
    id.rom_hash = kRom;
    id.build = kBuild;
    return id;
}

std::vector<uint8_t> renderer_blob(uint64_t generation, uint32_t seed, int colors, int depths, uint32_t pixels) {
    q::SemanticStateHeader header{};
    header.magic = q::SEMANTIC_STATE_MAGIC; header.version = q::SEMANTIC_STATE_VERSION;
    header.generation = generation; header.rdp.fill_color = 0x1234 + seed; header.vi.origin = 0x100;
    header.rdp.tmem[7] = uint8_t(seed);
    header.framebuffer_count = uint32_t(colors + depths);
    std::vector<uint8_t> blob(sizeof(header));
    std::memcpy(blob.data(), &header, sizeof(header));
    for (int i = 0; i < colors + depths; ++i) {
        q::SemanticFramebufferHeader fb{};
        fb.type = i < colors ? 1 : 2;
        fb.address = 0x100000u + uint32_t(i) * 0x40000; fb.width = 320; fb.height = pixels / 640; fb.siz = 2;
        fb.pixel_bytes = pixels;
        const auto* raw = reinterpret_cast<const uint8_t*>(&fb);
        blob.insert(blob.end(), raw, raw + sizeof(fb));
        for (uint32_t k = 0; k < pixels; ++k) blob.push_back(uint8_t(k * (seed + 3) + uint32_t(i) * 7));
    }
    return blob;
}

// A structurally valid snapshot touching every domain. `pages` nonzero pages
// spread over `extent`; renderer planes of `plane_bytes` each.
s::InMemorySnapshot make_snapshot(uint32_t seed, uint64_t extent = 16ull << 20, uint32_t pages = 6,
    uint32_t plane_bytes = 640 * 4) {
    s::InMemorySnapshot snap;
    snap.build = kBuild;
    snap.present = (1u << s::kDomainCount) - 1;
    auto& m = snap.memory;
    m.extent = extent;
    m.page_size = 4096;
    const uint64_t total_pages = extent / 4096;
    for (uint32_t i = 0; i < pages; ++i) {
        const uint32_t index = uint32_t((uint64_t(i) * 7919 + seed) % total_pages);
        m.pages.push_back(index);
    }
    std::sort(m.pages.begin(), m.pages.end());
    m.pages.erase(std::unique(m.pages.begin(), m.pages.end()), m.pages.end());
    for (size_t i = 0; i < m.pages.size(); ++i)
        for (uint32_t k = 0; k < 4096; ++k) m.data.push_back(uint8_t((k * 31 + i * 17 + seed) | 1));
    m.normalized = {{0x1000 + 0x1B0, 8}, {0x2000 + 0x1B0, 8}};
    auto& c = snap.continuations;
    c.logical_lifetime_counter = 5;
    s::ThreadState running;
    running.address = 0x80001000; running.logical_lifetime = 1; running.entrypoint = 0x80100100;
    running.run = s::RunState::Running; running.pending = 1; running.started = true;
    for (size_t i = 0; i < 32; ++i) { running.cpu.gpr[i] = i * 0x1111 + seed; running.cpu.fpr[i] = ~uint64_t(i); }
    running.cpu.fr = 1; running.cpu.rounding = 2; running.cpu.status = 0x24000000;
    running.frames.push_back({0x1001ull << 32 | 2, 3, 1, 2, 3, -1, 0x80200000, {}});
    running.frames.push_back({0x1001ull << 32 | 5, 1, 0, 0, 7, 0, 0, {seed, 2}});
    running.blocked = {0x42, 2, {1, 2, 3, 4}, -3, true};
    s::ThreadState idle;
    idle.address = 0x80001100; idle.logical_lifetime = 3; idle.entrypoint = 0x80100200;
    c.threads = {running, idle};
    snap.scheduler.running_queue_head = int32_t(0x80001000);
    snap.scheduler.inbox = {{int32_t(0x80003000), 1, false, true}, {int32_t(0x80003200), seed, true, false}};
    snap.time = {123456789 + seed, -42, {int32_t(0x80003800)}};
    auto& v = snap.vi;
    v.cur_state = 1; v.field = 0; v.total_vis = 999 + seed; v.remaining_retraces = 2; v.dummy_odd = true;
    v.states[0] = {0x80010000, int32_t(0x80100000), int32_t(0x80003000), 5, 1, 2, 3};
    v.states[1] = {1, int32_t(0x80200000), 0, 0, 4, 5, 6};
    for (size_t i = 0; i < 14; ++i) { v.regs[i] = uint32_t(i + seed); v.update_screen_regs[i] = uint32_t(i * 3); }
    v.sp = {int32_t(0x80003000), 1}; v.ai = {int32_t(0x80003200), 2};
    auto& a = snap.audio;
    a.guest_frequency = 22050; a.host_present = true; a.host_input_rate = 22050;
    a.host_output_rate = 48000; a.host_output_channels = 2;
    for (uint32_t i = 0; i < 8; ++i) a.host_history.push_back(0x3F000000u + i);
    for (uint32_t i = 0; i < 8 * 300; ++i) a.host_backlog.push_back(uint8_t(i ^ seed));
    snap.overlays.loaded = {{3, int32_t(0x80400000)}, {9, int32_t(0x80500000)}};
    snap.overlays.section_addresses = {int32_t(0x80000400), int32_t(0x80400000), 0, int32_t(0x80500000)};
    snap.rsp.dmem.resize(0x1000);
    for (size_t i = 0; i < snap.rsp.dmem.size(); ++i) snap.rsp.dmem[i] = uint8_t(i * 5 + seed);
    snap.renderer.blob = renderer_blob(77 + seed, seed, 2, 1, plane_bytes);
    snap.hashes = s::compute_hashes(snap);
    std::string why;
    if (!s::validate_snapshot(snap, why)) { std::fprintf(stderr, "fixture invalid: %s\n", why.c_str()); std::exit(1); }
    return snap;
}

std::vector<uint8_t> encode(const s::InMemorySnapshot& snap, const sb::Identity& id = identity(), const sb::Metadata& meta = {"test", "quick"}) {
    std::vector<uint8_t> out;
    std::string error;
    if (!sb::encode(snap, id, meta, out, error)) { std::fprintf(stderr, "encode: %s\n", error.c_str()); std::exit(1); }
    return out;
}

sb::Status decode_status(const std::vector<uint8_t>& file, const sb::Identity& id = identity(), sb::Limits limits = {}) {
    s::InMemorySnapshot out;
    return sb::decode(file, id, out, limits).status;
}

template<class T> T get(const std::vector<uint8_t>& f, size_t at) { T v; std::memcpy(&v, f.data() + at, sizeof(T)); return v; }
template<class T> void set(std::vector<uint8_t>& f, size_t at, T v) { std::memcpy(f.data() + at, &v, sizeof(T)); }

// Parsed table entry of a well-formed file (test-side view).
struct Entry { uint32_t type; uint16_t version, flags; uint64_t offset, uncompressed, stored, canonical, sum; };
std::vector<Entry> entries(const std::vector<uint8_t>& f) {
    std::vector<Entry> result;
    const uint32_t count = get<uint32_t>(f, 20);
    for (uint32_t i = 0; i < count; ++i) {
        const size_t at = 128 + size_t(i) * 48;
        result.push_back({get<uint32_t>(f, at), get<uint16_t>(f, at + 4), get<uint16_t>(f, at + 6), get<uint64_t>(f, at + 8),
            get<uint64_t>(f, at + 16), get<uint64_t>(f, at + 24), get<uint64_t>(f, at + 32), get<uint64_t>(f, at + 40)});
    }
    return result;
}
std::vector<uint8_t> payload_of(const std::vector<uint8_t>& f, const Entry& e) {
    return {f.begin() + long(e.offset), f.begin() + long(e.offset + e.stored)};
}
// Rebuilds a file from (entry, payload) pairs in the given table/physical
// order, updating offsets, sizes, count and checksums (reseal).
std::vector<uint8_t> rebuild(const std::vector<uint8_t>& original, std::vector<std::pair<Entry, std::vector<uint8_t>>> sections,
    const std::vector<size_t>& physical_order = {}) {
    std::vector<uint8_t> f(original.begin(), original.begin() + 128);
    const size_t table = 128 + sections.size() * 48;
    f.resize(table);
    std::vector<size_t> order = physical_order;
    if (order.empty()) for (size_t i = 0; i < sections.size(); ++i) order.push_back(i);
    uint64_t offset = table, total = 0;
    std::vector<uint64_t> offsets(sections.size());
    for (size_t i : order) { offsets[i] = offset; offset += sections[i].second.size(); total += sections[i].second.size(); }
    for (size_t i = 0; i < sections.size(); ++i) {
        auto& [e, payload] = sections[i];
        const size_t at = 128 + i * 48;
        set<uint32_t>(f, at, e.type); set<uint16_t>(f, at + 4, e.version); set<uint16_t>(f, at + 6, e.flags);
        set<uint64_t>(f, at + 8, offsets[i]); set<uint64_t>(f, at + 16, payload.size()); set<uint64_t>(f, at + 24, payload.size());
        set<uint64_t>(f, at + 32, e.canonical);
    }
    for (size_t i : order) f.insert(f.end(), sections[i].second.begin(), sections[i].second.end());
    set<uint32_t>(f, 20, uint32_t(sections.size()));
    set<uint64_t>(f, 72, total);
    set<uint64_t>(f, 80, total);
    sb::reseal(f);
    return f;
}
std::vector<std::pair<Entry, std::vector<uint8_t>>> split(const std::vector<uint8_t>& f) {
    std::vector<std::pair<Entry, std::vector<uint8_t>>> result;
    for (const auto& e : entries(f)) result.push_back({e, payload_of(f, e)});
    return result;
}

bool same_snapshot(const s::InMemorySnapshot& a, const s::InMemorySnapshot& b) {
    if (a.present != b.present || !(a.hashes == b.hashes) || a.build.corpus_digest != b.build.corpus_digest) return false;
    for (size_t d = 0; d < s::kDomainCount; ++d) {
        if (s::DomainId(d) == s::DomainId::Renderer || s::DomainId(d) == s::DomainId::Color || s::DomainId(d) == s::DomainId::Depth) continue;
        if (s::canonical_bytes(a, s::DomainId(d)) != s::canonical_bytes(b, s::DomainId(d))) return false;
    }
    auto normalized = [](std::vector<uint8_t> blob) { std::memset(blob.data() + 8, 0, 8); return blob; }; // generation
    return normalized(a.renderer.blob) == normalized(b.renderer.blob);
}

struct Stats {
    std::vector<uint64_t> v;
    void add(uint64_t x) { v.push_back(x); }
    uint64_t pct(double p) { auto s = v; std::sort(s.begin(), s.end()); return s[std::min(s.size() - 1, size_t(p * (s.size() - 1) + 0.5))]; }
    std::string line() { return "median=" + std::to_string(pct(0.5)) + " p95=" + std::to_string(pct(0.95)) + " max=" + std::to_string(pct(1.0)); }
};
uint64_t us(Clock::time_point t) { return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t).count(); }

size_t temp_files(const fs::path& dir) {
    size_t n = 0;
    for (auto& e : fs::directory_iterator(dir)) n += e.path().filename().string().find(".tmp-") != std::string::npos;
    return n;
}
}

int main(int argc, char** argv) {
    const fs::path dir = argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path() / "sbk-persistence-test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const auto snap = make_snapshot(1);
    const auto file = encode(snap);

    // 1. roundtrip, metadata
    {
        s::InMemorySnapshot out;
        sb::Metadata meta;
        const auto r = sb::decode(file, identity(), out, {}, &meta);
        if (!r.ok()) std::fprintf(stderr, "decode: %s %s\n", sb::status_name(r.status), r.detail.c_str());
        CHECK(r.ok());
        CHECK(same_snapshot(snap, out));
        CHECK(meta.writer == "test" && meta.slot == "quick");
        CHECK(get<uint64_t>(file, 88) == snap.hashes.aggregate);
        std::printf("roundtrip: file_bytes=%zu sections=%u\n", file.size(), get<uint32_t>(file, 20));
    }
    // 2. deterministic: identical bytes; host generation of the P3 blob excluded
    {
        CHECK(encode(snap) == file);
        auto other_generation = snap;
        other_generation.renderer.blob = renderer_blob(12345, 1, 2, 1, 640 * 4);
        other_generation.hashes = s::compute_hashes(other_generation);
        CHECK(other_generation.hashes == snap.hashes);
        CHECK(encode(other_generation) == file);
        CHECK(encode(make_snapshot(2)) != file);
        std::puts("deterministic encoding: identical bytes (generation normalized, no timestamps)");
    }
    // encode refuses invalid or stale snapshots
    {
        auto stale = snap; stale.time.logical_ns += 1; // hashes not recomputed
        std::vector<uint8_t> out; std::string error;
        CHECK(!sb::encode(stale, identity(), {}, out, error));
        auto invalid = snap; invalid.continuations.threads[0].run = s::RunState(7); invalid.hashes = s::compute_hashes(invalid);
        CHECK(!sb::encode(invalid, identity(), {}, out, error));
    }
    // 3/4/5. write -> read, atomic replace, previous save survives failures
    {
        const auto path = *st::slot_path(dir, st::kQuickSlot);
        CHECK(path.filename() == "quick.sbks");
        CHECK(!st::slot_path(dir, "../x") && !st::slot_path(dir, "") && !st::slot_path(dir, "A") && st::slot_path(dir, "slot_1"));
        std::vector<uint8_t> read; std::string error;
        CHECK(st::read_bounded(path, 1 << 30, read, error) == st::ReadStatus::NotFound);
        CHECK(st::write_atomic(path, file, error));
        CHECK(st::read_bounded(path, 1 << 30, read, error) == st::ReadStatus::Ok && read == file);
        CHECK(decode_status(read) == sb::Status::Ok);
        const auto second = encode(make_snapshot(2));
        CHECK(st::write_atomic(path, second, error));
        CHECK(st::read_bounded(path, 1 << 30, read, error) == st::ReadStatus::Ok && read == second);
        CHECK(temp_files(dir) == 0);
        for (auto fault : {st::WriteFault::CreateTemp, st::WriteFault::Write, st::WriteFault::Sync, st::WriteFault::Rename}) {
            CHECK(!st::write_atomic(path, file, error, fault));
            CHECK(st::read_bounded(path, 1 << 30, read, error) == st::ReadStatus::Ok && read == second);
            CHECK(decode_status(read) == sb::Status::Ok);
            CHECK(temp_files(dir) == 0);
        }
        CHECK(st::read_bounded(path, 100, read, error) == st::ReadStatus::TooLarge);
        // A stale temporary from an interrupted save is removed on the next save.
        const auto stale = dir / ".quick.sbks.tmp-1-1";
        { std::FILE* f = std::fopen(stale.c_str(), "wb"); CHECK(f); std::fputs("partial", f); std::fclose(f); }
        fs::last_write_time(stale, fs::file_time_type::clock::now() - std::chrono::hours(1));
        st::remove_stale_temporaries(path);
        CHECK(!fs::exists(stale));
        const auto mode = fs::status(path).permissions();
        CHECK((mode & fs::perms::owner_read) != fs::perms::none && (mode & fs::perms::others_write) == fs::perms::none);
        std::puts("storage: write/read, atomic replace, 4 injected failures keep the previous save, no temp leftovers");
    }
    // 6/7/8. identity and compatibility policy
    {
        auto id = identity(); id.rom_hash ^= 1;
        CHECK(decode_status(file, id) == sb::Status::WrongRom);
        id = identity(); id.game_id = "othergame";
        CHECK(decode_status(file, id) == sb::Status::WrongGame);
        id = identity(); id.build.corpus_digest ^= 1;
        CHECK(decode_status(file, id) == sb::Status::IncompatibleBuild);
        id = identity(); id.build.function_count += 1;
        CHECK(decode_status(file, id) == sb::Status::IncompatibleBuild);
        id = identity(); id.present_mask = 1;
        CHECK(decode_status(file, id) == sb::Status::IncompatibleBuild);
        auto f = file; set<uint32_t>(f, 64, s::kSnapshotVersion + 1); sb::reseal(f);
        CHECK(decode_status(f) == sb::Status::UnsupportedVersion);
        f = file; set<uint32_t>(f, 64, 0); sb::reseal(f);
        CHECK(decode_status(f) == sb::Status::IncompatibleBuild);
        f = file; set<uint16_t>(f, 8, 2); sb::reseal(f);
        CHECK(decode_status(f) == sb::Status::UnsupportedVersion);
        f = file; set<uint16_t>(f, 8, 0); sb::reseal(f);
        CHECK(decode_status(f) == sb::Status::CorruptHeader);
        f = file; set<uint32_t>(f, 12, 1); sb::reseal(f);
        CHECK(decode_status(f) == sb::Status::UnsupportedVersion);             // unknown flags
        f = file; f[18] = 1; sb::reseal(f);
        CHECK(decode_status(f) == sb::Status::UnsupportedVersion);             // zstd not supported
        f = file; set<uint16_t>(f, 16, 0x3412); sb::reseal(f);
        CHECK(decode_status(f) == sb::Status::CorruptHeader);                  // endianness marker
        f = file; f[24 + 14] = 'x'; sb::reseal(f);
        CHECK(decode_status(f) == sb::Status::CorruptHeader);                  // game id padding
        f = file; f[110] = 1; sb::reseal(f);
        CHECK(decode_status(f) == sb::Status::CorruptHeader);                  // reserved
        f = file; set<uint32_t>(f, 68, 1u << 20); sb::reseal(f);
        CHECK(decode_status(f) == sb::Status::UnsupportedVersion);             // unknown domain bit
        f = file; f[40] ^= 1;                                                 // not resealed
        CHECK(decode_status(f) == sb::Status::CorruptHeader);
        f = file; f[0] = 'X';
        CHECK(decode_status(f) == sb::Status::CorruptHeader);
        std::puts("compatibility: WRONG_ROM, WRONG_GAME, INCOMPATIBLE_BUILD, UNSUPPORTED_VERSION, CORRUPT_HEADER");
    }
    // 9. corruption: payload flips with and without resealed checksums
    {
        const auto es = entries(file);
        const auto memory = std::find_if(es.begin(), es.end(), [](auto& e) { return e.type == 1; });
        auto f = file; f[memory->offset + 100] ^= 0x40;
        CHECK(decode_status(f) == sb::Status::CorruptSection);
        sb::reseal(f);
        CHECK(decode_status(f) == sb::Status::HashMismatch);                   // valid checksums, wrong content
        f = file; set<uint64_t>(f, 88, get<uint64_t>(f, 88) ^ 1); sb::reseal(f);
        CHECK(decode_status(f) == sb::Status::HashMismatch);                   // aggregate
        // Invalid enum inside a payload with consistent hashes: structural rejection.
        auto bad = snap; bad.continuations.threads[0].run = s::RunState(7);
        const auto bad_hashes = s::compute_hashes(bad);
        f = file;
        const auto cont = std::find_if(es.begin(), es.end(), [](auto& e) { return e.type == 2; });
        CHECK(f[cont->offset + 38] == 1);
        f[cont->offset + 38] = 7;
        set<uint64_t>(f, 128 + size_t(cont - es.begin()) * 48 + 32, bad_hashes.domain[size_t(s::DomainId::Continuations)]);
        set<uint64_t>(f, 88, bad_hashes.aggregate);
        sb::reseal(f);
        CHECK(decode_status(f) == sb::Status::CorruptSection);
        // Boolean fields must be 0/1.
        f = file; f[cont->offset + 40] = 2; sb::reseal(f);                    // started flag
        CHECK(decode_status(f) == sb::Status::CorruptSection);
    }
    // 10. truncation at every structural boundary (and a sweep)
    {
        std::set<size_t> cuts = {0, 1, 7, 8, 9, 64, 119, 120, 127, 128, 129};
        const size_t table_end = 128 + 48 * get<uint32_t>(file, 20);
        for (size_t d : {size_t(0), size_t(1)}) { cuts.insert(table_end - d); cuts.insert(table_end + d); }
        for (const auto& e : entries(file)) for (int64_t d : {-1, 0, 1}) {
            cuts.insert(size_t(int64_t(e.offset) + d));
            cuts.insert(size_t(int64_t(e.offset + e.stored) + d));
        }
        for (size_t cut = 0; cut < file.size(); cut += 997) cuts.insert(cut);
        cuts.insert(file.size() - 1);
        size_t truncated = 0;
        for (size_t cut : cuts) {
            if (cut >= file.size()) continue;
            std::vector<uint8_t> f(file.begin(), file.begin() + long(cut));
            const auto status = decode_status(f);
            CHECK(status != sb::Status::Ok);
            truncated += status == sb::Status::Truncated;
            if (cut >= 128) CHECK(status == sb::Status::Truncated);
        }
        auto f = file; f.push_back(0);
        CHECK(decode_status(f) == sb::Status::CorruptHeader);                  // trailing bytes
        std::printf("truncation: %zu cut points rejected (%zu TRUNCATED)\n", cuts.size(), truncated);
    }
    // 11. random single-byte flips everywhere
    {
        std::mt19937_64 rng(0x5B5);
        for (int i = 0; i < 3000; ++i) {
            auto f = file;
            const size_t at = i < 256 ? size_t(i) : size_t(rng() % f.size());
            f[at] ^= uint8_t(1 + rng() % 255);
            CHECK(decode_status(f) != sb::Status::Ok);
        }
        // Random garbage and random prefixes of garbage.
        for (int i = 0; i < 300; ++i) {
            std::vector<uint8_t> f(rng() % 4096);
            for (auto& b : f) b = uint8_t(rng());
            if (i % 2 && f.size() >= 8) std::memcpy(f.data(), sb::kMagic.data(), 8);
            CHECK(decode_status(f) != sb::Status::Ok);
        }
        std::puts("random corruption: 3000 byte flips + 300 garbage files rejected");
    }
    // 12. table order and physical order are free; content identical
    {
        auto sections = split(file);
        std::reverse(sections.begin(), sections.end());
        std::vector<size_t> physical;
        for (size_t i = 0; i < sections.size(); ++i) physical.push_back((i * 5) % sections.size());
        std::sort(physical.begin(), physical.end());
        physical.erase(std::unique(physical.begin(), physical.end()), physical.end());
        CHECK(physical.size() == sections.size());
        std::rotate(physical.begin(), physical.begin() + 3, physical.end());
        const auto f = rebuild(file, sections, physical);
        s::InMemorySnapshot out;
        CHECK(sb::decode(f, identity(), out).ok());
        CHECK(same_snapshot(snap, out));
    }
    // 13/14. unknown optional section accepted; unknown required rejected
    {
        auto sections = split(file);
        sections.push_back({Entry{99, 1, 0, 0, 0, 0, 0, 0}, {1, 2, 3, 4}});
        CHECK(decode_status(rebuild(file, sections)) == sb::Status::Ok);
        sections.back().first.flags = sb::kSectionRequired;
        CHECK(decode_status(rebuild(file, sections)) == sb::Status::UnsupportedVersion);
        sections.back().first.flags = 4;                                        // unknown entry flag
        CHECK(decode_status(rebuild(file, sections)) == sb::Status::CorruptHeader);
    }
    // 15. oversized / overflowing lengths, limits, bomb, count fields
    {
        const auto es = entries(file);
        auto f = file; set<uint64_t>(f, 128 + 24, uint64_t(1) << 62); set<uint64_t>(f, 128 + 16, uint64_t(1) << 62); sb::reseal(f);
        CHECK(decode_status(f) == sb::Status::TooLarge);
        f = file; set<uint64_t>(f, 128 + 16, get<uint64_t>(f, 128 + 16) + 1); sb::reseal(f);
        CHECK(decode_status(f) == sb::Status::CorruptSection);                 // stored != uncompressed
        f = file; set<uint64_t>(f, 128 + 8, UINT64_MAX - 10); sb::reseal(f);
        CHECK(decode_status(f) == sb::Status::CorruptHeader);                  // offset overflow
        f = file; set<uint64_t>(f, 128 + 8, 16); sb::reseal(f);
        CHECK(decode_status(f) == sb::Status::CorruptHeader);                  // inside the header
        f = file; set<uint64_t>(f, 128 + 8, es[0].offset + 8); sb::reseal(f);
        CHECK(decode_status(f) != sb::Status::Ok);                             // overlap / out of payload
        f = file; set<uint64_t>(f, 80, UINT64_MAX - 64); sb::reseal(f);
        CHECK(decode_status(f) == sb::Status::CorruptHeader);                  // stored-size overflow
        f = file; set<uint32_t>(f, 20, 100000); sb::reseal(f);
        CHECK(decode_status(f) == sb::Status::CorruptHeader);                  // section count limit
        f = file; set<uint32_t>(f, 20, 0); sb::reseal(f);
        CHECK(decode_status(f) == sb::Status::CorruptHeader);
        sb::Limits small; small.max_file_bytes = 1000;
        CHECK(decode_status(file, identity(), small) == sb::Status::TooLarge);
        small = {}; small.max_section_bytes = 1024;                              // "decompression bomb" bound
        CHECK(decode_status(file, identity(), small) == sb::Status::TooLarge);
        // Huge element counts inside payloads never drive allocation.
        const auto memory = std::find_if(es.begin(), es.end(), [](auto& e) { return e.type == 1; });
        f = file; set<uint64_t>(f, memory->offset + 2 + 8 + 4, uint64_t(1) << 60); sb::reseal(f);
        CHECK(decode_status(f) == sb::Status::CorruptSection);
        const auto sched = std::find_if(es.begin(), es.end(), [](auto& e) { return e.type == 3; });
        f = file; set<uint64_t>(f, sched->offset + 2 + 4, UINT64_MAX); sb::reseal(f);
        CHECK(decode_status(f) == sb::Status::CorruptSection);
        std::puts("oversized: section/file limits, bomb bound, overflowing offsets and counts rejected without allocation");
    }
    // 16. duplicate / missing / overlapping sections
    {
        auto sections = split(file);
        auto dup = sections; dup.push_back(sections[0]);
        CHECK(decode_status(rebuild(file, dup)) == sb::Status::CorruptHeader);
        auto missing = sections;
        missing.erase(std::find_if(missing.begin(), missing.end(), [](auto& p) { return p.first.type == 9; }));
        CHECK(decode_status(rebuild(file, missing)) == sb::Status::MissingRequiredSection);
        auto version = sections; version[0].first.version = 2;
        CHECK(decode_status(rebuild(file, version)) == sb::Status::UnsupportedVersion);
        version[0].first.version = 0;
        CHECK(decode_status(rebuild(file, version)) == sb::Status::CorruptHeader);
        auto optional = sections; optional[0].first.flags = 0;                 // domain section not required
        CHECK(decode_status(rebuild(file, optional)) == sb::Status::CorruptHeader);
        auto f = file;                                                           // overlap with an equal-size gap
        const auto es = entries(f);
        set<uint64_t>(f, 128 + 48 + 8, es[1].offset - 1); sb::reseal(f);
        CHECK(decode_status(f) == sb::Status::CorruptHeader);
        std::puts("tables: duplicate, missing, versioned, optional-domain and overlapping sections rejected");
    }
    // 17. repeated load of the same bytes
    {
        for (int i = 0; i < 20; ++i) {
            s::InMemorySnapshot out;
            CHECK(sb::decode(file, identity(), out).ok() && out.hashes == snap.hashes);
        }
    }

    // ---- P5 host audio boundary (synthetic device) ----
    {
        struct Device {
            std::vector<uint8_t> queue;
            int clears = 0, queues = 0;
            bool fail = false;
            au::HostQueue ops() {
                return {[this] { return uint32_t(queue.size()); }, [this] { queue.clear(); ++clears; },
                    [this](const uint8_t* d, uint32_t n, std::string& e) {
                        if (fail) { e = "device refused"; return false; }
                        queue.insert(queue.end(), d, d + n); ++queues; return true; }};
            }
        };
        const au::DeviceFormat format{22050, 48000, 2};
        std::vector<float> history(8, 0.25f);
        auto submit = [](au::Boundary& b, Device& d, size_t frames, uint8_t tag) {
            std::vector<uint8_t> pcm(frames * 8);
            for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = uint8_t(tag + i);
            d.queue.insert(d.queue.end(), pcm.begin(), pcm.end());
            b.submitted(pcm.data(), pcm.size());
        };
        auto consume = [](Device& d, size_t bytes) { d.queue.erase(d.queue.begin(), d.queue.begin() + long(bytes)); };
        std::string error;
        // empty backlog
        {
            au::Boundary b(1 << 16); Device d; s::AudioState state;
            CHECK(b.capture(format, history, 0, state, error) && state.host_backlog.empty());
            submit(b, d, 100, 1);
            CHECK(b.install(state, format, history, d.ops(), error));
            CHECK(d.queue.empty() && d.clears == 1 && d.queues == 0 && b.ledger_filled() == 0);
        }
        // one buffer, several buffers with partial consumption (pending AI)
        for (int buffers : {1, 3, 7}) {
            au::Boundary b(1 << 16); Device d; s::AudioState state;
            for (int i = 0; i < buffers; ++i) submit(b, d, 64 + size_t(i) * 8, uint8_t(i * 40));
            consume(d, 8 * 10);                                 // device consumed 10 frames
            const auto expected = d.queue;
            CHECK(b.capture(format, history, uint32_t(d.queue.size()), state, error));
            CHECK(state.host_backlog == expected);
            submit(b, d, 200, 99);                              // abandoned future
            consume(d, 8 * 50);
            std::vector<float> h(8, 0.0f);
            // repeated restore of the same state: queued exactly once, no accumulation
            for (int r = 0; r < 5; ++r) {
                CHECK(b.install(state, format, h, d.ops(), error));
                CHECK(d.queue == expected && h == history && b.ledger_filled() == expected.size());
                s::AudioState again;
                CHECK(b.capture(format, h, uint32_t(d.queue.size()), again, error) && again.host_backlog == expected);
                submit(b, d, 30, uint8_t(r));                   // play continues, then restore again
                consume(d, 8 * 5);
            }
        }
        // rejections happen in validate, before touching the device
        {
            au::Boundary b(1 << 12); Device d; s::AudioState state;
            submit(b, d, 16, 3);
            CHECK(b.capture(format, history, uint32_t(d.queue.size()), state, error));
            auto other = format; other.output_rate = 44100;
            CHECK(!b.validate(state, other, history.size(), error));
            CHECK(!b.install(state, other, history, d.ops(), error) && d.clears == 0);
            auto misaligned = state; misaligned.host_backlog.push_back(0);
            CHECK(!b.validate(misaligned, format, history.size(), error));
            auto huge = state; huge.host_backlog.resize(1 << 13);
            CHECK(!b.validate(huge, format, history.size(), error));
            CHECK(!b.validate(state, format, history.size() + 1, error));
            CHECK(!b.capture(format, history, 1 << 13, state, error));   // queue beyond the ledger
            CHECK(!b.capture(format, history, 7, state, error));         // not frame aligned
            d.fail = true;
            CHECK(b.capture(format, history, uint32_t(d.queue.size()), state, error));
            CHECK(!b.install(state, format, history, d.ops(), error) && b.ledger_filled() == 0);
        }
        // ledger wraparound keeps the newest bytes in order
        {
            au::PcmLedger ledger(64);
            std::vector<uint8_t> all;
            for (int i = 0; i < 40; ++i) {
                std::vector<uint8_t> chunk(size_t(1 + i % 23));
                for (auto& x : chunk) x = uint8_t(all.size() + (&x - chunk.data()));
                all.insert(all.end(), chunk.begin(), chunk.end());
                ledger.append(chunk.data(), chunk.size());
                std::vector<uint8_t> tail;
                const size_t n = std::min<size_t>(all.size(), 64);
                CHECK(ledger.tail(n, tail) && std::equal(tail.begin(), tail.end(), all.end() - long(n)));
            }
        }
        std::puts("audio boundary: empty/1/3/7 buffers, partial consumption, 5x repeated restore, pre-mutation rejections, ledger wrap");
    }

    // ---- 19. synthetic stress and timings at live game scale ----
    {
        // Live-like shape: 512 MiB logical extent, ~660 nonzero pages (~2.7 MB),
        // renderer planes totalling ~300 KB. Synthetic data, NOT a live measurement.
        const auto big = make_snapshot(3, 512ull << 20, 660, 640 * 160);
        Stats encode_us, decode_us, save_us, load_us;
        std::vector<uint8_t> first;
        for (int i = 0; i < 100; ++i) {
            auto t = Clock::now();
            const auto bytes = encode(big);
            encode_us.add(us(t));
            if (first.empty()) first = bytes;
            CHECK(bytes == first);
            t = Clock::now();
            s::InMemorySnapshot out;
            CHECK(sb::decode(bytes, identity(), out).ok());
            decode_us.add(us(t));
            CHECK(out.hashes == big.hashes);
        }
        const auto path = dir / "stress.sbks";
        std::string error;
        for (int i = 0; i < 50; ++i) {
            auto t = Clock::now();
            CHECK(st::write_atomic(path, first, error));
            save_us.add(us(t));
            t = Clock::now();
            std::vector<uint8_t> bytes;
            CHECK(st::read_bounded(path, sb::Limits{}.max_file_bytes, bytes, error) == st::ReadStatus::Ok);
            s::InMemorySnapshot out;
            CHECK(sb::decode(bytes, identity(), out).ok() && out.hashes == big.hashes);
            load_us.add(us(t));
        }
        CHECK(temp_files(dir) == 0);
        std::printf("synthetic game-scale: file_bytes=%zu encode_us %s decode_us %s\n", first.size(),
            encode_us.line().c_str(), decode_us.line().c_str());
        std::printf("synthetic game-scale disk: save_us(write+fsync+rename) %s load_us(read+decode) %s\n",
            save_us.line().c_str(), load_us.line().c_str());
    }
    fs::remove_all(dir);
    std::puts("PASS: P5 audio boundary + P6 .sbks format/storage/parser hardening");
    return 0;
}
