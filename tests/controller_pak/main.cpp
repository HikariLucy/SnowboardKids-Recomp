#include "check.hpp"
#include "pfs/controller_pak.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using namespace sbk::pfs;

Identity identity(int marker) {
    Identity id;
    id.company_code = 0x4542;
    id.game_code = 0x4e534b45 + uint32_t(marker);
    id.game_name[0] = uint8_t(marker);
    return id;
}

std::vector<uint8_t> bytes(uint8_t marker, int size = 256) {
    std::vector<uint8_t> result(size);
    for (int i = 0; i < size; ++i) result[i] = uint8_t(marker + i);
    return result;
}

// Every replacement renames its temp image over the Pak; none may be left behind.
bool no_temp_files(const std::filesystem::path& root) {
    for (const auto& entry : std::filesystem::directory_iterator(root / "controller-paks"))
        if (entry.path().filename().string().find(".tmp-") != std::string::npos) return false;
    return true;
}

void normal(const std::filesystem::path& root) {
    ControllerPak pak(root);
    CHECK(pak.init(0) == Ok);
    int free = 0, maximum = 0, used = 0;
    CHECK(pak.free_blocks(0, free) == Ok && free == 123 * 256);
    CHECK(pak.num_files(0, maximum, used) == Ok && maximum == 16 && used == 0);
    int file = -1;
    CHECK(pak.find(0, identity(0), file) == Invalid && file == -1);
    CHECK(pak.allocate(0, identity(0), 0x7900, file) == Ok && file == 0);
    std::array<uint8_t, 32> unwritten{};
    CHECK(pak.read(0, file, 0, unwritten) == BadData);
    CHECK(pak.free_blocks(0, free) == Ok && free == 2 * 256);
    FileState state;
    CHECK(pak.file_state(0, file, state) == Ok && state.size == 0x7900);
    CHECK(state.identity == identity(0));
    const auto input = bytes(7, 0x78e0);
    CHECK(pak.write(0, file, 0, input) == Ok);
    std::vector<uint8_t> output(input.size());
    CHECK(pak.read(0, file, 0, output) == Ok && output == input);
    CHECK(pak.write(0, file, 0x7900, bytes(1)) == Invalid);
    CHECK(pak.read(0, file, -32, output) == Invalid);
    CHECK(pak.read(0, file, 1, output) == Invalid);
    CHECK(pak.read(0, file, 0, std::span<uint8_t>(output.data(), 1)) == Invalid);
    CHECK(pak.read(0, file, 0, std::span<uint8_t>()) == Invalid);
    CHECK(pak.allocate(0, identity(1), 3 * 256, file) == DataFull);
    CHECK(pak.remove(0, identity(0)) == Ok);
    CHECK(pak.free_blocks(0, free) == Ok && free == 123 * 256);
    CHECK(pak.allocate(0, identity(1), 123 * 256, file) == Ok);
    CHECK(pak.free_blocks(0, free) == Ok && free == 0);
    CHECK(pak.remove(0, identity(1)) == Ok);
    for (int i = 0; i < 16; ++i)
        CHECK(pak.allocate(0, identity(1000 + i), 256, file) == Ok);
    CHECK(pak.allocate(0, identity(2000), 256, file) == DirectoryFull);
    for (int i = 0; i < 16; ++i)
        CHECK(pak.remove(0, identity(1000 + i)) == Ok);
    pak.set_present(1, false);
    CHECK(pak.init(1) == NoPak);
    CHECK(!std::filesystem::exists(pak.path_for(1)));
    pak.set_present(1, true);
    CHECK(pak.init(1) == Ok);
    int p1 = -1, p2 = -1;
    CHECK(pak.allocate(0, identity(2), 256, p1) == Ok);
    CHECK(pak.allocate(1, identity(3), 256, p2) == Ok);
    CHECK(pak.write(0, p1, 0, bytes(1)) == Ok);
    CHECK(pak.write(1, p2, 0, bytes(2)) == Ok);
    std::vector<uint8_t> a(256), b(256);
    CHECK(pak.read(0, p1, 0, a) == Ok && a == bytes(1));
    CHECK(pak.read(1, p2, 0, b) == Ok && b == bytes(2));
    pak.set_present(1, false);
    CHECK(pak.read(1, p2, 0, b) == NoPak);
    CHECK(pak.read(0, p1, 0, a) == Ok && a == bytes(1));
    pak.set_present(1, true);
    CHECK(pak.read(1, p2, 0, b) == Ok && b == bytes(2));
    CHECK(pak.remove(0, identity(2)) == Ok);
    CHECK(pak.remove(1, identity(3)) == Ok);
    for (int cycle = 0; cycle < 100; ++cycle) {
        CHECK(pak.allocate(0, identity(cycle + 10), 256, file) == Ok);
        CHECK(pak.write(0, file, 0, bytes(uint8_t(cycle))) == Ok);
        CHECK(pak.read(0, file, 0, a) == Ok && a == bytes(uint8_t(cycle)));
        CHECK(pak.remove(0, identity(cycle + 10)) == Ok);
    }
    CHECK(pak.free_blocks(0, free) == Ok && free == 123 * 256);
    CHECK(pak.allocate(0, identity(3000), 256, file) == Ok);
    CHECK(pak.write(0, file, 224, bytes(3, 32)) == Ok);
    CHECK(pak.write(0, file, 256, bytes(3, 32)) == Invalid);
    CHECK(pak.read(0, file, 256, std::span<uint8_t>(a.data(), 32)) == Invalid);
    CHECK(pak.remove(0, identity(3000)) == Ok);
    std::cout << "PASS backend allocation, directory full, boundaries, isolation, 100 cycles\n";
}

