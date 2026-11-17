// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The benchmark's cover (benchmark.h LiveState::cover) for the sound: 0 clear to 255 black, set by the run on the game's
// thread each frame, 0 when no run is on. The game's sound fades with it (native_audio.cpp), so what the cover hides is
// not heard either: the title the run starts from, the travels between stages, the settings changing.

#include <atomic>
#include <cstdint>

namespace galaxy::bench {

inline std::atomic<std::uint32_t> g_cover_level{0u};

// The share of the game's sound the cover lets through, 1 clear to 0 black.
[[nodiscard]] inline float cover_audible() noexcept {
    return 1.0f - static_cast<float>(g_cover_level.load(std::memory_order_relaxed)) / 255.0f;
}

}  // namespace galaxy::bench
