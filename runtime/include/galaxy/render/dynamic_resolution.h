#pragma once

// Dynamic resolution's renderer side (RenderConfig::dynamic_resolution, resolution_governor.h). The scene targets are made
// once at the render size (the extent); each picture's 3D scene is drawn in the top-left rectangle of them that the
// governor's step gives, so a step change allocates nothing and waits for nothing. Every pass that reads a scene target
// reads that rectangle (its size, its texel limits); every picture the game is given holds the whole frame at the
// extent's size, so the game never sees the step.

#include "galaxy/launch_args.h"
#include "galaxy/platform_time.h"
#include "galaxy/render/frame_pass.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/resolution_governor.h"

#include <cmath>
#include <cstdint>

namespace galaxy::render {

// One picture's budget, ms: the period of the rate pictures are presented at, or for an unlimited rate the display's
// refresh (Unlimited draws as many pictures as the device makes; dynamic resolution keeps each within the display's
// period), and the game's step rate when neither is known.
[[nodiscard]] inline double resolution_budget_ms(double present_rate, double refresh_hz) noexcept {
    double rate = present_rate;
    if (!(rate > 0.0) || !std::isfinite(rate)) {
        rate = refresh_hz > 1.0 ? refresh_hz : kGameStepRate;
    }
    return 1000.0 / rate;
}

class DynamicResolution {
public:
    // Once a picture, before its scene. `on`: the setting, where the renderer offers it; `extent`: the size the scene
    // targets are made at; `floor`: the smallest share (sixteenths) the upscaler takes, 8 without one; `gpu_ms`: the
    // scene pictures' GPU time (the running average; 0 while none is measured); `budget_ms`: resolution_budget_ms.
    // Returns the step's rectangle: `extent` itself while off.
    FrameMap update(bool on, const FrameMap& extent, unsigned floor, double gpu_ms, double budget_ms) noexcept {
        const std::int64_t now = qpc_now();
        if (!(on || walk_) || extent.empty()) {
            on_ = false;
            return extent;
        }
        if (!on_ || extent != extent_) {
            // Turned on, or a new render size: from the whole of it.
            governor_.reset();
            on_ = true;
            extent_ = extent;
            last_qpc_ = now;
        }
        governor_.set_floor(floor);
        const double picture_ms = static_cast<double>(now - last_qpc_) * 1000.0 / static_cast<double>(qpc_frequency());
        last_qpc_ = now;
        if (walk_) {
            // Down every step the floor allows and back up, kResolutionStepsPictures scene pictures each.
            const std::uint64_t lowest = governor_.lowest_step();
            const std::uint64_t phase = lowest != 0u ? (walked_++ / kResolutionStepsPictures) % (2u * lowest) : 0u;
            governor_.set_step(static_cast<unsigned>(phase <= lowest ? phase : 2u * lowest - phase));
        } else {
            (void)governor_.update(gpu_ms, budget_ms, picture_ms);
        }
        return rectangle();
    }
    // --diagnostics resolution-steps: on whatever the setting, walking the steps (update) instead of following the GPU.
    void set_walk(bool walk) noexcept { walk_ = walk; }
    // The step's rectangle of the extent (the extent while off).
    [[nodiscard]] FrameMap rectangle() const noexcept {
        if (!on_) {
            return extent_;
        }
        const auto [width, height] = resolution_rectangle(extent_.width, extent_.height, governor_.sixteenths());
        return FrameMap(width, height);
    }
    [[nodiscard]] bool on() const noexcept { return on_; }
    [[nodiscard]] unsigned sixteenths() const noexcept { return on_ ? governor_.sixteenths() : 16u; }
    // The smallest step the rectangle may take (16 while off): what the passes' images are made for.
    [[nodiscard]] unsigned lowest_sixteenths() const noexcept { return on_ ? governor_.floor_sixteenths() : 16u; }

private:
    ResolutionGovernor governor_;
    FrameMap extent_{};
    bool on_ = false;
    bool walk_ = false;
    std::uint64_t walked_ = 0u;  // scene pictures since the walk began
    std::int64_t last_qpc_ = 0;
};

}  // namespace galaxy::render
