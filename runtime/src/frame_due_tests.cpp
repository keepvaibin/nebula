// SPDX-License-Identifier: GPL-3.0-only
// The frame wait's due moments (frame_due.h): kept on the runtime's own clock, and never hours ahead.
#include "galaxy/frame_due.h"

#include <cstdint>
#include <cstdio>

namespace {

int failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAILED: %s\n", what);
        ++failures;
    }
}

constexpr std::int64_t kPeriod = 16'666'667;
constexpr std::int64_t kHour = 3'600'000'000'000;

}  // namespace

int main() {
    // A healthy wait: due one period after the first reading, then one period after each due moment.
    {
        galaxy::FrameDue clock;
        std::int64_t now = 1'000 * kHour / 3600;  // an arbitrary start on the runtime's clock
        auto due = clock.due(now, kPeriod);
        expect(!due.cut && due.ns == now + kPeriod, "the first frame is due one period after the first reading");
        now = due.ns + 200'000;  // woke 0.2 ms late
        (void)clock.waited(now, kPeriod);
        due = clock.due(now, kPeriod);
        expect(!due.cut && due.ns == (1'000 * kHour / 3600) + 2 * kPeriod, "the next frame is one period after the one just due");
    }
    // P-274: a late wake-up keeps the due moments; the steps it missed run back to back, the game's time on the wall clock's.
    {
        galaxy::FrameDue clock;
        const std::int64_t start = 5'000'000'000;
        std::int64_t now = start;
        (void)clock.due(now, kPeriod);
        now = start + kPeriod + 3'000'000;  // the first wait returned 3 ms late
        expect(clock.waited(now, kPeriod) == 0, "a wake 3 ms late drops nothing");
        expect(clock.next() == start + 2 * kPeriod, "the next step keeps its due moment, not one period from the late wake");
        // A stall of 100 ms: the six steps it missed are due at once, one after another, each at its own moment.
        now = start + 2 * kPeriod + 100'000'000;
        expect(clock.waited(now, kPeriod) == 0, "a 100 ms stall is within the bound");
        int back_to_back = 0;
        while (clock.due(now, kPeriod).ns <= now) {
            ++back_to_back;
            now += 200'000;  // each step's work, 0.2 ms
            expect(clock.waited(now, kPeriod) == 0, "steps run back to back drop nothing");
        }
        expect(back_to_back == 6, "the six steps a 100 ms stall missed run back to back");
        expect(clock.next() == start + 9 * kPeriod, "then the clock is on its due moments again");
    }
    // Further behind than the bound (a load): those steps are dropped, said by the time returned; one period from now.
    {
        galaxy::FrameDue clock;
        std::int64_t now = 5'000'000'000;
        (void)clock.due(now, kPeriod);
        now += 100 * kPeriod;  // a stop of 100 frames: the wait for the step due one period in ended 99 periods late
        const std::int64_t dropped = clock.waited(now, kPeriod);
        expect(dropped == 98 * kPeriod, "a stop of 100 frames runs the step waited for and drops the 98 after it");
        expect(clock.next() == now + kPeriod, "and starts again one period from now");
        galaxy::FrameDue edge;
        (void)edge.due(0, kPeriod);
        expect(edge.waited(kPeriod + galaxy::FrameDue::kCatchUpNs + kPeriod, kPeriod) == 0 &&
                   edge.next() == 2 * kPeriod,
               "exactly the bound behind still runs its steps");
    }
    // The beta's fault: the moment kept was a steady_clock reading, hours ahead of the runtime's clock on a Mac that had slept.
    // A moment so far ahead is cut to one period, said by `cut` and `ahead_ns`; the wait is then one period, not 5 hours.
    {
        galaxy::FrameDue clock;
        const std::int64_t now = std::int64_t{40} * 1'000'000'000;
        clock.set_next(now + 5 * kHour);  // what the conversion of a steady_clock time point gave
        const auto due = clock.due(now, kPeriod);
        expect(due.cut, "a moment 5 hours ahead is cut");
        expect(due.ns == now + kPeriod, "the cut wait is one period");
        expect(due.ahead_ns == 5 * kHour, "the cut says how far ahead it was");
        expect(!clock.due(now, kPeriod).cut, "the cut moment is the next one: no second cut");
    }
    // The bound: what a healthy wait may be ahead (a period or two, a display's phase correction) passes; the boot's 2 ms period
    // still has the 250 ms floor; a long period (a 30 Hz step) has eight of itself.
    {
        galaxy::FrameDue clock;
        clock.set_next(1'000'000'000 + 3 * kPeriod);
        expect(!clock.due(1'000'000'000, kPeriod).cut, "three periods ahead is a wait");
        clock.set_next(1'000'000'000 + 240'000'000);
        expect(!clock.due(1'000'000'000, 2'000'000).cut, "240 ms ahead on the boot's 2 ms period is within the floor");
        clock.set_next(1'000'000'000 + 260'000'000);
        expect(clock.due(1'000'000'000, 2'000'000).cut, "260 ms ahead on the boot's period is cut");
        clock.set_next(1'000'000'000 + 7 * 33'333'334);
        expect(!clock.due(1'000'000'000, 33'333'334).cut, "seven 30 Hz periods ahead is within eight periods");
    }
    if (failures != 0) {
        std::printf("frame due tests FAILED\n");
        return 1;
    }
    std::printf("frame due tests passed\n");
    return 0;
}
