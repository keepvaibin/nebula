// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The platform's performance hints for the threads a step's work runs on (Android's ADPF, API 33+): the game thread and
// the render thread report each step's work against the game step's period (game_step.h), so the CPU governor raises the
// clocks before a step runs late instead of after. A step's work is the busier thread's CPU time (they run in parallel,
// pipelined). Hints are a no-op on other platforms and older Android releases.

#include <cstdint>

namespace galaxy::host {

// The calling thread does frame work (call once, from the thread itself).
void perf_hint_add_current_thread() noexcept;
// Once per game step: the step's work since the last call, reported to the hints and returned (nanoseconds; 0 where
// threads' CPU times cannot be read).
std::int64_t perf_hint_frame_done() noexcept;

}  // namespace galaxy::host
