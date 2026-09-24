#pragma once

#include <string>

// Minimal transient savestate feedback over the existing RecompFrontend UI:
// one non-modal context that captures neither input nor mouse, so gameplay
// input keeps flowing while it is visible. Main (frontend) thread only.
// SBK_SAVESTATE_TOAST=0 disables it (messages are still logged).
namespace sbk::savestate::toast {
void show(const std::string& text, bool error);
// Hides the message once its display time has elapsed.
void update();
}
