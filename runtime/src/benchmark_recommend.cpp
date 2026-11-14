// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/benchmark_recommend.h"

#include <algorithm>
#include <cmath>

namespace galaxy::bench {
namespace {

namespace cost = render::cost;

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
cost::Line line_of(const Measured& m, const Choice& c) {
    cost::Line line = m.base;
    for (std::size_t f = 0; f < kFeatureCount; ++f) {
        if (on(c, static_cast<Feature>(f))) {
            line.ms_per_mpixel += price(m, static_cast<Feature>(f));
        }
    }
    return line;
}

}  // namespace

double probe_ms(const Measured& m, const Choice& choice, double pixels) noexcept {
    return cost::predict_ms(line_of(m, choice), pixels);
}

Recommendation recommend(const Measured& m) noexcept {
    Recommendation r;
    if (!m.base.valid || !(m.worst_gpu_ms > 0.0) || m.panel_lines == 0u) {
        return r;
    }
    r.valid = true;
    const double budget = cost::budget_ms();
    const double tail = std::max(m.tail, kMinTail);
    const double current_probe = probe_ms(m, m.current, m.current_pixels);
    const double scale = current_probe > 0.0 ? m.worst_gpu_ms / current_probe : 1.0;
    const double cpu_ms = std::max(m.worst_game_cpu_ms, m.worst_render_cpu_ms) * tail;
    r.cpu_bound = cpu_ms > budget;
    r.cpu_thread = !r.cpu_bound ? "" : m.worst_game_cpu_ms >= m.worst_render_cpu_ms ? "game" : "render";
    const double panel_pixels = cost::pixels_at(m.panel_lines, m.aspect);
    const auto gpu_at = [&](const Choice& c, double pixels) { return probe_ms(m, c, pixels) * scale * tail; };

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

    const unsigned cap = std::min(2u * m.panel_lines, cost::kHeights[0]);
    for (const GiveUp step : kGiveUp) {
        if (step == GiveUp::HeightBelowPanel) {
            break;
        }
        // Steps 1-6: the largest height not below the panel's that fits with the passes still on.
        const unsigned h = cost::largest_height(line_of(m, c), m.aspect, budget, scale * tail, cap);
        if (h > m.panel_lines) {
            c.internal_height = h;
            return finish(c, cost::pixels_at(h, m.aspect));
        }
        if (gpu_at(c, panel_pixels) <= budget) {
            c.internal_height = 0u;
            return finish(c, panel_pixels);
        }
        turn_off(c, step);
    }
    // Step 7: every optional pass off; the panel's lines, else the largest height below them that fits, else the smallest.
    const unsigned below = cost::largest_height(line_of(m, c), m.aspect, budget, scale * tail, m.panel_lines);
    const unsigned smallest = cost::kHeights[std::size(cost::kHeights) - 1u];
    if (gpu_at(c, panel_pixels) <= budget || m.panel_lines <= smallest) {
        c.internal_height = 0u;
        return finish(c, panel_pixels);
    }
    c.internal_height = below == 0u ? smallest : below;
    return finish(c, cost::pixels_at(c.internal_height, m.aspect));
}

}  // namespace galaxy::bench
