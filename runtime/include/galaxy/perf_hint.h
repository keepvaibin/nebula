// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The platform's performance hints for the threads a frame's work runs on (Android's ADPF, API 33+): the game thread and
// the render thread report each frame's work against the 60 fps budget, so the CPU governor raises the clocks before a
// frame runs late instead of after. A frame's work is the busier thread's CPU time over the frame period (they run in
// parallel, pipelined). Hints are a no-op on other platforms and older Android releases.

#include <cstdint>

namespace galaxy::host {

// The calling thread does frame work (call once, from the thread itself).
void perf_hint_add_current_thread() noexcept;
// Once per game frame: the frame's work since the last call, reported to the hints and returned (nanoseconds; 0 where
// threads' CPU times cannot be read).
std::int64_t perf_hint_frame_done() noexcept;
// Windows: the machine runs on its battery, looked at every two seconds from perf_hint_frame_done (a change is logged once).
// Background threads that start on battery ask the OS for efficiency (windows_power.h lower_current_thread_priority); the
// game and render threads never do. False elsewhere.
[[nodiscard]] bool perf_hint_on_battery() noexcept;

}  // namespace galaxy::host
