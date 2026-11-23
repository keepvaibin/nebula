// SPDX-License-Identifier: GPL-3.0-only
// The start's watch (start_watch.h): a game thread that stops, a window that stays empty.
#include "galaxy/start_watch.h"

#include <cstdio>

namespace {
int failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAILED: %s\n", what);
        ++failures;
    }
}
}  // namespace

int main() {
    using galaxy::StartWatch;
    // A start that works: frames go on and the first picture comes at 1 s. Nothing is said in 3 minutes.
    {
        StartWatch watch;
        bool any = false;
        for (int second = 1; second <= 180; ++second) {
            const auto notice = watch.tick(second, static_cast<std::uint32_t>(second * 60), second >= 1, false);
            any = any || notice.log_no_frame || notice.show_no_picture;
        }
        expect(!any, "a start that works says nothing");
    }
    // The planted fault: the game thread is held after its first frame (the beta's frame wait that slept for hours) and no
    // picture is ever presented. The log says so at 10 s, again at 30 s, then each minute; the player is told once, at 20 s.
    {
        StartWatch watch;
        int logs = 0, told = 0;
        double first_log = 0.0, told_at = 0.0, second_log = 0.0;
        for (int second = 1; second <= 200; ++second) {
            const auto notice = watch.tick(second, 1u, false, false);
            if (notice.log_no_frame) {
                ++logs;
                if (logs == 1) first_log = second;
                if (logs == 2) second_log = second;
            }
            if (notice.show_no_picture) {
                ++told;
                told_at = second;
            }
        }
        expect(first_log == 11.0, "a game thread with no frame for 10 s (its last frame was seen at second 1) is logged then");
        expect(second_log == 31.0, "and again once 30 s are over");
        expect(logs == 4, "then each minute (seconds 11, 31, 92, 152 of 200)");
        expect(told == 1 && told_at == 20.0, "a window with no picture for 20 s is told once");
    }
    // Held on purpose (minimised): nothing is said, and the count starts again when the game goes on.
    {
        StartWatch watch;
        bool any = false;
        for (int second = 1; second <= 100; ++second) any = any || watch.tick(second, 5u, false, true).log_no_frame;
        expect(!any, "a paused game is not a stopped one");
        expect(!watch.tick(101, 5u, true, false).log_no_frame, "the count starts again after the pause");
    }
    // Frames that go on but no picture (the renderer could not present): the player is told, the log has no frame line.
    {
        StartWatch watch;
        bool log = false, told = false;
        for (int second = 1; second <= 40; ++second) {
            const auto notice = watch.tick(second, static_cast<std::uint32_t>(second * 60), false, false);
            log = log || notice.log_no_frame;
            told = told || notice.show_no_picture;
        }
        expect(!log && told, "frames without a picture: the player is told, the game thread is not accused");
    }
    if (failures != 0) {
        std::printf("start watch tests FAILED\n");
        return 1;
    }
    std::printf("start watch tests passed\n");
    return 0;
}
