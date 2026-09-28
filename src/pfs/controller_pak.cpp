#include "controller_pak.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace sbk::pfs {
namespace {
constexpr int inode_offset = 1 * page_size;
constexpr int mirror_offset = 2 * page_size;
constexpr int directory_offset = 3 * page_size;
constexpr int id_blocks[] = {1, 3, 4, 6};
#ifdef _WIN32
std::atomic<uint64_t> temp_sequence{0};
#endif

uint16_t be16(const uint8_t* p) {
    return (uint16_t(p[0]) << 8) | p[1];
}
uint32_t be32(const uint8_t* p) {
    return (uint32_t(be16(p)) << 16) | be16(p + 2);
}
void put16(uint8_t* p, uint16_t v) {
    p[0] = uint8_t(v >> 8);
    p[1] = uint8_t(v);
}
void put32(uint8_t* p, uint32_t v) {
    put16(p, uint16_t(v >> 16));
    put16(p + 2, uint16_t(v));
}
uint8_t inode_sum(const uint8_t* p) {
    unsigned sum = 0;
    for (int i = first_data_page * 2; i < int(page_size); ++i) sum += p[i];
    return uint8_t(sum);
}
uint16_t id_sum(const uint8_t* p, bool inverse) {
    uint32_t sum = 0;
    for (int i = 0; i < 28; i += 2) {
        const uint16_t word = be16(p + i);
        sum += inverse ? uint16_t(~word) : word;
    }
    return uint16_t(sum);
}
bool occupied(const uint8_t* dir) {
    return be16(dir + 4) != 0 && be32(dir) != 0;
}
Identity identity_at(const uint8_t* dir) {
    Identity id;
    id.game_code = be32(dir);
    id.company_code = be16(dir + 4);
    std::copy_n(dir + 12, 4, id.ext_name.begin());
    std::copy_n(dir + 16, 16, id.game_name.begin());
    return id;
}
uint32_t identity_hash(const Identity& id) {
    uint32_t hash = 2166136261u;
    auto feed = [&](uint8_t byte) { hash = (hash ^ byte) * 16777619u; };
    feed(uint8_t(id.company_code >> 8));
    feed(uint8_t(id.company_code));
    for (int shift = 24; shift >= 0; shift -= 8) feed(uint8_t(id.game_code >> shift));
    for (uint8_t byte : id.game_name) feed(byte);
    for (uint8_t byte : id.ext_name) feed(byte);
    return hash;
}
void trace(const char* event, int port, int file, int offset, int length,
           Error result, uint32_t hash = 0) {
    const char* enabled = std::getenv("SBK_PFS_TRACE");
    if (enabled && std::strcmp(enabled, "1") == 0)
        std::fprintf(stderr, "PFS %s port=%d file=%d identity=%08x offset=%d length=%d result=%d\n",
                     event, port + 1, file, hash, offset, length, result);
}

bool atomic_replace(const std::filesystem::path& path, const uint8_t* bytes, size_t size) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return false;
#ifdef _WIN32
    const auto temp = path.parent_path() /
        (path.filename().wstring() + L".tmp-" +
         std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++temp_sequence));
    HANDLE handle = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(handle, bytes, DWORD(size), &written, nullptr) && written == size;
    if (ok) ok = FlushFileBuffers(handle);
    CloseHandle(handle);
    if (ok) ok = MoveFileExW(temp.c_str(), path.c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) DeleteFileW(temp.c_str());
    return ok;
#else
    std::string temp = path.string() + ".tmp-XXXXXX";
    const int fd = ::mkstemp(temp.data());
    if (fd < 0) return false;
    size_t done = 0;
    bool ok = true;
    while (done < size) {
        const ssize_t count = ::write(fd, bytes + done, size - done);
        if (count <= 0) { ok = false; break; }
        done += size_t(count);
    }
    if (ok && ::fsync(fd) != 0) ok = false;
    if (::close(fd) != 0) ok = false;
    if (ok && ::rename(temp.c_str(), path.c_str()) != 0) ok = false;
    if (ok) {
        const int parent = ::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
        if (parent >= 0) {
            if (::fsync(parent) != 0) ok = false;
            ::close(parent);
        } else ok = false;
    }
    if (!ok) ::unlink(temp.c_str());
    return ok;
#endif
}
} // namespace

ControllerPak::ControllerPak(std::filesystem::path config_directory)
    : directory_(std::move(config_directory) / "controller-paks") {}

std::filesystem::path ControllerPak::path_for(int port) const {
    if (port < 0 || port >= 4) return {};
    return directory_ / ("port" + std::to_string(port + 1) + ".mpk");
}

