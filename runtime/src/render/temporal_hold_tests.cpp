// SPDX-License-Identifier: GPL-3.0-only
// Tests for galaxy/render/temporal_hold.h: a hold of the temporal upscaler on generated frames ends (it ended only
// with a settings change: one slow stretch turned interpolation off for the session), backs off while the GPU keeps
// failing, and a settings change decides again at once.
#include "galaxy/render/temporal_hold.h"

#include <cstdio>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::printf("FAIL: %s\n", message);
        ++failures;
    }
}

constexpr std::int64_t kSecond = 1'000'000'000;

}  // namespace

int main() {
    using galaxy::render::TemporalInterpHold;
    TemporalInterpHold hold;
    std::int64_t now = 100 * kSecond;
    expect(!hold.held(), "held before any hold");
    expect(!hold.lift_if_due(now), "lifted with no hold");

    // One slow stretch: held, then tried again after 30 s.
    hold.hold(now);
    expect(hold.held(), "not held after a hold");
    expect(!hold.lift_if_due(now + 29 * kSecond), "lifted before 30 s");
    expect(hold.lift_if_due(now + 30 * kSecond), "still held 30 s after one slow stretch");
    expect(!hold.held(), "held after it lifted");

    // The GPU keeps failing: 60 s, 120 s, 240 s, then 480 s at most.
    const std::int64_t waits[] = {60, 120, 240, 480, 480, 480};
    now += 30 * kSecond;
    for (const std::int64_t wait : waits) {
        hold.hold(now);
        hold.hold(now + kSecond);  // a second report while held changes nothing
        if (hold.wait_ns() != wait * kSecond) {
            std::printf("FAIL: a hold waits %lld s, not %lld\n", static_cast<long long>(hold.wait_ns() / kSecond),
                        static_cast<long long>(wait));
            ++failures;
        }
        expect(!hold.lift_if_due(now + wait * kSecond - 1), "lifted early");
        expect(hold.lift_if_due(now + wait * kSecond), "not lifted when due");
        now += wait * kSecond;
    }

    // A settings change decides again at once; the next hold waits 30 s again.
    hold.hold(now);
    hold.settings_changed();
    expect(!hold.held(), "held after the settings changed");
    hold.hold(now);
    expect(hold.wait_ns() == 30 * kSecond, "the first hold after a settings change does not wait 30 s");

    // At a frame's start: the settings are asked only while held; a change decides again, else it lifts when due.
    TemporalInterpHold frame;
    int asked = 0;
    const auto same = [&asked] {
        ++asked;
        return false;
    };
    const auto changed = [&asked] {
        ++asked;
        return true;
    };
    expect(!frame.lift_at_frame_start(same, now) && asked == 0, "the settings asked with no hold");
    frame.hold(now);
    expect(!frame.lift_at_frame_start(same, now + 29 * kSecond) && frame.held() && asked == 1,
           "lifted at a frame's start before its wait");
    expect(frame.lift_at_frame_start(same, now + 30 * kSecond) && !frame.held(), "not lifted at a frame's start when due");
    frame.hold(now);
    expect(frame.wait_ns() == 60 * kSecond, "the second hold does not wait 60 s");
    expect(!frame.lift_at_frame_start(changed, now) && !frame.held(), "a settings change at a frame's start kept the hold");
    frame.hold(now);
    expect(frame.wait_ns() == 30 * kSecond, "the hold after a change at a frame's start does not wait 30 s");

    if (failures != 0) {
        std::printf("%d failures\n", failures);
        return 1;
    }
    std::printf("temporal hold: tried again after 30 s, backing off to 8 min, a settings change at once\n");
    return 0;
}
