#include "config_tabs.hpp"
#include "virtual_pad.hpp"
#include "input_ports.hpp"
#include "audio_progress.hpp"
#include "pfs/hle.hpp"
#include "quiescence/probe.hpp"
#ifdef SBK_CONTINUATIONS
#include "continuation/execution.hpp"
#include "continuation/runtime_owner.hpp"
#include "savestate/dev_trigger.hpp"
#include "savestate/driver.hpp"
#include "savestate/host_audio.hpp"
#include "savestate/toast.hpp"
#endif
#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cinttypes>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <execinfo.h>
#include <unistd.h>
#endif

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include "nfd.h"
#include "sbk_version.h"

#include "librecomp/game.hpp"
#include "librecomp/rsp.hpp"
#include "recompinput/input_events.h"
#include "recompinput/input_state.h"
#include "recompinput/players.h"
#include "recompinput/profiles.h"
#include "recompui/config.h"
#include "recompui/renderer.h"
#include "recompui/program_config.h"
#include "recompui/recompui.h"
#include "util/file.h"
#include "recomp_theme.h"
#include "ui/menu.hpp"
#include "ui/first_run_window.hpp"
#include "ultramodern/ultramodern.hpp"
#include "ultramodern/config.hpp"
#include "module/module_loader.hpp"

namespace sbk {
void register_overlays();
module::GameModule g_game_module;
}

#if !defined(SBK_ROM_FREE_ENGINE)
extern RspUcodeFunc aspMain;
extern "C" void recomp_entrypoint(uint8_t* rdram, recomp_context* ctx);
#endif

gpr get_entrypoint_address() {
    if (sbk::g_game_module.is_loaded()) {
        return static_cast<gpr>(static_cast<int32_t>(sbk::g_game_module.api()->entrypoint_address));
    }
    return static_cast<gpr>(static_cast<int32_t>(0x80000400u));
}

static bool frontend_preview = false;
void traced_entrypoint(uint8_t* rdram, recomp_context* ctx);

// RecompFrontend expects these program-owned globals.
// Keep their names and linkage identical to the working frontend contract.
SDL_Window* window = nullptr;

// Published by the SDL/frontend thread; consumed by the guest input callbacks.
// Port zero remains keyboard-capable during the single-player setup flow.
static std::atomic<uint8_t> connected_port_mask{1};
static std::atomic<bool> single_player_input{true};

static void publish_controller_ports() {
    int controller_count = 0;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) ++controller_count;
    }
    const bool single = recompinput::players::is_single_player_mode();
    uint8_t assigned_attached = 0;
    if (!single) {
        for (int port = 0; port < 4; ++port) {
            if (!recompinput::players::get_player_is_assigned(port)) continue;
            const auto& player = recompinput::players::get_player(port);
            if (player.keyboard_enabled ||
                (player.controller && SDL_GameControllerGetAttached(player.controller))) {
                assigned_attached |= uint8_t(1u << port);
            }
        }
    }
    single_player_input.store(single, std::memory_order_release);
    const uint8_t mask = sbk::input_ports::presence_mask(single, controller_count, assigned_attached);
    if (std::getenv("SBK_TEST_MULTI_PAD_FILE")) {
        static uint8_t last_mask = 0xff;
        if (mask != last_mask) {
            std::fprintf(stderr, "VMULTI ports mask=%02x single=%d\n", mask, int(single));
            last_mask = mask;
        }
    }
    connected_port_mask.store(mask, std::memory_order_release);
    static uint8_t previous_pfs_mask = 0xff;
    if (mask != previous_pfs_mask) {
        const bool absent_p1 = std::getenv("SBK_PFS_ABSENT") &&
            std::strcmp(std::getenv("SBK_PFS_ABSENT"), "1") == 0;
        for (int port = 0; port < 4; ++port)
            sbk::pfs::set_port_present(port, (mask & (1u << port)) &&
                !(port == 0 && absent_p1));
        previous_pfs_mask = mask;
    }
}

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

#if defined(__linux__)
void crash_signal_handler(int signal_number) {
    constexpr int max_frames = 64;
    void* frames[max_frames];
    const int frame_count = backtrace(frames, max_frames);

    const char header[] = "\n=== SnowboardKidsRecompiled crash backtrace ===\n";
    write(STDERR_FILENO, header, sizeof(header) - 1);
    backtrace_symbols_fd(frames, frame_count, STDERR_FILENO);

    const char footer[] = "=== end crash backtrace ===\n";
    write(STDERR_FILENO, footer, sizeof(footer) - 1);

    _exit(128 + signal_number);
}

void install_crash_handlers() {
    std::signal(SIGSEGV, crash_signal_handler);
    std::signal(SIGABRT, crash_signal_handler);
}
#else
void install_crash_handlers() {
}
#endif

void initialize_controls(const std::filesystem::path& runtime_dir) {
    const std::filesystem::path controls_path = runtime_dir / "controls.json";
    const bool loaded_existing =
        recompinput::profiles::load_controls_config(controls_path);

    std::printf(
        "RecompInput controls: %s (%s)\n",
        loaded_existing ? "loaded existing config" : "created default bindings",
        controls_path.string().c_str()
    );
}

