#include "galaxy/frame_phase.h"
#include "galaxy/step_lock.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

using galaxy::host::FramePhase;
using galaxy::host::DisplayRefresh;

int g_failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++g_failures;
    }
}

constexpr std::int64_t kFrame = 1'000'000'000 / 60;

// The time from a frame's present to the next refresh after it.
std::int64_t to_refresh(std::int64_t present, const DisplayRefresh& display) {
    std::int64_t left = (display.vblank_ns - present) % display.period_ns;
    return left < 0 ? left + display.period_ns : left;
}

// Frames of `work` on this display: the last frame's distance from its present to a refresh, the longest and shortest
// start-to-start times once settled.
struct Result {
    std::int64_t last_gap = 0;
    std::int64_t shortest = INT64_MAX;
    std::int64_t longest = 0;
};
Result run(const DisplayRefresh& display, std::int64_t work, std::int64_t frame = kFrame, int frames = 2000) {
    FramePhase phase;
    std::int64_t start = 12'345'678;
    Result result;
    for (int i = 0; i < frames; ++i) {
        const std::int64_t next = phase.next_start(start, work, frame, display, false);
        if (i > frames / 2) {
            result.shortest = std::min(result.shortest, next - start);
            result.longest = std::max(result.longest, next - start);
        }
        start = next;
    }
    result.last_gap = to_refresh(start + work, display);
    return result;
}

