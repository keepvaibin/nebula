// SPDX-License-Identifier: GPL-3.0-only
// galaxy/render/first_start_scale.h (P-035): which Macs' first start draws the scene at the display's points.
#include "galaxy/render/first_start_scale.h"

#include <cstdio>

namespace {
int failures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAILED: %s\n", what);
        ++failures;
    }
}

using galaxy::render::FirstStartFacts;
using galaxy::render::first_start_draws_at_points;

// A MacBook Pro of 2017 with Intel HD Graphics 630 and a Retina panel, started the first time.
constexpr FirstStartFacts intel_retina() {
    FirstStartFacts f;
    f.unified_memory = true;
    f.contents_scale = 2.0;
    return f;
}
}  // namespace

int main() {
    static_assert(first_start_draws_at_points(intel_retina()), "the decision is a constant expression");
    expect(first_start_draws_at_points(intel_retina()), "Intel integrated graphics on a Retina panel: the points");

    FirstStartFacts f = intel_retina();
    f.contents_scale = 1.0;
    expect(!first_start_draws_at_points(f), "a display whose pixels are its points keeps them");
    f.contents_scale = 1.5;
    expect(first_start_draws_at_points(f), "1.5 pixels a point is enough");

    f = intel_retina();
    f.unified_memory = false;
    expect(!first_start_draws_at_points(f), "a discrete GPU (Radeon Pro) keeps the display's pixels");

    f = intel_retina();
    f.apple_gpu = true;
    expect(!first_start_draws_at_points(f), "Apple's GPU keeps the display's pixels");

    f = intel_retina();
    f.settings_exist = true;
    expect(!first_start_draws_at_points(f), "a later start keeps the player's settings");
    f = intel_retina();
    f.settings_path = false;
    expect(!first_start_draws_at_points(f), "no settings path: nothing is written");

    f = intel_retina();
    f.ordinary_start = false;
    expect(!first_start_draws_at_points(f), "a test's or a tool's start stays the player's");
    f = intel_retina();
    f.benchmark = true;
    expect(!first_start_draws_at_points(f), "a benchmark measures the settings as they are");

    f = intel_retina();
    f.upscaler = 2u;
    expect(!first_start_draws_at_points(f), "an upscaler chosen another way stays");
    f = intel_retina();
    f.internal_height = 720u;
    expect(!first_start_draws_at_points(f), "a fixed height chosen another way stays");
    f = intel_retina();
    f.spatial_quality = 1u;
    expect(!first_start_draws_at_points(f), "a quality chosen another way stays");

    f = intel_retina();
    f.layer = false;
    expect(!first_start_draws_at_points(f), "no Metal layer: nothing to measure");

    expect(galaxy::render::kFirstStartUpscaler == 1u && galaxy::render::kFirstStartSpatialQuality == 3u,
           "FSR 1 at PERFORMANCE");

    if (failures != 0) return 1;
    std::printf("first_start_scale_tests: all passed\n");
    return 0;
}
