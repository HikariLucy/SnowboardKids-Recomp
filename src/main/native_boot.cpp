#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include "librecomp/game.hpp"
#include "librecomp/rsp.hpp"
#include "recompui/renderer.h"
#include "recompui/program_config.h"
#include "recomp_theme.h"
#include "ultramodern/ultramodern.hpp"

namespace sbk {
void register_overlays();
}

extern RspUcodeFunc aspMain;
extern "C" void recomp_entrypoint(uint8_t* rdram, recomp_context* ctx);
gpr get_entrypoint_address();

void traced_entrypoint(uint8_t* rdram, recomp_context* ctx);

// RecompFrontend expects these program-owned globals.
// Keep their names and linkage identical to the working frontend contract.
SDL_Window* window = nullptr;

std::vector<recomp::GameEntry> supported_games = {
    {
        .rom_hash = 0xF384619787B78D4BULL,
        .internal_name = "SNOWBOARD KIDS",
        .display_name = "Snowboard Kids",
        .game_id = u8"snowboardkids.n64.us",
        .mod_game_id = "",
        .save_type = recomp::SaveType::None,
        .is_enabled = true,
        .has_compressed_code = false,
        .entrypoint_address = get_entrypoint_address(),
        .entrypoint = traced_entrypoint,
    },
};

namespace {

void host_message_box(const char* msg) {
    std::fprintf(stderr, "[runtime] %s\n", msg);
}

ultramodern::gfx_callbacks_t::gfx_data_t create_gfx() {
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        std::exit(EXIT_FAILURE);
    }

    std::printf("SDL video driver: %s\n", SDL_GetCurrentVideoDriver());
    return nullptr;
}

ultramodern::renderer::WindowHandle create_window(ultramodern::gfx_callbacks_t::gfx_data_t) {
    constexpr int width = 1280;
    constexpr int height = 720;

    window = SDL_CreateWindow(
        "Snowboard Kids: Recompiled — first boot",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width,
        height,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN
    );

    if (window == nullptr) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        std::exit(EXIT_FAILURE);
    }

    std::printf("SDL/Vulkan window created: %dx%d\n", width, height);
    return window;
}

void update_gfx(ultramodern::gfx_callbacks_t::gfx_data_t) {
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            ultramodern::quit();
        }
    }
}

void queue_samples(int16_t*, size_t) {
    // First-boot validation intentionally discards host audio.
}

size_t get_frames_remaining() {
    return 0;
}

void set_frequency(uint32_t freq) {
    std::printf("N64 audio frequency requested: %" PRIu32 " Hz\n", freq);
}

void poll_input() {
}

bool get_input(int controller_num, uint16_t* buttons, float* x, float* y) {
    if (controller_num != 0) {
        return false;
    }

    *buttons = 0;
    *x = 0.0f;
    *y = 0.0f;
    return true;
}

void set_rumble(int, bool) {
}

ultramodern::input::connected_device_info_t get_connected_device_info(int controller_num) {
    if (controller_num == 0) {
        return {
            .connected_device = ultramodern::input::Device::Controller,
            .connected_pak = ultramodern::input::Pak::None,
        };
    }

    return {
        .connected_device = ultramodern::input::Device::None,
        .connected_pak = ultramodern::input::Pak::None,
    };
}

RspUcodeFunc* get_rsp_microcode(const OSTask* task) {
    if (task->t.type == M_AUDTASK) {
        return aspMain;
    }

    std::fprintf(stderr, "Unknown RSP task type: %" PRIu32 "\n", task->t.type);
    return nullptr;
}

std::string get_game_thread_name(const OSThread* thread) {
    return "N64-" + std::to_string(thread->id);
}

 } // namespace

void traced_entrypoint(uint8_t* rdram, recomp_context* ctx) {
    std::puts(">>> ENTERING SNOWBOARD KIDS RECOMP_ENTRYPOINT");
    std::fflush(stdout);

    recomp_entrypoint(rdram, ctx);

    std::puts("<<< SNOWBOARD KIDS RECOMP_ENTRYPOINT RETURNED");
    std::fflush(stdout);
}

namespace {

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
        std::fprintf(stderr, "usage: SnowboardKidsRecompiled <snowboardkids.z64>\n");
        return EXIT_FAILURE;
    }

    const std::filesystem::path rom_path = argv[1];
    const std::filesystem::path runtime_dir =
        std::filesystem::current_path() / "runtime-data";

    std::error_code ec;
    std::filesystem::create_directories(runtime_dir, ec);
    if (ec) {
        std::fprintf(stderr, "Failed to create runtime data directory: %s\n", ec.message().c_str());
        return EXIT_FAILURE;
    }

    recompui::programconfig::set_program_name("Snowboard Kids: Recompiled");
    recompui::programconfig::set_program_id(u8"snowboardkids-recompiled");
    snowboardkids::theme::apply();

    recomp::register_config_path(runtime_dir);

    const recomp::GameEntry& game = supported_games[0];

    if (!recomp::register_game(game)) {
        std::fprintf(stderr, "Failed to register Snowboard Kids\n");
        return EXIT_FAILURE;
    }

    std::u8string game_id = game.game_id;
    const auto validation = recomp::select_rom(rom_path, game_id);
    std::printf("ROM validation: %s\n", validation_error_name(validation));

    if (validation != recomp::RomValidationError::Good) {
        return EXIT_FAILURE;
    }

    sbk::register_overlays();

    recomp::rsp::callbacks_t rsp_callbacks{
        .get_rsp_microcode = get_rsp_microcode,
    };

    ultramodern::renderer::callbacks_t renderer_callbacks{
        .create_render_context =
            [](uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool developer_mode) {
                return recompui::renderer::create_render_context(
                    rdram,
                    window_handle,
                    ultramodern::renderer::PresentationMode::PresentEarly,
                    developer_mode
                );
            },
    };

    ultramodern::audio_callbacks_t audio_callbacks{
        .queue_samples = queue_samples,
        .get_frames_remaining = get_frames_remaining,
        .set_frequency = set_frequency,
    };

    ultramodern::input::callbacks_t input_callbacks{
        .poll_input = poll_input,
        .get_input = get_input,
        .set_rumble = set_rumble,
        .get_connected_device_info = get_connected_device_info,
    };

    ultramodern::gfx_callbacks_t gfx_callbacks{
        .create_gfx = create_gfx,
        .create_window = create_window,
        .update_gfx = update_gfx,
    };

    ultramodern::events::callbacks_t events_callbacks{
        .vi_callback = nullptr,
        .gfx_init_callback = nullptr,
    };

    ultramodern::error_handling::callbacks_t error_callbacks{
        .message_box = host_message_box,
    };

    ultramodern::threads::callbacks_t thread_callbacks{
        .get_game_thread_name = get_game_thread_name,
    };

    std::printf("Generated entrypoint: 0x%08" PRIX32 "\n",
                static_cast<uint32_t>(game.entrypoint_address));
    std::puts("Starting N64ModernRuntime...");
    std::fflush(stdout);

    // Start the game immediately for this diagnostic executable. A launcher
    // and persistent frontend flow come later.
    recomp::start_game(game_id, "");

    recomp::start(
        recomp::Version{0, 0, 1, "-boot"},
        {},
        rsp_callbacks,
        renderer_callbacks,
        audio_callbacks,
        input_callbacks,
        gfx_callbacks,
        events_callbacks,
        error_callbacks,
        thread_callbacks
    );

    SDL_Quit();
    return EXIT_SUCCESS;
}
