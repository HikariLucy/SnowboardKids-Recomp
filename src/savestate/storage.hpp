#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// P6 savestate storage: atomic replacement of a slot file and bounded reads.
// A save never writes over the final file: it writes a unique temporary file
// in the same directory, flushes and fsyncs it, atomically renames it over the
// target and fsyncs the directory (POSIX). Any failure leaves the previous
// file untouched and removes the temporary file.
namespace sbk::savestate::storage {

// Test-only failure injection points for write_atomic.
enum class WriteFault : uint8_t { None, CreateTemp, Write, Sync, Rename };

bool write_atomic(const std::filesystem::path& target, std::span<const uint8_t> bytes, std::string& error,
    WriteFault fault = WriteFault::None);

enum class ReadStatus : uint8_t { Ok, NotFound, TooLarge, IoError };
// Reads at most `max_bytes`; the size is checked before any allocation.
ReadStatus read_bounded(const std::filesystem::path& path, uint64_t max_bytes, std::vector<uint8_t>& out,
    std::string& error);

// Slot identifiers are [a-z0-9_-]{1,32}; the file is <dir>/<slot>.sbks.
// The API takes a slot so more slots can be added without a format change.
constexpr std::string_view kQuickSlot = "quick";
constexpr std::string_view kExtension = ".sbks";
std::optional<std::filesystem::path> slot_path(const std::filesystem::path& directory, std::string_view slot);

// Removes temporary files (older than a minute) that an interrupted save of
// `target` left behind.
void remove_stale_temporaries(const std::filesystem::path& target);
}
