#pragma once

// Dynamic resolution's governor (RenderConfig::dynamic_resolution): the share of its targets the 3D scene is drawn in, in
// eighths of their lines and columns, from the GPU's measured time a frame. Pure; the renderer feeds it once a frame.

#include <cstdint>
#include <utility>

namespace galaxy::render {

// The steps, largest first: whole eighths, so a step's rectangle is the target's size times n/8 rounded once, and step 0 is
// the target itself (every pass then works as without dynamic resolution, bit for bit).
inline constexpr unsigned kResolutionEighths[] = {8u, 7u, 6u, 5u, 4u};
inline constexpr unsigned kResolutionStepCount = sizeof(kResolutionEighths) / sizeof(kResolutionEighths[0]);

// The rectangle a step draws in, from the top left of a `width` x `height` target: exactly the target at 8/8.
[[nodiscard]] inline std::pair<unsigned, unsigned> resolution_rectangle(unsigned width, unsigned height, unsigned eighths) noexcept {
    if (eighths >= 8u) {
        return {width, height};
    }
    const auto part = [eighths](unsigned size) {
        const unsigned scaled = static_cast<unsigned>((static_cast<std::uint64_t>(size) * eighths + 4u) / 8u);
        return scaled != 0u ? scaled : 1u;
    };
    return {part(width), part(height)};
}

class ResolutionGovernor {
public:
    // Frames after a change before the measurement is the new step's: the pass timers' running average (0.1 a frame) keeps
    // under 15 % of the old step after 20 frames.
    static constexpr unsigned kSettleFrames = 20u;
    // Frames over 90 % of the budget, running, before a step down.
    static constexpr unsigned kOverFrames = 3u;
    // Frames with room for the next step up, running, before a step up.
    static constexpr unsigned kRoomFrames = 60u;

    // One frame: the GPU's time for it (the pass timers' sum, ms) and the budget (ms). Returns the step's eighths.
    unsigned update(double gpu_ms, double budget_ms) noexcept {
        if (since_change_ < kSettleFrames) {
            ++since_change_;
            return eighths();
        }
        if (!(gpu_ms > 0.0) || !(budget_ms > 0.0)) {
            over_ = room_ = 0u;
            return eighths();
        }
        const double area = area_of(step_);
        // Down: the largest step whose pixels, at this frame's time per pixel, fit 80 % of the budget; one step at least.
        if (gpu_ms > 0.9 * budget_ms) {
            room_ = 0u;
            if (++over_ >= kOverFrames && step_ + 1u < kResolutionStepCount) {
                unsigned next = step_ + 1u;
                while (next + 1u < kResolutionStepCount && gpu_ms * area_of(next) / area > 0.8 * budget_ms) {
                    ++next;
                }
                change(next);
            }
            return eighths();
        }
        over_ = 0u;
        // Up: one step, once the next step's pixels at this time per pixel (the fixed part scaled too: an over-estimate) have
        // stayed under 75 % of the budget.
        if (step_ != 0u && gpu_ms * area_of(step_ - 1u) / area < 0.75 * budget_ms) {
            if (++room_ >= kRoomFrames) {
                change(step_ - 1u);
            }
        } else {
            room_ = 0u;
        }
        return eighths();
    }
    // Back to the whole target (dynamic resolution turned off or on, a new target size).
    void reset() noexcept {
        step_ = 0u;
        since_change_ = 0u;
        over_ = room_ = 0u;
    }
    [[nodiscard]] unsigned eighths() const noexcept { return kResolutionEighths[step_]; }

private:
    [[nodiscard]] static double area_of(unsigned step) noexcept {
        const double e = static_cast<double>(kResolutionEighths[step]);
        return e * e;
    }
    void change(unsigned step) noexcept {
        step_ = step;
        since_change_ = 0u;
        over_ = room_ = 0u;
    }
    unsigned step_ = 0u;
    unsigned since_change_ = 0u;
    unsigned over_ = 0u;
    unsigned room_ = 0u;
};

}  // namespace galaxy::render
