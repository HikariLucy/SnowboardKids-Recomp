#include "main/input_ports.hpp"
#include <cassert>

int main() {
    using sbk::input_ports::presence_mask;
    // Before assignment, the guest can see the number of attached SDL pads.
    assert(presence_mask(true, 1, 0) == 0x1);
    assert(presence_mask(true, 2, 0) == 0x3);
    assert(presence_mask(true, 3, 0) == 0x7);
    assert(presence_mask(true, 4, 0) == 0xf);
    // After assignment, only independently assigned and attached ports exist.
    assert(presence_mask(false, 4, 0xf) == 0xf);
    assert(presence_mask(false, 3, 0xb) == 0xb);
    assert(presence_mask(false, 4, 0xb) == 0xb);
    assert(presence_mask(false, 4, 0x0) == 0x0);
}
