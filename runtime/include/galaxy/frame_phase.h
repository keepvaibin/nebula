// When the game's next frame starts. A frame reads its input as it starts and its picture appears at the display's
// next refresh, so the time between is latency: a frame starts as late as its work allows, its present landing just
// before a refresh. The display's clock also sets the frames' period, so no frame is repeated or dropped against it.
#pragma once

#include <cstdint>
#include <optional>

namespace galaxy::host {

// The display's refreshes: one at `vblank_ns` (its vertical blank), then every `period_ns`, on the frame clock's time base.
struct DisplayRefresh {
    std::int64_t vblank_ns = 0;
    std::int64_t period_ns = 0;
    bool variable = false;  // the refreshes follow the pictures (render/variable_refresh.h): the step is never locked
};

class FramePhase {
public:
    // Before a refresh, to be sure of it.
    static constexpr std::int64_t kMarginNs = 1'500'000;
    // How far one frame's start moves towards its place: unseen, since a present lands on a refresh anyway.
    static constexpr std::int64_t kSlewNs = 500'000;
    // A frame past its budget has missed its refresh (in-between pictures off): plain pacing for this many frames.
    static constexpr int kPlainFrames = 600;
    // With in-between pictures the budget's rise a step at most: 0.24 ms a second.
    static constexpr std::int64_t kBetweenRiseNs = 4'000;
    // With in-between pictures off the budget is held at the second longest work of the last kPeakSteps to twice that
    // many steps (5 to 10 seconds), at most kHeldAboveNs over the work's 90th percentile (moved kTypicalMoveNs a step),
    // and the margin; it falls only when that is kSettleNs under it.
    static constexpr int kPeakSteps = 300;
    static constexpr std::int64_t kSettleNs = 500'000;
    static constexpr std::int64_t kHeldAboveNs = 1'000'000;
    static constexpr std::int64_t kTypicalMoveNs = 10'000;

    // The frame that started at `last_start_ns` took `work_ns` to its present (0: not known yet). `period_ns`: the
    // frame's length; `in_between`: in-between pictures are on. Returns the next frame's start. Without refreshes, on
    // a display the step does not lock to (galaxy/step_lock.h), after a missed frame, or when the work leaves no room
    // to start later, frames simply follow `period_ns`. Locked with in-between pictures (two refreshes a step or more), a
    // frame keeps the game's own clock: one frame takes a refresh more or fewer when the refreshes' cadence has drifted
    // a whole refresh from it (a display a little off 240 Hz, as 239.966 Hz: the game's speed stays 60).
    [[nodiscard]] std::int64_t next_start(std::int64_t last_start_ns, std::int64_t work_ns, std::int64_t period_ns,
                                          const std::optional<DisplayRefresh>& refresh, bool in_between) noexcept;
    [[nodiscard]] std::int64_t budget_ns() const noexcept { return budget_ns_; }
    [[nodiscard]] bool plain() const noexcept { return plain_frames_ != 0; }

private:
    std::int64_t budget_ns_ = 8'000'000;
    int plain_frames_ = 0;
    std::int64_t ideal_ns_ = 0;  // locked: this frame's start on the game's own clock (0: not locked)
    // In-between pictures off: the work's 90th percentile; the longest and second longest work of this span of kPeakSteps
    // steps, the second longest of the span before, and the steps of this span.
    std::int64_t typical_ns_ = 0;
    std::int64_t peak_ns_ = 0;
    std::int64_t again_ns_ = 0;
    std::int64_t again_before_ns_ = 0;
    int peak_steps_ = 0;
};

// Two readings of the display's clock agree: a whole number of refreshes apart. Not on a variable refresh display, whose
// refreshes follow the presents.
[[nodiscard]] bool refreshes_in_step(std::int64_t earlier_vblank_ns, const DisplayRefresh& now) noexcept;

}  // namespace galaxy::host
