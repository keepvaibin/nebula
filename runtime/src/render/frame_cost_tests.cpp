// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/render/frame_cost.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool value, const char* label) {
    if (!value) throw std::runtime_error(label);
}
bool near(double a, double b) {
    return std::fabs(a - b) < 1e-6;
}
}  // namespace

int main() {
    namespace cost = galaxy::render::cost;
    try {
        // Two points give the line through them: 2 ms fixed, 4 ms a megapixel.
        const std::vector<cost::Sample> two{{0.5e6, 4.0}, {1.0e6, 6.0}};
        const cost::Line line = cost::fit(two);
        check(line.valid && near(line.fixed_ms, 2.0) && near(line.ms_per_mpixel, 4.0), "two points");
        check(near(cost::predict_ms(line, 2.0e6), 10.0), "prediction on the line");

        // Least squares over noisy points stays on the trend.
        const std::vector<cost::Sample> three{{0.4e6, 3.5}, {0.9e6, 6.1}, {2.0e6, 10.4}};
        const cost::Line noisy = cost::fit(three);
        check(noisy.valid && noisy.ms_per_mpixel > 3.5 && noisy.ms_per_mpixel < 4.5, "three points");

        check(!cost::fit(std::vector<cost::Sample>{{1.0e6, 5.0}}).valid, "one point is no line");
        check(!cost::fit(std::vector<cost::Sample>{{1.0e6, 5.0}, {1.0e6, 6.0}}).valid, "one size is no line");
        check(!cost::fit(std::vector<cost::Sample>{{0.0, 5.0}, {-1.0, 6.0}}).valid, "sizes must be positive");

        // A faster big run than small run (noise): flat at the mean, never negative.
        const cost::Line flat = cost::fit(std::vector<cost::Sample>{{0.5e6, 3.0}, {1.0e6, 2.8}});
        check(flat.valid && flat.ms_per_mpixel == 0.0 && near(flat.fixed_ms, 2.9), "negative slope clamps");
        // A line that would cross below zero goes through the origin instead.
        const cost::Line origin = cost::fit(std::vector<cost::Sample>{{0.5e6, 0.5}, {1.0e6, 3.0}});
        check(origin.valid && origin.fixed_ms == 0.0 && origin.ms_per_mpixel > 0.0, "negative intercept clamps");

        check(near(cost::budget_ms(), 0.85 * 1000.0 / 60.0), "budget is 85 % of the 60 Hz step");
        check(near(cost::pixels_at(480u, 16.0 / 9.0), 854.0 * 480.0), "480p at 16:9 is 854 x 480");
        check(near(cost::pixels_at(720u, 16.0 / 9.0), 1280.0 * 720.0), "720p at 16:9");
        check(near(cost::pixels_at(480u, 0.0), 854.0 * 480.0), "an unknown aspect is 16:9");

        // 1 ms fixed + 10 ms a megapixel: 720p (0.92 Mpx) = 10.2 ms fits 14.17, 1080p (2.07 Mpx) = 21.7 does not.
        const cost::Line gpu{1.0, 10.0, true};
        const double budget = cost::budget_ms();
        check(cost::largest_height(gpu, 16.0 / 9.0, budget, 1.0, 2160u) == 720u, "largest height that fits");
        check(cost::largest_height(gpu, 16.0 / 9.0, budget, 1.5, 2160u) == 480u, "a heavier stage lowers it");
        check(cost::largest_height(gpu, 16.0 / 9.0, budget, 4.0, 2160u) == 0u, "nothing fits");
        check(cost::largest_height(cost::Line{0.1, 0.1, true}, 16.0 / 9.0, budget, 1.0, 1080u) == 1080u,
              "never above the cap");
        check(cost::largest_height(cost::Line{}, 16.0 / 9.0, budget, 1.0, 2160u) == 0u, "no line, no choice");
    } catch (const std::exception& failure) {
        std::cerr << "frame_cost: " << failure.what() << '\n';
        return 1;
    }
    std::cout << "frame_cost: all checks passed\n";
    return 0;
}
