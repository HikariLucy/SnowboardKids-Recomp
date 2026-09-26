#pragma once
#include "first_run_process.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <tuple>
struct SDL_Renderer;
struct SDL_Texture;
typedef struct FT_LibraryRec_* FT_Library;
typedef struct FT_FaceRec_* FT_Face;
namespace sbk::first_run {
struct Rect { int x = 0, y = 0, w = 0, h = 0; };
enum class Action { None, UseSaved, Choose, Cancel, Retry, Start, Quit };
// Everything the window draws. Counts come only from builder protocol lines.
struct View {
    Model model;
    Phase failed_at = Phase::Missing; // step that was active when Error was entered
    std::string message, saved_rom;
    std::array<Action, 3> buttons{};
    int button_count = 0, focus = 0, hover = -1;
};
struct Layout {
    Rect panel, title, tag, steps, heading, detail, progress, progress_label;
    std::array<Rect, 3> buttons{};
    int button_count = 0;
    float scale = 1.0f;
};
// Uniformly scaled 1280x720 design centered in any drawable size.
Layout layout_for(int width, int height, int button_count);
const char* action_label(Action action);
// The screen offered for each phase; focus defaults to the first entry.
void set_buttons(View& view);
class Fonts {
public:
    explicit Fonts(const std::filesystem::path& assets);
    ~Fonts();
    Fonts(const Fonts&) = delete;
    Fonts& operator=(const Fonts&) = delete;
    bool ok() const { return heading && body; }
    // Pixel width of text at px; used to shrink labels to their box.
    int measure(bool is_heading, int px, const std::string& text);
    SDL_Texture* text(SDL_Renderer* renderer, bool is_heading, int px, const std::string& text,
                      uint32_t rgba, int& w, int& h);
    void clear();
    int smallest_px = 1 << 30; // smallest size any label was shrunk to
private:
    FT_Library library = nullptr;
    FT_Face heading = nullptr, body = nullptr;
    std::map<std::tuple<bool, int, uint32_t, std::string>, std::tuple<SDL_Texture*, int, int>> cache;
    SDL_Renderer* owner = nullptr;
};
Layout render(SDL_Renderer* renderer, Fonts& fonts, const View& view, int width, int height, uint32_t ticks);
struct Host {
    std::filesystem::path install_dir, runtime_dir, assets_dir;
    std::filesystem::path initial_rom, saved_rom; // initial: CLI/env, skips selection
    std::filesystem::path module_dir, log;
    // Return an empty string on success, otherwise a user-facing reason.
    std::function<std::string(const std::filesystem::path&)> validate_rom;
    std::function<std::string()> load_module;
    // Native file dialog; false when cancelled or unavailable.
    std::function<bool(std::filesystem::path&)> choose_rom;
};
enum class Outcome { Ready, Quit, Failed };
// Opens the setup window; falls back to console when no display is available.
Outcome run(const Host& host);
}