void process_a(const std::filesystem::path& root) {
    ControllerPak pak(root);
    CHECK(pak.init(0) == Ok);
    int file;
    CHECK(pak.allocate(0, identity(9), 256, file) == Ok);
    CHECK(pak.write(0, file, 0, bytes(11)) == Ok);
}
void process_b(const std::filesystem::path& root) {
    ControllerPak pak(root);
    CHECK(pak.init(0) == Ok);
    int file;
    CHECK(pak.find(0, identity(9), file) == Ok);
    std::vector<uint8_t> output(256);
    CHECK(pak.read(0, file, 0, output) == Ok && output == bytes(11));
    CHECK(pak.write(0, file, 0, bytes(22)) == Ok);
}
void process_c(const std::filesystem::path& root) {
    ControllerPak pak(root);
    CHECK(pak.init(0) == Ok);
    int file;
    CHECK(pak.find(0, identity(9), file) == Ok);
    std::vector<uint8_t> output(256);
    CHECK(pak.read(0, file, 0, output) == Ok && output == bytes(22));
    for (int i = 0; i < 100; ++i) {
        ControllerPak reopened(root);
        CHECK(reopened.init(0) == Ok);
        CHECK(reopened.find(0, identity(9), file) == Ok);
        CHECK(reopened.read(0, file, 0, output) == Ok && output == bytes(22));
    }
    CHECK(no_temp_files(root));
    std::cout << "PASS cross-process A/B/C and 100 reopen cycles\n";
}

// Non-ASCII directory created in-process (not via narrow argv): the backend
// must persist, replace and reopen through std::filesystem / wide Win32 APIs.
void unicode_paths(const std::filesystem::path& root) {
    const auto dir = root / std::filesystem::path(u8"Snowboard Kids PFS \u00f3 \u00f1");
    int file;
    {
        ControllerPak pak(dir);
        CHECK(pak.init(0) == Ok);
        CHECK(pak.allocate(0, identity(20), 256, file) == Ok);
        CHECK(pak.write(0, file, 0, bytes(21)) == Ok);
        CHECK(pak.write(0, file, 0, bytes(23)) == Ok); // replaces the existing image
    }
    ControllerPak reopened(dir);
    CHECK(reopened.init(0) == Ok);
    CHECK(reopened.find(0, identity(20), file) == Ok);
    std::vector<uint8_t> output(256);
    CHECK(reopened.read(0, file, 0, output) == Ok && output == bytes(23));
    CHECK(std::filesystem::file_size(dir / "controller-paks" / "port1.mpk") == image_size);
    CHECK(no_temp_files(dir));
    std::cout << "PASS non-ASCII path persist, replace and reopen\n";
}

void corrupt(const std::filesystem::path& root, const std::string& type) {
    ControllerPak original(root);
    CHECK(original.init(0) == Ok);
    if (type == "chain") {
        int file_no;
        CHECK(original.allocate(0, identity(5), 256, file_no) == Ok);
    }
    const auto path = original.path_for(0);
    std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
    CHECK(file);
    if (type == "truncate") {
        file.close();
        std::filesystem::resize_file(path, 1);
    } else if (type == "wrong-size") {
        file.close();
        std::filesystem::resize_file(path, image_size + 1);
    } else if (type == "chain") {
        std::array<uint8_t, page_size> inode{};
        file.seekg(page_size);
        file.read(reinterpret_cast<char*>(inode.data()), inode.size());
        inode[10] = 0;
        inode[11] = 5; // page 5 links to itself
        unsigned sum = 0;
        for (int i = first_data_page * 2; i < int(page_size); ++i) sum += inode[i];
        inode[1] = uint8_t(sum);
        file.seekp(page_size);
        file.write(reinterpret_cast<const char*>(inode.data()), inode.size());
        file.seekp(2 * page_size);
        file.write(reinterpret_cast<const char*>(inode.data()), inode.size());
        file.close();
    } else if (type == "id-all") {
        for (int block : {1, 3, 4, 6}) {
            file.seekp(block * int(block_size) + 28);
            file.put(char(0xff));
        }
        file.close();
    } else {
        int offset = type == "id" ? 32 + 28 : type == "inode" ? 256 + 11 : 768 + 4;
        file.seekp(offset);
        file.put(char(0x80));
        file.close();
    }
    auto read_image = [&] {
        std::ifstream input(path, std::ios::binary);
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(input), {});
    };
    const auto before = read_image();
    ControllerPak reopened(root);
    CHECK(reopened.init(0) != Ok);
    if (type == "id-all") CHECK(reopened.repair_id(0) == IdFatal);
    CHECK(read_image() == before);
    std::cout << "PASS corruption " << type << " preserved\n";
}

void run_hle(const std::filesystem::path& root);
std::string compiler_id();
int run_suite(const char* argv0);

// No arguments: the whole suite, each mode in its own process. With a mode:
// one isolated check, as the suite (or a developer) invokes it.
int main(int argc, char** argv) {
    if (argc == 1) return run_suite(argv[0]);
    const std::string mode = argv[1];
    if (mode == "--compiler" && argc == 2) {
        std::cout << compiler_id() << "\n";
        return 0;
    }
    CHECK(argc == 3 || argc == 4);
    const std::filesystem::path root = argv[2];
    if (mode == "normal") normal(root);
    else if (mode == "a") process_a(root);
    else if (mode == "b") process_b(root);
    else if (mode == "c") process_c(root);
    else if (mode == "corrupt" && argc == 4) corrupt(root, argv[3]);
    else if (mode == "unicode") unicode_paths(root);
    else if (mode == "hle") run_hle(root);
    else CHECK(!"unknown mode");
}
