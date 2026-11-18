// SPDX-License-Identifier: GPL-3.0-only
// The vsync grid (galaxy/vsync_grid.h) the Android and Apple display clocks run the game's frames on: the interval
// through jitter, callbacks the platform skipped, rate changes up and down, a long gap and the system's stated period;
// and the frame slots: on the grid, a whole number of vsyncs apart, from now after a stall, none on a stale grid or one
// whose vsyncs do not divide the frame period.
#include "galaxy/vsync_grid.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

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
    // After a stall, from now: the slot is the first vsync from now, not a run of missed slots.
    check(grid.next_slot(kGamePeriod, last, last + 100000000, false, slot, frame_period) && slot >= last + 100000000 &&
              slot - (last + 100000000) < grid.interval_ns,
          "after a stall the next slot is the first vsync from now");
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

}  // namespace

int main() {
    test_steady();
    test_skipped_callbacks();
    test_rate_changes();
    test_stated_and_gaps();
    test_slots();
    if (failures == 0) {
        std::printf("vsync grid tests: all pass\n");
    }
    return failures == 0 ? 0 : 1;
}
