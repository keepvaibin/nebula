// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/benchmark_recommend.h"

#include <algorithm>
#include <cmath>

namespace galaxy::bench {
namespace {

bool measured(const Measured& m, Feature feature) {
    const double price = m.price_per_mpixel[static_cast<std::size_t>(feature)];
    return price >= 0.0 && std::isfinite(price);
}

double price(const Measured& m, Feature feature) {
    return measured(m, feature) ? m.price_per_mpixel[static_cast<std::size_t>(feature)] : 0.0;
}

bool on(const Choice& c, Feature feature) {
    switch (feature) {
    case Feature::Msaa: return c.msaa_samples > 1u;
    case Feature::Ao: return c.ambient_occlusion;
    case Feature::Enhanced: return c.enhanced_lighting;
    case Feature::Smaa: return c.aa != 0u;
    case Feature::Bloom: return c.bloom != 0u;
    case Feature::Count: break;
    }
    return false;
}

void turn_off(Choice& c, GiveUp step) {
    switch (step) {
    case GiveUp::Msaa: c.msaa_samples = 1u; break;
    case GiveUp::Ao: c.ambient_occlusion = false; break;
    case GiveUp::Enhanced: c.enhanced_lighting = false; break;
    case GiveUp::Smaa: c.aa = 0u; break;
    case GiveUp::Bloom: c.bloom = 0u; break;
    case GiveUp::HeightAbovePanel:
    case GiveUp::HeightBelowPanel: break;
    }
}

// The probe's line for a choice's passes: their prices join the per-pixel term.
render::GpuTimeFit fit_of(const Measured& m, const Choice& c) {
    render::GpuTimeFit fit = m.base;
    for (std::size_t f = 0; f < kFeatureCount; ++f) {
        if (on(c, static_cast<Feature>(f))) {
            fit.b_ms_per_pixel += price(m, static_cast<Feature>(f)) / 1e6;
        }
    }
    return fit;
}

}  // namespace

double probe_ms(const Measured& m, const Choice& choice, double pixels) noexcept {
    return render::predicted_gpu_ms(fit_of(m, choice), pixels);
}

Recommendation recommend(const Measured& m) noexcept {
    Recommendation r;
    if (!m.base.valid || !(m.worst_gpu_ms > 0.0) || m.output_lines == 0u || m.output_width == 0u) {
        return r;
    }
    r.valid = true;
    const unsigned width = m.output_width, lines = m.output_lines;
    const double budget = render::fit_budget_ms();
    const double tail = std::max(m.tail, kMinTail);
    const double current_probe = probe_ms(m, m.current, m.current_pixels);
    // The heaviest stage over the probe's stage, at the player's settings, and the frame's tail: the fit's heavy factor.
    const double heavy = (current_probe > 0.0 ? m.worst_gpu_ms / current_probe : 1.0) * tail;
    const double cpu_ms = std::max(m.worst_game_cpu_ms, m.worst_render_cpu_ms) * tail;
    r.cpu_bound = cpu_ms > budget;
    r.cpu_thread = !r.cpu_bound ? "" : m.worst_game_cpu_ms >= m.worst_render_cpu_ms ? "game" : "render";
    const auto pixels_at = [&](unsigned height) { return render::scene_pixels(height, width, lines); };
    const auto gpu_at = [&](const Choice& c, double pixels) { return probe_ms(m, c, pixels) * heavy; };

    // The richest start: every pass the probe measured, at its measured quality (MSAA 4, SMAA high); the player's bloom
    // strength when it is on.
    Choice c = m.current;
    c.msaa_samples = measured(m, Feature::Msaa) ? 4u : 1u;
    c.ambient_occlusion = measured(m, Feature::Ao);
    c.enhanced_lighting = measured(m, Feature::Enhanced);
    c.aa = measured(m, Feature::Smaa) ? 1u : 0u;
    c.smaa_quality = c.aa != 0u ? 2u : m.current.smaa_quality;
    c.bloom = !measured(m, Feature::Bloom) ? 0u : m.current.bloom != 0u ? m.current.bloom : 100u;

    const auto finish = [&](const Choice& chosen, double pixels) {
        r.choice = chosen;
        r.predicted_ms = std::max(gpu_at(chosen, pixels), cpu_ms);
        r.fits = r.predicted_ms <= budget;
        return r;
    };

    if (m.upscaler) {
        // The upscaler's quality sets the scene's size: passes only, at the size measured.
        r.height_left_out = true;
        c.internal_height = m.current.internal_height;
        for (const GiveUp step : kGiveUp) {
            if (gpu_at(c, m.current_pixels) <= budget) {
                break;
            }
            turn_off(c, step);
        }
        return finish(c, m.current_pixels);
    }

    // Steps 1-6: with the passes still on, the largest height not below the panel's that fits (above it up to twice its
    // lines, 2160 at most); when none, the next pass goes.
    const unsigned above = std::min(2u * lines, render::kInternalHeights.back());
    for (const GiveUp step : kGiveUp) {
        if (step == GiveUp::HeightBelowPanel) {
            break;
        }
        const unsigned h = render::largest_fitting_height(fit_of(m, c), width, lines, heavy, budget, above);
        if ((h == 0u || h > lines) && gpu_at(c, pixels_at(h)) <= budget) {
            c.internal_height = h;
            return finish(c, pixels_at(h));
        }
        turn_off(c, step);
    }
    // Step 7: every optional pass off; the largest height below the panel's that fits, else 480 (the fit's floor).
    c.internal_height = render::largest_fitting_height(fit_of(m, c), width, lines, heavy, budget);
    return finish(c, pixels_at(c.internal_height));
}

}  // namespace galaxy::bench
