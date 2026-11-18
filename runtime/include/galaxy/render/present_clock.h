#pragma once
// When the last picture went to the display (steady clock), for the host's frame pacing.
#include <atomic>
#include <cstdint>

namespace galaxy::render {
inline std::atomic<std::int64_t> g_last_present_ns{0};
// In-between pictures on (RenderCore, per list): the game's step keeps exactly 60 (galaxy/step_lock.h).
inline std::atomic<bool> g_in_between_on{false};
// The display varies its refresh with the presents (the renderer's own detection): any frame rate shows evenly.
inline std::atomic<bool> g_variable_refresh{false};
}
