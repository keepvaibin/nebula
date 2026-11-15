// SPDX-License-Identifier: GPL-3.0-only
// resolution_fit.h beyond render_config_tests.cpp's cases: least squares over noisy samples, the clamps, the budget, the
// pixel counts and the cap above the output.
#include "galaxy/render/resolution_fit.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* label) {
    if (!value) throw std::runtime_error(label);
}
bool near(double a, double b, double tolerance = 1e-9) {
    return std::fabs(a - b) < tolerance;
}
}  // namespace

int main() {
    namespace render = galaxy::render;
    try {
        {
            // Two points give the line through them: 2 ms fixed, 4 ms a megapixel.
            const double pixels[] = {0.5e6, 1.0e6};
            const double ms[] = {4.0, 6.0};
            const render::GpuTimeFit fit = render::fit_gpu_time(pixels, ms, 2u);
            check(fit.valid && near(fit.a_ms, 2.0) && near(fit.b_ms_per_pixel, 4e-6, 1e-15), "two points");
            check(near(render::predicted_gpu_ms(fit, 2.0e6), 10.0), "prediction on the line");
        }
        {
            // Least squares over noisy points stays on the trend.
            const double pixels[] = {0.4e6, 0.9e6, 2.0e6};
            const double ms[] = {3.5, 6.1, 10.4};
            const render::GpuTimeFit fit = render::fit_gpu_time(pixels, ms, 3u);
            check(fit.valid && fit.b_ms_per_pixel > 3.5e-6 && fit.b_ms_per_pixel < 4.5e-6, "three noisy points");
        }
        {
            const double pixels[] = {1.0e6, -1.0};
            const double ms[] = {5.0, 6.0};
            check(!render::fit_gpu_time(pixels, ms, 1u).valid, "one point is no line");
            check(!render::fit_gpu_time(pixels, ms, 2u).valid, "a negative pixel count is refused");
            const double bad_ms[] = {5.0, NAN};
            const double sizes[] = {0.5e6, 1.0e6};
            check(!render::fit_gpu_time(sizes, bad_ms, 2u).valid, "a time that is not a number is refused");
        }
        {
            // A faster big run than small run (noise): flat at the mean, never a negative slope.
            const double pixels[] = {0.5e6, 1.0e6};
            const double ms[] = {3.0, 2.8};
            const render::GpuTimeFit fit = render::fit_gpu_time(pixels, ms, 2u);
            check(fit.valid && fit.b_ms_per_pixel == 0.0 && near(fit.a_ms, 2.9), "a negative slope is flat");
            // A line that would cross below zero keeps its slope with no fixed part.
            const double steep[] = {0.5, 3.0};
            const render::GpuTimeFit origin = render::fit_gpu_time(pixels, steep, 2u);
            check(origin.valid && origin.a_ms == 0.0 && origin.b_ms_per_pixel > 0.0, "a negative intercept is 0");
        }
        check(near(render::fit_budget_ms(), render::kFitFrameShare * 1000.0 / 60.0), "the budget is a share of the 60 Hz step");
        {
            const double p480 = render::scene_pixels(480u, 1920u, 1080u);
            check(near(p480, 853.0 * 480.0) || near(p480, 854.0 * 480.0), "480 lines of 16:9");
            check(near(render::scene_pixels(0u, 1920u, 1080u), 1920.0 * 1080.0), "match display is the output");
            check(near(render::scene_pixels(2160u, 1920u, 1080u), 3840.0 * 2160.0), "2160 lines above a 1080 output");
        }
        {
            // 1 ms + 10 ms a megapixel on a 1920x1080 output, budget 13.3 ms: 720 (0.92 Mpx) = 10.2 ms fits, match display
            // (2.07 Mpx) = 21.7 does not.
            const render::GpuTimeFit fit{1.0, 10e-6, true};
            const double budget = render::fit_budget_ms();
            check(render::largest_fitting_height(fit, 1920u, 1080u, 1.0, budget) == 720u, "the largest height that fits");
            check(render::largest_fitting_height(fit, 1920u, 1080u, 1.4, budget) == 480u, "a heavier stage lowers it");
            check(render::largest_fitting_height(fit, 1920u, 1080u, 4.0, budget) == 480u, "nothing fits: the 480 floor");
            const render::GpuTimeFit fast{0.1, 0.1e-6, true};
            check(render::largest_fitting_height(fast, 1920u, 1080u, 1.0, budget, 1440u) == 1440u, "never above the cap");
            check(render::largest_fitting_height(fast, 1920u, 1080u, 1.0, budget) == 0u, "no cap: match display");
        }
    } catch (const std::exception& failure) {
        std::cerr << "resolution_fit: " << failure.what() << '\n';
        return 1;
    }
    std::cout << "resolution_fit: all checks passed\n";
    return 0;
}
