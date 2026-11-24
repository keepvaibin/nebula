// SPDX-License-Identifier: GPL-3.0-only
// The vsync grid (galaxy/vsync_grid.h) the Android and Apple display clocks run the game's frames on: the interval
// through jitter, callbacks the platform skipped, rate changes up and down, a long gap and the system's stated period;
// and the frame slots: on the grid, a whole number of vsyncs apart, from now after a stall, none on a stale grid or one
// whose vsyncs do not divide the frame period.
#include "galaxy/vsync_grid.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr std::int64_t kHz60 = 16666667;   // one vsync at 60 Hz
constexpr std::int64_t kHz120 = 8333333;
constexpr std::int64_t kHz90 = 11111111;
constexpr std::int64_t kHz5994 = 16683350;  // 59.94 Hz
constexpr std::int64_t kGamePeriod = 16666667;

bool near(std::int64_t a, std::int64_t b, std::int64_t tolerance) {
    return std::llabs(a - b) <= tolerance;
}

// Vsyncs `interval` apart from `start`, `count` of them, each delivered with a timestamp off by up to `jitter` ns
// (a fixed pattern); `skip`: every skip-th callback is not delivered (0: none skipped). Returns the last vsync's time.
std::int64_t feed(galaxy::VsyncGrid& grid, std::int64_t start, std::int64_t interval, int count, std::int64_t jitter,
                  int skip, std::int64_t stated = 0) {
    static const int pattern[] = {0, 3, -2, 5, -4, 1, -5, 2};
    std::int64_t t = start;
    for (int i = 0; i < count; ++i) {
        t = start + static_cast<std::int64_t>(i) * interval;
        if (skip != 0 && i % skip == skip - 1) {
            continue;
        }
        grid.note(t + jitter * pattern[i % 8] / 5, stated);
    }
    return t;
}

void test_steady() {
    galaxy::VsyncGrid grid;
    feed(grid, 1000000000, kHz60, 120, 200000, 0);
    check(near(grid.interval_ns, kHz60, kHz60 / 1000), "steady 60 Hz with 0.2 ms jitter: the interval within 0.1%");
}

void test_skipped_callbacks() {
    // Callbacks the platform did not deliver: every 7th. The interval stays the display's; the slots stay on.
    galaxy::VsyncGrid grid;
    const std::int64_t last = feed(grid, 1000000000, kHz60, 200, 100000, 7);
    check(near(grid.interval_ns, kHz60, kHz60 / 200), "skipped callbacks are not a new rate (within 0.5%)");
    std::int64_t slot = 0, frame_period = 0;
    check(grid.next_slot(kGamePeriod, 0, last + 1000, false, slot, frame_period), "the grid holds through skipped callbacks");

    // One skipped callback right after a steady run: the very next slot is still on the grid.
    galaxy::VsyncGrid one;
    const std::int64_t end = feed(one, 1000000000, kHz60, 60, 0, 0);
    one.note(end + 2 * kHz60, 0);
    check(near(one.interval_ns, kHz60, kHz60 / 200), "one skipped callback moves the interval under 0.5%");
    check(one.next_slot(kGamePeriod, end + kHz60, end + 2 * kHz60 + 1000, false, slot, frame_period),
          "the frame after one skipped callback is on the grid");
}

void test_rate_changes() {
    std::int64_t slot = 0, frame_period = 0;
    // 60 to 120 Hz, no stated period: the new rate at its first gap.
    galaxy::VsyncGrid up;
    const std::int64_t end = feed(up, 1000000000, kHz60, 60, 0, 0);
    up.note(end + kHz120, 0);
    check(near(up.interval_ns, kHz120, 1000), "a faster display is its new rate at once");
    // 120 to 60 Hz: every gap two of the old intervals; three in a row are the new rate.
    galaxy::VsyncGrid down;
    const std::int64_t fast = feed(down, 1000000000, kHz120, 120, 0, 0);
    for (int i = 1; i <= 3; ++i) {
        down.note(fast + i * kHz60, 0);
    }
    check(near(down.interval_ns, kHz60, 1000), "a display that fell to half its rate is its new rate after three gaps");
    // 60 to 90 Hz: the game's frames go to the host's clock.
    galaxy::VsyncGrid ninety;
    const std::int64_t at = feed(ninety, 1000000000, kHz60, 60, 0, 0);
    const std::int64_t last = feed(ninety, at + kHz90, kHz90, 30, 0, 0);
    check(near(ninety.interval_ns, kHz90, 1000), "90 Hz is taken");
    check(!ninety.next_slot(kGamePeriod, 0, last + 1000, false, slot, frame_period), "90 Hz does not divide a 60 Hz frame");
}

