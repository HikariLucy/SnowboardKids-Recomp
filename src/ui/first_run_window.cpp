#include "first_run_window.hpp"
#include <SDL.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <vector>
namespace sbk::first_run {
namespace {
// Palette shared with src/ui/recomp_theme.cpp and assets/sbk-ui.
constexpr uint32_t INK = 0x172B46FF, SNOW = 0xF5F8EDFF, ICE = 0xD5EDF1FF, SLOPE = 0x235A78FF,
                   SUN = 0xFFD35CFF, WARN = 0x7B351CFF, MUTED = 0x536573FF, OFF = 0xDCE2E4FF;
constexpr int DESIGN_W = 1280, DESIGN_H = 720, MIN_LEGIBLE_PX = 12;
void color(SDL_Renderer* r, uint32_t c) { SDL_SetRenderDrawColor(r, c >> 24, (c >> 16) & 255, (c >> 8) & 255, c & 255); }
void fill(SDL_Renderer* r, Rect a, uint32_t c) { color(r, c); SDL_Rect q{a.x, a.y, a.w, a.h}; SDL_RenderFillRect(r, &q); }
// Chunky panel: corners cut in two pixel-art steps instead of rounded.
void stepped(SDL_Renderer* r, Rect a, int step, uint32_t c) {
    fill(r, {a.x + 2 * step, a.y, a.w - 4 * step, a.h}, c);
    fill(r, {a.x + step, a.y + step, a.w - 2 * step, a.h - 2 * step}, c);
    fill(r, {a.x, a.y + 2 * step, a.w, a.h - 4 * step}, c);
}
void framed(SDL_Renderer* r, Rect a, int step, int border, uint32_t face) {
    stepped(r, a, step, INK);
    stepped(r, {a.x + border, a.y + border, a.w - 2 * border, a.h - 2 * border}, std::max(1, step - border / 2), face);
}
uint32_t decode(const std::string& s, size_t& i) {
    const auto c = static_cast<unsigned char>(s[i++]);
    const int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    uint32_t cp = extra == 3 ? c & 7 : extra == 2 ? c & 15 : extra == 1 ? c & 31 : c;
    for (int n = 0; n < extra && i < s.size(); ++n) cp = (cp << 6) | (static_cast<unsigned char>(s[i++]) & 63);
    return cp;
}
Rect scaled(const Layout& l, int x, int y, int w, int h, Rect origin) {
    return {origin.x + int(x * l.scale), origin.y + int(y * l.scale), int(w * l.scale), int(h * l.scale)};
}
struct Step { Phase phase; const char* label; };
constexpr Step STEPS[] = {
    {Phase::Select, "Choose ROM"}, {Phase::Validate, "Check ROM + tools"},
    {Phase::GenerateCPU, "Prepare CPU code"}, {Phase::GenerateRSP, "Generate audio code"},
    {Phase::Compile, "Compile module"}, {Phase::ValidateModule, "Validate module"},
    {Phase::Install, "Install"}, {Phase::Ready, "Ready"}};
int order(Phase p) {
    for (int i = 0; i < 8; ++i) if (STEPS[i].phase == p) return i;
    return -1; // Missing / Error are not steps
}
}
Layout layout_for(int width, int height, int button_count) {
    Layout l;
    l.scale = std::min(width / float(DESIGN_W), height / float(DESIGN_H));
    const Rect origin{int((width - DESIGN_W * l.scale) / 2), int((height - DESIGN_H * l.scale) / 2), 0, 0};
    l.panel = scaled(l, 64, 40, 1152, 640, origin);
    l.title = scaled(l, 112, 76, 700, 64, origin);
    l.tag = scaled(l, 112, 146, 360, 36, origin);
    l.steps = scaled(l, 112, 212, 400, 8 * 50, origin);
    l.heading = scaled(l, 560, 212, 608, 60, origin);
    l.detail = scaled(l, 560, 284, 608, 176, origin);
    l.progress = scaled(l, 560, 472, 608, 32, origin);
    l.progress_label = scaled(l, 560, 512, 608, 32, origin);
    l.button_count = std::clamp(button_count, 0, 3);
    for (int i = 0; i < l.button_count; ++i) {
        const int slot = 3 - l.button_count + i; // right aligned
        l.buttons[i] = scaled(l, 560 + slot * 208, 568, 192, 72, origin);
    }
    return l;
}
const char* action_label(Action a) {
    switch (a) {
    case Action::UseSaved: return "Use saved ROM";
    case Action::Choose: return "Choose ROM";
    case Action::Cancel: return "Cancel";
    case Action::Retry: return "Retry";
    case Action::Start: return "Start";
    case Action::Quit: return "Quit";
    case Action::None: break;
    }
    return "";
}
void set_buttons(View& v) {
    auto set = [&](std::initializer_list<Action> list) {
        v.button_count = 0;
        for (auto a : list) v.buttons[v.button_count++] = a;
        v.focus = 0; v.hover = -1;
    };
    switch (v.model.phase) {
    case Phase::Missing: case Phase::Select:
        if (v.saved_rom.empty()) set({Action::Choose, Action::Quit});
        else set({Action::UseSaved, Action::Choose, Action::Quit});
        break;
    case Phase::Ready: set({Action::Start}); break;
    case Phase::Error: set({Action::Retry, Action::Choose, Action::Quit}); break;
    default: set({Action::Cancel}); break;
    }
}
Fonts::Fonts(const std::filesystem::path& assets) {
    if (FT_Init_FreeType(&library)) { library = nullptr; return; }
    if (FT_New_Face(library, (assets / "Fredoka.ttf").string().c_str(), 0, &heading)) heading = nullptr;
    // Fredoka ships as a variable font whose default instance is its lightest.
    FT_MM_Var* axes = nullptr;
    if (heading && FT_HAS_MULTIPLE_MASTERS(heading) && !FT_Get_MM_Var(heading, &axes)) {
        std::vector<FT_Fixed> coords(axes->num_axis);
        for (FT_UInt i = 0; i < axes->num_axis; ++i) {
            const auto& a = axes->axis[i];
            coords[i] = a.tag == FT_MAKE_TAG('w', 'g', 'h', 't') ? std::clamp<FT_Fixed>(600 << 16, a.minimum, a.maximum) : a.def;
        }
        FT_Set_Var_Design_Coordinates(heading, axes->num_axis, coords.data());
        FT_Done_MM_Var(library, axes);
    }
    if (FT_New_Face(library, (assets / "LatoLatin-Bold.ttf").string().c_str(), 0, &body)) body = nullptr;
}
Fonts::~Fonts() {
    clear();
    if (heading) FT_Done_Face(heading);
    if (body) FT_Done_Face(body);
    if (library) FT_Done_FreeType(library);
}
void Fonts::clear() {
    for (auto& [key, value] : cache) SDL_DestroyTexture(std::get<0>(value));
    cache.clear();
}
int Fonts::measure(bool is_heading, int px, const std::string& s) {
    FT_Face face = is_heading ? heading : body;
    if (!face || FT_Set_Pixel_Sizes(face, 0, px)) return 0;
    int w = 0;
    for (size_t i = 0; i < s.size();) {
        if (!FT_Load_Char(face, decode(s, i), FT_LOAD_DEFAULT)) w += face->glyph->advance.x >> 6;
    }
    return w;
}
SDL_Texture* Fonts::text(SDL_Renderer* r, bool is_heading, int px, const std::string& s, uint32_t rgba, int& w, int& h) {
    if (owner != r) { clear(); owner = r; }
    const auto key = std::make_tuple(is_heading, px, rgba, s);
    if (auto it = cache.find(key); it != cache.end()) {
        w = std::get<1>(it->second); h = std::get<2>(it->second);
        return std::get<0>(it->second);
    }
    if (cache.size() > 512) clear(); // resizing regenerates every label
    FT_Face face = is_heading ? heading : body;
    w = std::max(1, measure(is_heading, px, s));
    const int ascent = face->size->metrics.ascender >> 6;
    h = std::max(1, int((face->size->metrics.ascender - face->size->metrics.descender) >> 6));
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surface) return nullptr;
    SDL_FillRect(surface, nullptr, 0);
    int pen = 0;
    for (size_t i = 0; i < s.size();) {
        if (FT_Load_Char(face, decode(s, i), FT_LOAD_RENDER)) continue;
        const FT_Bitmap& bm = face->glyph->bitmap;
        for (unsigned y = 0; y < bm.rows; ++y) {
            for (unsigned x = 0; x < bm.width; ++x) {
                const int tx = pen + face->glyph->bitmap_left + int(x), ty = ascent - face->glyph->bitmap_top + int(y);
                if (tx < 0 || ty < 0 || tx >= w || ty >= h) continue;
                auto* px_out = static_cast<uint8_t*>(surface->pixels) + ty * surface->pitch + tx * 4;
                px_out[0] = rgba >> 24; px_out[1] = (rgba >> 16) & 255; px_out[2] = (rgba >> 8) & 255;
                px_out[3] = std::max<uint8_t>(px_out[3], bm.buffer[y * bm.pitch + x]);
            }
        }
        pen += face->glyph->advance.x >> 6;
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(r, surface);
    SDL_FreeSurface(surface);
    if (texture) SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    cache[key] = {texture, w, h};
    return texture;
}
namespace {
// Draws one line, shrinking it until it fits the box width. Never clips.
void label(SDL_Renderer* r, Fonts& f, bool heading, int px, const std::string& s, uint32_t c, Rect box, bool center = false) {
    if (s.empty()) return;
    while (px > MIN_LEGIBLE_PX && f.measure(heading, px, s) > box.w) --px;
    f.smallest_px = std::min(f.smallest_px, px);
    int w, h;
    SDL_Texture* t = f.text(r, heading, px, s, c, w, h);
    if (!t) return;
    SDL_Rect dst{center ? box.x + (box.w - w) / 2 : box.x, box.y + (box.h - h) / 2, w, h};
    SDL_RenderCopy(r, t, nullptr, &dst);
}
std::deque<std::string> wrap(Fonts& f, int px, const std::string& s, int width) {
    std::deque<std::string> lines;
    std::string line, word;
    auto flush_word = [&] {
        if (word.empty()) return;
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && f.measure(false, px, candidate) > width) { lines.push_back(line); line = word; }
        else line = candidate;
        word.clear();
    };
    for (char c : s) {
        if (c == ' ' || c == '\n') { flush_word(); if (c == '\n') { lines.push_back(line); line.clear(); } }
        else word += c;
    }
    flush_word();
    if (!line.empty()) lines.push_back(line);
    return lines;
}
void background(SDL_Renderer* r, int w, int h, uint32_t ticks, float scale) {
    fill(r, {0, 0, w, h}, SLOPE);
    // Same silhouette as assets/sbk-ui/slope.svg, stretched to the window.
    auto ridge = [&](std::initializer_list<std::pair<int, int>> pts, uint32_t c) {
        std::vector<SDL_Vertex> v;
        std::vector<int> idx;
        const SDL_Color col{Uint8(c >> 24), Uint8((c >> 16) & 255), Uint8((c >> 8) & 255), 255};
        for (auto [x, y] : pts) v.push_back({{x * w / 1920.0f, y * h / 1080.0f}, col, {0, 0}});
        v.push_back({{float(w), float(h)}, col, {0, 0}});
        v.push_back({{0.0f, float(h)}, col, {0, 0}});
        const int n = int(v.size());
        for (int i = 0; i + 1 < n - 2; ++i) { idx.insert(idx.end(), {i, i + 1, n - 1}); idx.insert(idx.end(), {i + 1, n - 2, n - 1}); }
        SDL_RenderGeometry(r, nullptr, v.data(), n, idx.data(), int(idx.size()));
    };
    ridge({{0, 940}, {310, 660}, {410, 740}, {620, 500}, {810, 750}, {1070, 440}, {1270, 670}, {1430, 500}, {1920, 940}}, ICE);
    ridge({{0, 1020}, {520, 870}, {930, 1000}, {1390, 830}, {1920, 960}}, SNOW);
    // A few square flakes; motion is time-based and cheap.
    const int size = std::max(2, int(6 * scale));
    for (int i = 0; i < 28; ++i) {
        const int x = (i * 7919 + int(ticks / (40 + i % 5 * 10))) % std::max(1, w);
        const int y = (i * 104729 + int(ticks / (18 + i % 7 * 4))) % std::max(1, h);
        fill(r, {x, y, size, size}, SNOW);
    }
}
}
Layout render(SDL_Renderer* r, Fonts& f, const View& v, int w, int h, uint32_t ticks) {
    const Layout l = layout_for(w, h, v.button_count);
    const float s = l.scale;
    const int step = std::max(2, int(8 * s)), border = std::max(2, int(6 * s));
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    background(r, w, h, ticks, s);
    stepped(r, {l.panel.x + border, l.panel.y + border, l.panel.w, l.panel.h}, step, INK); // drop shadow
    framed(r, l.panel, step, border, SNOW);
    label(r, f, true, int(56 * s), "SNOWBOARD KIDS", INK, l.title);
    framed(r, l.tag, std::max(1, step / 2), std::max(2, int(3 * s)), SUN);
    label(r, f, false, int(20 * s), "PC PORT  /  FIRST-TIME SETUP", INK, l.tag, true);

    const int current = order(v.model.phase);
    for (int i = 0; i < 8; ++i) {
        const Rect row{l.steps.x, l.steps.y + int(i * 50 * s), l.steps.w, int(44 * s)};
        const int reached = v.model.phase == Phase::Error ? order(v.failed_at) : current;
        const bool done = reached > i;
        const bool active = reached == i;
        const bool failed = active && v.model.phase == Phase::Error;
        const Rect box{row.x, row.y + int(8 * s), int(28 * s), int(28 * s)};
        fill(r, box, INK);
        const int b = std::max(2, int(4 * s));
        const bool blink = (ticks / 400) % 2 == 0;
        fill(r, {box.x + b, box.y + b, box.w - 2 * b, box.h - 2 * b}, done ? INK : failed ? WARN : active ? (blink ? SUN : SNOW) : SNOW);
        std::string text = STEPS[i].label;
        if (STEPS[i].phase == Phase::Compile && v.model.total > 0 && current >= i)
            text += " [" + std::to_string(v.model.current) + "/" + std::to_string(v.model.total) + "]";
        label(r, f, false, int(26 * s), text, failed ? WARN : done || active ? INK : MUTED,
              {row.x + int(44 * s), row.y, row.w - int(44 * s), row.h});
    }

    const bool error = v.model.phase == Phase::Error;
    label(r, f, true, int(40 * s), phase_name(v.model.phase), error ? WARN : INK, l.heading);
    const int body_px = int(24 * s);
    auto lines = wrap(f, body_px, v.message, l.detail.w);
    const int line_h = int(34 * s), max_lines = std::max(1, l.detail.h / line_h);
    while (int(lines.size()) > max_lines) lines.pop_front(); // keep the newest text
    for (size_t i = 0; i < lines.size(); ++i)
        label(r, f, false, body_px, lines[i], error ? WARN : INK, {l.detail.x, l.detail.y + int(i) * line_h, l.detail.w, line_h});

    // Determinate only for real compile counts; other phases say they are working.
    if (v.model.phase == Phase::Compile && v.model.total > 0) {
        framed(r, l.progress, std::max(1, step / 2), std::max(2, int(3 * s)), ICE);
        const int inset = std::max(2, int(6 * s));
        fill(r, {l.progress.x + inset, l.progress.y + inset,
                 (l.progress.w - 2 * inset) * v.model.current / v.model.total, l.progress.h - 2 * inset}, SLOPE);
        label(r, f, false, int(22 * s), "Compiled " + std::to_string(v.model.current) + " of " +
              std::to_string(v.model.total) + " translation units", INK, l.progress_label);
    } else if (current > 0 && v.model.phase != Phase::Ready) {
        label(r, f, false, int(22 * s), std::string("Working") + std::string(1 + (ticks / 350) % 3, '.'), MUTED, l.progress_label);
    }

    for (int i = 0; i < l.button_count; ++i) {
        const Rect b = l.buttons[i];
        const bool focus = v.focus == i, hover = v.hover == i;
        const int lift = focus ? 0 : std::max(2, int(6 * s));
        stepped(r, {b.x + std::max(2, int(6 * s)), b.y + std::max(2, int(6 * s)), b.w - lift, b.h - lift}, step / 2 + 1, INK);
        framed(r, {b.x, b.y, b.w - lift, b.h - lift}, std::max(1, step / 2), focus ? std::max(3, int(5 * s)) : std::max(2, int(3 * s)),
               focus ? SUN : hover ? ICE : SNOW);
        label(r, f, true, int(28 * s), action_label(v.buttons[i]), INK,
              {b.x + int(8 * s), b.y, b.w - lift - int(16 * s), b.h - lift}, true);
    }
    return l;
}
namespace {
struct Session {
    const Host& host;
    View view;
    Process process;
    std::filesystem::path rom;
    std::deque<std::string> recent; // builder's own diagnostics for the error screen
    bool cancelled = false;
    void enter(Phase p, std::string message) {
        if (p == Phase::Error) view.failed_at = view.model.phase;
        view.model.phase = p; view.message = std::move(message);
        if (p != Phase::Compile) { view.model.current = view.model.total = 0; }
        set_buttons(view);
    }
    // Validates on the host, then hands generation to the builder process.
    void start(const std::filesystem::path& selected) {
        rom = selected;
        enter(Phase::Validate, "Checking " + rom.filename().string() + " against the supported USA release.");
        if (auto reason = host.validate_rom(rom); !reason.empty()) { enter(Phase::Error, reason); return; }
        std::error_code ec;
        std::filesystem::create_directories(host.log.parent_path(), ec);
        std::string error;
        const auto args = builder_command(host.install_dir, rom, host.module_dir);
        if (args.empty()) { enter(Phase::Error, "The module builder script is missing from this installation."); return; }
        recent.clear();
        cancelled = false;
        if (!process.start(args, host.log, error)) { enter(Phase::Error, error); return; }
        enter(Phase::Validate, "Builder started. Your ROM stays on this computer.");
    }
    void line(const std::string& text) {
        const Phase before = view.model.phase;
        view.model.line(text);
        if (text.rfind("SBK_PROGRESS\t", 0) == 0) {
            if (view.model.phase != before) {
                static const char* notes[] = {"", "", "Checking ROM and C++ toolchain.", "Locating this project's translated CPU code.",
                    "Generating audio microcode from your ROM.", "Compiling the game module with your C++ compiler.",
                    "Checking module ABI and exported symbols.", "Installing into your user data folder.", "", ""};
                view.message = notes[int(view.model.phase)];
            }
            return;
        }
        if (!text.empty()) { recent.push_back(text); if (recent.size() > 4) recent.pop_front(); }
    }
    void finished() {
        if (view.model.phase == Phase::Error) return;
        if (process.exit_code() != 0) {
            std::string reason = "Module build stopped (exit " + std::to_string(process.exit_code()) + ").";
            for (const auto& l : recent) reason += "\n" + l;
            reason += "\nLog: " + host.log.string();
            enter(Phase::Error, reason);
            return;
        }
        enter(Phase::ValidateModule, "Loading the new module and checking its ABI.");
        if (auto reason = host.load_module(); !reason.empty()) { enter(Phase::Error, reason); return; }
        enter(Phase::Ready, "Game module installed in your user data folder. Have fun on the slopes!");
    }
};
Outcome run_console(const Host& host) {
    Session session{host};
    std::filesystem::path rom = !host.initial_rom.empty() ? host.initial_rom : host.saved_rom;
    if (rom.empty()) {
        std::puts("[FIRST-RUN] Please select your Snowboard Kids (USA) ROM to generate the local game module...");
        if (!host.choose_rom || !host.choose_rom(rom)) { std::puts("[FIRST-RUN] No ROM selected. Pass one: SnowboardKidsEngine <path-to-rom>"); return Outcome::Quit; }
    }
    session.start(rom);
    Phase shown = Phase::Missing;
    int shown_count = -1;
    while (session.process.running()) {
        session.process.poll([&](const std::string& l) { session.line(l); });
        const auto& m = session.view.model;
        if (m.phase != shown || m.current != shown_count) {
            const std::string counts = m.total ? " [" + std::to_string(m.current) + "/" + std::to_string(m.total) + "]" : "";
            std::printf("[FIRST-RUN] %s%s\n", phase_name(m.phase), counts.c_str());
            shown = m.phase; shown_count = m.current;
        }
        SDL_Delay(10);
    }
    if (session.view.model.phase != Phase::Error) session.finished();
    std::printf("[FIRST-RUN] %s: %s\n", phase_name(session.view.model.phase), session.view.message.c_str());
    return session.view.model.phase == Phase::Ready ? Outcome::Ready : Outcome::Failed;
}
}
Outcome run(const Host& host) {
    const char* force_console = std::getenv("SBK_FIRST_RUN_CONSOLE");
    const bool headless = force_console && std::strcmp(force_console, "1") == 0;
    if (headless || SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) return run_console(host);
    SDL_Window* window = SDL_CreateWindow("Snowboard Kids: Recompiled - Setup", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC) : nullptr;
    if (!renderer && window) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    auto fonts = std::make_unique<Fonts>(host.assets_dir);
    if (!renderer || !fonts->ok()) {
        std::fprintf(stderr, "[FIRST-RUN] Setup window unavailable (%s); using console setup.\n", renderer ? "fonts missing" : SDL_GetError());
        fonts.reset();
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS);
        return run_console(host);
    }
    SDL_SetWindowMinimumSize(window, 960, 540);
    std::vector<SDL_GameController*> pads;
    Session session{host};
    session.view.saved_rom = host.saved_rom.empty() ? "" : host.saved_rom.filename().string();
    session.enter(Phase::Missing, "No game module was found. Choose your own Snowboard Kids (USA) ROM and this "
                  "computer builds one locally. Nothing is downloaded or uploaded.");
    if (!session.view.saved_rom.empty()) session.view.message += "\nSaved ROM: " + session.view.saved_rom;
    const bool automatic = !host.initial_rom.empty();
    if (automatic) session.start(host.initial_rom);
    Outcome outcome = Outcome::Quit;
    bool running = true;
    Layout last = layout_for(1280, 720, session.view.button_count);
    int axis_latch = 0;
    auto activate = [&](Action a) {
        std::filesystem::path picked;
        switch (a) {
        case Action::UseSaved: session.start(host.saved_rom); break;
        case Action::Choose:
            session.enter(Phase::Select, "Pick the ROM file in the dialog.");
            if (host.choose_rom && host.choose_rom(picked)) session.start(picked);
            else session.enter(Phase::Missing, "No ROM selected. Choose your Snowboard Kids (USA) ROM to continue.");
            break;
        case Action::Retry: session.start(session.rom.empty() ? host.saved_rom : session.rom); break;
        case Action::Cancel: session.cancelled = true; session.process.cancel(); session.view.message = "Stopping the builder..."; break;
        case Action::Start: outcome = Outcome::Ready; running = false; break;
        case Action::Quit: outcome = Outcome::Quit; running = false; break;
        case Action::None: break;
        }
    };
    auto move = [&](int delta) {
        auto& v = session.view;
        if (v.button_count) v.focus = (v.focus + delta + v.button_count) % v.button_count;
    };
    auto back = [&] {
        const auto& v = session.view;
        for (int i = 0; i < v.button_count; ++i)
            if (v.buttons[i] == Action::Cancel || v.buttons[i] == Action::Quit) { activate(v.buttons[i]); return; }
    };
    auto hit = [&](int x, int y) {
        int ww, wh, ow, oh;
        SDL_GetWindowSize(window, &ww, &wh);
        SDL_GetRendererOutputSize(renderer, &ow, &oh);
        x = x * ow / std::max(1, ww); y = y * oh / std::max(1, wh);
        for (int i = 0; i < last.button_count; ++i) {
            const Rect b = last.buttons[i];
            if (x >= b.x && y >= b.y && x < b.x + b.w && y < b.y + b.h) return i;
        }
        return -1;
    };
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            auto& v = session.view;
            switch (e.type) {
            case SDL_QUIT: session.process.cancel(); outcome = Outcome::Quit; running = false; break;
            case SDL_KEYDOWN:
                switch (e.key.keysym.sym) {
                case SDLK_LEFT: case SDLK_UP: move(-1); break;
                case SDLK_RIGHT: case SDLK_DOWN: move(1); break;
                case SDLK_TAB: move((e.key.keysym.mod & KMOD_SHIFT) ? -1 : 1); break;
                case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE:
                    if (v.button_count) activate(v.buttons[v.focus]);
                    break;
                case SDLK_ESCAPE: back(); break;
                }
                break;
            case SDL_CONTROLLERDEVICEADDED:
                if (auto* pad = SDL_GameControllerOpen(e.cdevice.which)) pads.push_back(pad);
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT: case SDL_CONTROLLER_BUTTON_DPAD_UP: move(-1); break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: case SDL_CONTROLLER_BUTTON_DPAD_DOWN: move(1); break;
                case SDL_CONTROLLER_BUTTON_A: if (v.button_count) activate(v.buttons[v.focus]); break;
                // B, or X (the N64 B position), like Menu Back in the options overlay.
                case SDL_CONTROLLER_BUTTON_B: case SDL_CONTROLLER_BUTTON_X: back(); break;
                }
                break;
            case SDL_CONTROLLERAXISMOTION:
                if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX || e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                    const int dir = e.caxis.value > 20000 ? 1 : e.caxis.value < -20000 ? -1 : 0;
                    if (dir && !axis_latch) move(dir);
                    axis_latch = dir;
                }
                break;
            case SDL_MOUSEMOTION: {
                const int i = hit(e.motion.x, e.motion.y);
                v.hover = i;
                if (i >= 0) v.focus = i;
                break;
            }
            case SDL_MOUSEBUTTONUP:
                if (e.button.button == SDL_BUTTON_LEFT) {
                    const int i = hit(e.button.x, e.button.y);
                    if (i >= 0) activate(v.buttons[i]);
                }
                break;
            }
        }
        if (session.process.running()) {
            session.process.poll([&](const std::string& l) { session.line(l); });
            if (!session.process.running()) {
                if (session.cancelled) session.enter(Phase::Missing, "Setup cancelled. Nothing was installed.");
                else session.finished();
                if (automatic && session.view.model.phase == Phase::Ready) { outcome = Outcome::Ready; running = false; }
            }
        }
        int w, h;
        SDL_GetRendererOutputSize(renderer, &w, &h);
        last = render(renderer, *fonts, session.view, w, h, SDL_GetTicks());
        SDL_RenderPresent(renderer);
        SDL_Delay(8);
    }
    // Leave SDL as found; the game window and input layer initialize their own state.
    for (auto* pad : pads) SDL_GameControllerClose(pad);
    fonts.reset();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS);
    return outcome;
}
}
