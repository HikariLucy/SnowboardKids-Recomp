// ROM-free first-run window checks: every phase at every target size renders
// through a software renderer with rects in bounds, no overlap and legible text.
// SBK_FIRST_RUN_SNAPSHOTS=<dir> also writes BMPs for human visual review.
#include "ui/first_run_window.hpp"
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace sbk::first_run;
static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::fprintf(stderr, "FAIL: " __VA_ARGS__); std::fputc('\n', stderr); ++failures; } } while (0)
static bool inside(Rect a, Rect b) { return a.x >= b.x && a.y >= b.y && a.x + a.w <= b.x + b.w && a.y + a.h <= b.y + b.h; }
static bool overlap(Rect a, Rect b) { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; }
int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s <staged assets dir>\n", argv[0]); return 2; }
    // Button sets per phase are the navigation contract.
    View v;
    v.model.phase = Phase::Missing; set_buttons(v);
    CHECK(v.button_count == 2 && v.buttons[0] == Action::Choose && v.buttons[1] == Action::Quit, "missing buttons");
    v.saved_rom = "rom.z64"; set_buttons(v);
    CHECK(v.button_count == 3 && v.buttons[0] == Action::UseSaved && v.focus == 0, "saved rom offered first");
    v.model.phase = Phase::Compile; set_buttons(v);
    CHECK(v.button_count == 1 && v.buttons[0] == Action::Cancel, "build is cancellable");
    v.model.phase = Phase::Error; set_buttons(v);
    CHECK(v.button_count == 3 && v.buttons[0] == Action::Retry && v.buttons[2] == Action::Quit, "error recovery");
    v.model.phase = Phase::Ready; set_buttons(v);
    CHECK(v.button_count == 1 && v.buttons[0] == Action::Start, "ready starts");

    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 2; }
    Fonts fonts{argv[1]};
    CHECK(fonts.ok(), "fonts load from staged assets");
    const char* snapshots = std::getenv("SBK_FIRST_RUN_SNAPSHOTS");
    const struct { int w, h; const char* name; } sizes[] = {
        {1280, 720, "720p"}, {1920, 1080, "1080p"}, {2560, 1440, "1440p"}, {3840, 2160, "4k"},
        {1024, 768, "4x3"}, {960, 540, "min"}, {2560, 1080, "ultrawide"}};
    struct Case { Phase phase; int current, total; const char* message; const char* name; };
    const Case cases[] = {
        {Phase::Missing, 0, 0, "No game module was found. Choose your own Snowboard Kids (USA) ROM and this computer builds one locally.", "missing"},
        {Phase::GenerateRSP, 0, 0, "Generating audio microcode from your ROM.", "rsp"},
        {Phase::Compile, 17, 43, "Compiling the game module with your C++ compiler.", "compile"},
        {Phase::Ready, 0, 0, "Game module installed in your user data folder. Have fun on the slopes!", "ready"},
        {Phase::Error, 0, 0, "Module build stopped (exit 3).\nerror: no C++ compiler found on PATH\nLog: /home/user/.config/snowboardkids-recompiled/logs/first-run.log", "error"}};
    for (const auto& size : sizes) {
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, size.w, size.h, 32, SDL_PIXELFORMAT_RGBA32);
        SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
        CHECK(renderer, "software renderer %s", size.name);
        if (!renderer) continue;
        for (const auto& c : cases) {
            View view;
            view.model.phase = c.phase; view.model.current = c.current; view.model.total = c.total;
            view.message = c.message;
            view.saved_rom = c.phase == Phase::Missing ? "Snowboard Kids (USA).z64" : "";
            view.failed_at = Phase::Compile;
            set_buttons(view);
            fonts.smallest_px = 1 << 30;
            const Layout l = render(renderer, fonts, view, size.w, size.h, 1000);
            const Rect screen{0, 0, size.w, size.h};
            CHECK(inside(l.panel, screen), "%s/%s panel on screen", size.name, c.name);
            const Rect parts[] = {l.title, l.tag, l.steps, l.heading, l.detail, l.progress, l.progress_label};
            for (const auto& part : parts) CHECK(inside(part, l.panel), "%s/%s section inside panel", size.name, c.name);
            for (int i = 0; i < 7; ++i)
                for (int j = i + 1; j < 7; ++j) CHECK(!overlap(parts[i], parts[j]), "%s/%s sections %d,%d overlap", size.name, c.name, i, j);
            CHECK(l.button_count == view.button_count, "%s/%s button count", size.name, c.name);
            for (int i = 0; i < l.button_count; ++i) {
                CHECK(inside(l.buttons[i], l.panel), "%s/%s button %d inside", size.name, c.name, i);
                CHECK(l.buttons[i].w >= 96 && l.buttons[i].h >= 40, "%s/%s button %d target size", size.name, c.name, i);
                for (const auto& part : parts) CHECK(!overlap(l.buttons[i], part), "%s/%s button %d overlaps text", size.name, c.name, i);
                for (int j = i + 1; j < l.button_count; ++j) CHECK(!overlap(l.buttons[i], l.buttons[j]), "%s/%s buttons overlap", size.name, c.name);
            }
            // Labels shrink to fit their boxes; they must never become illegible.
            CHECK(fonts.smallest_px >= 14, "%s/%s smallest label %dpx", size.name, c.name, fonts.smallest_px);
            if (snapshots) {
                const std::string path = std::string(snapshots) + "/first-run-" + c.name + "-" + size.name + ".bmp";
                SDL_SaveBMP(surface, path.c_str());
            }
        }
        fonts.clear();
        SDL_DestroyRenderer(renderer);
        SDL_FreeSurface(surface);
    }
    SDL_Quit();
    if (failures) { std::fprintf(stderr, "%d first-run UI checks failed\n", failures); return 1; }
    std::puts("first-run UI: 7 sizes x 5 phases in bounds, no overlap, labels >= 14px");
    return 0;
}
