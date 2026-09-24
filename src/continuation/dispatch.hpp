#pragma once
#include "frame.hpp"
#include "recomp.h"
#include <stdexcept>
namespace sbk::continuation {
using Step = Action(uint8_t*, recomp_context*, Frame&);
struct Descriptor {
    uint64_t id;
    uint32_t guest_address;
    size_t scratch_count;
    Step* step;
    recomp_func_t* token; // transient lookup adapter, never continuation identity
    const char* name;
};
bool register_function(Descriptor descriptor);
const Descriptor& descriptor(uint64_t id);
const Descriptor& descriptor_for_token(recomp_func_t* token);
Action step(uint8_t* rdram, recomp_context* context, Frame& frame);
Frame make_frame(uint64_t id);
// A runtime must supply this entry. Generated calls never invoke these thunks.
void enter(uint64_t id, uint8_t* rdram, recomp_context* context);
}
