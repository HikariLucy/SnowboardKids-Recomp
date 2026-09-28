#include <SDL.h>
#include "recompinput/input_binding.h"
#include "recompinput/input_state.h"
#include "recompui/recompui.h"
#include <cstdio>

static bool capturing = false;
static bool disabled = false;
static bool skip = false;
static int queued = 0;
namespace recompinput {
bool all_input_disabled() { return disabled; }
namespace binding {
bool is_binding() { return capturing; }
bool should_skip_events() { return skip; }
}
void queue_if_enabled(SDL_Event* event);
}
namespace recompui { void queue_event(const SDL_Event&) { ++queued; } }
int main() {
 SDL_Event event{}; event.type = SDL_CONTROLLERBUTTONDOWN;
 recompinput::queue_if_enabled(&event);
 if (queued != 1) return 1;
 capturing = true;
 recompinput::queue_if_enabled(&event);
 if (queued != 1) { std::puts("FAIL captured controller button reached frontend"); return 2; }
 event.type = SDL_KEYUP;
 recompinput::queue_if_enabled(&event);
 if (queued != 2) { std::puts("FAIL release event lost during capture"); return 6; }
 event.type = SDL_CONTROLLERAXISMOTION;
 recompinput::queue_if_enabled(&event);
 if (queued != 2) { std::puts("FAIL captured controller axis reached frontend"); return 3; }
 capturing = false; skip = true;
 recompinput::queue_if_enabled(&event);
 if (queued != 2) return 4;
 skip = false; disabled = true;
 recompinput::queue_if_enabled(&event);
 if (queued != 2) return 5;
 std::puts("PASS capture owns frontend events");
}
