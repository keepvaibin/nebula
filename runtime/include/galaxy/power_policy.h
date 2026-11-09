// SPDX-License-Identifier: GPL-3.0-only
// What the OS is told about the player's presence: when the process asks for efficiency (EcoQoS) and when it holds the display on.
// Pure decisions from times and window states; the OS calls are in windows_power.h and the host's main loop applies them.
#pragma once

#include <cstdint>

namespace galaxy::host {

// A pad or key used within the playing window, or a cutscene, keeps the display on. Milliseconds on one monotonic clock.
inline constexpr std::int64_t kPowerPlayingMs = 180'000;
inline constexpr std::int64_t kPowerMovieMs = 2'000;

struct PowerFacts {
    std::int64_t now_ms = 0;
    std::int64_t last_input_ms = 0;   // the latest pad, key, stick or pointer movement the game was given
    std::int64_t last_movie_ms = -1;  // the latest movie sound the game played (a cutscene); negative: none yet
    bool background = false;          // the window is not in front
    bool minimised = false;
};

struct PowerWish {
    bool efficient = false;         // the process asks for efficiency cores and lower clocks
    bool display_required = false;  // the display stays on (and the system awake)
    friend bool operator==(const PowerWish&, const PowerWish&) = default;
};

[[nodiscard]] constexpr PowerWish power_wish(const PowerFacts& facts) noexcept {
    const bool movie = facts.last_movie_ms >= 0 && facts.now_ms - facts.last_movie_ms < kPowerMovieMs;
    const bool present = !facts.background && !facts.minimised;
    PowerWish wish;
    // Only away (behind another window or minimised). A player in front with no input may be watching (an in-game demo, the
    // scenery): efficiency there could cost frames on a slow machine, and the game never runs slower than it can.
    wish.efficient = !present;
    // Only in front, and only while someone plays or watches.
    wish.display_required = present && (movie || facts.now_ms - facts.last_input_ms < kPowerPlayingMs);
    return wish;
}

}  // namespace galaxy::host
