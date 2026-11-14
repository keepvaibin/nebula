// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// GPU time against scene pixels, and the largest internal height whose predicted time fits the frame: one fit for the
// first start's choice (render_config) and the benchmark's recommendation (benchmark_recommend.h). Pure: no renderer,
// no state.

#include <cstddef>
#include <span>

namespace galaxy::render::cost {

// One measured configuration: the scene's pixels and the GPU's milliseconds a frame there.
struct Sample {
    double pixels = 0.0;
    double ms = 0.0;
};

// t = fixed_ms + ms_per_mpixel * pixels / 1e6: what does not follow the size (vertices, draws, fixed-size passes) and what
// does (every pass a pixel).
struct Line {
    double fixed_ms = 0.0;
    double ms_per_mpixel = 0.0;
    bool valid = false;
};

// Least squares over the samples; invalid without two distinct pixel counts. Both terms are kept >= 0: noise in a fast
// device's two runs must not predict a negative time (a negative slope becomes a flat line through the mean, a negative
// intercept a line through the origin).
[[nodiscard]] Line fit(std::span<const Sample> samples) noexcept;
[[nodiscard]] double predict_ms(const Line& line, double pixels) noexcept;

// The internal heights a choice is made from (render_config's internal_height set without 144, 240 and 360), largest first.
inline constexpr unsigned kHeights[] = {2160u, 1440u, 1080u, 720u, 480u};
// A choice fits when its predicted frame takes at most this share of the frame time.
inline constexpr double kBudgetShare = 0.85;

// 85 % of the game step: the game presents one picture a 60 Hz step unless interpolation adds frames, whatever the
// display's rate (16.67 ms -> 14.17 ms).
[[nodiscard]] double budget_ms() noexcept;

// The scene's pixels at `height` lines for an output of `aspect` (width / height): width = height * aspect rounded to even.
[[nodiscard]] double pixels_at(unsigned height, double aspect) noexcept;

// The largest kHeights entry not above `max_height` whose predicted time times `scale` (a heavier stage than the one
// measured) is at most `budget`; 0 when none fits, not even 480.
[[nodiscard]] unsigned largest_height(const Line& line, double aspect, double budget, double scale,
                                      unsigned max_height) noexcept;

}  // namespace galaxy::render::cost
