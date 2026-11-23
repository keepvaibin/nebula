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
        clock.waited(now, kPeriod);
        due = clock.due(now, kPeriod);
        expect(!due.cut && due.ns == (1'000 * kHour / 3600) + 2 * kPeriod, "the next frame is one period after the one just due");
    }
    // A late wake-up skips the frames it missed: one period from now, not a run of frames to catch up.
    {
        galaxy::FrameDue clock;
        std::int64_t now = 5'000'000'000;
        (void)clock.due(now, kPeriod);
        now += 100 * kPeriod;  // a stop of 100 frames
        clock.waited(now, kPeriod);
        expect(clock.next() == now + kPeriod, "a late wake-up starts again one period from now");
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
