#include "storage.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace sbk::savestate::storage {
namespace fs = std::filesystem;
namespace {
std::atomic_uint64_t temp_counter{0};

std::string temp_prefix(const fs::path& target) { return "." + target.filename().string() + ".tmp-"; }

fs::path temp_path(const fs::path& target) {
#ifdef _WIN32
    const unsigned long pid = GetCurrentProcessId();
#else
    const unsigned long pid = static_cast<unsigned long>(getpid());
#endif
    return target.parent_path() / (temp_prefix(target) + std::to_string(pid) + "-" + std::to_string(++temp_counter));
}

std::string errno_text(const char* what) { return std::string(what) + ": " + std::strerror(errno); }

#ifndef _WIN32
bool write_all(int fd, const uint8_t* data, size_t size) {
    while (size) {
        const ssize_t written = ::write(fd, data, size);
        if (written < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        data += written;
        size -= size_t(written);
    }
    return true;
}
#endif
}

bool write_atomic(const fs::path& target, std::span<const uint8_t> bytes, std::string& error, WriteFault fault) {
    std::error_code ec;
    const fs::path directory = target.parent_path().empty() ? fs::path(".") : target.parent_path();
    fs::create_directories(directory, ec);
    if (ec) { error = "create directory " + directory.string() + ": " + ec.message(); return false; }
    const fs::path temp = temp_path(target.parent_path().empty() ? fs::path(".") / target : target);
#ifdef _WIN32
    // Best effort on Windows: flushed temp + MoveFileEx(REPLACE | WRITE_THROUGH).
    HANDLE file = fault == WriteFault::CreateTemp ? INVALID_HANDLE_VALUE
        : CreateFileW(temp.wstring().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { error = "create temporary file"; return false; }
    bool ok = fault != WriteFault::Write;
    size_t done = 0;
    while (ok && done < bytes.size()) {
        DWORD written = 0;
        const DWORD chunk = DWORD(std::min<size_t>(bytes.size() - done, 1u << 30));
        ok = WriteFile(file, bytes.data() + done, chunk, &written, nullptr) && written;
        done += written;
    }
    if (ok) ok = fault != WriteFault::Sync && FlushFileBuffers(file);
    CloseHandle(file);
    if (ok) ok = fault != WriteFault::Rename &&
        MoveFileExW(temp.wstring().c_str(), target.wstring().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) {
        error = "write/flush/replace failed";
        DeleteFileW(temp.wstring().c_str());
        return false;
    }
    return true;
#else
    const int fd = fault == WriteFault::CreateTemp ? (errno = EACCES, -1)
        : ::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (fd < 0) { error = errno_text("create temporary file"); return false; }
    auto abandon = [&](std::string why) {
        ::close(fd);
        ::unlink(temp.c_str());
        error = std::move(why);
        return false;
    };
    if (fault == WriteFault::Write) {
        // Simulate a short write (e.g. ENOSPC): part of the data reaches the temp file.
        write_all(fd, bytes.data(), bytes.size() / 2);
        errno = ENOSPC;
        return abandon(errno_text("write"));
    }
    if (!write_all(fd, bytes.data(), bytes.size())) return abandon(errno_text("write"));
    if (fault == WriteFault::Sync) { errno = EIO; return abandon(errno_text("fsync")); }
    if (::fsync(fd) != 0) return abandon(errno_text("fsync"));
    if (::close(fd) != 0) {
        ::unlink(temp.c_str());
        error = errno_text("close");
        return false;
    }
    if (fault == WriteFault::Rename || ::rename(temp.c_str(), target.c_str()) != 0) {
        if (fault == WriteFault::Rename) errno = EXDEV;
        error = errno_text("rename");
        ::unlink(temp.c_str());
        return false;
    }
    // Make the rename itself durable. A failure here cannot undo the replace;
    // the new file is complete either way, so it is reported but not fatal.
    const int dir = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir >= 0) {
        if (::fsync(dir) != 0) std::fprintf(stderr, "savestate: directory fsync failed: %s\n", std::strerror(errno));
        ::close(dir);
    }
    return true;
#endif
}

ReadStatus read_bounded(const fs::path& path, uint64_t max_bytes, std::vector<uint8_t>& out, std::string& error) {
    std::error_code ec;
    const auto status = fs::status(path, ec);
    if (ec || !fs::exists(status)) { error = "not found: " + path.string(); return ReadStatus::NotFound; }
    if (!fs::is_regular_file(status)) { error = "not a regular file: " + path.string(); return ReadStatus::IoError; }
    const uint64_t size = fs::file_size(path, ec);
    if (ec) { error = "size: " + ec.message(); return ReadStatus::IoError; }
    if (size > max_bytes) { error = "file exceeds " + std::to_string(max_bytes) + " bytes"; return ReadStatus::TooLarge; }
    std::ifstream input(path, std::ios::binary);
    if (!input) { error = "open failed: " + path.string(); return ReadStatus::IoError; }
    out.resize(size_t(size));
    input.read(reinterpret_cast<char*>(out.data()), std::streamsize(size));
    if (input.gcount() != std::streamsize(size)) { error = "short read"; return ReadStatus::IoError; }
    char extra;
    if (input.read(&extra, 1).gcount() != 0) { error = "file grew while reading"; return ReadStatus::IoError; }
    return ReadStatus::Ok;
}

std::optional<fs::path> slot_path(const fs::path& directory, std::string_view slot) {
    if (slot.empty() || slot.size() > 32) return std::nullopt;
    for (char c : slot) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return std::nullopt;
    }
    return directory / (std::string(slot) + std::string(kExtension));
}

void remove_stale_temporaries(const fs::path& target) {
    std::error_code ec;
    const fs::path directory = target.parent_path().empty() ? fs::path(".") : target.parent_path();
    const std::string prefix = temp_prefix(target);
    // Only temporaries old enough that no save can still be writing them.
    const auto cutoff = fs::file_time_type::clock::now() - std::chrono::minutes(1);
    for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        std::error_code entry_ec;
        if (name.rfind(prefix, 0) != 0 || !it->is_regular_file(entry_ec)) continue;
        if (fs::last_write_time(it->path(), entry_ec) < cutoff && !entry_ec) fs::remove(it->path(), entry_ec);
    }
}
}
