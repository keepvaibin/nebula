#include "galaxy/frame_phase.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace galaxy::host {

std::int64_t frame_retraces(std::int64_t period_ns, std::int64_t retrace_period_ns) noexcept {
    if (period_ns <= 0 || retrace_period_ns <= 0) return 0;
    const std::int64_t count = (period_ns + retrace_period_ns / 2) / retrace_period_ns;
    if (count < 1) return 0;
    const std::int64_t off = std::llabs(count * retrace_period_ns - period_ns);
    return off * 100 <= period_ns ? count : 0;
}

bool retraces_in_step(std::int64_t earlier_vblank_ns, const Retraces& now) noexcept {
    if (now.period_ns <= 0) return false;
    std::int64_t off = (now.vblank_ns - earlier_vblank_ns) % now.period_ns;
    if (off < 0) off += now.period_ns;
    return std::min(off, now.period_ns - off) <= now.period_ns / 20;
}

std::int64_t FramePhase::next_start(std::int64_t last_start_ns, std::int64_t work_ns, std::int64_t period_ns,
                                    const std::optional<Retraces>& retraces) noexcept {
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
    if (!retraces || plain_frames_ != 0 || budget_ns_ > period_ns - period_ns / 4) return last_start_ns + period_ns;
    const std::int64_t count = frame_retraces(period_ns, retraces->period_ns);
    if (count == 0) return last_start_ns + period_ns;
    const std::int64_t step = count * retraces->period_ns;
    const std::int64_t cadence = last_start_ns + step;
    // The start nearest the cadence that lands its present before a retrace.
    const std::int64_t aim = retraces->vblank_ns - budget_ns_;
    std::int64_t error = (aim - cadence) % retraces->period_ns;
    if (error > retraces->period_ns / 2) error -= retraces->period_ns;
    if (error < -retraces->period_ns / 2) error += retraces->period_ns;
    return cadence + std::clamp(error, -kSlewNs, kSlewNs);
}

}  // namespace galaxy::host
