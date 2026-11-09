#include "galaxy/power_policy.h"

#include <cstdio>

namespace {
bool passed = true;

void expect(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAILED: %s\n", what);
        passed = false;
    }
}
}  // namespace

int main() {
    using namespace galaxy::host;
    PowerFacts facts;
    facts.now_ms = 1'000'000;
    facts.last_input_ms = facts.now_ms - 1'000;

    // Playing in front: full speed, display held.
    PowerWish wish = power_wish(facts);
    expect(!wish.efficient && wish.display_required, "in front and just played: full speed, display held");

    // Idle a minute in front: efficiency, the display still held for the playing window.
    facts.last_input_ms = facts.now_ms - kPowerIdleMs;
    wish = power_wish(facts);
    expect(wish.efficient && wish.display_required, "idle for a minute: efficiency, display still held");
    facts.last_input_ms = facts.now_ms - kPowerIdleMs + 1;
    expect(!power_wish(facts).efficient, "one millisecond short of a minute is not idle");

    // Idle past the playing window: the display is let go.
    facts.last_input_ms = facts.now_ms - kPowerPlayingMs;
    wish = power_wish(facts);
    expect(wish.efficient && !wish.display_required, "idle for minutes: the display is let go");

    // A cutscene with no input: full speed and the display held, until its sound has stopped for two seconds.
    facts.last_movie_ms = facts.now_ms - 500;
    wish = power_wish(facts);
    expect(!wish.efficient && wish.display_required, "a cutscene keeps full speed and the display");
    facts.last_movie_ms = facts.now_ms - kPowerMovieMs;
    wish = power_wish(facts);
    expect(wish.efficient && !wish.display_required, "a cutscene that ended no longer counts");
    facts.last_movie_ms = -1;

    // Not in front: efficiency and never the display, whatever was just pressed.
    facts.last_input_ms = facts.now_ms - 10;
    facts.background = true;
    wish = power_wish(facts);
    expect(wish.efficient && !wish.display_required, "in the background: efficiency, no display hold");
    facts.last_movie_ms = facts.now_ms - 10;
    expect(!power_wish(facts).display_required, "a cutscene behind another window does not hold the display");
    facts.background = false;
    facts.minimised = true;
    wish = power_wish(facts);
    expect(wish.efficient && !wish.display_required, "minimised: efficiency, no display hold");

    std::puts(passed ? "Power policy checks passed" : "Power policy checks FAILED");
    return passed ? 0 : 1;
}
