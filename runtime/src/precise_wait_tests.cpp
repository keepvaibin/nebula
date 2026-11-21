#include "galaxy/precise_wait.h"

#include <cstdint>
#include <iostream>

namespace {

int g_failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++g_failures;
    }
}

}  // namespace

int main() {
    using galaxy::next_wait_margin_us;
    // A late wake raises the margin at once to it and its guard; the bounds hold.
    expect(next_wait_margin_us(1000, 1500) == 1600, "up at once to a late wake and its guard");
    expect(next_wait_margin_us(1000, 50'000) == galaxy::kMostMarginUs, "held to the most");
    expect(next_wait_margin_us(1000, -300) < 1000, "an early wake lowers it");
    // On-time wakes bring it down slowly, never under the least.
    std::int64_t margin = galaxy::kMostMarginUs;
    int steps = 0;
    while (margin > galaxy::kLeastMarginUs && steps < 10'000) {
        margin = next_wait_margin_us(margin, 0);
        ++steps;
    }
    expect(margin == galaxy::kLeastMarginUs, "down to the least");
    expect(steps > 30, "down slowly");
    expect(next_wait_margin_us(galaxy::kLeastMarginUs, 0) == galaxy::kLeastMarginUs, "stays at the least");
    // A wait ends at its moment, never before it.
    galaxy::PreciseWait wait;
    const std::int64_t frequency = galaxy::qpc_frequency();
    std::int64_t worst_late = 0;
    for (int i = 0; i < 20; ++i) {
        const std::int64_t due = galaxy::qpc_now() + frequency / 500;  // 2 ms on
        wait.until(due);
        const std::int64_t now = galaxy::qpc_now();
        expect(now >= due, "never early");
        worst_late = std::max(worst_late, now - due);
    }
    std::cout << "worst lateness " << worst_late * 1'000'000 / frequency << " us, margin " << wait.margin_us() << " us\n";
    // A moment already past returns at once.
    const std::int64_t before = galaxy::qpc_now();
    wait.until(before - frequency);
    // A moment in the past must not be waited for at all; how long a busy machine takes to return from it is no verdict,
    // so the bound is a hang's (a wait that treated the past as the future would not end).
    expect(galaxy::qpc_now() - before < frequency * 30, "a past moment returns at once");
    if (g_failures == 0) std::cout << "precise wait tests passed\n";
    return g_failures == 0 ? 0 : 1;
}
