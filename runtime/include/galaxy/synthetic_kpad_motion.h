#pragma once

#include "galaxy/native_input.h"
#include <array>
#include <cmath>

namespace galaxy::input {

// Adapted from WiiCompiled (GPL-3.0), revision
// 9b7b9913e4ab60b9c57fa4be56b8da308e76d06e:
// runtime/src/hle/input/kpad.cpp::Length, Distance, WriteStatus and
// WriteUnifiedStatus's inverse raw-accelerometer/KPAD axis convention.
// Galaxy uses its own calibrated synthetic 10-bit sample, not SDL sensors.
struct SyntheticKpadMotion {
    std::array<float, 3> acceleration{};
    float magnitude = 1.0f;
    float speed = 0.0f;
};

struct SyntheticKpadMotionState {
    std::array<float, 3> previous{0.0f, -1.0f, 0.0f};
};

[[nodiscard]] inline SyntheticKpadMotion sample_synthetic_kpad_motion(
    Axis10 raw, SyntheticKpadMotionState& state) noexcept {
    constexpr float counts_per_g = 100.0f;
    const float x = (static_cast<float>(raw.x) - kWiimoteRestAccelX) / counts_per_g;
    const float y = (static_cast<float>(raw.y) - kWiimoteRestAccelY) / counts_per_g;
    const float z = 1.0f + (static_cast<float>(raw.z) - kWiimoteRestAccelZ) / counts_per_g;
    SyntheticKpadMotion sample;
    sample.acceleration = {-x, -z, y};
    const auto length = [](const std::array<float, 3>& v) {
        return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    };
    sample.magnitude = length(sample.acceleration);
    std::array<float, 3> difference{};
    for (std::size_t i = 0; i < 3; ++i)
        difference[i] = sample.acceleration[i] - state.previous[i];
    sample.speed = length(difference);
    state.previous = sample.acceleration;
    return sample;
}

} // namespace galaxy::input
