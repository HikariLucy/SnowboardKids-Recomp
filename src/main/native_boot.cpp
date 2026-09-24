#include "quiescence/probe.hpp"
#ifdef SBK_CONTINUATIONS
#include "continuation/execution.hpp"
#include "continuation/runtime_owner.hpp"
#include "savestate/dev_trigger.hpp"
#endif
#include <algorithm>
#include <array>
#include <cinttypes>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#if defined(__linux__)
#include <execinfo.h>
#include <unistd.h>
#endif

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include "librecomp/game.hpp"
#include "librecomp/rsp.hpp"
#include "recompinput/input_events.h"
#include "recompinput/input_state.h"
#include "recompinput/profiles.h"
#include "recompui/config.h"
#include "recompui/renderer.h"
#include "recompui/program_config.h"
#include "recomp_theme.h"
#include "ultramodern/ultramodern.hpp"
#include "ultramodern/config.hpp"

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

void init_frontend_config() {
    // Minimal standard RecompFrontend configuration for first boot.
    // Game-specific settings can be added after the native boot path works.
    recompui::config::GeneralTabOptions general_options{};
    general_options.has_rumble_strength = true;
    general_options.has_gyro_sensitivity = false;
    general_options.has_mouse_sensitivity = false;

    recompui::config::create_general_tab(general_options);
    recompui::config::create_graphics_tab();
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
    recompui::config::create_controls_tab();
    recompui::config::create_sound_tab();
    recompui::config::create_mods_tab();
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
constexpr uint32_t bytes_per_frame = input_channels * sizeof(float);
static std::array<float, duplicated_input_frames * input_channels> duplicated_sample_buffer{};

#ifdef SBK_CONTINUATIONS
// P4 semantic audio boundary. SDL cannot read back queued PCM, so the most
// recent submitted output bytes are mirrored here (bounded ring). At a Frozen
// boundary the device is paused, so the queued byte count selects exactly the
// not-yet-consumed backlog. Only this PCM (plus the conversion history) is
// snapshot state; the SDL device itself is never serialized.
class AudioLedger {
    std::vector<uint8_t> ring_ = std::vector<uint8_t>(4 * 1024 * 1024);
    size_t head_ = 0, filled_ = 0;
public:
    void append(const uint8_t* data, size_t size) {
        for (size_t i = 0; i < size; ++i) ring_[(head_ + i) % ring_.size()] = data[i];
        head_ = (head_ + size) % ring_.size();
        filled_ = std::min(ring_.size(), filled_ + size);
    }
    bool tail(size_t size, std::vector<uint8_t>& out) const {
        if (size > filled_) return false;
        out.resize(size);
        const size_t start = (head_ + ring_.size() - size) % ring_.size();
        for (size_t i = 0; i < size; ++i) out[i] = ring_[(start + i) % ring_.size()];
        return true;
    }
    void reset(const std::vector<uint8_t>& contents) {
        head_ = filled_ = 0;
        append(contents.data(), contents.size());
    }
};
static AudioLedger audio_ledger;
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
    recompinput::handle_events();
    sbk::quiescence::probe_poll();
#ifdef SBK_CONTINUATIONS
    if (sbk::savestate::dev::enabled()) {
        // DEVELOPMENT ONLY hotkeys: read without consuming SDL events.
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        const bool ctrl = keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL];
        sbk::savestate::dev::poll(ctrl && keys[SDL_SCANCODE_F6], ctrl && keys[SDL_SCANCODE_F7]);
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

    SDL_QueueAudio(audio_device, samples_to_queue, bytes_to_queue);
#ifdef SBK_CONTINUATIONS
    audio_ledger.append(reinterpret_cast<const uint8_t*>(samples_to_queue), bytes_to_queue);
#endif
}

#ifdef SBK_CONTINUATIONS
bool capture_host_audio(sbk::savestate::AudioState& state, std::string& error) {
    if (audio_device == 0) { error = "no audio device"; return false; }
    state.host_input_rate = sample_rate;
    state.host_output_rate = output_sample_rate;
    state.host_output_channels = output_channels;
    state.host_history.clear();
    for (float sample : duplicated_sample_buffer) {
        uint32_t bits;
        std::memcpy(&bits, &sample, sizeof(bits));
        state.host_history.push_back(bits);
    }
    const uint32_t queued = SDL_GetQueuedAudioSize(audio_device);
    if (!audio_ledger.tail(queued, state.host_backlog)) {
        error = "queued audio exceeds the ledger";
        return false;
    }
    return true;
}

