// SPDX-License-Identifier: GPL-3.0-only
// MR::readSeqData and MR::runSeqTick (Game/Sound/SeqRead.hpp). N-3025: a sequence's register-named table reads stay in
// its program's bytes; the console read whatever followed. N-3026: a track's tick ends after cSeqTickStepMost operations
// that never wait; the console ran them for ever. The test's program waits by itself past cProbeBound steps, so a tick
// that never ends shows as a failure, not a hang.
#include "Game/Sound/SeqRead.hpp"

#include <math.h>
#include <stdio.h>

namespace {
    int sFailures = 0;
    constexpr u32 cProbeBound = MR::cSeqTickStepMost + 1000;

    void expect(bool ok, const char* what) {
        if (!ok) {
            printf("FAILED: %s\n", what);
            sFailures++;
        }
    }
}  // namespace

int main() {
    // A program of 6 bytes, then bytes that are not its own.
    const u8 memory[10] = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xee, 0xee, 0xee, 0xee};
    const u32 bytes = 6;
    u32 value = 0;
    expect(MR::readSeqData< 4 >(memory, bytes, 2, value) && value == 0x56789abc, "4 bytes that end at the program's end");
    expect(MR::readSeqData< 1 >(memory, bytes, 0, value) && value == 0x12, "the first byte");
    expect(MR::readSeqData< 3 >(memory, bytes, 1, value) && value == 0x345678, "3 bytes");
    value = 7;
    expect(!MR::readSeqData< 4 >(memory, bytes, 3, value) && value == 7, "4 bytes that run past the end are not read");
    expect(!MR::readSeqData< 1 >(memory, bytes, 6, value), "the byte after the end is not read");
    expect(!MR::readSeqData< 2 >(memory, bytes, 0xFFFFFFFFull + 2, value), "an address and index past 2^32 do not wrap");
    expect(MR::holdsSeqData< 6 >(bytes, 0) && !MR::holdsSeqData< 16 >(bytes, 0), "a filter's 16 bytes in a 6-byte program");

    // A tick: 3 operations, then a wait.
    u32 steps = 0;
    bool waiting = false;
    s32 ran = MR::runSeqTick([&] { return !waiting; }, [&] {
        waiting = ++steps == 3;
        return 0;
    });
    expect(ran == 0 && steps == 3, "a tick runs its operations to the wait");
    // A step that ends the track.
    steps = 0;
    ran = MR::runSeqTick([&] { return true; }, [&] { return ++steps == 2 ? -1 : 0; });
    expect(ran == -1 && steps == 2, "a step that ends the track ends the tick");
    // A loop that never waits.
    steps = 0;
    ran = MR::runSeqTick([&] { return steps < cProbeBound; }, [&] {
        steps++;
        return 0;
    });
    expect(ran == -2 && steps == MR::cSeqTickStepMost, "a loop that never waits ends after the most operations");

    // N-3029: a beat clock with no finite time is no clock.
    expect(MR::isSeqClockTime(0.0) && MR::isSeqClockTime(12.5), "a clock at or over 0 gives a time");
    expect(!MR::isSeqClockTime(-1.0), "a clock under 0 gives none (no beat clock)");
    expect(!MR::isSeqClockTime(HUGE_VAL), "an infinite clock (a rhythm tempo of 0) gives none");
    expect(!MR::isSeqClockTime(nan("")), "a clock that is not a number gives none");
    if (sFailures == 0) {
        printf("native_seq_read_tests: all passed\n");
    }
    return sFailures == 0 ? 0 : 1;
}
