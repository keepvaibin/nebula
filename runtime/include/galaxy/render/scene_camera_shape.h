#pragma once

// The scene camera's numbers (scene_camera.hlsl), the same on every backend: the wide scene target's colour is the
// game's own display values, with what the enhanced lighting adds past white left in; the camera brings it back to
// the display at the 3D scene's end. A value at or below the knee passes unchanged, so what the game authored stays.

#include <algorithm>
#include <cmath>

namespace galaxy::render::camera {

// Linear light: where the shoulder starts (its values pass unchanged below). Authored white comes out at 0.963 linear.
inline constexpr float kKnee = 0.9f;
// The eye closes while the scene holds light past white, which only the enhanced lighting makes: its exposure falls
// toward this at most, and a picture of the game's own colours, however bright, leaves it open.
inline constexpr float kLeastExposure = 0.85f;
// Light past white, averaged over the picture, that takes the exposure to 1 / (1 + 1).
inline constexpr float kOverRange = 1.0f;
// The eye's time constants, seconds: it closes quickly and opens slowly.
inline constexpr float kCloseSeconds = 0.35f;
inline constexpr float kOpenSeconds = 1.5f;
// The meter's grid over the picture.
inline constexpr unsigned kMeterColumns = 16u;
inline constexpr unsigned kMeterRows = 9u;

// How far toward its target the exposure moves over `seconds`, closing or opening; any frame rate reaches it alike.
[[nodiscard]] inline float exposure_step(float seconds, bool closing) noexcept {
    const float tau = closing ? kCloseSeconds : kOpenSeconds;
    return 1.0f - std::exp(-std::clamp(seconds, 0.0f, 1.0f) / tau);
}

// The constants of both passes.
struct Constants {
    float width = 0.0f, height = 0.0f;  // the picture's pixels
    float seconds = 0.0f;               // since the last meter
    float pad = 0.0f;
};

}  // namespace galaxy::render::camera