void apply_resolution_override() {
    if (const char* res_env = std::getenv("SBK_RESOLUTION")) {
        std::string res_str = res_env;
        for (auto& c : res_str) c = std::tolower(c);
        ultramodern::renderer::Resolution res = ultramodern::renderer::Resolution::Auto;
        if (res_str == "original" || res_str == "1x") {
            res = ultramodern::renderer::Resolution::Original;
        } else if (res_str == "2160p" || res_str == "4k" || res_str == "2160") {
            res = ultramodern::renderer::Resolution::P2160;
        } else if (res_str == "1080p" || res_str == "1080") {
            res = ultramodern::renderer::Resolution::P1080;
        } else if (res_str == "720p" || res_str == "720") {
            res = ultramodern::renderer::Resolution::P720;
        } else if (res_str == "auto") {
            res = ultramodern::renderer::Resolution::Auto;
        }
        recompui::config::get_graphics_config().set_option_value(recompui::config::graphics::options::res_option, static_cast<uint32_t>(res));
        recompui::config::get_graphics_config().apply_option_value(recompui::config::graphics::options::res_option);
        auto config = ultramodern::renderer::get_graphics_config();
        config.res_option = res;
        ultramodern::renderer::set_graphics_config(config);
        std::printf("Configured resolution from SBK_RESOLUTION: %s -> enum %d\n", res_env, static_cast<int>(res));
    }
}

void init_frontend_config(const recomp::GameEntry& game, bool mods_initialized) {
    // Minimal standard RecompFrontend configuration for first boot.
    // Game-specific settings can be added after the native boot path works.
    recompui::config::GeneralTabOptions general_options{};
    general_options.has_rumble_strength = true;
    general_options.has_gyro_sensitivity = false;
    general_options.has_mouse_sensitivity = false;

    sbk::ui::register_menu(frontend_preview);
    for (const auto tab : sbk::frontend::config_tabs(game.mod_game_id, mods_initialized)) {
        switch (tab) {
            case sbk::frontend::ConfigTab::General: recompui::config::create_general_tab(general_options); break;
            case sbk::frontend::ConfigTab::Graphics:
                recompui::config::create_graphics_tab();
                apply_resolution_override();
                break;
            case sbk::frontend::ConfigTab::Controls: recompui::config::create_controls_tab(); break;
            case sbk::frontend::ConfigTab::Sound: recompui::config::create_sound_tab("Audio"); break;
            case sbk::frontend::ConfigTab::Mods:
                // ModMenu requires the game mod id before the tab can build it.
                recompui::update_game_mod_id(game.mod_game_id);
                recompui::config::create_mods_tab();
                break;
        }
    }
    recompui::config::finalize();

    std::puts("RecompFrontend configuration finalized.");
}

ultramodern::renderer::PresentationMode selected_presentation_mode() {
    static const ultramodern::renderer::PresentationMode mode = []() {
        const char* value = std::getenv("SBK_PRESENT_MODE");
        if (value == nullptr || std::strcmp(value, "console") == 0) {
            return ultramodern::renderer::PresentationMode::Console;
        }
        if (std::strcmp(value, "skip-buffering") == 0) {
            return ultramodern::renderer::PresentationMode::SkipBuffering;
        }
        if (std::strcmp(value, "present-early") == 0) {
            return ultramodern::renderer::PresentationMode::PresentEarly;
        }

        std::fprintf(
            stderr,
            "Unknown SBK_PRESENT_MODE='%s'; using console.\n",
            value
        );
        return ultramodern::renderer::PresentationMode::Console;
    }();

    return mode;
}

const char* presentation_mode_name(ultramodern::renderer::PresentationMode mode) {
    switch (mode) {
        case ultramodern::renderer::PresentationMode::Console:
            return "console";
        case ultramodern::renderer::PresentationMode::SkipBuffering:
            return "skip-buffering";
        case ultramodern::renderer::PresentationMode::PresentEarly:
            return "present-early";
    }

    return "unknown";
}

void host_message_box(const char* msg) {
    std::fprintf(stderr, "[runtime] %s\n", msg);
}

static SDL_AudioCVT audio_convert{};
static SDL_AudioDeviceID audio_device = 0;
static uint32_t sample_rate = 48000;
static uint32_t output_sample_rate = 48000;
constexpr uint32_t input_channels = 2;
static uint32_t output_channels = 2;
constexpr uint32_t duplicated_input_frames = 4;
static uint32_t discarded_output_frames = 0;
static std::array<float, duplicated_input_frames * input_channels> duplicated_sample_buffer{};

#ifdef SBK_CONTINUATIONS
// P4/P5 semantic audio boundary (src/savestate/host_audio.hpp): a bounded
// mirror of submitted PCM selects the unconsumed backlog at a Frozen boundary.
// The SDL device itself is never serialized.
static sbk::savestate::audio::Boundary audio_boundary;
#endif

void update_audio_converter() {
    const int ret = SDL_BuildAudioCVT(
        &audio_convert,
        AUDIO_F32,
        input_channels,
        static_cast<int>(sample_rate),
        AUDIO_F32,
        output_channels,
        static_cast<int>(output_sample_rate)
    );

    if (ret < 0) {
        std::fprintf(stderr, "SDL_BuildAudioCVT failed: %s\n", SDL_GetError());
        std::exit(EXIT_FAILURE);
    }

    discarded_output_frames = duplicated_input_frames * output_sample_rate / sample_rate;
}

