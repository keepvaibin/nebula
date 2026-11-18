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
    if (work_ns > budget_ns_) {
        plain_frames_ = kPlainFrames;
    } else if (plain_frames_ != 0) {
        --plain_frames_;
    }
    // Up at once, for the longest frame lately; down slowly. A frame past its period (a stage loading) says nothing.
    if (work_ns > 0 && work_ns < period_ns) {
        budget_ns_ = std::max(budget_ns_ - budget_ns_ / 256, work_ns + kMarginNs);
    }
    budget_ns_ = std::min(budget_ns_, period_ns - kMarginNs);
    if (!refresh || plain_frames_ != 0 || budget_ns_ > period_ns - period_ns / 4) return last_start_ns + period_ns;
    const std::int64_t count = step_lock_refreshes(period_ns, refresh->period_ns, in_between);
    if (count == 0) return last_start_ns + period_ns;
    const std::int64_t step = count * refresh->period_ns;
    const std::int64_t cadence = last_start_ns + step;
    // The start nearest the cadence that lands its present before a refresh.
    const std::int64_t aim = refresh->vblank_ns - budget_ns_;
    std::int64_t error = (aim - cadence) % refresh->period_ns;
    if (error > refresh->period_ns / 2) error -= refresh->period_ns;
    if (error < -refresh->period_ns / 2) error += refresh->period_ns;
    return cadence + std::clamp(error, -kSlewNs, kSlewNs);
}

}  // namespace galaxy::host
