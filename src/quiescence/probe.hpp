#pragma once
#include <cstdint>
namespace sbk::quiescence {
void probe_init(void (*audio_pause)(bool), uint32_t (*audio_size)());
void probe_memory(uint8_t* rdram);
void probe_poll();
bool probe_input(int controller, uint16_t* buttons, float* x, float* y);
}
