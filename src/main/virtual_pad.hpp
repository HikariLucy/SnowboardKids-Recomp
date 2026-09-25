#pragma once

#include <cstdint>

// DEVELOPMENT ONLY input probe (CONTROL-P1). With SBK_TEST_PAD_FILE set, a
// virtual SDL game controller is attached and driven from that file, so the
// real SDL GameController -> RecompInput -> runtime -> guest path runs without
// hardware. The file holds one line: "<leftx> <lefty> <buttons>" with SDL axis
// values (-32768..32767) and a hex mask of SDL_GameControllerButton bits.
// "detach" disconnects the pad. Guest pad bytes are logged when they change,
// and host rumble requests to the pad are logged.
namespace sbk::virtual_pad {
void set_memory(uint8_t* rdram);
void poll(); // frontend thread, once per update_gfx
}
