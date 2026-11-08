// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/input/wiimote_motion_pointer.h"

#include <algorithm>
#include <cmath>

namespace galaxy::input::wiimote {

namespace {

// The picture's aspect the pointer's height is measured in (content_viewport.h kDesignContentAspect).
constexpr float kAspect = 16.0f / 9.0f;
// A MotionPlus sample comes every 10 ms alone, every 20 ms with a Nunchuk sharing the reports.
constexpr float kFirstSampleSeconds = 0.01f;
constexpr float kLongestSampleSeconds = 0.05f;

}  // namespace

void MotionPointer::reset() noexcept {
    *this = MotionPointer{};
}

void MotionPointer::apply(PlayerInput& input, const MotionRates& rates, std::uint64_t now_us) noexcept {
    const bool two = input.buttons.two;
    input.buttons.two = false;

    if (rates.fresh) {
        float dt = kFirstSampleSeconds;
        if (last_rates_us_ != 0u && now_us > last_rates_us_) {
            dt = std::min(static_cast<float>(now_us - last_rates_us_) * 1e-6f, kLongestSampleSeconds);
        }
        last_rates_us_ = now_us;
        switch_hid::ImuSample sample{};
        sample.accel = to_pad_frame(input.gravity);
        sample.gyro = to_pad_frame(rates.rate);
        switch_hid::MotionSettings settings{};
        settings.shake = false;  // the remote's swings are wiimote::Motion's
        settings.gyro_pointer = true;
        settings.picture_aspect = kAspect;
        const switch_hid::SwitchMotion::Output out = motion_.feed(sample, dt, now_us, settings);
        x_ = std::clamp(x_ + out.pointer_x, -kGyroPointerLimit, kGyroPointerLimit);
        y_ = std::clamp(y_ + out.pointer_y, -kGyroPointerLimit, kGyroPointerLimit);
    }

    // 2: to the middle; held, it stays there; let go, the remote's pose now is the middle's.
    const RecenterHold::Output recenter = recenter_.update(two);
    if (recenter.pressed) started_ = true;
    if (recenter.pinned) {
        x_ = 0.0f;
        y_ = 0.0f;
    }
    const bool hidden = hide_.update(x_, y_, now_us, true, recenter.pressed, true, kDefaultPointerHideSeconds,
                                     kDefaultPointerHideRadius);
    const bool in_picture = std::fabs(x_) <= kPointerVisibleLimit && std::fabs(y_) <= kPointerVisibleLimit;
    input.pointer_on = started_ && !hidden && in_picture && remote_faces_screen(input.gravity);
    input.pointer_x = x_;
    input.pointer_y = -y_;
    // The roll the sensor bar would show, from gravity alone (wiimote_input.cpp sensor_bar_view's level).
    const float roll_length = std::sqrt(input.gravity[2] * input.gravity[2] + input.gravity[0] * input.gravity[0]);
    if (roll_length > 0.0f && roll_length < 2.0f) {
        input.roll_x = input.gravity[2] / roll_length;
        input.roll_y = input.gravity[0] / roll_length;
    } else {
        input.roll_x = 1.0f;
        input.roll_y = 0.0f;
    }
    input.distance = kVirtualPointerDistance;
}

}  // namespace galaxy::input::wiimote
