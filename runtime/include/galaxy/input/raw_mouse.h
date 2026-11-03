// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <limits>
#include <mutex>

namespace galaxy::input {
struct RawMouseMotion {
    std::int64_t x = 0, y = 0;
    bool moved = false;
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