bool reset_audio(uint32_t output_freq) {
    SDL_AudioSpec desired{
        .freq = static_cast<int>(output_freq),
        .format = AUDIO_F32,
        .channels = static_cast<Uint8>(output_channels),
        .silence = 0,
        .samples = 0x100,
        .padding = 0,
        .size = 0,
        .callback = nullptr,
        .userdata = nullptr,
    };

    audio_device = SDL_OpenAudioDevice(nullptr, false, &desired, nullptr, 0);
    if (audio_device == 0) {
        std::fprintf(stderr, "SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_PauseAudioDevice(audio_device, 0);
    output_sample_rate = output_freq;
    update_audio_converter();
    std::printf("SDL audio device opened at %" PRIu32 " Hz\n", output_freq);
    return true;
}

ultramodern::gfx_callbacks_t::gfx_data_t create_gfx() {
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) < 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        std::exit(EXIT_FAILURE);
    }

    if (!reset_audio(48000)) {
        std::exit(EXIT_FAILURE);
    }

    std::printf("SDL video driver: %s\n", SDL_GetCurrentVideoDriver());
    return nullptr;
}

ultramodern::renderer::WindowHandle create_window(ultramodern::gfx_callbacks_t::gfx_data_t) {
    constexpr int width = 1280;
    constexpr int height = 720;

    window = SDL_CreateWindow(
        "Snowboard Kids: Recompiled",
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
    recompinput::handle_events();
    sbk::ui::poll_menu_actions();
    sbk::quiescence::probe_poll();
    sbk::virtual_pad::poll();
    publish_controller_ports();
#ifdef SBK_CONTINUATIONS
    if (sbk::savestate::driver::enabled()) {
        // Hotkeys are read without consuming SDL events. F5/F8 (no modifier)
        // are the user quick save/load; Ctrl+F6/Ctrl+F7 are DEVELOPMENT ONLY.
        // Ignored while a frontend menu captures input.
        static bool save_was_down = false, load_was_down = false;
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        const bool ctrl = keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL];
        const bool alt = keys[SDL_SCANCODE_LALT] || keys[SDL_SCANCODE_RALT];
        const bool menu = recompui::is_context_capturing_input();
        const bool save = keys[SDL_SCANCODE_F5] && !ctrl && !alt && !menu;
        const bool load = keys[SDL_SCANCODE_F8] && !ctrl && !alt && !menu;
        if (save && !save_was_down) sbk::savestate::driver::quick_save();
        if (load && !load_was_down) sbk::savestate::driver::quick_load();
        save_was_down = save;
        load_was_down = load;
        sbk::savestate::dev::poll(ctrl && keys[SDL_SCANCODE_F6], ctrl && keys[SDL_SCANCODE_F7]);
        sbk::savestate::driver::poll();
        sbk::savestate::toast::update();
    }
#endif
#ifdef SBK_CONTINUATIONS
    static uint64_t s_frame_idx = 0;
    if (++s_frame_idx % 120 == 0) {
        std::fprintf(stderr, "P4A heartbeat frame=%llu live_owners=%zu total_owners=%llu total_dispatches=%llu startup_retired=%d\n",
            (unsigned long long)s_frame_idx,
            sbk::continuation::live_owner_count(),
            (unsigned long long)sbk::continuation::total_registered_owners(),
            (unsigned long long)sbk::continuation::total_dispatch_count(),
            sbk::continuation::startup_is_retired() ? 1 : 0);
    }
#endif
}

void queue_samples(int16_t* audio_data, size_t sample_count) {
    if (audio_device == 0 || sample_count == 0) {
        return;
    }

    static std::vector<float> swap_buffer;

    const size_t resampled_sample_count = sample_count + duplicated_input_frames * input_channels;
    const size_t max_sample_count =
        std::max(resampled_sample_count, resampled_sample_count * static_cast<size_t>(audio_convert.len_mult));

    if (max_sample_count > swap_buffer.size()) {
        swap_buffer.resize(max_sample_count);
    }

    for (size_t i = 0; i < duplicated_input_frames * input_channels; ++i) {
        swap_buffer[i] = duplicated_sample_buffer[i];
    }

    for (size_t i = 0; i + 1 < sample_count; i += input_channels) {
        // Swap stereo channels to compensate for the N64/RDRAM endian layout.
        swap_buffer[i + duplicated_input_frames * input_channels] =
            audio_data[i + 1] * (0.5f / 32768.0f);
        swap_buffer[i + 1 + duplicated_input_frames * input_channels] =
            audio_data[i] * (0.5f / 32768.0f);
    }

    if (sample_count >= duplicated_input_frames * input_channels) {
        for (size_t i = 0; i < duplicated_input_frames * input_channels; ++i) {
            duplicated_sample_buffer[i] = swap_buffer[i + sample_count];
        }
    }

    audio_convert.buf = reinterpret_cast<Uint8*>(swap_buffer.data());
    audio_convert.len =
        static_cast<int>((sample_count + duplicated_input_frames * input_channels) * sizeof(float));

    if (SDL_ConvertAudio(&audio_convert) < 0) {
        std::fprintf(stderr, "SDL_ConvertAudio failed: %s\n", SDL_GetError());
        return;
    }

    const uint32_t discarded_bytes =
        output_channels * discarded_output_frames * sizeof(float);
    if (static_cast<uint32_t>(audio_convert.len_cvt) <= discarded_bytes) {
        return;
    }

    const uint32_t bytes_to_queue =
        static_cast<uint32_t>(audio_convert.len_cvt) - discarded_bytes;
    float* samples_to_queue =
        swap_buffer.data() + output_channels * discarded_output_frames / 2;

    const bool queued = SDL_QueueAudio(audio_device, samples_to_queue, bytes_to_queue) == 0;
#ifdef SBK_CONTINUATIONS
    // Mirror only what SDL accepted, so the ledger never runs ahead of the queue.
    if (queued) audio_boundary.submitted(reinterpret_cast<const uint8_t*>(samples_to_queue), bytes_to_queue);
#else
    (void)queued;
#endif
}

