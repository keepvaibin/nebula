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
    // A frame past its period (a stage loading) says nothing. With in-between pictures up for the longest frame lately by
    // kBetweenRiseNs a step at most, down slowly: the budget moves the step against the refreshes and every picture's
    // moment with it, so one step's hitch moves nothing at once (R-008); a lasting rise of the work is followed at 0.24 ms
    // a second. Without them up at once for a longer step, and held at the work that came back in the last 5 to 10
    // seconds (the second longest of a span of kPeakSteps steps, so one step alone holds nothing), at most kHeldAboveNs
    // over the work's 90th percentile, and the margin: down to that only when it is kSettleNs under the budget. Each move
    // shows the step's picture that much later or sooner after its step; falling a little after every step and rising
    // at the next long one, the budget moved the pictures by up to a millisecond all the time, and a long step that came
    // after the fall missed its refresh (R-009: 7 refreshes missed in 70 s). The steps around a stage's load take 3 to
    // 4.5 ms where the others take 1 to 2.5: held, they moved every picture by 2 ms for 5 to 10 seconds and then back.
    if (work_ns > 0 && work_ns < period_ns) {
        if (in_between) {
            const std::int64_t wanted = std::max(budget_ns_ - budget_ns_ / 256, work_ns + kMarginNs);
            budget_ns_ = std::min(wanted, budget_ns_ + kBetweenRiseNs);
        } else {
            // The 90th percentile, a streaming estimate: up 9 parts for a step above it, down 1 for one below.
            const std::int64_t move = work_ns > typical_ns_ ? 9 * kTypicalMoveNs : -kTypicalMoveNs;
            typical_ns_ = std::max<std::int64_t>(0, typical_ns_ + move);
            if (work_ns > peak_ns_) {
                again_ns_ = peak_ns_;
                peak_ns_ = work_ns;
            } else {
                again_ns_ = std::max(again_ns_, work_ns);
            }
            if (++peak_steps_ == kPeakSteps) {
                again_before_ns_ = again_ns_;
                peak_ns_ = 0;
                again_ns_ = 0;
                peak_steps_ = 0;
            }
            const std::int64_t again = std::min(std::max(again_ns_, again_before_ns_), typical_ns_ + kHeldAboveNs);
            const std::int64_t held = std::max(again, work_ns) + kMarginNs;
            if (held > budget_ns_ || held < budget_ns_ - kSettleNs) budget_ns_ = held;
        }
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
        // A whole refresh of drift before a step takes one more or fewer: it then sits a refresh from the next, so the
        // start's own moves towards its place never take it back and forth (R-009: at half a refresh the steps went
        // 3 and 5 refreshes by turns, each moving the step against the display).
        const std::int64_t drift = cadence - ideal_ns_;
        if (drift > refresh->period_ns) cadence -= refresh->period_ns;
        if (drift < -refresh->period_ns) cadence += refresh->period_ns;
    }
    // The start nearest the cadence that lands its present before a refresh.
    const std::int64_t aim = refresh->vblank_ns - budget_ns_;
    std::int64_t error = (aim - cadence) % refresh->period_ns;
    if (error > refresh->period_ns / 2) error -= refresh->period_ns;
    if (error < -refresh->period_ns / 2) error += refresh->period_ns;
    return cadence + std::clamp(error, -kSlewNs, kSlewNs);
}

}  // namespace galaxy::host
