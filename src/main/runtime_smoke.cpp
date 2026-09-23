#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string_view>

#include "librecomp/game.hpp"

extern "C" void recomp_entrypoint(uint8_t* rdram, recomp_context* ctx);
gpr get_entrypoint_address();

namespace {

constexpr std::u8string_view kGameId = u8"snowboardkids.n64.us";

const char* validation_error_name(recomp::RomValidationError error) {
    switch (error) {
        case recomp::RomValidationError::Good: return "Good";
        case recomp::RomValidationError::FailedToOpen: return "FailedToOpen";
        case recomp::RomValidationError::NotARom: return "NotARom";
        case recomp::RomValidationError::IncorrectRom: return "IncorrectRom";
        case recomp::RomValidationError::NotYet: return "NotYet";
        case recomp::RomValidationError::IncorrectVersion: return "IncorrectVersion";
        case recomp::RomValidationError::OtherError: return "OtherError";
    }
    return "Unknown";
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: SnowboardKidsRuntimeSmoke <snowboardkids.z64>\n";
        return EXIT_FAILURE;
    }

    const recomp::GameEntry game{
        .rom_hash = 0xF384619787B78D4BULL,
        .internal_name = "SNOWBOARD KIDS",
        .display_name = "Snowboard Kids",
        .game_id = std::u8string{kGameId},
        .mod_game_id = "snowboardkids",
        .save_type = recomp::SaveType::None,
        .is_enabled = true,
        .has_compressed_code = false,
        .entrypoint_address = get_entrypoint_address(),
        .entrypoint = recomp_entrypoint,
    };

    if (game.entrypoint_address != static_cast<gpr>(static_cast<int32_t>(0x80000400u))) {
        std::cerr << "Unexpected generated entrypoint: 0x"
                  << std::hex << static_cast<uint64_t>(game.entrypoint_address) << "\n";
        return EXIT_FAILURE;
    }

    const std::filesystem::path smoke_dir =
        std::filesystem::temp_directory_path() / "snowboardkids-recomp-runtime-smoke";

    std::error_code ec;
    std::filesystem::remove_all(smoke_dir, ec);
    std::filesystem::create_directories(smoke_dir, ec);
    if (ec) {
        std::cerr << "Failed to create smoke-test config directory: "
                  << ec.message() << "\n";
        return EXIT_FAILURE;
    }

    recomp::register_config_path(smoke_dir);
    if (!recomp::register_game(game)) {
        std::cerr << "Failed to register Snowboard Kids GameEntry\n";
        return EXIT_FAILURE;
    }

    std::u8string game_id{kGameId};
    const auto validation = recomp::select_rom(argv[1], game_id);

    std::cout << "Snowboard Kids — N64ModernRuntime smoke test\n";
    std::cout << "============================================\n";
    std::cout << "GameEntry       : registered\n";
    std::cout << "Internal name   : " << game.internal_name << "\n";
    std::cout << "ROM hash        : 0xF384619787B78D4B\n";
    std::cout << "Entrypoint      : 0x80000400\n";
    std::cout << "Cartridge save  : None (Controller Pak/PFS handled separately)\n";
    std::cout << "ROM validation  : " << validation_error_name(validation) << "\n";

    if (validation != recomp::RomValidationError::Good) {
        std::filesystem::remove_all(smoke_dir, ec);
        return EXIT_FAILURE;
    }

    if (!recomp::load_stored_rom(game_id)) {
        std::cerr << "Runtime accepted the ROM but failed to load the stored copy\n";
        std::filesystem::remove_all(smoke_dir, ec);
        return EXIT_FAILURE;
    }

    const auto loaded_rom = recomp::get_rom();
    std::cout << "Runtime ROM load: OK (" << loaded_rom.size() << " bytes)\n";
    std::cout << "Result          : PASS\n";

    std::filesystem::remove_all(smoke_dir, ec);
    return EXIT_SUCCESS;
}
