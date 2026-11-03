#pragma once
// When the last picture went to the display (steady clock), for the host's frame pacing.
#include <atomic>
#include <cstdint>

namespace galaxy::render {
inline std::atomic<std::int64_t> g_last_present_ns{0};
}