void test_counts() {
    const auto refreshes_per_frame = [](std::int64_t period, std::int64_t refresh) {
        return galaxy::step_lock_refreshes(period, refresh, false);
    };
    expect(refreshes_per_frame(kFrame, 1'000'000'000 / 60) == 1, "60 Hz: one refresh a frame");
    expect(refreshes_per_frame(kFrame, 16'683'350) == 1, "59.94 Hz: one refresh a frame");
    expect(refreshes_per_frame(kFrame, 1'000'000'000 / 120) == 2, "120 Hz: two");
    expect(refreshes_per_frame(kFrame, 1'000'000'000 / 240) == 4, "240 Hz: four");
    expect(refreshes_per_frame(kFrame, 1'000'000'000 / 144) == 0, "144 Hz: none fits");
    expect(refreshes_per_frame(kFrame, 1'000'000'000 / 75) == 0, "75 Hz: none fits");
    expect(refreshes_per_frame(kFrame, 0) == 0, "no display");
}

void test_settles() {
    for (const std::int64_t refresh : {std::int64_t{1'000'000'000 / 60}, std::int64_t{16'683'350}, std::int64_t{1'000'000'000 / 120}}) {
        const DisplayRefresh display{987'654, refresh};
        for (const std::int64_t work : {std::int64_t{1'000'000}, std::int64_t{3'000'000}, std::int64_t{5'000'000}}) {
            const Result r = run(display, work);
            expect(r.last_gap >= FramePhase::kMarginNs - 100'000 && r.last_gap <= FramePhase::kMarginNs + 2'000'000,
                   "a settled present lands just before a refresh");
            const std::int64_t step = galaxy::step_lock_refreshes(kFrame, refresh, false) * refresh;
            expect(std::llabs(r.shortest - step) <= 1'000 && std::llabs(r.longest - step) <= 1'000, "settled frames keep the display's period");
        }
    }
}

void test_free_running() {
    FramePhase phase;
    expect(phase.next_start(1000, 3'000'000, kFrame, std::nullopt, false) == 1000 + kFrame, "no display clock: the frame's period");
    expect(phase.next_start(1000, 3'000'000, kFrame, DisplayRefresh{0, 1'000'000'000 / 144}, false) == 1000 + kFrame, "144 Hz: the frame's period");
}

void test_budget() {
    FramePhase phase;
    const DisplayRefresh display{0, kFrame};
    std::int64_t start = 0;
    for (int i = 0; i < 4000; ++i) start = phase.next_start(start, 2'000'000, kFrame, display, false);
    expect(std::llabs(phase.budget_ns() - (2'000'000 + FramePhase::kMarginNs)) < 50'000, "the budget falls to the work and the margin");
    start = phase.next_start(start, 9'000'000, kFrame, display, false);
    expect(phase.budget_ns() == 9'000'000 + FramePhase::kMarginNs, "a longer frame raises it at once");
    const std::int64_t after = phase.budget_ns();
    start = phase.next_start(start, 500'000'000, kFrame, display, false);
    expect(phase.budget_ns() == after, "a stage's load says nothing");
    start = phase.next_start(start, kFrame - 1, kFrame, display, false);
    expect(phase.budget_ns() == kFrame - FramePhase::kMarginNs, "never more than a frame");
    (void)start;
    // One frame's move is bounded.
    FramePhase slow;
    const std::int64_t first = slow.next_start(0, 1'000'000, kFrame, DisplayRefresh{5'000'000, kFrame}, false);
    expect(std::llabs(first - kFrame) <= FramePhase::kSlewNs, "a start moves a little at a time");
}

void test_falls_back() {
    const DisplayRefresh display{0, kFrame};
    FramePhase phase;
    std::int64_t start = 1'000'000;
    for (int i = 0; i < 1000; ++i) start = phase.next_start(start, 3'000'000, kFrame, display, false);
    expect(!phase.plain(), "steady work keeps the phase");
    const std::int64_t missed = phase.next_start(start, phase.budget_ns() + 1, kFrame, display, false);
    expect(phase.plain() && missed == start + kFrame, "a missed frame: plain pacing at once");
    start = missed;
    for (int i = 0; i < FramePhase::kPlainFrames - 2; ++i) {
        const std::int64_t next = phase.next_start(start, 3'000'000, kFrame, display, false);
        expect(next == start + kFrame, "plain frames keep the plain period");
        start = next;
    }
    for (int i = 0; i < 4; ++i) start = phase.next_start(start, 3'000'000, kFrame, display, false);
    expect(!phase.plain(), "the phase comes back after its pause");

    // Work near the frame's length leaves nothing to gain.
    FramePhase weak;
    std::int64_t weak_start = 0;
    for (int i = 0; i < 700; ++i) {
        const std::int64_t next = weak.next_start(weak_start, 13'000'000, kFrame, display, false);
        if (i > 2) expect(next == weak_start + kFrame, "a weak machine: plain pacing");
        weak_start = next;
    }
}

void test_in_step() {
    using galaxy::host::refreshes_in_step;
    const DisplayRefresh now{1'000'000'000, kFrame};
    expect(refreshes_in_step(1'000'000'000 - 30 * kFrame, now), "a fixed display's refreshes");
    expect(refreshes_in_step(1'000'000'000 - 30 * kFrame - 50'000, now), "rounding is in step");
    expect(!refreshes_in_step(1'000'000'000 - 30 * kFrame - kFrame / 3, now), "refreshes that follow something else are not");
    expect(!refreshes_in_step(0, DisplayRefresh{0, 0}), "no display");
}

// The lock rule (galaxy/step_lock.h): 0.2% with in-between pictures off, an exact multiple with them on. It was 1%
// here (and 3% on Android and Apple), so a 60.5 Hz panel set the game's speed, and with in-between pictures on a
// 59.94 Hz display ran the game 0.1% slow.
void test_lock_rule() {
    using galaxy::step_lock_refreshes;
    expect(step_lock_refreshes(kFrame, 16'683'350, false) == 1, "59.94 Hz, in-between pictures off: locked");
    expect(step_lock_refreshes(kFrame, 16'683'350, true) == 0, "59.94 Hz, in-between pictures on: exactly 60");
    expect(step_lock_refreshes(kFrame, 16'528'926, false) == 0, "60.5 Hz (0.8% off): exactly 60");
    expect(step_lock_refreshes(kFrame, 16'260'163, false) == 0, "61.5 Hz (2.5% off): exactly 60");
    expect(step_lock_refreshes(kFrame, 1'000'000'000 / 240, true) == 4, "240 Hz, in-between pictures on: locked");
    expect(step_lock_refreshes(kFrame, 1'000'000'000 / 120, true) == 2, "120 Hz, in-between pictures on: locked");
    expect(step_lock_refreshes(kFrame, 4'170'837, true) == 0, "239.76 Hz, in-between pictures on: exactly 60");
    // The frame phase: in-between pictures on a 59.94 Hz display keep the game's own period.
    FramePhase phase;
    const DisplayRefresh ntsc{987'654, 16'683'350};
    std::int64_t start = 0;
    bool own = true;
    for (int i = 0; i < 600; ++i) {
        const std::int64_t next = phase.next_start(start, 3'000'000, kFrame, ntsc, true);
        own = own && next - start == kFrame;
        start = next;
    }
    expect(own, "in-between pictures on at 59.94 Hz: every step exactly 60 Hz");
}

}  // namespace

int main() {
    test_counts();
    test_lock_rule();
    test_settles();
    test_free_running();
    test_budget();
    test_falls_back();
    test_in_step();
    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "frame phase tests passed\n";
    return 0;
}