#ifdef SBK_CONTINUATIONS
sbk::savestate::audio::DeviceFormat host_audio_format() {
    return {sample_rate, output_sample_rate, output_channels};
}

bool capture_host_audio(sbk::savestate::AudioState& state, std::string& error) {
    if (audio_device == 0) { error = "no audio device"; return false; }
    // The device is paused at CloseVI: the queue size is the exact backlog.
    return audio_boundary.capture(host_audio_format(), duplicated_sample_buffer,
        SDL_GetQueuedAudioSize(audio_device), state, error);
}

bool validate_host_audio(const sbk::savestate::AudioState& state, std::string& error) {
    if (audio_device == 0) { error = "no audio device"; return false; }
    return audio_boundary.validate(state, host_audio_format(), duplicated_sample_buffer.size(), error);
}

bool install_host_audio(const sbk::savestate::AudioState& state, std::string& error) {
    if (audio_device == 0) { error = "no audio device"; return false; }
    const sbk::savestate::audio::HostQueue queue{
        [] { return SDL_GetQueuedAudioSize(audio_device); },
        [] { SDL_ClearQueuedAudio(audio_device); },
        [](const uint8_t* data, uint32_t size, std::string& why) {
            if (SDL_QueueAudio(audio_device, data, size) == 0) return true;
            why = SDL_GetError();
            return false;
        }};
    // The guest frequency (and converter) was already restored by the domain.
    return audio_boundary.install(state, host_audio_format(), duplicated_sample_buffer, queue, error);
}
#endif

size_t get_frames_remaining() {
    if (audio_device == 0) {
        return 0;
    }

    return sbk::audio_progress::remaining_frames(
        SDL_GetQueuedAudioSize(audio_device), output_channels,
        sample_rate, output_sample_rate);
}

void set_frequency(uint32_t freq) {
    std::printf("N64 audio frequency requested: %" PRIu32 " Hz\n", freq);
    sample_rate = freq;
    update_audio_converter();
}

void poll_input() {
    recompinput::poll_inputs();
}

void start_game_on_first_vi() {
    if (frontend_preview) return;
    static bool started = false;
    if (started) {
        return;
    }

    started = true;
    std::puts("First safe VI reached; starting Snowboard Kids...");
    std::fflush(stdout);
    recomp::start_game(u8"snowboardkids.n64.us", "");
}

void on_vi() {
    start_game_on_first_vi();
    recompinput::update_rumble();
}

bool get_input(int controller_num, uint16_t* buttons, float* x, float* y) {
    if (sbk::quiescence::probe_input(controller_num, buttons, x, y)) return true;
    if (controller_num < 0 || controller_num >= 4) {
        return false;
    }

    *buttons = 0;
    *x = 0.0f;
    *y = 0.0f;
    if (!(connected_port_mask.load(std::memory_order_acquire) & (1u << controller_num))) {
        return false;
    }
    // Provisional setup ports let the guest offer 2–4 players. They remain
    // neutral until RecompInput's assignment flow maps distinct devices.
    if (single_player_input.load(std::memory_order_acquire) && controller_num != 0) {
        return true;
    }

    return recompinput::profiles::get_n64_input(controller_num, buttons, x, y);
}

void set_rumble(int controller_num, bool on) {
    recompinput::set_rumble(controller_num, on);
}

ultramodern::input::connected_device_info_t get_connected_device_info(int controller_num) {
    if (controller_num >= 0 && controller_num < 4 &&
        (connected_port_mask.load(std::memory_order_acquire) & (1u << controller_num))) {
        // A Rumble Pak lets the game's osMotorInit succeed, so its rumble reaches
        // recompinput::set_rumble. Controller Pak calls stay NOPACK in librecomp.
        return {
            .connected_device = ultramodern::input::Device::Controller,
            .connected_pak = ultramodern::input::Pak::RumblePak,
        };
    }

    return {
        .connected_device = ultramodern::input::Device::None,
        .connected_pak = ultramodern::input::Pak::None,
    };
}

