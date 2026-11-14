// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/render/frame_cost.h"

#include "galaxy/game_step.h"

#include <cmath>

namespace galaxy::render::cost {

Line fit(std::span<const Sample> samples) noexcept {
    double n = 0.0, sx = 0.0, sy = 0.0;
    for (const Sample& s : samples) {
        if (!(s.pixels > 0.0) || !std::isfinite(s.ms) || s.ms < 0.0) {
            continue;
        }
        n += 1.0;
        sx += s.pixels / 1e6;
        sy += s.ms;
    }
    if (n < 2.0) {
        return {};
    }
    const double mx = sx / n, my = sy / n;
    double sxx = 0.0, sxy = 0.0;
    for (const Sample& s : samples) {
        if (!(s.pixels > 0.0) || !std::isfinite(s.ms) || s.ms < 0.0) {
            continue;
        }
        const double dx = s.pixels / 1e6 - mx;
        sxx += dx * dx;
        sxy += dx * (s.ms - my);
    }
    if (!(sxx > 1e-12)) {
        return {};
    }
    Line line;
    line.valid = true;
    line.ms_per_mpixel = sxy / sxx;
    line.fixed_ms = my - line.ms_per_mpixel * mx;
    if (line.ms_per_mpixel < 0.0) {
        line.ms_per_mpixel = 0.0;
        line.fixed_ms = my;
    } else if (line.fixed_ms < 0.0) {
        // Through the origin: the least-squares slope of y = b x.
        double sxx0 = 0.0, sxy0 = 0.0;
        for (const Sample& s : samples) {
            if (!(s.pixels > 0.0) || !std::isfinite(s.ms) || s.ms < 0.0) {
                continue;
            }
            const double x = s.pixels / 1e6;
            sxx0 += x * x;
            sxy0 += x * s.ms;
        }
        line.fixed_ms = 0.0;
        line.ms_per_mpixel = sxy0 / sxx0;
    }
    return line;
}

double predict_ms(const Line& line, double pixels) noexcept {
    return line.fixed_ms + line.ms_per_mpixel * pixels / 1e6;
}

double budget_ms() noexcept {
    return kBudgetShare * 1000.0 / static_cast<double>(kGameStepsPerSecond);
}

double pixels_at(unsigned height, double aspect) noexcept {
    if (!(aspect > 0.0) || !std::isfinite(aspect)) {
        aspect = 16.0 / 9.0;
    }
    const double width = 2.0 * std::round(static_cast<double>(height) * aspect / 2.0);
    return width * static_cast<double>(height);
}

unsigned largest_height(const Line& line, double aspect, double budget, double scale, unsigned max_height) noexcept {
    if (!line.valid) {
        return 0u;
    }
    for (const unsigned height : kHeights) {
        if (height <= max_height && predict_ms(line, pixels_at(height, aspect)) * scale <= budget) {
            return height;
        }
    }
    return 0u;
}

}  // namespace galaxy::render::cost
