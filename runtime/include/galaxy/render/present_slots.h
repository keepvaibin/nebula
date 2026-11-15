// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Presentation times (VK_GOOGLE_display_timing on Android), as Swappy paces: the vsync each picture asks to be shown
// at, and the pipeline depth the past presentation times set. renderer_vk.cpp presents by it; present_slots_tests.cpp
// drives it with simulated displays at many refresh rates.
//
// A picture with a moment (an interpolated picture: RenderCore's slot, a vblank, at any refresh rate) is asked for at
// the vsync a pipeline depth after that moment: it shows its moment a constant time later, so the motion stays even
// whatever slots the renderer skipped. A game frame (no interpolation) gets the present slot after the previous
// frame's on the game's frame grid (host_window::present_grid), a pipeline depth after the latest frame start. A
// picture not ready by its slot is shown at the next vsync; the slots after it keep their places.

#include <cstdint>

namespace galaxy::render {

// What a present is timed by, every time on CLOCK_MONOTONIC nanoseconds.
struct PresentClock {
    bool by_moment = false;        // `moment` given: a picture timed by its moment on the vsync grid
    std::int64_t moment = 0;       // the moment the picture shows
    std::int64_t grid = 0;         // a vblank of the display
    std::int64_t vsync = 0;        // the display's vsync interval
    std::int64_t frame_start = 0;  // a game frame: the latest frame start on the game's frame grid
    std::int64_t period = 0;       // the game's frame period
    std::int64_t step = 0;         // one present period (1 s / the present rate)
};

class PresentSlots {
public:
    // The pipeline depth's bounds: at least the GPU's work and the compositor's latch, at most four frame periods.
    static constexpr std::int64_t kLeastDepthNs = 12'000'000;

    // The slot (the vsync the picture asks to show at) of the next present.
    [[nodiscard]] std::int64_t place(const PresentClock& clock) noexcept;
    // A present not timed: the next timed one starts its slots again.
    void reset() noexcept {
        last_slot_ = 0;
        skip_ = false;
    }
    // A picture timed by its moment never shares or precedes the vsync of the one before it, so a lower depth comes in
    // a vsync at a time. Where pictures are a vsync apart that needs one picture fewer: true once when the renderer is
    // to skip its next slot (RenderCore), so the picture after it shows a vsync after this one, a vsync sooner after
    // its moment. Only the latest place() sets it.
    [[nodiscard]] bool take_skip() noexcept {
        const bool skip = skip_;
        skip_ = false;
        return skip;
    }

    // A past presentation of a timed present: the slot it asked for, the earliest it could have been shown
    // (earliestPresentTime), when it was (actualPresentTime), how long before the compositor latched it it was ready
    // (presentMargin), and its present period. Sets the depth by windows of 60 presents: a window with two or more not
    // ready by their slot (shown later than asked, by less than two frame periods: a longer wait is a stall, not the
    // device's pace) adds the present periods they missed by, two at most. A calm stretch (300 presents, doubling up
    // to 4800 after a step down that did not hold) whose presents were all ready a period and 2 ms before the
    // compositor latched them takes a period off, down to kLeastDepthNs (in whole units) at the least.
    void observe(std::int64_t requested, std::int64_t earliest, std::int64_t actual, std::int64_t margin,
                 std::int64_t step, std::int64_t half_vsync) noexcept;

    // The time from the latest picture's moment (or frame start) to its slot.
    [[nodiscard]] std::int64_t depth_used() const noexcept { return depth_used_; }
    // The most a late present missed its slot by since the last call.
    [[nodiscard]] std::int64_t take_late_max() noexcept {
        const std::int64_t most = late_max_;
        late_max_ = 0;
        return most;
    }

private:
    bool depth_set_ = false;       // depth_ns_ holds a depth (two frame periods at the first present)
    bool skip_ = false;
    std::int64_t depth_ns_ = 0;    // the depth wanted, in whole units (vsyncs or present periods)
    std::int64_t least_ns_ = kLeastDepthNs;  // kLeastDepthNs in whole units
    std::int64_t depth_used_ = 0;  // see depth_used (0: no slot yet)
    std::int64_t last_slot_ = 0;
    unsigned window_frames_ = 0;
    unsigned window_late_ = 0;
    std::int64_t window_miss_ = 0;  // the most a late present in the window missed its slot by
    unsigned calm_frames_ = 0;
    unsigned calm_needed_ = 300;
    unsigned since_step_down_ = ~0u;
    std::int64_t calm_margin_ = INT64_MAX;
    std::int64_t late_max_ = 0;
};

}  // namespace galaxy::render
