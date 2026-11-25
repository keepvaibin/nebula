#pragma once

// The game's fixed step: its simulation runs this many steps a second whatever the display's rate; the renderer
// presents each step or interpolates between them (nebula_game_api.h NEBULA_STEPS_PER_SECOND, checked equal by the host).

#include <cstdint>

namespace galaxy {

inline constexpr int kGameStepsPerSecond = 60;
inline constexpr double kGameStepRate = kGameStepsPerSecond;
inline constexpr std::int64_t kGameStepNs = 1000000000ll / kGameStepsPerSecond;

// A frame-rate setting is the game's own frames (each step shown as it is, no in-between pictures) when it is above 0
// and at most the step rate and this allowance (the literal 61 of Android's surface vote and display mode carried it;
// the code never said why, so it is kept as it was). Android's vote takes such a rate as a fixed source and leaves the
// display mode to the system; GameActivity.java's FrameRates.OWN_RATE_ALLOWANCE is the same 1.
inline constexpr double kOwnFrameRateAllowance = 1.0;
[[nodiscard]] constexpr bool own_frame_rate(double rate, double step_rate = kGameStepRate) noexcept {
    return rate > 0.0 && rate <= step_rate + kOwnFrameRateAllowance;
}

}  // namespace galaxy
