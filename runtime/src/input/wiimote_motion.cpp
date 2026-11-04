// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/input/wiimote_motion.h"

#include <algorithm>
#include <cmath>

namespace galaxy::input::wiimote {

namespace {
// The most the remote's and the Nunchuk's sensors read, in g, and how far each report moves the eased tilt.
constexpr float kMainLimit = 3.4f;
constexpr float kSubLimit = 2.1f;
constexpr float kFollow = 0.15f;
constexpr int kHistory = 128;
// A swing: this far from where the hand was 20 frames ago. The Nunchuk's takes more.
constexpr float kMainSwing = 1.0f;
constexpr float kSubSwing = 2.0f;
constexpr int kSwingBack = 20;
constexpr int kUnlatchFrames = 6;
// A hand turning in circles: the rise over the last 15 frames, and how long it keeps counting as one.
constexpr float kCentrifugal = 0.4f;
constexpr int kCentrifugalBack = 15;
constexpr int kCentrifugalRelease = 8;
constexpr int kPulseAfter = 30;
constexpr int kPulseEvery = 15;
// The camera loses the bar for a moment now and then: reports before the pointer comes on, and after it stays.
constexpr int kValidBeforeOn = 5;
constexpr int kInvalidBeforeOff = 10;

float clamped(float value, float limit) {
    return std::clamp(value, -limit, limit);
}
}  // namespace

Motion::Motion() {
    main_.swing_distance = kMainSwing;
    sub_.swing_distance = kSubSwing;
}

void Motion::reset() {
    const std::lock_guard<std::mutex> lock(lock_);
    const float main_distance = main_.swing_distance;
    const float sub_distance = sub_.swing_distance;
    main_ = Hand{};
    sub_ = Hand{};
    main_.swing_distance = main_distance;
    sub_.swing_distance = sub_distance;
    in_screen_ = false;
    pointer_passed_ = false;
    valid_samples_ = invalid_samples_ = 0;
}

void Motion::push(Hand& hand, const std::array<float, 3>& value) {
    hand.newest = (hand.newest + 1) % kHistory;
    hand.history[static_cast<std::size_t>(hand.newest)] = value;
    hand.count = std::min(hand.count + 1, kHistory);
}

bool Motion::past(const Hand& hand, int index, std::array<float, 3>& out) const {
    if (index >= hand.count) {
        return false;
    }
    out = hand.history[static_cast<std::size_t>((hand.newest - index + kHistory) % kHistory)];
    return true;
}

void Motion::add(const PlayerInput& report) {
    const std::lock_guard<std::mutex> lock(lock_);
    newest_ = report;
    for (std::size_t i = 0; i < 3; ++i) {
        main_.eased[i] += kFollow * (clamped(report.gravity[i], kMainLimit) - main_.eased[i]);
        sub_.eased[i] += kFollow * (clamped(report.nunchuk_gravity[i], kSubLimit) - sub_.eased[i]);
    }
    push(main_, main_.eased);
    main_.arrived = true;
    push(sub_, sub_.eased);
    sub_.arrived = true;

    if (!report.pointer_on) {
        valid_samples_ = 0;
        ++invalid_samples_;
        return;
    }
    if ((!in_screen_ && valid_samples_ >= kValidBeforeOn) || (in_screen_ && invalid_samples_ <= kInvalidBeforeOff)) {
        pointer_x_ = report.pointer_x;
        pointer_y_ = report.pointer_y;
        roll_x_ = report.roll_x;
        roll_y_ = report.roll_y;
        pointer_passed_ = true;
    }
    invalid_samples_ = 0;
    ++valid_samples_;
}

// A frame's look at a hand: whether a spin starts now.
bool Motion::update(Hand& hand) {
    if (!hand.arrived) {
        const std::array<float, 3> previous =
            hand.newest >= 0 ? hand.history[static_cast<std::size_t>(hand.newest)] : std::array<float, 3>{};
        push(hand, previous);
    }
    hand.arrived = false;

    std::array<float, 3> now{};
    std::array<float, 3> then{};
    const bool known = past(hand, 0, now) && past(hand, kSwingBack, then);
    bool swing = false;
    if (known) {
        const float dx = now[0] - then[0];
        const float dy = now[1] - then[1];
        const float dz = now[2] - then[2];
        swing = std::sqrt(dx * dx + dy * dy + dz * dz) >= hand.swing_distance;
    }
    hand.below_frames = swing ? 0 : hand.below_frames + 1;
    if (hand.latched) {
        if (hand.below_frames > kUnlatchFrames) {
            hand.latched = false;
        }
    } else if (swing) {
        hand.latched = true;
    }
    hand.latched_frames = hand.latched ? hand.latched_frames + 1 : 0;

    if (!known) {
        hand.centrifugal = false;
        hand.held_frames = 0;
        hand.cooldown_frames = 0;
    }
    float rise = 0.0f;
    float peak = 0.0f;
    if (hand.count >= kCentrifugalBack) {
        for (int i = 1; i < kCentrifugalBack; ++i) {
            std::array<float, 3> newer{};
            std::array<float, 3> older{};
            (void)past(hand, i - 1, newer);
            (void)past(hand, i, older);
            rise += newer[1] - older[1];
            peak = std::max(peak, rise);
        }
    }
    const bool detected = peak > kCentrifugal;
    hand.cooldown_frames = detected ? 0 : hand.cooldown_frames + 1;
    if (hand.centrifugal) {
        if (hand.cooldown_frames > kCentrifugalRelease) {
            hand.centrifugal = false;
        }
    } else if (detected) {
        hand.centrifugal = true;
    }
    hand.held_frames = hand.centrifugal ? hand.held_frames + 1 : 0;

    if (hand.latched_frames == 1) {
        return true;
    }
    return hand.held_frames >= kPulseAfter && (hand.held_frames - kPulseAfter) % kPulseEvery == 0 &&
           hand.cooldown_frames < kPulseEvery;
}

PlayerInput Motion::read(MotionFrame& frame) {
    const std::lock_guard<std::mutex> lock(lock_);
    frame.spin_presses = update(main_) ? 1u : 0u;
    frame.sub_spin_presses = update(sub_) ? 1u : 0u;
    frame.tilt = main_.eased;
    frame.sub_tilt = sub_.eased;

    in_screen_ = pointer_passed_;
    pointer_passed_ = false;
    PlayerInput out = newest_;
    out.pointer_on = in_screen_;
    out.pointer_x = pointer_x_;
    out.pointer_y = pointer_y_;
    out.roll_x = roll_x_;
    out.roll_y = roll_y_;
    return out;
}

}  // namespace galaxy::input::wiimote
