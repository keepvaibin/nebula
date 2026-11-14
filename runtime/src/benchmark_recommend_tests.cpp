// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/benchmark_recommend.h"

#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
void check(bool value, const char* label) {
    if (!value) throw std::runtime_error(label);
}

using galaxy::bench::Feature;
using galaxy::bench::Measured;

// A 1920x1080 output, the player at the defaults (panel lines, bloom 100, nothing else), the probe's stage the
// heaviest (scale 1), a steady frame (tail 1: the floor 1.1 is used).
Measured device(double fixed, double per_mpixel, double msaa, double ao, double enhanced, double smaa, double bloom) {
    Measured m;
    m.base = {fixed, per_mpixel / 1e6, true};
    const double prices[] = {msaa, ao, enhanced, smaa, bloom};
    for (std::size_t i = 0; i < galaxy::bench::kFeatureCount; ++i) m.price_per_mpixel[i] = prices[i];
    m.output_width = 1920u;
    m.output_lines = 1080u;
    m.current = {};
    m.current_pixels = galaxy::render::scene_pixels(0u, 1920u, 1080u);
    m.worst_gpu_ms = galaxy::bench::probe_ms(m, m.current, m.current_pixels);
    m.tail = 1.0;
    return m;
}
}  // namespace

int main() {
    namespace bench = galaxy::bench;
    try {
        {
            const bench::Recommendation r = bench::recommend(device(1.0, 1.0, 0.5, 0.5, 0.5, 0.5, 0.5));
            check(r.valid && r.fits && !r.cpu_bound, "a capable GPU fits");
            check(r.choice.internal_height == 0u, "every pass at the panel's lines (1440 with them does not fit)");
            check(r.choice.msaa_samples == 4u && r.choice.ambient_occlusion && r.choice.enhanced_lighting &&
                      r.choice.aa == 1u && r.choice.smaa_quality == 2u && r.choice.bloom == 100u,
                  "every measured pass on");
        }
        {
            const bench::Recommendation r = bench::recommend(device(0.5, 0.5, 0.1, 0.1, 0.1, 0.1, 0.1));
            check(r.fits && r.choice.internal_height == 2160u, "a strong GPU renders above the panel (cap 2x, 2160)");
        }
        {
            const bench::Recommendation r = bench::recommend(device(2.0, 10.0, 4.0, 3.0, 2.0, 1.0, 0.5));
            check(r.fits && r.choice.internal_height == 720u, "a weak GPU gives up every pass, then lines: 720");
            check(r.choice.msaa_samples == 1u && !r.choice.ambient_occlusion && !r.choice.enhanced_lighting &&
                      r.choice.aa == 0u && r.choice.bloom == 0u,
                  "passes given up before lines below the panel");
            check(r.predicted_ms <= galaxy::render::fit_budget_ms(), "the prediction is within the budget");
        }
        {
            // The order: with only MSAA's price too high, MSAA goes and everything after it stays.
            const bench::Recommendation r = bench::recommend(device(1.0, 3.0, 6.0, 0.2, 0.2, 0.2, 0.2));
            check(r.fits && r.choice.internal_height == 0u && r.choice.msaa_samples == 1u && r.choice.ambient_occlusion &&
                      r.choice.enhanced_lighting && r.choice.aa == 1u && r.choice.bloom == 100u,
                  "MSAA is the first pass given up");
        }
        {
            const bench::Recommendation r = bench::recommend(device(10.0, 20.0, 1.0, 1.0, 1.0, 1.0, 1.0));
            check(r.valid && !r.fits && r.choice.internal_height == 480u, "nothing fits: the smallest choice, said so");
        }
        {
            Measured m = device(1.0, 1.0, 0.5, 0.5, 0.5, 0.5, 0.5);
            m.worst_game_cpu_ms = 15.0;
            m.worst_render_cpu_ms = 4.0;
            const bench::Recommendation r = bench::recommend(m);
            check(r.cpu_bound && std::string_view(r.cpu_thread) == "game" && !r.fits, "a CPU-bound device is told so");
            check(r.choice.internal_height == 0u && r.choice.ambient_occlusion, "the GPU choice is still made");
        }
        {
            Measured m = device(1.0, 1.0, 0.5, 0.5, 0.5, 0.5, 0.5);
            m.worst_gpu_ms *= 2.0;  // a stage twice the probe's cost
            const bench::Recommendation r = bench::recommend(m);
            check(r.fits && r.choice.msaa_samples == 1u, "a heavier stage than the probe's lowers the choice");
        }
        {
            Measured m = device(1.0, 1.0, -1.0, 0.5, -1.0, 0.5, 0.5);
            const bench::Recommendation r = bench::recommend(m);
            check(r.choice.msaa_samples == 1u && !r.choice.enhanced_lighting && r.choice.ambient_occlusion,
                  "a pass the probe did not measure stays off");
        }
        {
            Measured m = device(2.0, 10.0, 4.0, 3.0, 2.0, 1.0, 0.5);
            m.upscaler = true;
            m.current.internal_height = 1080u;
            m.current_pixels = galaxy::render::scene_pixels(720u, 1920u, 1080u);
            m.worst_gpu_ms = bench::probe_ms(m, m.current, m.current_pixels);
            const bench::Recommendation r = bench::recommend(m);
            check(r.height_left_out && r.choice.internal_height == 1080u, "an upscaler keeps its height");
        }
        {
            Measured m = device(1.0, 1.0, 0.5, 0.5, 0.5, 0.5, 0.5);
            m.tail = 2.0;
            check(bench::recommend(m).choice.msaa_samples == 1u, "a long tail counts against the choice");
            m.base.valid = false;
            check(!bench::recommend(m).valid, "no probe line, no recommendation");
        }
        check(bench::kGiveUp[0] == bench::GiveUp::HeightAbovePanel && bench::kGiveUp[6] == bench::GiveUp::HeightBelowPanel,
              "the order starts and ends with lines");
    } catch (const std::exception& failure) {
        std::cerr << "benchmark_recommend: " << failure.what() << '\n';
        return 1;
    }
    std::cout << "benchmark_recommend: all checks passed\n";
    return 0;
}
