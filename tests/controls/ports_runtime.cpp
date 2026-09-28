#include "ultramodern/input.hpp"
#include "ultramodern/ultra64.h"
#include <cassert>

extern "C" void osContGetReadData(OSContPad* pads);
namespace ultramodern { void send_si_message() {} }

static bool port2_connected = false;
static uint16_t port0_button = 0x1000;

static bool input(int port, uint16_t* buttons, float* x, float* y) {
    if (port == 2 && !port2_connected) return false;
    *buttons = port == 0 ? port0_button : (port == 2 ? 0x8000 : 0);
    *x = 0;
    *y = 0;
    return true;
}

int main() {
    ultramodern::input::callbacks_t callbacks{};
    callbacks.get_input = input;
    ultramodern::input::set_callbacks(callbacks);
    osContSetCh(nullptr, 4);
    OSContPad pads[4]{};
    for (auto& pad : pads) {
        pad.button = 0xFFFF;
        pad.stick_x = 40;
        pad.stick_y = -40;
    }
    osContGetReadData(pads);
    assert(pads[0].button == 0x1000 && pads[0].err_no == 0);
    assert(pads[1].button == 0 && pads[1].err_no == 0);
    assert(pads[2].button == 0 && pads[2].stick_x == 0 &&
           pads[2].stick_y == 0 && pads[2].err_no == 8);
    assert(pads[3].button == 0 && pads[3].err_no == 0);
    for (int cycle = 0; cycle < 100; ++cycle) {
        port2_connected = (cycle & 1) != 0;
        port0_button = (cycle & 1) != 0 ? 0x1000 : 0;
        osContGetReadData(pads);
        assert(pads[0].button == port0_button && pads[0].err_no == 0);
        assert(pads[1].button == 0 && pads[1].err_no == 0);
        assert(pads[2].button == (port2_connected ? 0x8000 : 0) &&
               pads[2].stick_x == 0 && pads[2].stick_y == 0 &&
               pads[2].err_no == (port2_connected ? 0 : 8));
        assert(pads[3].button == 0 && pads[3].err_no == 0);
    }
}
