#pragma once

// The game's fixed step: its simulation runs this many steps a second whatever the display's rate; the renderer
// presents each step or interpolates between them (nebula_game_api.h NEBULA_STEPS_PER_SECOND, checked equal by the host).

#include <cstdint>

namespace galaxy {

inline constexpr int kGameStepsPerSecond = 60;
inline constexpr double kGameStepRate = kGameStepsPerSecond;
inline constexpr std::int64_t kGameStepNs = 1000000000ll / kGameStepsPerSecond;

}  // namespace galaxy
