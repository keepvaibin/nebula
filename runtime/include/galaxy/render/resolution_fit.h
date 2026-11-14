#pragma once

// The GPU's time a frame as a line in the 3D scene's pixels (gpu_ms = a + b x pixels) and the internal height it
// recommends. One fit for every caller: the first start's calibration (render_core.cpp) and the benchmark. Pure.

#include <cstddef>
#include <cstdint>

#include "galaxy/render/render_config.h"

namespace galaxy::render {

struct GpuTimeFit {
    double a_ms = 0.0;            // the part no pixel count moves
    double b_ms_per_pixel = 0.0;  // never below 0
    bool valid = false;           // at least two distinct pixel counts, finite times
};

// The least-squares line through `count` samples (pixels[i], ms[i]).
[[nodiscard]] inline GpuTimeFit fit_gpu_time(const double* pixels, const double* ms, std::size_t count) noexcept {
    GpuTimeFit fit;
    if (count < 2u) {
        return fit;
    }
    double mean_p = 0.0, mean_t = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        if (!(pixels[i] >= 0.0) || !(ms[i] >= 0.0) || pixels[i] > 1e12 || ms[i] > 1e6) {
            return fit;
        }
        mean_p += pixels[i];
        mean_t += ms[i];
    }
    mean_p /= static_cast<double>(count);
    mean_t /= static_cast<double>(count);
    double spread = 0.0, together = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        spread += (pixels[i] - mean_p) * (pixels[i] - mean_p);
        together += (pixels[i] - mean_p) * (ms[i] - mean_t);
    }
    // One pixel count alone (or counts closer than a pixel apart) fixes no slope.
    if (!(spread >= static_cast<double>(count))) {
        return fit;
    }
    fit.b_ms_per_pixel = together > 0.0 ? together / spread : 0.0;
    fit.a_ms = mean_t - fit.b_ms_per_pixel * mean_p;
    if (fit.a_ms < 0.0) {
        fit.a_ms = 0.0;
    }
    fit.valid = true;
    return fit;
}

[[nodiscard]] inline double predicted_gpu_ms(const GpuTimeFit& fit, double pixels) noexcept {
    return fit.a_ms + fit.b_ms_per_pixel * pixels;
}

// The 3D scene's pixels at internal height `height` (0: match display) on a `width` x `lines` output, as the renderer sizes
// it (internal_extent).
[[nodiscard]] inline double scene_pixels(unsigned height, unsigned width, unsigned lines, unsigned max_dimension = 16384u) {
    const auto [w, h] = internal_extent(width, lines, height, max_dimension);
    return static_cast<double>(w) * static_cast<double>(h);
}

// The heights a fit chooses among, lightest first: the INTERNAL RESOLUTION row's steps from 480 below the output's lines,
// then match display (0). The output's own height is match display, never a step of its own.
inline constexpr unsigned kFitLowestHeight = 480u;
// The share of the display's frame time the GPU may take (the rest: the compositor, frame-to-frame swings).
inline constexpr double kFitFrameShare = 0.8;
// A measurement made in a light scene (the first start's: the title), scaled to the heaviest stages' (board rc.md, item 6).
// The benchmark times the stages themselves and passes 1.
inline constexpr double kFitHeavyStage = 2.5;

// The largest of those heights whose predicted time x `heavy_factor` stays within `budget_ms`; 480 when none does (the
// floor the game is meant to hold), 0 when the output has no more lines than 480.
[[nodiscard]] inline unsigned largest_fitting_height(const GpuTimeFit& fit, unsigned width, unsigned lines, double heavy_factor,
                                                     double budget_ms) {
    if (lines <= kFitLowestHeight) {
        return 0u;
    }
    if (!fit.valid) {
        return kFitLowestHeight;
    }
    unsigned chosen = kFitLowestHeight;
    for (const unsigned height : kInternalHeights) {
        if (height < kFitLowestHeight || height == 0u || height >= lines) {
            continue;
        }
        if (predicted_gpu_ms(fit, scene_pixels(height, width, lines)) * heavy_factor <= budget_ms) {
            chosen = height;
        }
    }
    if (predicted_gpu_ms(fit, scene_pixels(0u, width, lines)) * heavy_factor <= budget_ms) {
        chosen = 0u;
    }
    return chosen;
}

}  // namespace galaxy::render
