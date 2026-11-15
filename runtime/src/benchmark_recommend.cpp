// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/benchmark_recommend.h"
#include "galaxy/render/render_config.h"

#include <algorithm>
#include <cmath>
#include <charconv>
#include <string>

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
    case Feature::Smaa: return c.aa == 1u || c.aa == 2u;  // SMAA, or auto (SMAA below 1.5x the window); FXAA unpriced
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

bool parse_recommendation(std::string_view text, render::RenderConfig& config) {
    // write_result's own form: {"schema":1,... "status":"complete" ... "recommendation":{... "settings":[{"key":"k",
    // "current":n,"recommended":m},...]} ...}.
    constexpr std::string_view kKey = "{\"key\":\"";
    constexpr std::string_view kRecommended = "\"recommended\":";
    if (!text.starts_with("{\"schema\":1,") || text.find("\"status\":\"complete\"") == std::string_view::npos) {
        return false;
    }
    const std::size_t at = text.find("\"recommendation\":{");
    if (at == std::string_view::npos) return false;
    std::size_t p = text.find("\"settings\":[", at);
    const std::size_t end = p == std::string_view::npos ? p : text.find(']', p);
    if (end == std::string_view::npos) return false;
    render::RenderConfig c = config;
    unsigned rows = 0u;
    while ((p = text.find(kKey, p)) != std::string_view::npos && p < end) {
        p += kKey.size();
        const std::size_t key_end = text.find('"', p);
        const std::size_t value_at = text.find(kRecommended, p);
        if (key_end == std::string_view::npos || value_at == std::string_view::npos || value_at > end) return false;
        const std::string_view key = text.substr(p, key_end - p);
        const char* const first = text.data() + value_at + kRecommended.size();
        unsigned value = 0u;
        const auto [last, error] = std::from_chars(first, text.data() + end, value);
        if (error != std::errc{} || last == first) return false;
        if (key == "internal_height") {
            if (std::find(render::kInternalHeights.begin(), render::kInternalHeights.end(), value) ==
                render::kInternalHeights.end()) return false;
            c.internal_height = value;
        } else if (key == "msaa_samples") {
            if (value != 1u && value != 2u && value != 4u && value != 8u) return false;
            c.msaa_samples = value;
        } else if (key == "ambient_occlusion" && value <= 1u) {
            c.ambient_occlusion = value != 0u;
        } else if (key == "enhanced_lighting" && value <= 1u) {
            c.enhanced_lighting = value != 0u;
        } else if (key == "aa" && value <= 3u) {
            c.aa_mode = value;
        } else if (key == "smaa_quality" && value <= 3u) {
            c.smaa_quality = value;
        } else if (key == "bloom" && value <= render::kMaxBloom) {
            c.bloom = value;
        } else {
            return false;  // not a key or a value write_result writes
        }
        ++rows;
        p = value_at;
    }
    if (rows == 0u) return false;
    config = c;
    return true;
}

}  // namespace galaxy::bench
