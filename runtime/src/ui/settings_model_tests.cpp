#include "galaxy/ui/settings_model.h"

#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error The shared settings model must not include Windows.h
#endif

#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* label) {
    if (!value) throw std::runtime_error(label);
}
}

int main() {
    using namespace galaxy::ui;
    try {
        galaxy::render::RenderConfig config;
        config.frame_generation = 0;
        config.max_fps = 60;
        FrameRateCaps sixty{60};
        check(interpolation_modes(sixty) == std::vector{Interpolation::kOff}, "60 Hz has no interpolation target");
        check(galaxy::ui::kStepRate == 60 && galaxy::ui::kHalfStepRate == 30, "the step rates are the game's");
        FrameRateCaps unknown{0};
        check(interpolation_targets(Interpolation::kNative, unknown).back() == 0u, "an unknown display rate offers its own rate");
        FrameRateCaps high{144};
        check(interpolation_targets(Interpolation::kNative, high) == std::vector<unsigned>{75, 90, 100, 120, 0}, "144 Hz target choices");
        config.max_fps = 165;
        normalize_interpolation(config, high);
        check(config.max_fps == 0, "target clamps to display rate");
        config.max_fps = 70;
        normalize_interpolation(config, high);
        check(config.max_fps == 75, "target uses lowest available rate");
        config.max_fps = 50;
        normalize_interpolation(config, high);
        check(config.max_fps == 60, "base frame rate snaps to 60");
        config.max_fps = 20;
        normalize_interpolation(config, high);
        check(config.max_fps == 30, "base frame rate snaps to 30");
        FrameRateCaps fast{540};
        check(interpolation_targets(Interpolation::kNative, fast) ==
                  std::vector<unsigned>{75, 90, 100, 120, 144, 165, 200, 240, 280, 300, 360, 480, 500, 0},
              "540 Hz target choices");
        FrameRateCaps no_vsync{144};
        no_vsync.unlimited = true;
        check(interpolation_targets(Interpolation::kNative, no_vsync) ==
                  std::vector<unsigned>{75, 90, 100, 120, 0, kUnlimitedTarget},
              "without VSync, Unlimited after the display's rate");
        config.max_fps = galaxy::render::kUnlimitedFrameRate;
        check(interpolation_of(config) == Interpolation::kNative && frame_rate_target(config.max_fps) == kUnlimitedTarget &&
                  galaxy::render::unlimited_frame_rate(frame_rate_of_target(kUnlimitedTarget)),
              "Unlimited is native interpolation");
        normalize_interpolation(config, no_vsync);
        check(galaxy::render::unlimited_frame_rate(config.max_fps), "Unlimited kept without VSync");
        normalize_interpolation(config, high);
        check(config.max_fps == 0, "with VSync, Unlimited becomes the display's rate");
        FrameRateCaps fixed{240, true, true, false, 2};
        check(interpolation_targets(Interpolation::kFrameGeneration, fixed) == std::vector<unsigned>{120, 180, 0}, "fixed generation whole multiples");
        check(native_settings_first_row(0, 9, 12) == 1, "scroll selected row into view");
        check(native_settings_first_row(3, 1, 12) == 1, "scroll backwards");
        check(quality_render_fraction(0) == 1.0f && quality_render_fraction(3) == 0.5f, "render fraction by quality");
        check(quality_render_fraction(5) == 1.0f / 1.3f && quality_render_fraction(9) == 1.0f / 1.3f, "ultra quality, the last");
        auto row = button_row("actions", {"ONE", "TWO"}, {}, "help");
        check(row.value == "ONE|TWO" && row.buttons.size() == 2, "button row view");
        std::cout << "settings model: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
