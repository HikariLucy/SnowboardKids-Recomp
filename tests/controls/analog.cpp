// CONTROL-P1 analog path, no SDL device: the real RecompInput radial deadzone
// (recompinput/deadzone.h) feeding the real N64ModernRuntime stick conversion
// (convert_to_n64_range in libultramodern), as get_n64_input -> osContGetReadData do.
#include "recompinput/deadzone.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

void convert_to_n64_range(float x, float y, int8_t& stick_x, int8_t& stick_y); // ultramodern/src/input.cpp
namespace ultramodern { void send_si_message() {} } // only referenced by osContStartReadData in the same object

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

constexpr float kDeadzone = 0.05f; // RecompFrontend default joystick_deadzone = 5

struct Stick { int x, y; };
// Host axis values in [-1, 1] -> guest OSContPad stick bytes, with the per-axis
// clamp get_n64_input applies after adding keyboard input.
Stick guest(float x, float y) {
    float dx, dy;
    recompinput::radial_deadzone(x, y, kDeadzone, &dx, &dy);
    int8_t sx, sy;
    convert_to_n64_range(std::clamp(dx, -1.0f, 1.0f), std::clamp(dy, -1.0f, 1.0f), sx, sy);
    return {sx, sy};
}
float angle(float x, float y) { return std::atan2(y, x); }
}

int main() {
    const Stick neutral = guest(0, 0);
    check(neutral.x == 0 && neutral.y == 0, "neutral -> 0,0");

    bool noise = true;
    for (float a = 0; a < 6.3f; a += 0.1f) {
        const Stick s = guest(0.049f * std::cos(a), 0.049f * std::sin(a));
        noise = noise && s.x == 0 && s.y == 0;
    }
    check(noise, "noise inside the radial deadzone -> 0,0 in every direction");

    const Stick r = guest(1, 0), l = guest(-1, 0), u = guest(0, 1), d = guest(0, -1);
    check(r.x >= 80 && r.y == 0 && l.x <= -80 && l.y == 0, "full X cardinal -> +-80..82, no Y leak");
    check(u.y >= 80 && u.x == 0 && d.y <= -80 && d.x == 0, "full Y cardinal -> +-80..82, no X leak");

    const Stick diag = guest(0.7071f, 0.7071f);
    check(diag.x == diag.y && diag.x >= 64 && diag.x <= 70, "full diagonal -> equal axes on the N64 octagon (~69)");
    const Stick square = guest(1, 1); // square-gated stick or keyboard W+D
    check(square.x == diag.x && square.y == diag.y, "(1,1) is normalized to the unit circle: no sqrt(2) overflow");

    const Stick half = guest(0.5f, 0), quarter = guest(0.25f, 0);
    check(half.x > 30 && half.x < 45 && quarter.x > 10 && quarter.x < half.x, "partial deflection keeps relative magnitude");

    const Stick small = guest(0.06f, 0.30f);
    check(small.x > 0 && std::fabs(angle(small.x, small.y) - angle(0.06f, 0.30f)) < 0.1f,
          "small diagonal keeps its direction (the per-axis deadzone bent it by ~9 degrees)");

    bool bounded = true;
    for (float a = 0; a < 6.3f; a += 0.05f) {
        for (float m : {0.3f, 0.9f, 1.0f, 1.2f, 1.5f}) {
            const Stick s = guest(m * std::cos(a), m * std::sin(a));
            bounded = bounded && std::abs(s.x) <= 82 && std::abs(s.y) <= 82;
        }
    }
    check(bounded, "no direction or overdriven magnitude exceeds the guest range");

    float nx, ny;
    recompinput::radial_deadzone(NAN, 0.5f, kDeadzone, &nx, &ny);
    check(nx == 0 && ny == 0, "NaN input -> neutral");

    std::printf("%s controls analog (%d failure%s)\n", failures ? "FAIL" : "PASS", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