void test_stated_and_gaps() {
    std::int64_t slot = 0, frame_period = 0;
    // The system's stated period stands over measured gaps.
    galaxy::VsyncGrid stated;
    feed(stated, 1000000000, kHz60 + 50000, 30, 300000, 0, kHz60);
    check(stated.interval_ns == kHz60, "the stated period stands");
    // A long gap (out of sight): the interval kept; no slots while stale; slots again when vsyncs come back.
    galaxy::VsyncGrid gap;
    const std::int64_t end = feed(gap, 1000000000, kHz60, 60, 0, 0);
    gap.note(end + 2000000000, 0);
    check(near(gap.interval_ns, kHz60, 1000), "a two-second gap keeps the interval");
    check(!gap.next_slot(kGamePeriod, end, end + 2000000000 + 300000000, false, slot, frame_period), "no slot on a stale grid");
    // Out of order and repeated vsyncs change nothing.
    const std::int64_t before = gap.interval_ns;
    gap.note(end, 0);
    gap.note(end + 2000000000, 0);
    check(gap.interval_ns == before && gap.vsync_ns == end + 2000000000, "an old or repeated vsync is ignored");
}

void test_slots() {
    std::int64_t slot = 0, frame_period = 0;
    // 60 Hz frames on 120 Hz: two vsyncs apart, each on the grid.
    galaxy::VsyncGrid grid;
    const std::int64_t last_vsync = feed(grid, 1000000000, kHz120, 240, 0, 0);
    std::int64_t last = 0;
    bool on_grid = true, spaced = true;
    std::int64_t now = last_vsync + 1000;
    for (int i = 0; i < 20; ++i) {
        // The vsyncs up to now, as the platform delivers them while the frames run.
        while (grid.vsync_ns + kHz120 <= now) {
            grid.note(grid.vsync_ns + kHz120, 0);
        }
        std::int64_t next = 0;
        if (!grid.next_slot(kGamePeriod, last, now, false, next, frame_period)) {
            on_grid = false;
            break;
        }
        on_grid = on_grid && (next - 1000000000) % kHz120 == 0;
        spaced = spaced && (last == 0 || next - last == 2 * grid.interval_ns);
        last = next;
        now = next + 1000;
    }
    check(on_grid, "slots lie on the vsync grid");
    check(spaced, "60 Hz frames on 120 Hz are two vsyncs apart");
    check(frame_period == 2 * grid.interval_ns, "the frame period is two vsyncs");
    // P-274: after a stall of 90 ms the slots are kept: the next is the one after the last, though past, and the frames
    // it missed run back to back, each on its own slot of the grid, until they are on time again.
    {
        galaxy::VsyncGrid stalled = grid;
        std::int64_t at = last;
        std::int64_t later = last + 90000000;
        while (stalled.vsync_ns + kHz120 <= later) {
            stalled.note(stalled.vsync_ns + kHz120, 0);
        }
        std::int64_t dropped = 0;
        int behind = 0;
        bool kept = true;
        for (;;) {
            if (!stalled.next_slot(kGamePeriod, at, later, false, slot, frame_period, &dropped)) {
                kept = false;
                break;
            }
            kept = kept && slot == at + 2 * kHz120 && (slot - 1000000000) % kHz120 == 0;
            if (slot >= later) {
                break;
            }
            ++behind;
            at = slot;
            later += 200000;  // each frame's work, 0.2 ms
        }
        check(kept && dropped == 0, "after a 90 ms stall every slot is kept, two vsyncs after the one before");
        check(behind == 5, "the five frames a 90 ms stall missed run back to back");
    }
    // Further behind than the bound (FrameDue::kCatchUpNs): from now, the first vsync from now, and the time dropped said.
    {
        galaxy::VsyncGrid stalled = grid;
        const std::int64_t later = last + 300000000;
        while (stalled.vsync_ns + kHz120 <= later) {
            stalled.note(stalled.vsync_ns + kHz120, 0);
        }
        std::int64_t dropped = 0;
        check(stalled.next_slot(kGamePeriod, last, later, false, slot, frame_period, &dropped) && slot >= later &&
                  slot - later < stalled.interval_ns && dropped > galaxy::FrameDue::kCatchUpNs,
              "after a 300 ms stall the next slot is the first vsync from now, the time dropped said");
    }
    // The first slot, with no last: the first vsync from now.
    check(grid.next_slot(kGamePeriod, 0, grid.vsync_ns + 5000000, false, slot, frame_period) && slot == grid.vsync_ns + kHz120,
          "the first slot is the next vsync");
    // 59.94 Hz runs a 60 Hz game on its grid (within 0.2%, in-between pictures off).
    galaxy::VsyncGrid ntsc;
    const std::int64_t end = feed(ntsc, 1000000000, kHz5994, 60, 0, 0);
    check(ntsc.next_slot(kGamePeriod, 0, end + 1000, false, slot, frame_period) && frame_period == ntsc.interval_ns,
          "59.94 Hz carries a 60 Hz game, one vsync a frame");
    // A 61 Hz panel (1.7% off) does not set the game's speed: the host's clock runs it (it did within 3%).
    galaxy::VsyncGrid fast;
    const std::int64_t fast_end = feed(fast, 1000000000, 16393443, 60, 0, 0);
    check(!fast.next_slot(kGamePeriod, 0, fast_end + 1000, false, slot, frame_period), "61 Hz does not carry the game");
    // In-between pictures on: 59.94 Hz keeps the game at exactly 60; 120 Hz carries it.
    check(!ntsc.next_slot(kGamePeriod, 0, end + 1000, true, slot, frame_period),
          "59.94 Hz with in-between pictures keeps exactly 60");
    check(grid.next_slot(kGamePeriod, 0, grid.vsync_ns + 1000, true, slot, frame_period) && frame_period == 2 * grid.interval_ns,
          "120 Hz with in-between pictures carries the game, two vsyncs a frame");
    // No grid before the first interval.
    galaxy::VsyncGrid none;
    none.note(1000000000, 0);
    check(!none.next_slot(kGamePeriod, 0, 1000000000, false, slot, frame_period), "one vsync is no grid");
}

