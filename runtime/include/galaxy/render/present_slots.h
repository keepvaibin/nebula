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

#include <array>
#include <cstddef>
#include <cstdint>

namespace galaxy::render {

// What a swap chain keeps of the pictures presented to it. Every renderer names its own (swap_queue()) and follows the
// two rules below from it; present_slots_tests.cpp shows each on a simulated display.
enum class SwapQueue : std::uint8_t {
    EveryPicture,   // every picture is shown, in order, each from a refresh on (FIFO: VSync)
    NewestPicture,  // one picture waits and the next present replaces it; shown whole from a refresh on (MAILBOX: VSync
                    // off where the surface cannot tear, as on Android)
    Tearing,        // shown at once, part way down a refresh (IMMEDIATE, ALLOW_TEARING: VSync off)
};

// Presentation times (a slot each picture waits for) only where the queue keeps every picture. Where it keeps the newest
// alone, a picture waiting for a slot ahead is replaced by the next present: pictures thrown away, the depth raised for
// the misses, more thrown away, down to a few shown a second and pauses of seconds.
[[nodiscard]] constexpr bool presents_timed(SwapQueue queue) noexcept {
    return queue == SwapQueue::EveryPicture;
}

// Interpolated pictures on the display's refreshes wherever every picture shows whole from a refresh on. A slot clock
// of its own beats against the refreshes there: two pictures in one refresh (one never seen), then a refresh shown
// twice, over and over. Only a tearing queue shows a picture between refreshes, so only there is a clock of its own
// worth anything.
[[nodiscard]] constexpr bool pictures_on_refreshes(SwapQueue queue) noexcept {
    return queue != SwapQueue::Tearing;
}

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

// The depth from a picture's moment to the refresh asked for it, set by past presentations (observe): PresentSlots' and
// the frame schedule's display lead (frame_schedule.h, a display model's platforms). Every time in nanoseconds.
// Where the presents ask no time (Direct3D's flip model) the swap chain's queue paces them: a picture is shown at the
// first refresh after its present that the one before it leaves free, so the depth is at most the queue's depth (the
// frame latency object's maximum) in refreshes above the least. A picture started earlier waits for the queue on the
// render thread past the next picture's start, and nothing holds a picture to its refresh: a lateness is the picture's
// own (FrameSchedule::display_lead_for).
//
// The rule (G10.5, brief AD: the depth is fixed in play and changes only where no motion is on screen to follow). Each
// present tells the depth it needed: from its moment to the latest time the compositor would still have shown it at its
// refresh (when it was ready, the compositor's latch and the display's hold), the same whatever depth it asked with, so
// it says what a lower depth would have given (the margin a present had says only how far it was from the depth it
// asked). A window's figure is the need all its presents but one were within (the second largest: a depth under it
// would have left two late, which raises it). The depth the ready times ask for (target) is the least whole unit at or
// above the largest figure of the last 8 windows (8 s: a burst in play leaves it 8 s after it ended, so a moment the depth
// may move after that takes it back to what the ready times ask) and of the last four loads, with a 1 ms guard: the
// worst ready time the scene transitions measure sizes it (brief AD; G3.7's load worker shrinks it by the same rule). It
// moves to that target
// only while moving is allowed (allow: a closed wipe, a scene loading, a game that does not tell), at each window's end,
// down as well as up. In play it holds, but for one step up of a unit after a window with two or more presents not
// ready by their slot, one reported event (steps_in_play); later late windows of the same stretch of play wait for the
// next moment it may move, whose target holds their needs. A depth above two game steps (three refreshes at 60 Hz) is a
// fault state, each second there counted (take_deep_ns), and its way back is the next moment it may move.
class PresentDepth {
public:
    // The guard above the ready times (the card's "plus a 1 ms guard").
    static constexpr std::int64_t kGuardNs = 1'000'000;
    // How long after a mode change or a stage load (quiet) late presents count for nothing.
    static constexpr std::int64_t kQuietNs = 2'000'000'000;