void ControllerPak::set_present(int port, bool present) {
    std::lock_guard lock(mutex_);
    if (port < 0 || port >= 4) return;
    slots_[port].present = present;
    slots_[port].loaded = false;
}

ControllerPak::Image ControllerPak::blank() {
    Image image{};
    uint8_t id[32]{};
    put16(id + 24, 1); // PFS_ID_DEVICE_ID_BIT
    id[26] = 1;        // PFS_BANKS_256K
    put16(id + 28, id_sum(id, false));
    put16(id + 30, id_sum(id, true));
    for (int block : id_blocks)
        std::copy_n(id, 32, image.data() + block * block_size);
    for (int page = first_data_page; page < total_pages; ++page)
        put16(image.data() + inode_offset + page * 2, 3); // PFS_PAGE_NOT_USED
    image[inode_offset + 1] = inode_sum(image.data() + inode_offset);
    std::copy_n(image.data() + inode_offset, page_size, image.data() + mirror_offset);
    return image;
}

Error ControllerPak::chain(const Image& image, int file_no,
                           std::array<uint8_t, total_pages>& pages, int& count) {
    count = 0;
    if (file_no < 0 || file_no >= directory_entries) return Invalid;
    const auto* dir = image.data() + directory_offset + file_no * block_size;
    if (!occupied(dir)) return Invalid;
    int page = be16(dir + 6);
    std::array<bool, total_pages> seen{};
    while (page != 1) {
        if (page < first_data_page || page >= total_pages || seen[page]) return Inconsistent;
        seen[page] = true;
        pages[count++] = uint8_t(page);
        page = be16(image.data() + inode_offset + page * 2);
    }
    return count ? Ok : Inconsistent;
}

Error ControllerPak::validate(const Image& image) {
    const auto* id = image.data() + id_blocks[0] * block_size;
    if (be16(id + 24) != 1 || id[26] != 1 ||
        be16(id + 28) != id_sum(id, false) ||
        be16(id + 30) != id_sum(id, true)) return IdFatal;
    for (int block : id_blocks)
        if (std::memcmp(id, image.data() + block * block_size, 32) != 0) return IdFatal;
    const auto* inode = image.data() + inode_offset;
    if (inode[1] != inode_sum(inode) ||
        std::memcmp(inode, image.data() + mirror_offset, page_size) != 0)
        return Inconsistent;
    std::array<bool, total_pages> owned{};
    for (int file = 0; file < directory_entries; ++file) {
        const auto* dir = image.data() + directory_offset + file * block_size;
        if (!occupied(dir)) {
            if (be16(dir + 4) || be32(dir) || be16(dir + 6)) return Inconsistent;
            continue;
        }
        std::array<uint8_t, total_pages> pages{};
        int count = 0;
        if (chain(image, file, pages, count) != Ok) return Inconsistent;
        for (int i = 0; i < count; ++i) {
            if (owned[pages[i]]) return Inconsistent;
            owned[pages[i]] = true;
        }
    }
    for (int page = first_data_page; page < total_pages; ++page)
        if (owned[page] == (be16(inode + page * 2) == 3)) return Inconsistent;
    return Ok;
}

Error ControllerPak::ready(int port) {
    if (port < 0 || port >= 4) return Invalid;
    auto& slot = slots_[port];
    if (!slot.present) return NoPak;
    if (slot.loaded) return slot.error;
    slot.loaded = true;
    const auto path = path_for(port);
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec) return slot.error = ControllerFailure;
    if (!exists) {
        const Image image = blank();
        if (!atomic_replace(path, image.data(), image.size()))
            return slot.error = ControllerFailure;
        slot.image = image;
        return slot.error = Ok;
    }
    if (std::filesystem::file_size(path, ec) != image_size || ec)
        return slot.error = IdFatal;
    std::ifstream stream(path, std::ios::binary);
    if (!stream.read(reinterpret_cast<char*>(slot.image.data()), slot.image.size()))
        return slot.error = ControllerFailure;
    return slot.error = validate(slot.image);
}

Error ControllerPak::commit(int port, const Image& next) {
    if (validate(next) != Ok) return Inconsistent;
    if (!atomic_replace(path_for(port), next.data(), next.size())) return ControllerFailure;
    slots_[port].image = next;
    return Ok;
}

Error ControllerPak::init(int port) {
    std::lock_guard lock(mutex_);
    const Error result = ready(port);
    trace("INIT", port, -1, 0, 0, result);
    return result;
}

