#include "virtual_pad.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include <SDL2/SDL.h>

namespace sbk::virtual_pad {
namespace {
// Guest symbols of the US ROM (snowboardkids-decomp build map).
constexpr uint32_t kControllerPads = 0x800E4C00; // OSContPad[4] written by osContGetReadData
constexpr uint32_t kPlayerStickX = 0x80123788;   // s8[4], after gAnalogStickResponseCurve
constexpr uint32_t kPlayerStickY = 0x8012378C;
constexpr uint32_t kPlayerInputHeld = 0x80123758; // s32[4]
constexpr uint32_t kRumbleMotorStatuses = 0x800EC898; // s32[4], osMotorInit result per port

uint8_t* rdram = nullptr;
const char* path = nullptr;
SDL_Joystick* joystick = nullptr;
std::string last_line, last_guest;

uint8_t byte(uint32_t address) { return rdram[(address - 0x80000000u) ^ 3]; }
uint32_t word(uint32_t address) { return *reinterpret_cast<const uint32_t*>(rdram + (address - 0x80000000u)); }

int SDLCALL rumble(void*, Uint16 low, Uint16 high) {
    std::fprintf(stderr, "VPAD rumble low=%u high=%u\n", unsigned(low), unsigned(high));
    return 0;
}

void attach() {
    SDL_VirtualJoystickDesc desc{};
    desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    desc.naxes = SDL_CONTROLLER_AXIS_MAX;
    desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    desc.name = "SBK virtual pad";
    desc.Rumble = rumble;
    const int index = SDL_JoystickAttachVirtualEx(&desc);
    joystick = index < 0 ? nullptr : SDL_JoystickOpen(index);
    std::fprintf(stderr, "VPAD attach %s (%s)\n", joystick ? "ok" : "failed", joystick ? SDL_JoystickName(joystick) : SDL_GetError());
}

void detach() {
    const SDL_JoystickID id = SDL_JoystickInstanceID(joystick);
    SDL_JoystickClose(joystick);
    joystick = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_JoystickGetDeviceInstanceID(i) == id) SDL_JoystickDetachVirtual(i);
    }
    std::fprintf(stderr, "VPAD detach\n");
}

void apply(const std::string& line) {
    if (line == "detach") {
        if (joystick) detach();
        return;
    }
    if (!joystick) attach();
    if (!joystick) return;
    std::istringstream fields(line);
    int lx = 0, ly = 0;
    unsigned buttons = 0;
    fields >> lx >> ly >> std::hex >> buttons;
    SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_LEFTX, Sint16(lx));
    SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_LEFTY, Sint16(ly));
    for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b) SDL_JoystickSetVirtualButton(joystick, b, (buttons >> b) & 1);
    std::fprintf(stderr, "VPAD host lx=%d ly=%d buttons=%x\n", lx, ly, buttons);
}

void log_guest() {
    if (!rdram) return;
    char buf[160];
    std::snprintf(buf, sizeof(buf), "pad0 button=%04x stick_x=%d stick_y=%d err=%u | stickX=%d stickY=%d held=%08x motor0=%d",
        unsigned(byte(kControllerPads) << 8 | byte(kControllerPads + 1)), int(int8_t(byte(kControllerPads + 2))),
        int(int8_t(byte(kControllerPads + 3))), unsigned(byte(kControllerPads + 4)),
        int(int8_t(byte(kPlayerStickX))), int(int8_t(byte(kPlayerStickY))), unsigned(word(kPlayerInputHeld)),
        int(int32_t(word(kRumbleMotorStatuses))));
    if (last_guest != buf) {
        last_guest = buf;
        std::fprintf(stderr, "VPAD guest %s\n", buf);
    }
}
}

void set_memory(uint8_t* memory) { rdram = memory; }

void poll() {
    static const bool enabled = (path = std::getenv("SBK_TEST_PAD_FILE")) != nullptr;
    if (!enabled) return;
    std::ifstream input(path);
    std::string line;
    if (std::getline(input, line) && line != last_line) {
        last_line = line;
        apply(line);
    }
    log_guest();
}
}
