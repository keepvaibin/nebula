// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
namespace galaxy {
// The latest point on a presentation grid at or before a reference tick. Both clocks are monotonic nanoseconds.
inline std::int64_t presentation_grid_at(std::int64_t reference, std::int64_t presentation, std::int64_t interval) noexcept {
    if (interval <= 0 || presentation <= 0) return reference;
    auto remainder = (reference - presentation) % interval;
    if (remainder < 0) remainder += interval;
    return reference - remainder;
}
}
