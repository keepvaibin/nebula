// When the game's next frame starts. A frame reads its input as it starts and its picture appears at the display's
// next retrace, so the time between is latency: a frame starts as late as its work allows, its present landing just
// before a retrace. The display's clock also sets the frames' period, so no frame is repeated or dropped against it.
#pragma once

#include <cstdint>
#include <optional>

namespace galaxy::host {

// The display's retraces: one at `vblank_ns`, then every `period_ns`, on the frame clock's time base.
struct Retraces {
    std::int64_t vblank_ns = 0;
    std::int64_t period_ns = 0;
};

class FramePhase {
public:
    // Before a retrace, to be sure of it.
    static constexpr std::int64_t kMarginNs = 1'500'000;
    // How far one frame's start moves towards its place: unseen, since a present lands on a retrace anyway.
    static constexpr std::int64_t kSlewNs = 500'000;
    // A frame past its budget has missed its retrace: plain pacing for this many frames.
    static constexpr int kPlainFrames = 600;

    // The frame that started at `last_start_ns` took `work_ns` to its present (0: not known yet). `period_ns`: the
    // frame's length. Returns the next frame's start. Without retraces, on a display whose rate is no multiple of
    // the frame's, after a missed frame, or when the work leaves no room to start later, frames simply follow
    // `period_ns`.
    [[nodiscard]] std::int64_t next_start(std::int64_t last_start_ns, std::int64_t work_ns, std::int64_t period_ns,
                                          const std::optional<Retraces>& retraces) noexcept;
    [[nodiscard]] std::int64_t budget_ns() const noexcept { return budget_ns_; }
    [[nodiscard]] bool plain() const noexcept { return plain_frames_ != 0; }

private:
    std::int64_t budget_ns_ = 8'000'000;
    int plain_frames_ = 0;
};

// Two readings of the display's clock agree: a whole number of retraces apart. Not on a variable refresh display, whose
// retraces follow the presents.
[[nodiscard]] bool retraces_in_step(std::int64_t earlier_vblank_ns, const Retraces& now) noexcept;

// The retraces that fit a frame of `period_ns`: its length in whole retraces, or 0 when none is within 1%.
[[nodiscard]] std::int64_t frame_retraces(std::int64_t period_ns, std::int64_t retrace_period_ns) noexcept;

}  // namespace galaxy::host