// G2.1 proof 7: the Android and Apple step-wait decision (wait_display_frame's next_slot) at every sample rate of the
// native plan's "Any refresh rate" paragraph, with in-between pictures on and off. At 60, 120, 180, 240, 360 and 480 Hz
// (59.94 and 119.88 Hz with them off) the step follows the grid at 1, 2, 3, 4, 6 and 8 vsyncs and each slot is a
// vsync; at every other rate, whole or not, there is no slot and the host's own clock runs the game at exactly 60. A
// random rate within 0.5 % of a multiple of 60 is left out (the lock rule's own tolerance decides it).
void test_every_rate() {
    std::vector<double> rates{40.0, 48.0, 50.0, 59.94, 60.0, 72.0, 75.0, 82.37, 90.0, 100.0, 119.88, 120.0,
                              144.0, 165.0, 180.0, 200.0, 240.0, 360.0, 480.0, 500.0};
    std::mt19937 random(20261124u);
    std::uniform_real_distribution<double> any(40.0, 500.0);
    for (int i = 0; i < 300; ++i) rates.push_back(any(random));
    unsigned wrong = 0;
    for (const bool in_between : {false, true}) {
        for (const double rate : rates) {
            int expected = 0;
            for (const int n : {1, 2, 3, 4, 6, 8}) {
                if (rate == 60.0 * n) expected = n;
            }
            if (rate == 59.94) expected = in_between ? 0 : 1;
            if (rate == 119.88) expected = in_between ? 0 : 2;
            const double multiple = std::round(rate / 60.0);
            if (expected == 0 && rate != 59.94 && rate != 119.88 && multiple >= 1.0 &&
                std::abs(rate / 60.0 - multiple) / multiple < 0.005) {
                continue;
            }
            const std::int64_t interval = std::llround(1e9 / rate);
            galaxy::VsyncGrid grid;
            const std::int64_t last_vsync = feed(grid, 1000000000, interval, 120, 0, 0);
            std::int64_t slot = 0, frame_period = 0;
            std::int64_t last = 0;
            bool ok = true;
            std::int64_t now = last_vsync + 1000;
            for (int i = 0; i < 20; ++i) {
                while (grid.vsync_ns + interval <= now) {
                    grid.note(grid.vsync_ns + interval, 0);
                }
                const bool locked = grid.next_slot(kGamePeriod, last, now, in_between, slot, frame_period);
                if (locked != (expected != 0)) {
                    ok = false;
                    break;
                }
                if (!locked) break;
                ok = ok && (slot - 1000000000) % interval == 0 && frame_period == expected * interval &&
                     (last == 0 || slot - last == expected * interval);
                last = slot;
                now = slot + 1000;
            }
            if (!ok && wrong++ < 10u) {
                std::printf("  %.3f Hz, in-between pictures %s: expected %d vsyncs a step\n", rate,
                            in_between ? "on" : "off", expected);
            }
        }
    }
    check(wrong == 0u, "every sample rate: the step follows the grid at the multiples only, each slot a vsync");
}

}  // namespace

int main() {
    test_steady();
    test_skipped_callbacks();
    test_rate_changes();
    test_stated_and_gaps();
    test_slots();
    test_every_rate();
    if (failures == 0) {
        std::printf("vsync grid tests: all pass\n");
    }
    return failures == 0 ? 0 : 1;
}
