// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The benchmark's recommended settings: a pure function of its measurements and the device (benchmark.h runs them).
// Settings are named by their config.ini keys (render_config.cpp's reader) so every app shows and applies them.

#include "galaxy/render/resolution_fit.h"

#include <cstddef>
#include <string_view>

namespace galaxy::render {
struct RenderConfig;
}  // namespace galaxy::render

namespace galaxy::bench {

// The optional passes a recommendation turns on or off, as the probe measured them.
enum class Feature : unsigned { Msaa, Ao, Enhanced, Smaa, Bloom, Count };
inline constexpr std::size_t kFeatureCount = static_cast<std::size_t>(Feature::Count);

// The settings a choice sets, by key: internal_height (0: the panel's lines), msaa_samples, ambient_occlusion,
// enhanced_lighting, aa (0 off, 1 SMAA), smaa_quality, bloom (percent).
struct Choice {
    unsigned internal_height = 0u;
    unsigned msaa_samples = 1u;
    bool ambient_occlusion = false;
    bool enhanced_lighting = false;
    unsigned aa = 0u;
    unsigned smaa_quality = 2u;
    unsigned bloom = 100u;
};

// The order in which quality is given up, one step at a time, until the predicted frame fits. Steps 1 and 7 move the
// internal height; the others turn a pass off.
enum class GiveUp : unsigned {
    HeightAbovePanel,  // 1. internal height above the panel's lines, down to the panel's
    Msaa,              // 2. MSAA 4 -> 1 (the probe measures 4)
    Ao,                // 3. ambient occlusion off
    Enhanced,          // 4. enhanced lighting off
    Smaa,              // 5. SMAA (high, as measured) off
    Bloom,             // 6. bloom off
    HeightBelowPanel,  // 7. internal height from the panel's down to 480
};
inline constexpr GiveUp kGiveUp[] = {GiveUp::HeightAbovePanel, GiveUp::Msaa,  GiveUp::Ao,
                                     GiveUp::Enhanced,         GiveUp::Smaa,  GiveUp::Bloom,
                                     GiveUp::HeightBelowPanel};

struct Measured {
    // The probe stage: GPU ms against pixels with every optional pass off (two internal heights), and each pass's price
    // (its run alone minus the run without it, per megapixel); a price < 0 is not measured (the pass did not run).
    render::GpuTimeFit base;
    double price_per_mpixel[kFeatureCount] = {-1.0, -1.0, -1.0, -1.0, -1.0};
    // The heaviest stage of the run at the player's settings, and the probe's prediction for those settings: their ratio
    // carries the probe's shape to that stage.
    double worst_gpu_ms = 0.0;
    double worst_game_cpu_ms = 0.0;
    double worst_render_cpu_ms = 0.0;
    // That stage's presented-interval p95 over p50 (the frame's tail over its middle); at least kMinTail is used.
    double tail = 1.0;
    // The player's settings, the scene pixels they rendered at, and whether a temporal or spatial upscaler set them.
    Choice current;
    double current_pixels = 0.0;
    bool upscaler = false;
    // The output's size.
    unsigned output_width = 0u;
    unsigned output_lines = 0u;
};

inline constexpr double kMinTail = 1.1;

struct Recommendation {
    bool valid = false;            // the probe gave a line and a stage was measured
    Choice choice;
    double predicted_ms = 0.0;     // the heaviest stage's frame, p95, with the choice
    bool fits = false;             // predicted_ms <= render::fit_budget_ms()
    bool height_left_out = false;  // an upscaler sets the height: internal_height is not recommended
    bool cpu_bound = false;        // the game or render thread alone passes the budget: resolution cannot help
    const char* cpu_thread = "";   // "game" or "render" when cpu_bound
};

// The probe stage's GPU ms for a choice at `pixels` (the base line plus the price of each pass it turns on).
[[nodiscard]] double probe_ms(const Measured& m, const Choice& choice, double pixels) noexcept;
[[nodiscard]] Recommendation recommend(const Measured& m) noexcept;

// A benchmark result's text (benchmark.cpp write_result, schema 1): when its status is complete and it has a
// recommendation, its recommended settings over `config` and true; else false and `config` as it was. Every key and
// value is checked against what write_result writes; anything else refuses the whole result.
[[nodiscard]] bool parse_recommendation(std::string_view json, render::RenderConfig& config);

}  // namespace galaxy::bench