RspUcodeFunc* get_rsp_microcode(const OSTask* task) {
    if (sbk::g_game_module.is_loaded()) {
        auto ucode = sbk::g_game_module.api()->get_rsp_microcode(task);
        return reinterpret_cast<RspUcodeFunc*>(ucode);
    }
#if !defined(SBK_ROM_FREE_ENGINE)
    if (task->t.type == M_AUDTASK) {
        return aspMain;
    }
#endif

    std::fprintf(stderr, "Unknown RSP task type: %" PRIu32 "\n", task->t.type);
    return nullptr;
}

std::string get_game_thread_name(const OSThread* thread) {
    return "N64-" + std::to_string(thread->id);
}

 } // namespace

void traced_entrypoint(uint8_t* rdram, recomp_context* ctx) {
    sbk::quiescence::probe_memory(rdram);
    sbk::virtual_pad::set_memory(rdram);
#ifdef SBK_CONTINUATIONS
    sbk::continuation::set_player_count_request_callback(
        recompinput::players::request_game_player_count);
    sbk::savestate::driver::set_memory(rdram);
#endif
    std::puts(">>> ENTERING SNOWBOARD KIDS RECOMP_ENTRYPOINT");
    std::fflush(stdout);

    if (sbk::g_game_module.is_loaded()) {
        sbk::g_game_module.api()->entrypoint(rdram, ctx);
    }
#if !defined(SBK_ROM_FREE_ENGINE)
    else {
        recomp_entrypoint(rdram, ctx);
    }
#else
    else {
        std::fprintf(stderr, "Fatal: No game module loaded in split-runtime engine.\n");
        std::abort();
    }
#endif

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

static int invoke_local_module_builder(const std::filesystem::path& install_dir,
                                       const std::filesystem::path& rom_path,
                                       const std::filesystem::path& target_dir,
                                       const std::filesystem::path& log) {
    // Literal argv: ROM paths are never interpreted by a shell.
    const auto args = sbk::first_run::builder_command(install_dir, rom_path, target_dir, false);
    if (args.empty()) {
        std::fprintf(stderr, "Cannot locate build-game-module.py in install or working directories.\n");
        return 2;
    }
    std::error_code ec;
    std::filesystem::create_directories(log.parent_path(), ec);
    sbk::first_run::Process process;
    std::string error;
    if (!process.start(args, log, error)) {
        std::fprintf(stderr, "[BUILDER] %s\n", error.c_str());
        return 2;
    }
    while (process.running()) {
        process.poll([](const std::string& line) { std::printf("%s\n", line.c_str()); });
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::fflush(stdout);
    return process.exit_code();
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path explicit_module_path;
    std::filesystem::path rom_path;
    std::filesystem::path build_module_rom;
    std::filesystem::path validate_module_path;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frontend-preview") == 0) {
            frontend_preview = true;
            continue;
        }
        if (std::strcmp(argv[i], "--version") == 0) {
            std::printf("Snowboard Kids Recompiled %s\ncommit %s\n", SBK_VERSION, SBK_COMMIT);
            return EXIT_SUCCESS;
        }
        if (std::strcmp(argv[i], "--help") == 0) {
            std::puts("Usage: SnowboardKidsEngine [options] [path-to-your-USA-ROM]\n"
                      "Without a path, select your own ROM in the file dialog.\n\n"
                      "Options:\n"
                      "  --help                 Show help options\n"
                      "  --version              Show version info\n"
                      "  --frontend-preview     Open ROM-free frontend for visual review\n"
                      "  --module <path>        Explicit path to SnowboardKidsGame dynamic module\n"
                      "  --rom <path>           Explicit path to Snowboard Kids (USA) ROM\n"
                      "  --build-module <rom>   Build and install game module from specified ROM and exit\n"
                      "  --validate-module <mod> Validate specified module ABI and print metadata");
            return EXIT_SUCCESS;
        }
        if (std::strcmp(argv[i], "--validate-module") == 0 && i + 1 < argc) {
            validate_module_path = std::filesystem::absolute(argv[++i]);
            continue;
        }
        if (std::strcmp(argv[i], "--build-module") == 0 && i + 1 < argc) {
            build_module_rom = std::filesystem::absolute(argv[++i]);
            continue;
        }
        if (std::strcmp(argv[i], "--module") == 0 && i + 1 < argc) {
            explicit_module_path = std::filesystem::absolute(argv[++i]);
            continue;
        }
        if (std::strcmp(argv[i], "--rom") == 0 && i + 1 < argc) {
            rom_path = std::filesystem::absolute(argv[++i]);
            continue;
        }
        if (argv[i][0] != '-') {
            rom_path = std::filesystem::absolute(argv[i]);
        }
    }

    if (!validate_module_path.empty()) {
        std::string err;
        sbk::module::GameModule mod;
        auto st = mod.load(validate_module_path, err);
        if (st != sbk::module::LoadStatus::Success) {
            std::fprintf(stderr, "MODULE_LOAD_ERROR (%s): %s\n", sbk::module::status_string(st), err.c_str());
            return 1;
        }
        if (!mod.validate(0xF384619787B78D4BULL, "snowboardkids.n64.us", err)) {
            std::fprintf(stderr, "MODULE_VALIDATION_ERROR: %s\n", err.c_str());
            return 1;
        }
        const auto* api = mod.api();
        std::printf("MODULE_VALID: magic=0x%016llX abi=%u corpus=0x%016llX funcs=%u hle=%u\n",
                    (unsigned long long)api->magic, api->abi_version,
                    (unsigned long long)api->corpus_digest, api->function_count, api->hle_count);
        return 0;
    }

    sbk::quiescence::probe_init(
        [](bool paused) { if (audio_device) SDL_PauseAudioDevice(audio_device, paused ? 1 : 0); },
        []() -> uint32_t { return audio_device ? SDL_GetQueuedAudioSize(audio_device) : 0; });
    install_crash_handlers();

    // RecompFrontend resolves its read-only assets against cwd on desktop.
    // Anchor that lookup at the executable, independent of the launch cwd.
    char* base_path = SDL_GetBasePath();
    if (!base_path) {
        std::fprintf(stderr, "Cannot locate application assets: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }
    const std::filesystem::path install_dir = base_path;
    SDL_free(base_path);
    std::error_code ec;
    std::filesystem::current_path(install_dir, ec);
    if (ec) {
        std::fprintf(stderr, "Cannot open application directory: %s\n", ec.message().c_str());
        return EXIT_FAILURE;
    }

    recompui::programconfig::set_program_name("Snowboard Kids: Recompiled");
    recompui::programconfig::set_program_id(u8"snowboardkids-recompiled");
    std::filesystem::path runtime_dir;
    if (const char* override_path = std::getenv("SBK_USER_DATA_DIR")) {
        runtime_dir = override_path;
    } else if (std::filesystem::exists(install_dir / "CMakeCache.txt") &&
               std::filesystem::exists(install_dir / "runtime-data")) {
        runtime_dir = install_dir / "runtime-data"; // existing developer layout
    } else {
        runtime_dir = recompui::file::get_app_folder_path();
    }
    if (runtime_dir.empty()) {
        std::fprintf(stderr, "Cannot determine a writable user data directory. Set SBK_USER_DATA_DIR.\n");
        return EXIT_FAILURE;
    }

    std::filesystem::create_directories(runtime_dir, ec);
    if (ec) {
        std::fprintf(stderr, "Failed to create runtime data directory: %s\n", ec.message().c_str());
        return EXIT_FAILURE;
    }

    if (!build_module_rom.empty()) {
        std::filesystem::path target_modules_dir = runtime_dir / "modules" / "snowboardkids-us";
        int res = invoke_local_module_builder(install_dir, build_module_rom, target_modules_dir,
                                              runtime_dir / "logs" / "build-module.log");
        if (res == 0) {
            std::puts("[BUILDER] Module built and installed successfully.");
            return EXIT_SUCCESS;
        } else {
            std::fprintf(stderr, "[BUILDER] Module build failed with exit code %d.\n", res);
            return EXIT_FAILURE;
        }
    }

    recomp::register_config_path(runtime_dir);
    sbk::pfs::configure(recomp::get_config_path());
    for (int port = 0; port < 4; ++port)
        sbk::pfs::set_port_present(port, port == 0 &&
            !(std::getenv("SBK_PFS_ABSENT") &&
              std::strcmp(std::getenv("SBK_PFS_ABSENT"), "1") == 0));
    recomp::GameEntry& game = supported_games[0];
    game.entrypoint_address = get_entrypoint_address();
    if (!recomp::register_game(game)) {
        std::fprintf(stderr, "Failed to register Snowboard Kids\n");
        return EXIT_FAILURE;
    }

    // Attempt to discover and load game module (checking user data directory first)
    std::string mod_err;
    auto mod_status = sbk::module::LoadStatus::FileNotFound;
    if (!frontend_preview) mod_status = sbk::g_game_module.load_candidate(install_dir, runtime_dir, explicit_module_path, mod_err);

    // First-run state machine if module is missing
    if (!frontend_preview && !sbk::g_game_module.is_loaded()) {
#if defined(SBK_ROM_FREE_ENGINE)
        std::printf("[FIRST-RUN] No compatible Snowboard Kids game module found (%s).\n",
                    sbk::module::status_string(mod_status));

        if (rom_path.empty()) {
            if (const char* env_rom = std::getenv("SBK_ROM_PATH")) rom_path = env_rom;
        }
        // Remembered in user data (runtime_dir), never beside the executable.
        const std::filesystem::path last_rom_file = runtime_dir / "last_rom_path.txt";
        std::filesystem::path saved_rom;
        if (std::ifstream ifs(last_rom_file); ifs) {
            std::string line;
            if (std::getline(ifs, line) && !line.empty() && std::filesystem::is_regular_file(line)) saved_rom = line;
        }
        sbk::first_run::Host host;
        host.install_dir = install_dir;
        host.runtime_dir = runtime_dir;
        host.assets_dir = install_dir / "assets";
        host.initial_rom = rom_path;
        host.saved_rom = saved_rom;
        host.module_dir = runtime_dir / "modules" / "snowboardkids-us";
        host.log = runtime_dir / "logs" / "first-run.log";
        host.validate_rom = [&](const std::filesystem::path& candidate) -> std::string {
            std::u8string game_id = game.game_id;
            const auto val = recomp::select_rom(candidate, game_id);
            if (val != recomp::RomValidationError::Good) {
                return std::string("This file is not a supported Snowboard Kids (USA) ROM (") +
                       validation_error_name(val) + ").";
            }
            rom_path = candidate;
            if (std::ofstream ofs(last_rom_file); ofs) ofs << candidate.string() << "\n";
            return {};
        };
        host.load_module = [&]() -> std::string {
            mod_status = sbk::g_game_module.load_candidate(install_dir, runtime_dir, explicit_module_path, mod_err);
            if (mod_status == sbk::module::LoadStatus::Success) return {};
            return std::string("The new module did not load (") + sbk::module::status_string(mod_status) + "): " + mod_err;
        };
        host.choose_rom = [](std::filesystem::path& out) {
            if (NFD_Init() != NFD_OKAY) {
                std::fprintf(stderr, "Cannot open ROM file dialog: %s\n", NFD_GetError());
                return false;
            }
            bool selected = false;
            recompui::file::open_file_dialog([&](bool success, const std::filesystem::path& path) {
                selected = success;
                if (success) out = path;
            });
            NFD_Quit();
            return selected;
        };
        switch (sbk::first_run::run(host)) {
            case sbk::first_run::Outcome::Ready: break;
            case sbk::first_run::Outcome::Quit:
                std::puts("[FIRST-RUN] Setup closed before a module was installed. Exiting cleanly.");
                return EXIT_SUCCESS;
            case sbk::first_run::Outcome::Failed:
                return EXIT_FAILURE;
        }
        std::printf("[FIRST-RUN] Successfully loaded newly installed game module: %s\n",
                    sbk::g_game_module.loaded_path().c_str());
#else
        std::fprintf(stderr, "[MODULE] Candidate load failed (%s): %s\n",
                     sbk::module::status_string(mod_status), mod_err.c_str());
#endif
    }

    if (sbk::g_game_module.is_loaded()) {
        std::printf("[MODULE] Loaded game module from: %s\n", sbk::g_game_module.loaded_path().c_str());
        SbkEngineApiV1 engine_api{};
        engine_api.abi_version = 1;
        engine_api.struct_size = sizeof(SbkEngineApiV1);
        engine_api.switch_error = [](const char* section, uint32_t jtbl_addr, uint32_t target) {
            std::fprintf(stderr, "Jump table error in %s: jtbl 0x%08X, target 0x%08X\n",
                         section ? section : "unknown", jtbl_addr, target);
            std::abort();
        };
        engine_api.dmem = dmem;
#ifdef SBK_CONTINUATIONS
        engine_api.continuation_enter = [](uint64_t id, uint8_t* rdram, recomp_context* ctx) {
            sbk::continuation::enter(id, rdram, ctx);
        };
#endif
        if (!sbk::g_game_module.initialize(engine_api, mod_err)) {
            std::fprintf(stderr, "Failed to initialize game module: %s\n", mod_err.c_str());
            return EXIT_FAILURE;
        }
#ifdef SBK_CONTINUATIONS
        const auto* api = sbk::g_game_module.api();
        for (size_t i = 0; i < api->continuation_count; ++i) {
            const auto& desc = api->continuations[i];
            sbk::continuation::Descriptor d{};
            d.id = desc.id;
            d.guest_address = desc.guest_address;
            d.scratch_count = desc.scratch_count;
            d.step = reinterpret_cast<sbk::continuation::Step*>(desc.step);
            d.token = reinterpret_cast<recomp_func_t*>(desc.token);
            d.name = desc.name;
            sbk::continuation::register_function(d);
        }
#endif
    }

#ifdef SBK_CONTINUATIONS
    // Savestates. This arms the P2 coordinator, so it must run before the first
    // device producer: init_frontend_config() below already queues a renderer
    // UpdateConfigAction, and work admitted while P2 is disabled is never
    // counted (LIVE-STARTUP-01). SBK_SAVESTATES=0 disables them; the P2 probe
    // (SBK_P2_CYCLES) owns the barrier exclusively when set.
    if (const char* flag = std::getenv("SBK_SAVESTATES"); frontend_preview || (flag && std::strcmp(flag, "0") == 0)) {
        std::fprintf(stderr, "SAVESTATE disabled by SBK_SAVESTATES=0\n");
    } else if (std::getenv("SBK_P2_CYCLES")) {
        std::fprintf(stderr, "SAVESTATE disabled: SBK_P2_CYCLES probe already drives the barrier\n");
    } else {
        sbk::savestate::driver::Config config{};
        if (sbk::g_game_module.is_loaded()) {
            config.build = {
                sbk::g_game_module.api()->corpus_digest,
                sbk::g_game_module.api()->function_count,
                sbk::g_game_module.api()->hle_count
            };
            config.rom_hash = sbk::g_game_module.api()->rom_hash;
        } else {
            // Snapshots restore only into the same generated corpus/manifest.
            constexpr const char* corpus = "76260cb8f0e080d7dc7f0e5d0ad3ac7d355de81d98acf4ce2a9a23132cd1ae43";
            uint64_t digest = 14695981039346656037ull;
            for (const char* c = corpus; *c; ++c) { digest ^= static_cast<unsigned char>(*c); digest *= 1099511628211ull; }
            config.build = {digest, 1981, 56};
            config.rom_hash = game.rom_hash; // select_rom() below verifies the ROM against this XXH3-64
        }
        config.audio = {capture_host_audio, install_host_audio, validate_host_audio};
        config.audio_pause = [](bool paused) { if (audio_device) SDL_PauseAudioDevice(audio_device, paused ? 1 : 0); };
        config.directory = recomp::get_config_path() / "savestates";
        config.notify = [](sbk::savestate::driver::Notice notice) {
            sbk::savestate::toast::show(sbk::savestate::driver::notice_text(notice),
                sbk::savestate::driver::notice_is_error(notice));
        };
        sbk::savestate::driver::init(config);
        sbk::savestate::dev::init();
    }
#endif

    snowboardkids::theme::apply();
    initialize_controls(runtime_dir);
    game.entrypoint_address = get_entrypoint_address();
    init_frontend_config(game, false); // Mod subsystem is not initialized for this game.
    if (frontend_preview) {
        recompui::register_launcher_update_callback([](recompui::LauncherMenu*) {
            static bool opened = false;
            if (!opened) {
                opened = true;
                recompui::config::open();
            }
        });
    }


    if (!frontend_preview) {
    if (rom_path.empty()) {
        std::filesystem::path last_rom_file = runtime_dir / "last_rom_path.txt";
        if (std::filesystem::exists(last_rom_file)) {
            std::ifstream ifs(last_rom_file);
            std::string line;
            if (std::getline(ifs, line) && !line.empty()) {
                std::filesystem::path saved_path(line);
                if (std::filesystem::exists(saved_path)) {
                    rom_path = saved_path;
                }
            }
        }
    }

    if (rom_path.empty()) {
        if (NFD_Init() != NFD_OKAY) {
            std::fprintf(stderr, "Cannot open ROM selector: %s\n", NFD_GetError());
            return EXIT_FAILURE;
        }
        bool selected = false;
        recompui::file::open_file_dialog([&](bool success, const std::filesystem::path& path) {
            selected = success;
            if (success) rom_path = path;
        });
        NFD_Quit();
        if (!selected) {
            std::puts("ROM selection cancelled.");
            return EXIT_SUCCESS;
        }
    }
    {
        std::ofstream ofs(runtime_dir / "last_rom_path.txt");
        if (ofs) ofs << rom_path.string() << "\n";
    }
    std::u8string game_id = game.game_id;
    const auto validation = recomp::select_rom(rom_path, game_id);
    std::printf("ROM validation: %s\n", validation_error_name(validation));

    if (validation != recomp::RomValidationError::Good) {
        std::fprintf(stderr, "The selected file is not the supported Snowboard Kids (USA) ROM.\n");
        return EXIT_FAILURE;
    }

    if (sbk::g_game_module.is_loaded()) {
        if (!sbk::g_game_module.validate(game.rom_hash, "snowboardkids.n64.us", mod_err)) {
            std::fprintf(stderr, "Game module validation failed: %s\n", mod_err.c_str());
            return EXIT_FAILURE;
        }
    }
#if defined(SBK_ROM_FREE_ENGINE)
    else {
        std::fprintf(stderr, "Error: No SnowboardKidsGame dynamic module found.\n"
                             "Please build the game module from your ROM with:\n"
                             "  python3 scripts/build-game-module.py <path-to-rom>\n");
        return EXIT_FAILURE;
    }
#endif

    }

    sbk::register_overlays();

    recomp::rsp::callbacks_t rsp_callbacks{
        .get_rsp_microcode = get_rsp_microcode,
    };

    ultramodern::renderer::callbacks_t renderer_callbacks{
        .create_render_context =
            [](uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool developer_mode) {
                const auto mode = selected_presentation_mode();
                std::printf("RT64 presentation mode: %s\n", presentation_mode_name(mode));
                return recompui::renderer::create_render_context(
                    rdram,
                    window_handle,
                    mode,
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
        .vi_callback = on_vi,
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

#ifdef SBK_CONTINUATIONS
    std::fprintf(stderr, "P4A backend=continuation manifest_functions=1981 manifest_hle=56 corpus_sha256=76260cb8f0e080d7dc7f0e5d0ad3ac7d355de81d98acf4ce2a9a23132cd1ae43\n");
#else
    std::fprintf(stderr, "P4A backend=ordinary\n");
#endif

    // Do not call start_game() before the runtime starts. N64ModernRuntime's
    // VI thread needs one dummy retrace to seed a valid VI mode/framebuffer.
    // The first VI callback above starts the game immediately after that safe
    // initialization point.
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

#ifdef SBK_CONTINUATIONS
    sbk::savestate::driver::shutdown();
    std::fprintf(stderr, "P4A shutdown total_dispatches=%llu total_owners=%llu startup_retired=%d\n",
        (unsigned long long)sbk::continuation::total_dispatch_count(),
        (unsigned long long)sbk::continuation::total_registered_owners(),
        sbk::continuation::startup_is_retired() ? 1 : 0);
#endif

    SDL_Quit();
    // Guest threads run on detached workers that are never joined. Skip static
    // destructors (registries, queues) they may still reference (SHUTDOWN-01).
    std::fflush(nullptr);
    std::_Exit(EXIT_SUCCESS);
}