int ControllerPak::find_in(const Image& image, const Identity& identity) {
    for (int i = 0; i < directory_entries; ++i) {
        const auto* dir = image.data() + directory_offset + i * block_size;
        if (occupied(dir) && identity_at(dir) == identity) return i;
    }
    return -1;
}

Error ControllerPak::find(int port, const Identity& identity, int& file_no) {
    std::lock_guard lock(mutex_);
    file_no = -1;
    const Error status = ready(port);
    if (status != Ok) return status;
    file_no = find_in(slots_[port].image, identity);
    const Error result = file_no < 0 ? Invalid : Ok;
    trace("FIND", port, file_no, 0, 0, result, identity_hash(identity));
    return result;
}

Error ControllerPak::allocate(int port, const Identity& identity, int size, int& file_no) {
    std::lock_guard lock(mutex_);
    file_no = -1;
    const Error status = ready(port);
    if (status != Ok) return status;
    if (!identity.company_code || !identity.game_code || size <= 0 ||
        size > int((total_pages - first_data_page) * page_size)) return Invalid;
    auto& image = slots_[port].image;
    if (find_in(image, identity) >= 0) return Exists;
    int entry = -1;
    for (int i = 0; i < directory_entries; ++i) {
        if (!occupied(image.data() + directory_offset + i * block_size)) {
            entry = i;
            break;
        }
    }
    if (entry < 0) return DirectoryFull;
    const int required = (size + int(page_size) - 1) / int(page_size);
    std::array<uint8_t, total_pages> pages{};
    int found = 0;
    for (int page = first_data_page; page < total_pages; ++page)
        if (be16(image.data() + inode_offset + page * 2) == 3 && found < required)
            pages[found++] = uint8_t(page);
    if (found != required) return DataFull;
    Image next = image;
    auto* inode = next.data() + inode_offset;
    for (int i = 0; i < required; ++i)
        put16(inode + pages[i] * 2, i + 1 == required ? 1 : pages[i + 1]);
    inode[1] = inode_sum(inode);
    std::copy_n(inode, page_size, next.data() + mirror_offset);
    auto* dir = next.data() + directory_offset + entry * block_size;
    put32(dir, identity.game_code);
    put16(dir + 4, identity.company_code);
    put16(dir + 6, pages[0]);
    std::copy(identity.ext_name.begin(), identity.ext_name.end(), dir + 12);
    std::copy(identity.game_name.begin(), identity.game_name.end(), dir + 16);
    const Error result = commit(port, next);
    if (result == Ok) file_no = entry;
    trace("ALLOCATE", port, file_no, 0, size, result, identity_hash(identity));
    return result;
}

Error ControllerPak::file_state(int port, int file_no, FileState& state) {
    std::lock_guard lock(mutex_);
    const Error status = ready(port);
    if (status != Ok) return status;
    std::array<uint8_t, total_pages> pages{};
    int count = 0;
    const Error result = chain(slots_[port].image, file_no, pages, count);
    if (result != Ok) return result;
    state.identity = identity_at(slots_[port].image.data() + directory_offset + file_no * block_size);
    state.size = uint32_t(count * page_size);
    return Ok;
}

Error ControllerPak::free_blocks(int port, int& bytes) {
    std::lock_guard lock(mutex_);
    const Error status = ready(port);
    if (status != Ok) return status;
    int count = 0;
    const auto& image = slots_[port].image;
    for (int page = first_data_page; page < total_pages; ++page)
        if (be16(image.data() + inode_offset + page * 2) == 3) ++count;
    bytes = count * int(page_size);
    return Ok;
}

Error ControllerPak::num_files(int port, int& maximum, int& used) {
    std::lock_guard lock(mutex_);
    const Error status = ready(port);
    if (status != Ok) return status;
    maximum = directory_entries;
    used = 0;
    for (int i = 0; i < directory_entries; ++i)
        used += occupied(slots_[port].image.data() + directory_offset + i * block_size);
    return Ok;
}

Error ControllerPak::read(int port, int file_no, int offset, std::span<uint8_t> data) {
    std::lock_guard lock(mutex_);
    const Error status = ready(port);
    if (status != Ok) return status;
    if (offset < 0 || offset % block_size || data.empty() ||
        data.size() % block_size || data.size() > image_size) return Invalid;
    std::array<uint8_t, total_pages> pages{};
    int count = 0;
    const Error chain_status = chain(slots_[port].image, file_no, pages, count);
    if (chain_status != Ok) return chain_status;
    if (!(slots_[port].image[directory_offset + file_no * block_size + 8] & 2))
        return BadData;
    if (size_t(offset) > size_t(count) * page_size ||
        data.size() > size_t(count) * page_size - size_t(offset)) return Invalid;
    for (size_t i = 0; i < data.size(); ++i) {
        const size_t pos = size_t(offset) + i;
        data[i] = slots_[port].image[size_t(pages[pos / page_size]) * page_size + pos % page_size];
    }
    trace("READ", port, file_no, offset, int(data.size()), Ok,
          identity_hash(identity_at(slots_[port].image.data() + directory_offset + file_no * block_size)));
    return Ok;
}