bool install_host_audio(const sbk::savestate::AudioState& state, std::string& error) {
    if (audio_device == 0) { error = "no audio device"; return false; }
    if (state.host_output_rate != output_sample_rate || state.host_output_channels != output_channels) {
        error = "audio device output format changed since capture";
        return false;
    }
    // The guest frequency (and converter) was already restored by the domain.
    if (state.host_input_rate != sample_rate || state.host_history.size() != duplicated_sample_buffer.size()) {
        error = "audio conversion state shape mismatch";
        return false;
    }
    for (size_t i = 0; i < duplicated_sample_buffer.size(); ++i)
        std::memcpy(&duplicated_sample_buffer[i], &state.host_history[i], sizeof(float));
    // Future audio of the abandoned timeline is dropped; the saved backlog is
    // queued exactly once. The device stays paused until the barrier releases.
    SDL_ClearQueuedAudio(audio_device);
    if (!state.host_backlog.empty() &&
        SDL_QueueAudio(audio_device, state.host_backlog.data(), static_cast<Uint32>(state.host_backlog.size())) != 0) {
        error = SDL_GetError();
        return false;
    }
    audio_ledger.reset(state.host_backlog);
    return true;
}
#endif

size_t get_frames_remaining() {
    if (audio_device == 0) {
        return 0;
    }

    uint64_t buffered_byte_count = SDL_GetQueuedAudioSize(audio_device);
    buffered_byte_count =
        buffered_byte_count * 2 * sample_rate / output_sample_rate / output_channels;

    constexpr float buffer_offset_frames = 1.0f;
    const uint32_t frames_per_vi = sample_rate / 60;
    const uint32_t offset =
        static_cast<uint32_t>(buffer_offset_frames * bytes_per_frame * frames_per_vi);

    buffered_byte_count = buffered_byte_count > offset
        ? buffered_byte_count - offset
        : 0;

    return static_cast<size_t>(buffered_byte_count / bytes_per_frame);
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

    return recompinput::profiles::get_n64_input(controller_num, buttons, x, y);
}

void set_rumble(int controller_num, bool on) {
    recompinput::set_rumble(controller_num, on);
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
    sbk::quiescence::probe_memory(rdram);
#ifdef SBK_CONTINUATIONS
    sbk::savestate::dev::set_memory(rdram);
#endif
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
    sbk::quiescence::probe_init(
        [](bool paused) { if (audio_device) SDL_PauseAudioDevice(audio_device, paused ? 1 : 0); },
        []() -> uint32_t { return audio_device ? SDL_GetQueuedAudioSize(audio_device) : 0; });
    install_crash_handlers();
#ifdef SBK_CONTINUATIONS
    {
        // Snapshots restore only into the same generated corpus/manifest.
        constexpr const char* corpus = "76260cb8f0e080d7dc7f0e5d0ad3ac7d355de81d98acf4ce2a9a23132cd1ae43";
        uint64_t digest = 14695981039346656037ull;
        for (const char* c = corpus; *c; ++c) { digest ^= static_cast<unsigned char>(*c); digest *= 1099511628211ull; }
        sbk::savestate::dev::Config config{};
        config.build = {digest, 1981, 56};
        config.audio = {capture_host_audio, install_host_audio};
        config.audio_pause = [](bool paused) { if (audio_device) SDL_PauseAudioDevice(audio_device, paused ? 1 : 0); };
        sbk::savestate::dev::init(config);
    }
#endif

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
    recomp::register_config_path(runtime_dir);
    snowboardkids::theme::apply();
    initialize_controls(runtime_dir);
    init_frontend_config();

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
    std::fprintf(stderr, "P4A shutdown total_dispatches=%llu total_owners=%llu startup_retired=%d\n",
        (unsigned long long)sbk::continuation::total_dispatch_count(),
        (unsigned long long)sbk::continuation::total_registered_owners(),
        sbk::continuation::startup_is_retired() ? 1 : 0);
#endif

    SDL_Quit();
    return EXIT_SUCCESS;
}
