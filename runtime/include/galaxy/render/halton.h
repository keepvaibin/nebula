// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The Halton sequence the temporal upscalers jitter by (bases 2 and 3). Every caller takes the value for index % phases + 1,
// centred by subtracting 0.5.

namespace galaxy::render {

inline float halton(unsigned index, unsigned base) {
    float result = 0.0f;
    float fraction = 1.0f / static_cast<float>(base);
    while (index > 0u) {
        result += fraction * static_cast<float>(index % base);
        index /= base;
        fraction /= static_cast<float>(base);
    }
    return result;
}

}  // namespace galaxy::render
