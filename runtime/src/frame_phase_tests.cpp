#include "galaxy/frame_phase.h"
#include "galaxy/render/present_clock.h"
#include "galaxy/step_lock.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>

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
        return galaxy::step_lock_refreshes(period, refresh, false, false);
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
            const std::int64_t step = galaxy::step_lock_refreshes(kFrame, refresh, false, false) * refresh;
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
    expect(step_lock_refreshes(kFrame, 16'683'350, false, false) == 1, "59.94 Hz, in-between pictures off: locked");
    expect(step_lock_refreshes(kFrame, 16'683'350, true, false) == 0, "59.94 Hz, in-between pictures on: exactly 60");
    expect(step_lock_refreshes(kFrame, 16'528'926, false, false) == 0, "60.5 Hz (0.8% off): exactly 60");
    expect(step_lock_refreshes(kFrame, 16'260'163, false, false) == 0, "61.5 Hz (2.5% off): exactly 60");
    expect(step_lock_refreshes(kFrame, 1'000'000'000 / 240, true, false) == 4, "240 Hz, in-between pictures on: locked");
    expect(step_lock_refreshes(kFrame, 1'000'000'000 / 120, true, false) == 2, "120 Hz, in-between pictures on: locked");
    expect(step_lock_refreshes(kFrame, 4'170'837, true, false) == 0, "239.76 Hz, in-between pictures on: exactly 60");
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

// I-013: a step's work is its own, from its moment to when the render thread had its list done (present_clock.h
// StepWork), not the time of the last real picture, which with in-between pictures is the arrival delay or nothing.
void test_step_work() {
    constexpr std::int64_t kMs = 1'000'000;
    galaxy::render::StepWork log;
    const std::int64_t step = 1'000'000'000;
    log.record(step, step + 5 * kMs);
    // Its real picture is shown at +30 ms, two steps on: the measure before (that picture's time less the step's
    // moment) gave 30 ms, or 0 when the real picture was never shown.
    const std::int64_t own_work = log.work(step);
    expect(own_work == 5 * kMs, "a step done at +5 ms whose real picture shows at +30 ms: work 5 ms");
    expect(log.work(step + kFrame) == 0, "a step not done yet: no work known");
    expect(log.work(0) == 0, "no step moment: no work");
    log.record(0, step);
    expect(log.work(0) == 0, "a list with no step moment is not kept");
    // The last eight steps are kept, each read by its own moment.
    for (int i = 0; i < 20; ++i) {
        const std::int64_t at = step + i * kFrame;
        log.record(at, at + (i + 1) * 100'000);
    }
    expect(log.work(step + 19 * kFrame) == 20 * 100'000, "the newest step's own work");
    expect(log.work(step + 12 * kFrame) == 13 * 100'000, "the eighth newest still kept");
    expect(log.work(step + 11 * kFrame) == 0, "older ones written over");
    // The phase from it: 5 ms of work lowers the budget to the work and the margin, where 30 ms (past the step) said
    // nothing and left it as it was.
    FramePhase own;
    FramePhase before;
    const DisplayRefresh display{0, 1'000'000'000 / 120};
    std::int64_t start_own = 0;
    std::int64_t start_before = 0;
    for (int i = 0; i < 4000; ++i) {
        start_own = own.next_start(start_own, own_work, kFrame, display, true);
        start_before = before.next_start(start_before, 30 * kMs, kFrame, display, true);
    }
    expect(std::llabs(own.budget_ns() - (5 * kMs + FramePhase::kMarginNs)) < 50'000,
           "the step's own 5 ms: the budget is the work and the margin");
    expect(before.budget_ns() != own.budget_ns(), "the real picture's 30 ms gave another budget");
}

// The sample rates of the native plan's "Any refresh rate" paragraph (05-NATIVE-LANE-G.md): the named ones and 300
// random ones between 40 and 500 Hz from a fixed seed.
std::vector<double> sample_rates() {
    std::vector<double> rates{40.0, 48.0, 50.0, 59.94, 60.0, 72.0, 75.0, 82.37, 90.0, 100.0, 119.88, 120.0,
                              144.0, 165.0, 180.0, 200.0, 240.0, 360.0, 480.0, 500.0};
    std::mt19937 random(20261124u);
    std::uniform_real_distribution<double> any(40.0, 500.0);
    for (int i = 0; i < 300; ++i) rates.push_back(any(random));
    return rates;
}

// The refreshes a step must follow at `rate`, from the rate alone: 60, 120, 180, 240, 360 and 480 Hz take 1, 2, 3, 4, 6
// and 8; 59.94 and 119.88 Hz 1 and 2 with in-between pictures off. -1: a rate near a multiple of 60 that is not one of
// these (a random one within 0.5 % of it), which the test leaves out; 0: the host's own clock at exactly 60.
int expected_refreshes(double rate, bool in_between) {
    for (const int n : {1, 2, 3, 4, 6, 8}) {
        if (rate == 60.0 * n) return n;
    }
    if (rate == 59.94) return in_between ? 0 : 1;
    if (rate == 119.88) return in_between ? 0 : 2;
    const double multiple = std::round(rate / 60.0);
    if (multiple >= 1.0 && std::abs(rate / 60.0 - multiple) / multiple < 0.005) return -1;
    return 0;
}

// G2.1 proof 7: the step's decision at every sample rate, with in-between pictures on and off: the lock rule
// (step_lock_refreshes) and the frame phase (FramePhase::next_start). At a whole multiple the steps follow the grid at its
// refreshes and a settled present lands just before a refresh; at every other rate every step is exactly 60 Hz.
void test_every_rate() {
    unsigned wrong = 0;
    unsigned checked = 0;
    for (const bool in_between : {false, true}) {
        for (const double rate : sample_rates()) {
            const int expected = expected_refreshes(rate, in_between);
            if (expected < 0) continue;
            ++checked;
            const std::int64_t refresh = std::llround(1e9 / rate);
            bool ok = galaxy::step_lock_refreshes(kFrame, refresh, in_between, false) == expected;
            FramePhase phase;
            const DisplayRefresh display{987'654, refresh};
            std::int64_t start = 12'345'678;
            for (int i = 0; i < 1200; ++i) {
                const std::int64_t next = phase.next_start(start, 3'000'000, kFrame, display, in_between);
                if (i > 600) {
                    ok = ok && (expected == 0 ? next - start == kFrame
                                              : std::llabs(next - start - expected * refresh) <= 1'000);
                }
                start = next;
            }
            if (expected != 0) {
                const std::int64_t gap = to_refresh(start + 3'000'000, display);
                ok = ok && gap >= FramePhase::kMarginNs - 100'000 && gap <= FramePhase::kMarginNs + 2'000'000;
            }
            if (!ok && wrong++ < 10u) {
                std::printf("  %.3f Hz, in-between pictures %s: expected %d refreshes a step\n", rate,
                            in_between ? "on" : "off", expected);
            }
        }
    }
    expect(checked > 600u, "most sample rates checked");
    expect(wrong == 0u, "every sample rate: the step locks at the multiples only, else exactly 60 Hz");
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
    test_step_work();
    test_every_rate();
    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "frame phase tests passed\n";
    return 0;
}
