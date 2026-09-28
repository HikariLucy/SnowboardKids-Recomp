#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <span>

namespace sbk::pfs {

// Values are from the game's PR/os_pfs.h. Geometry follows its libultra.
enum Error : int {
    Ok = 0, NoPak = 1, NewPack = 2, Inconsistent = 3,
    ControllerFailure = 4, Invalid = 5, BadData = 6,
    DataFull = 7, DirectoryFull = 8, Exists = 9,
    IdFatal = 10, WrongDevice = 11,
};

constexpr std::size_t image_size = 32768;
constexpr std::size_t block_size = 32;
constexpr std::size_t page_size = 256;
constexpr int directory_entries = 16;
constexpr int first_data_page = 5;
constexpr int total_pages = 128;

struct Identity {
    uint16_t company_code{};
    uint32_t game_code{};
    std::array<uint8_t, 16> game_name{};
    std::array<uint8_t, 4> ext_name{};
    bool operator==(const Identity&) const = default;
};

struct FileState {
    Identity identity{};
    uint32_t size{};
};

// One service owns four independent raw images. Mutations commit to disk
// before replacing the in-memory image; snapshots never own this object.
class ControllerPak {
public:
    explicit ControllerPak(std::filesystem::path config_directory);
    void set_present(int port, bool present);
    Error init(int port);
    Error find(int port, const Identity& identity, int& file_no);
    Error allocate(int port, const Identity& identity, int size, int& file_no);
    Error file_state(int port, int file_no, FileState& state);
    Error free_blocks(int port, int& bytes);
    Error num_files(int port, int& maximum, int& used);
    Error read(int port, int file_no, int offset, std::span<uint8_t> data);
    Error write(int port, int file_no, int offset, std::span<const uint8_t> data);
    Error remove(int port, const Identity& identity);
    Error repair_id(int port);
    Error id_and_label(int port, std::array<uint8_t, 32>& id,
                       std::array<uint8_t, 32>& label);
    std::filesystem::path path_for(int port) const;

private:
    using Image = std::array<uint8_t, image_size>;
    struct Slot {
        bool present = true;
        bool loaded = false;
        Error error = Ok;
        Image image{};
    };
    std::filesystem::path directory_;
    std::array<Slot, 4> slots_{};
    mutable std::mutex mutex_;

    Error ready(int port);
    Error commit(int port, const Image& next);
    static Image blank();
    static Error validate(const Image& image);
    static int find_in(const Image& image, const Identity& identity);
    static Error chain(const Image& image, int file_no, std::array<uint8_t, total_pages>& pages, int& count);
};

} // namespace sbk::pfs
