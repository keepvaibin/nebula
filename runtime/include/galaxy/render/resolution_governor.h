#pragma once

// Dynamic resolution's governor (RenderConfig::dynamic_resolution): the share of the render size the 3D scene is drawn at,
// in sixteenths of its lines and columns (16/16 down to 8/16, a quarter of the pixels), from the GPU's measured time for a
// picture. A step is about 6 % of a side: a change reads as a slight softening, not a jump. Pure; the renderer feeds it
// once a picture with the scene's GPU time, the picture's budget and the time since the picture before.
//
// Every wait is both a number of pictures and a time: the pictures let the GPU timers' running average take in the new
// step, the time keeps changes at a pace the eye reads as steady whatever the picture rate (at 3,000 pictures a second,
// 20 pictures are 7 ms; at 30, 0.7 s).

#include <algorithm>
#include <cstdint>
#include <utility>

namespace galaxy::render {

// The steps, largest first: whole sixteenths, so a step's rectangle is the render size times n/16 rounded once, and step 0
// is the render size itself (every pass then works as without dynamic resolution, bit for bit).
inline constexpr unsigned kResolutionSixteenths[] = {16u, 15u, 14u, 13u, 12u, 11u, 10u, 9u, 8u};
inline constexpr unsigned kResolutionStepCount = sizeof(kResolutionSixteenths) / sizeof(kResolutionSixteenths[0]);

// The rectangle a step draws in, from the top left of a `width` x `height` render size: exactly that size at 16/16.
[[nodiscard]] inline std::pair<unsigned, unsigned> resolution_rectangle(unsigned width, unsigned height,
                                                                        unsigned sixteenths) noexcept {
    if (sixteenths >= 16u) {
        return {width, height};
    }
    const auto part = [sixteenths](unsigned size) {
        const unsigned scaled = static_cast<unsigned>((static_cast<std::uint64_t>(size) * sixteenths + 8u) / 16u);
        return scaled != 0u ? scaled : 1u;
    };
    return {part(width), part(height)};
}

// The step whose sixteenths are the smallest at or above `sixteenths` (the lowest step a floor allows).
[[nodiscard]] inline unsigned resolution_step_at_least(unsigned sixteenths) noexcept {
    unsigned step = 0u;
    while (step + 1u < kResolutionStepCount && kResolutionSixteenths[step + 1u] >= sixteenths) {
        ++step;
    }
    return step;
}

class ResolutionGovernor {
public:
    // After a change, before the measurement is the new step's: the pass timers' running average (0.1 a picture) keeps
    // under 15 % of the old step after 20 pictures; 0.2 s at least.
    static constexpr unsigned kSettlePictures = 20u;
    static constexpr double kSettleMs = 200.0;
    // Over 90 % of the budget, running, before a step down: 3 pictures and 50 ms (a single slow picture is not a trend).
    static constexpr unsigned kOverPictures = 3u;
    static constexpr double kOverMs = 50.0;
    // Room for the next step up, running, before a step up: 60 pictures and 1 s.
    static constexpr unsigned kRoomPictures = 60u;
    static constexpr double kRoomMs = 1000.0;

    // One picture: the GPU's time for its scene (the pass timers' sum, ms), the picture's budget (ms) and the time since
    // the picture before (ms). Returns the step's sixteenths.
    unsigned update(double gpu_ms, double budget_ms, double picture_ms) noexcept {
        const double elapsed = picture_ms > 0.0 ? picture_ms : 0.0;
        if (since_change_.pictures < kSettlePictures || since_change_.ms < kSettleMs) {
            since_change_.add(elapsed);
            return sixteenths();
        }
        if (!(gpu_ms > 0.0) || !(budget_ms > 0.0)) {
            over_ = room_ = {};
            return sixteenths();
        }
        const double area = area_of(step_);
        // Down: the largest step whose pixels, at this picture's time per pixel, fit 80 % of the budget; one step at least.
        if (gpu_ms > 0.9 * budget_ms) {
            room_ = {};
            over_.add(elapsed);
            if (over_.pictures >= kOverPictures && over_.ms >= kOverMs && step_ < lowest_) {
                unsigned next = step_ + 1u;
                while (next < lowest_ && gpu_ms * area_of(next) / area > 0.8 * budget_ms) {
                    ++next;
                }
                change(next);
            }
            return sixteenths();
        }
        over_ = {};
        // Up: one step, once the next step's pixels at this time per pixel (the fixed part scaled too: an over-estimate)
        // have stayed under 75 % of the budget.
        if (step_ != 0u && gpu_ms * area_of(step_ - 1u) / area < 0.75 * budget_ms) {
            room_.add(elapsed);
            if (room_.pictures >= kRoomPictures && room_.ms >= kRoomMs) {
                change(step_ - 1u);
            }
        } else {
            room_ = {};
        }
        return sixteenths();
    }
    // Back to the whole render size (dynamic resolution turned off or on, a new render size).
    void reset() noexcept {
        step_ = 0u;
        since_change_ = over_ = room_ = {};
    }
    // The smallest share the scene may be drawn at (an upscaler's lowest input size; 8/16 without one). A step below
    // it moves up to it at once.
    void set_floor(unsigned sixteenths) noexcept {
        lowest_ = resolution_step_at_least(std::clamp(sixteenths, kResolutionSixteenths[kResolutionStepCount - 1u], 16u));
        if (step_ > lowest_) {
            change(lowest_);
        }
    }
    // A test's step (--diagnostics resolution-steps), held to the floor; the measurements start again.
    void set_step(unsigned step) noexcept {
        const unsigned held = std::min(step, lowest_);
        if (held != step_) {
            change(held);
        }
    }
    [[nodiscard]] unsigned step() const noexcept { return step_; }
    [[nodiscard]] unsigned lowest_step() const noexcept { return lowest_; }
    [[nodiscard]] unsigned sixteenths() const noexcept { return kResolutionSixteenths[step_]; }
    [[nodiscard]] unsigned floor_sixteenths() const noexcept { return kResolutionSixteenths[lowest_]; }

private:
    struct Run {
        unsigned pictures = 0u;
        double ms = 0.0;
        void add(double elapsed) noexcept {
            pictures = pictures < 0xFFFFFFFFu ? pictures + 1u : pictures;
            ms += elapsed;
        }
    };
    [[nodiscard]] static double area_of(unsigned step) noexcept {
        const double s = static_cast<double>(kResolutionSixteenths[step]);
        return s * s;
    }
    void change(unsigned step) noexcept {
        step_ = step;
        since_change_ = over_ = room_ = {};
    }
    unsigned step_ = 0u;
    unsigned lowest_ = kResolutionStepCount - 1u;
    Run since_change_;
    Run over_;
    Run room_;
};

}  // namespace galaxy::render
