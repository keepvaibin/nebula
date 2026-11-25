#include "galaxy/frame_phase.h"

#include "galaxy/step_lock.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace galaxy::host {

bool refreshes_in_step(std::int64_t earlier_vblank_ns, const DisplayRefresh& now) noexcept {
    if (now.period_ns <= 0) return false;
    std::int64_t off = (now.vblank_ns - earlier_vblank_ns) % now.period_ns;
    if (off < 0) off += now.period_ns;
    return std::min(off, now.period_ns - off) <= now.period_ns / 20;
}

std::int64_t FramePhase::next_start(std::int64_t last_start_ns, std::int64_t work_ns, std::int64_t period_ns,
                                    const std::optional<DisplayRefresh>& refresh, bool in_between) noexcept {
    // A step past its budget has missed its refresh, where it presents its own picture. With in-between pictures it only
    // starts them later (the display lead takes it, render/frame_schedule.h): the phase stays, so every picture keeps its
    // moment against its step's (R-008: a plain stretch let the step's place wander, then jump at its end).
    if (work_ns > budget_ns_ && !in_between) {
        plain_frames_ = kPlainFrames;
    } else if (plain_frames_ != 0) {
        --plain_frames_;
    }
    // Up at once, for the longest frame lately; down slowly. A frame past its period (a stage loading) says nothing.
    // With in-between pictures up by kBetweenRiseNs a step at most: the budget moves the step against the refreshes and
    // every picture's moment with it, so one step's hitch moves nothing at once (R-008); a lasting rise of the work is
    // followed at 0.24 ms a second.
    if (work_ns > 0 && work_ns < period_ns) {
        const std::int64_t wanted = std::max(budget_ns_ - budget_ns_ / 256, work_ns + kMarginNs);
        budget_ns_ = in_between ? std::min(wanted, budget_ns_ + kBetweenRiseNs) : wanted;
    }
    budget_ns_ = std::min(budget_ns_, period_ns - kMarginNs);
    const auto plain = [&] {
        ideal_ns_ = 0;
        return last_start_ns + period_ns;
    };
    if (!refresh || plain_frames_ != 0 || budget_ns_ > period_ns - period_ns / 4) return plain();
    const std::int64_t count = step_lock_refreshes(period_ns, refresh->period_ns, in_between, refresh->variable);
    if (count == 0) return plain();
    const std::int64_t step = count * refresh->period_ns;
    std::int64_t cadence = last_start_ns + step;
    // With in-between pictures the game's own clock, from the frame that started the lock (or one late by more than a
    // frame: a stall restarts it): every refresh still shows a new picture. Without them a step a refresh shorter or
    // longer would show its picture for fewer or more refreshes, so the step keeps the display's pace (59.94 Hz: one
    // refresh a step).
    if (!in_between || count < 2) {
        ideal_ns_ = 0;
    } else {
        if (ideal_ns_ == 0 || std::llabs(last_start_ns - ideal_ns_) > period_ns) ideal_ns_ = last_start_ns;
        ideal_ns_ += period_ns;
        const std::int64_t drift = cadence - ideal_ns_;
        if (drift > refresh->period_ns / 2) cadence -= refresh->period_ns;
        if (drift < -refresh->period_ns / 2) cadence += refresh->period_ns;
    }
    // The start nearest the cadence that lands its present before a refresh.
    const std::int64_t aim = refresh->vblank_ns - budget_ns_;
    std::int64_t error = (aim - cadence) % refresh->period_ns;
    if (error > refresh->period_ns / 2) error -= refresh->period_ns;
    if (error < -refresh->period_ns / 2) error += refresh->period_ns;
    return cadence + std::clamp(error, -kSlewNs, kSlewNs);
}

}  // namespace galaxy::host
