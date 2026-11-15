// SPDX-License-Identifier: GPL-3.0-only
// present_slots.h: the vsync each picture asks to be shown at, and the pipeline depth.
#include "galaxy/render/present_slots.h"

#include "galaxy/game_step.h"

#include <algorithm>

namespace galaxy::render {

std::int64_t PresentSlots::place(const PresentClock& clock) noexcept {
    const std::int64_t period = clock.by_moment ? kGameStepNs : clock.period;
    // The depth in whole vsyncs for a picture timed by its moment, in whole present periods for a game frame.
    const std::int64_t unit = std::max<std::int64_t>(1, clock.by_moment ? clock.vsync : clock.step);
    if (!depth_set_) {
        depth_set_ = true;
        depth_ns_ = 2 * period;
    }
    const std::int64_t least = (kLeastDepthNs + unit - 1) / unit * unit;
    depth_ns_ = std::clamp(depth_ns_, least, std::max(least, 4 * period));
    const std::int64_t depth = (depth_ns_ + unit - 1) / unit * unit;
    depth_ns_ = depth;
    least_ns_ = least;
    std::int64_t slot = 0;
    skip_ = false;
    if (clock.by_moment) {
        // The vsync nearest the moment and the depth; a vsync after the slot before it at the soonest (a lower depth),
        // unless that slot is far ahead (a clock started again).
        const std::int64_t wanted = clock.moment + depth - clock.grid;
        slot = clock.grid + (wanted + (wanted >= 0 ? unit / 2 : -unit / 2)) / unit * unit;
        if (last_slot_ != 0 && slot < last_slot_ + unit && slot > last_slot_ - 4 * period) {
            slot = last_slot_ + unit;
            // The next picture at the depth would be too soon as well: pictures a vsync apart. One fewer brings the
            // depth down a vsync.
            skip_ = clock.moment + std::max<std::int64_t>(clock.step, unit) + depth < slot + unit;
        }
        depth_used_ = slot - clock.moment;
    } else {
        // The slot after the previous frame's (moved by a depth change), on the grid of the latest frame start; the
        // depth after that start when out of place: the first frame, after a stall, or frames ahead of it.
        const std::int64_t step = std::max<std::int64_t>(1, clock.step);
        slot = last_slot_ + step + (depth_used_ != 0 ? depth - depth_used_ : 0);
        const std::int64_t first = clock.frame_start + depth;
        if (last_slot_ == 0 || slot > first + period || slot < std::max(clock.frame_start + step, first - 2 * period + step)) {
            slot = first;
        } else {
            slot = clock.frame_start + (slot - clock.frame_start + step / 2) / step * step;
        }
        depth_used_ = depth;
    }
    last_slot_ = slot;
    return slot;
}

void PresentSlots::observe(std::int64_t requested, std::int64_t earliest, std::int64_t actual, std::int64_t margin,
                           std::int64_t step, std::int64_t half_vsync) noexcept {
    step = std::max<std::int64_t>(step, 1);
    if (actual > requested + half_vsync && earliest > requested + half_vsync) {
        const std::int64_t missed = earliest - requested;
        late_max_ = std::max(late_max_, missed);
        if (missed <= 2 * kGameStepNs) {
            ++window_late_;
            window_miss_ = std::max(window_miss_, missed);
        }
    } else if (actual <= requested + half_vsync) {
        calm_margin_ = std::min(calm_margin_, margin);
    }
    if (++window_frames_ < 60u) {
        return;
    }
    if (window_late_ >= 2u) {
        depth_ns_ += std::clamp<std::int64_t>((window_miss_ + step - 1) / step, 1, 2) * step;
        if (since_step_down_ < 600u) {
            calm_needed_ = std::min(calm_needed_ * 2u, 4800u);
        }
        since_step_down_ = ~0u;
        calm_frames_ = 0u;
        calm_margin_ = INT64_MAX;
    } else if (window_late_ == 0u) {
        calm_frames_ += window_frames_;
        if (calm_frames_ >= calm_needed_) {
            const std::int64_t down = std::min(step, depth_ns_ - least_ns_);
            if (down > 0 && calm_margin_ >= down + 2'000'000) {
                depth_ns_ -= down;
                since_step_down_ = 0u;
            }
            calm_frames_ = 0u;
            calm_margin_ = INT64_MAX;
        }
    } else {
        calm_frames_ = 0u;
        calm_margin_ = INT64_MAX;
    }
    if (since_step_down_ != ~0u) {
        since_step_down_ += window_frames_;
    }
    window_frames_ = window_late_ = 0u;
    window_miss_ = 0;
}

}  // namespace galaxy::render
