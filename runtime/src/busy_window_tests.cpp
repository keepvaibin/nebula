// SPDX-License-Identifier: GPL-3.0-only
// perf_hint.h BusyWindow (P-069): a frame thread's work is the wall time from its marks of work to its marks of
// waiting, by windows. Time blocked inside the work counts, the wait for the next does not; work still going at a
// window's end is split between the two windows; repeated marks change nothing.
#include "galaxy/perf_hint.h"

#include <cstdint>
#include <cstdio>

namespace {

bool expect(bool ok, const char* what) {
    if (!ok) std::fprintf(stderr, "FAILED: %s\n", what);
    return ok;
}

constexpr std::int64_t kMs = 1'000'000;

}  // namespace

int main() {
    bool ok = true;
    using galaxy::host::BusyWindow;
    {
        BusyWindow w;
        ok &= expect(w.close(10 * kMs) == 0, "no marks: no work");
    }
    {
        // Two pictures in a step: 3 ms and 4 ms of work, 9 ms of waiting between them.
        BusyWindow w;
        w.mark(0, true);
        w.mark(3 * kMs, false);
        w.mark(12 * kMs, true);
        w.mark(16 * kMs, false);
        ok &= expect(w.close(16'666'667) == 7 * kMs, "two spans of work add, the waits between do not");
        ok &= expect(w.close(33 * kMs) == 0, "a window with no work after them is 0");
    }
    {
        // Work going on at the window's end: counted to the end, the rest goes to the next window.
        BusyWindow w;
        w.mark(10 * kMs, true);
        ok &= expect(w.close(16 * kMs) == 6 * kMs, "work going at the end counts to the end");
        w.mark(20 * kMs, false);
        ok &= expect(w.close(32 * kMs) == 4 * kMs, "and its rest to the next window");
    }
    {
        // A second mark of work keeps the first start; a second mark of waiting adds nothing.
        BusyWindow w;
        w.mark(0, true);
        w.mark(5 * kMs, true);
        w.mark(8 * kMs, false);
        w.mark(12 * kMs, false);
        ok &= expect(w.close(16 * kMs) == 8 * kMs, "repeated marks change nothing");
    }
    {
        // A clock read that goes back (another core's read): no negative work.
        BusyWindow w;
        w.mark(10 * kMs, true);
        w.mark(9 * kMs, false);
        ok &= expect(w.close(16 * kMs) == 0, "no negative work");
    }
    std::printf("%s\n", ok ? "busy_window: every case holds" : "busy_window: FAILED");
    return ok ? 0 : 1;
}
