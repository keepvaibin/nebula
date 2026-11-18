// SPDX-License-Identifier: GPL-3.0-only
// The Mac's first start on integrated graphics (RendererMtl::choose_first_start_scale, P-035): whether the scene is
// drawn at the display's size in points and enlarged to its pixels with FSR 1 at PERFORMANCE. Kept apart from the
// renderer so every platform's unit tests run it (first_start_scale_tests.cpp).
#pragma once

#include <cstdint>

namespace galaxy::render {

struct FirstStartFacts {
    bool ordinary_start = true;    // not a hidden, timed or extension-driven start (a test's, a tool's)
    bool benchmark = false;        // a benchmark measures the settings as they are
    bool settings_path = true;     // where the render settings are written is known
    bool settings_exist = false;   // not a first start: the player's settings stand
    bool apple_gpu = false;        // Apple's GPU (every Apple silicon Mac)
    bool unified_memory = false;   // one memory with the CPU: with !apple_gpu, an Intel Mac's integrated graphics
    bool layer = true;             // the window has its Metal layer
    std::uint32_t upscaler = 0;          // the UPSCALER row as chosen so far
    std::uint32_t internal_height = 0;   // a fixed drawing height chosen so far
    std::uint32_t spatial_quality = 0;   // the upscaler's quality chosen so far
    double contents_scale = 1.0;   // the display's pixels per point
};

// The render settings a first start writes on that Mac: FSR 1 at PERFORMANCE (half the output's size each way).
inline constexpr std::uint32_t kFirstStartUpscaler = 1u;
inline constexpr std::uint32_t kFirstStartSpatialQuality = 3u;

// True when the first start draws at the display's points: an ordinary first start, no benchmark, on an Intel Mac's
// integrated graphics (not Apple's GPU, one memory), on a display with more pixels than points (1.5 or more a point),
// with nothing chosen yet. A discrete GPU, every Apple GPU and a display whose pixels are its points keep the
// display's own pixels.
constexpr bool first_start_draws_at_points(const FirstStartFacts& f) {
    if (!f.ordinary_start || f.benchmark) return false;
    if (!f.settings_path || f.settings_exist || f.apple_gpu || !f.unified_memory || !f.layer) return false;
    if (f.upscaler != 0u || f.internal_height != 0u || f.spatial_quality != 0u) return false;
    return f.contents_scale >= 1.5;
}

}  // namespace galaxy::render
