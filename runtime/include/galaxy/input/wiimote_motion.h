// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// A real Wii Remote's sensors as what the player did: its reports go in as they arrive, and each game frame takes the
// tilt, the spins and whether the pointer is usable. Nothing past this reads a sensor.

#include "galaxy/native_input.h"

#include <array>
#include <cstdint>
#include <mutex>

namespace galaxy::input::wiimote {

struct MotionFrame {
    std::array<float, 3> tilt{0.0f, 0.0f, 1.0f};
    std::array<float, 3> sub_tilt{0.0f, 0.0f, 1.0f};
    std::uint8_t spin_presses = 0;
    std::uint8_t sub_spin_presses = 0;
};

class Motion {
public:
    Motion();

    void reset();
    // A report, read (wiimote_input.h Reader).
    void add(const PlayerInput& report);
    // This frame: the newest report, with the pointer as it should be used, and `frame`.
    [[nodiscard]] PlayerInput read(MotionFrame& frame);

private:
    struct Hand {
        std::array<std::array<float, 3>, 128> history{};
        int newest = -1;
        int count = 0;
        std::array<float, 3> eased{0.0f, 0.0f, 1.0f};
        bool arrived = false;  // a report since the last frame
        float swing_distance = 1.0f;
        bool latched = false;
        int latched_frames = 0;
        int below_frames = 0;
        bool centrifugal = false;
        int held_frames = 0;
        int cooldown_frames = 0;
    };

    void push(Hand& hand, const std::array<float, 3>& value);
    [[nodiscard]] bool past(const Hand& hand, int index, std::array<float, 3>& out) const;
    [[nodiscard]] bool update(Hand& hand);

    std::mutex lock_;
    PlayerInput newest_{};
    Hand main_;
    Hand sub_;
    bool in_screen_ = false;
    bool pointer_passed_ = false;
    int valid_samples_ = 0;
    int invalid_samples_ = 0;
    float pointer_x_ = 0.0f;
    float pointer_y_ = 0.0f;
    float roll_x_ = 1.0f;
    float roll_y_ = 0.0f;
};

}  // namespace galaxy::input::wiimote