Error ControllerPak::write(int port, int file_no, int offset, std::span<const uint8_t> data) {
    std::lock_guard lock(mutex_);
    const Error status = ready(port);
    if (status != Ok) return status;
    if (offset < 0 || offset % block_size || data.empty() ||
        data.size() % block_size || data.size() > image_size) return Invalid;
    std::array<uint8_t, total_pages> pages{};
    int count = 0;
    const Error chain_status = chain(slots_[port].image, file_no, pages, count);
    if (chain_status != Ok) return chain_status;
    if (size_t(offset) > size_t(count) * page_size ||
        data.size() > size_t(count) * page_size - size_t(offset)) return Invalid;
    Image next = slots_[port].image;
    for (size_t i = 0; i < data.size(); ++i) {
        const size_t pos = size_t(offset) + i;
        next[size_t(pages[pos / page_size]) * page_size + pos % page_size] = data[i];
    }
    next[directory_offset + file_no * block_size + 8] |= 2; // DIR_STATUS_OCCUPIED
    const Error result = commit(port, next);
    trace("WRITE", port, file_no, offset, int(data.size()), result,
          identity_hash(identity_at(next.data() + directory_offset + file_no * block_size)));
    return result;
}

Error ControllerPak::remove(int port, const Identity& identity) {
    std::lock_guard lock(mutex_);
    const Error status = ready(port);
    if (status != Ok) return status;
    if (!identity.company_code || !identity.game_code) return Invalid;
    const int file = find_in(slots_[port].image, identity);
    if (file < 0) return Invalid;
    std::array<uint8_t, total_pages> pages{};
    int count = 0;
    const Error chain_status = chain(slots_[port].image, file, pages, count);
    if (chain_status != Ok) return chain_status;
    Image next = slots_[port].image;
    auto* inode = next.data() + inode_offset;
    for (int i = 0; i < count; ++i) {
        put16(inode + pages[i] * 2, 3);
        std::fill_n(next.data() + size_t(pages[i]) * page_size, page_size, 0);
    }
    inode[1] = inode_sum(inode);
    std::copy_n(inode, page_size, next.data() + mirror_offset);
    std::fill_n(next.data() + directory_offset + file * block_size, block_size, 0);
    const Error result = commit(port, next);
    trace("DELETE", port, file, 0, 0, result, identity_hash(identity));
    return result;
}

Error ControllerPak::repair_id(int port) {
    std::lock_guard lock(mutex_);
    const Error status = ready(port);
    if (status == Ok || status == NoPak || status == Invalid) return status;
    if (status != IdFatal) return status;
    const Image& image = slots_[port].image;
    const uint8_t* candidate = nullptr;
    for (int block : id_blocks) {
        const auto* id = image.data() + block * block_size;
        if (be16(id + 24) != 1 || id[26] != 1 ||
            be16(id + 28) != id_sum(id, false) ||
            be16(id + 30) != id_sum(id, true)) continue;
        int copies = 0;
        for (int other : id_blocks)
            copies += std::memcmp(id, image.data() + other * block_size, block_size) == 0;
        if (copies >= 2) { candidate = id; break; }
    }
    if (!candidate) return IdFatal;
    Image next = image;
    for (int block : id_blocks)
        std::copy_n(candidate, block_size, next.data() + block * block_size);
    if (validate(next) != Ok) return IdFatal;
    const Error result = commit(port, next);
    if (result == Ok) slots_[port].error = Ok;
    trace("REPAIR", port, -1, 0, 0, result);
    return result;
}

Error ControllerPak::id_and_label(int port, std::array<uint8_t, 32>& id,
                                  std::array<uint8_t, 32>& label) {
    std::lock_guard lock(mutex_);
    const Error status = ready(port);
    if (status != Ok) return status;
    std::copy_n(slots_[port].image.data() + block_size, 32, id.begin());
    std::copy_n(slots_[port].image.data() + 7 * block_size, 32, label.begin());
    return Ok;
}

} // namespace sbk::pfs