    // The depth in whole `unit`s, at least `least` and at most `most` (both rounded up to whole units): `initial` at the
    // first call, then what observe() left.
    [[nodiscard]] std::int64_t depth(std::int64_t initial, std::int64_t least, std::int64_t most,
                                     std::int64_t unit) noexcept;
    // A past presentation of a timed present: the slot it asked for, the earliest it could have been shown
    // (earliestPresentTime), when it was (actualPresentTime), the depth it needed (above), and its present period. A
    // present not ready by its slot (shown later than asked and not earlier possible, by less than two game steps: a
    // longer wait is a stall, not the device's pace) is late; a need more than two game steps above the depth is a
    // stall's and sizes nothing. Windows of a second of presents by their shown times, so the same at every rate.
    void observe(std::int64_t requested, std::int64_t earliest, std::int64_t actual, std::int64_t need,
                 std::int64_t step, std::int64_t half_vsync) noexcept;
    // A mode change (a refresh-rate change) or a stage load at `at`: the presents shown in the kQuietNs after it neither
    // count late for the step up nor size the target (they still count in quiet_late, and as misses where the renderer
    // counts them).
    void quiet(std::int64_t at) noexcept { quiet_at_ = at; }
    // The depth lower by `by`, and every need it holds (the next depth() keeps it at its least at the least): a least
    // that fell takes the depth above it down with it.
    void lower(std::int64_t by) noexcept;
    // Whether the depth may move to its target now (`move`), and whether a load is in progress (`load`: its windows' worst
    // is kept as one of the last four loads when it ends). Both by default as a game that does not tell: moving, no load.
    void allow(bool move, bool load) noexcept;
    // The depth the ready times ask for (above); 0 before any present told its need.
    [[nodiscard]] std::int64_t target() const noexcept;
    // The steps up in play since the last call (each a reported event).
    [[nodiscard]] unsigned take_steps_in_play() noexcept { return take(steps_in_play_); }
    // The presents late within kQuietNs of a mode change or a stage load since the last call (named misses).
    [[nodiscard]] unsigned take_quiet_late() noexcept { return take(quiet_late_); }
    // The time spent at a depth above two game steps since the last call (a named miss each second).
    [[nodiscard]] std::int64_t take_deep_ns() noexcept { return take(deep_ns_); }
    // The most a late present missed its slot by since the last call.
    [[nodiscard]] std::int64_t take_late_max() noexcept { return take(late_max_); }

private:
    template <class T>
    static T take(T& value) noexcept {
        const T was = value;
        value = T{};
        return was;
    }
    static constexpr std::int64_t kWindowNs = 1'000'000'000;
    static constexpr std::size_t kRecentWindows = 8;
    static constexpr std::size_t kLoadsKept = 4;
    bool depth_set_ = false;       // depth_ns_ holds a depth (`initial` at the first call)
    std::int64_t depth_ns_ = 0;    // the depth wanted, in whole units (vsyncs or present periods)
    std::int64_t unit_ = 0;        // the unit of the latest depth()
    std::int64_t step_ns_ = 0;     // the present period of the latest observe()
    std::int64_t window_start_ = 0;  // the first shown time of the window (0: none yet)
    unsigned window_late_ = 0;
    unsigned window_needs_ = 0;    // the needs the window holds, and its two largest
    std::int64_t need_top_ = 0;
    std::int64_t need_second_ = 0;
    std::array<std::int64_t, kRecentWindows> recent_{};  // the last windows' figures (0: none)
    std::size_t recent_next_ = 0;
    std::int64_t load_now_ = 0;                          // the worst figure of the load in progress
    std::array<std::int64_t, kLoadsKept> loads_{};       // the worst figure of each of the last loads
    std::size_t loads_next_ = 0;
    bool move_ = true;
    bool load_ = false;
    bool stepped_ = false;          // a step up in this stretch of play
    std::int64_t quiet_at_ = 0;
    unsigned steps_in_play_ = 0;
    unsigned quiet_late_ = 0;
    std::int64_t deep_ns_ = 0;
    std::int64_t late_max_ = 0;
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

    // A past presentation of a timed present (PresentDepth::observe): the depth, down to kLeastDepthNs at the least. The
    // need of a present whose platform tells how early it was ready for its earliest refresh (presentMargin) is
    // need_of(earliestPresentTime, margin, its reference()).
    void observe(std::int64_t requested, std::int64_t earliest, std::int64_t actual, std::int64_t need,
                 std::int64_t step, std::int64_t half_vsync) noexcept {
        depth_.observe(requested, earliest, actual, need, step, half_vsync);
    }
    // The depth a present needed: from its reference to the latest moment it could have been ready and still shown at
    // `earliest`, the compositor's latch included. VK_GOOGLE_display_timing counts presentMargin against the earliest
    // refresh it could have been shown at (Android's loader moves earliestPresentTime back a refresh at a time and takes
    // the refresh off the margin each time), so the two together are the same however much later it was shown; with the
    // shown refresh in place of the earliest a present on time with room would need the depth it asked.
    [[nodiscard]] static constexpr std::int64_t need_of(std::int64_t earliest, std::int64_t margin,
                                                        std::int64_t reference) noexcept {
        return earliest - margin - reference;
    }
    // The depth's moments (PresentDepth::allow, quiet).
    void allow(bool move, bool load) noexcept { depth_.allow(move, load); }
    void quiet(std::int64_t at) noexcept { depth_.quiet(at); }

    // The time from the latest picture's moment (or frame start) to its slot.
    [[nodiscard]] std::int64_t depth_used() const noexcept { return depth_used_; }
    // The moment the latest place() counted its depth from: the picture's moment, or for a game frame its slot less the
    // depth (the pace-depth plant's later asking not included).
    [[nodiscard]] std::int64_t reference() const noexcept { return reference_; }
    // The depth's reports since the last call (PresentDepth).
    [[nodiscard]] std::int64_t take_late_max() noexcept { return depth_.take_late_max(); }
    [[nodiscard]] unsigned take_steps_in_play() noexcept { return depth_.take_steps_in_play(); }
    [[nodiscard]] unsigned take_quiet_late() noexcept { return depth_.take_quiet_late(); }
    [[nodiscard]] std::int64_t take_deep_ns() noexcept { return depth_.take_deep_ns(); }

private:
    PresentDepth depth_;  // two frame periods at the first present
    bool skip_ = false;
    std::int64_t depth_used_ = 0;  // see depth_used (0: no slot yet)
    std::int64_t reference_ = 0;   // see reference
    std::int64_t last_slot_ = 0;
};

}  // namespace galaxy::render
