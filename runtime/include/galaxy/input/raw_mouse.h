// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <limits>
#include <mutex>

namespace galaxy::input {
struct RawMouseMotion {
    std::int64_t x = 0, y = 0;
    bool moved = false;
    // x and y are a position, 0..65535 across the screen (or the virtual desktop), not a motion: Remote Desktop,
    // virtual machines, pen tablets.
    bool absolute = false;
};

// Motion from a device that reports positions (P-076): the difference of successive positions, in pixels, which is
// what a relative mouse's counts are at the system's default speed. The first position after a reset gives none.
class AbsoluteMouseTracker {
public:
    // `position` 0..65535 over a `span` pixels wide (and as high, for y).
    [[nodiscard]] RawMouseMotion step(std::int64_t x, std::int64_t y, std::int64_t span_x, std::int64_t span_y) noexcept {
        const std::int64_t px = pixels(x, span_x);
        const std::int64_t py = pixels(y, span_y);
        RawMouseMotion out{};
        if (seen_) {
            out.x = px - last_x_;
            out.y = py - last_y_;
            out.moved = out.x != 0 || out.y != 0;
        }
        seen_ = true;
        last_x_ = px;
        last_y_ = py;
        return out;
    }
    void reset() noexcept { seen_ = false; }

private:
    static constexpr std::int64_t pixels(std::int64_t position, std::int64_t span) noexcept {
        return (position < 0 ? 0 : position > 65535 ? 65535 : position) * (span > 0 ? span : 1) / 65536;
    }
    bool seen_ = false;
    std::int64_t last_x_ = 0, last_y_ = 0;
};

// Relative device motion is activity, never an absolute pointer position.
class RawMouseAccumulator {
public:
    void relative(std::int32_t x, std::int32_t y) noexcept {
        const std::lock_guard lock(mutex_);
        motion_.x = add(motion_.x, x);
        motion_.y = add(motion_.y, y);
        motion_.moved = motion_.moved || x != 0 || y != 0;
    }
    RawMouseMotion sample() noexcept {
        const std::lock_guard lock(mutex_);
        const auto result = motion_;
        motion_ = {};
        return result;
    }
    void clear() noexcept { (void)sample(); }
    static constexpr std::int64_t add(std::int64_t a, std::int32_t b) noexcept {
        if (b > 0 && a > std::numeric_limits<std::int64_t>::max() - b)
            return std::numeric_limits<std::int64_t>::max();
        if (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b)
            return std::numeric_limits<std::int64_t>::min();
        return a + b;
    }
private:
    std::mutex mutex_;
    RawMouseMotion motion_{};
};
}  // namespace galaxy::input
