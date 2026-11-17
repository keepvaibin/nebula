// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The platform's performance hints for the threads a step's work runs on (Android's ADPF, API 33+): the game thread and
// the render thread report each step's work against the game step's period (game_step.h), so the CPU governor raises the
// clocks before a step runs late instead of after. A step's work is the busier thread's CPU time (they run in parallel,
// pipelined). Hints are a no-op on other platforms and older Android releases.

#include <cstddef>
#include <cstdint>

namespace galaxy::host {

// The calling thread does frame work (call once, from the thread itself). name says which in the pacing line ("game",
// "render"); it must outlive the process's threads (a string literal).
void perf_hint_add_current_thread(const char* name) noexcept;
// The frame's target duration in nanoseconds, as the frame clock keeps it (the game step, the shorter boot frame): the hint
// session's target follows it, through APerformanceHint_updateTargetWorkDuration where the system has it, else a new session.
// Any thread; a no-op where there are no hints.
void perf_hint_set_target(std::int64_t target_ns) noexcept;
// Once per game step: the step's work since the last call, reported to the hints and returned (nanoseconds; 0 where
// threads' CPU times cannot be read).
std::int64_t perf_hint_frame_done() noexcept;
// One frame thread's CPU time in the window the last perf_hint_frame_done closed.
struct ThreadWork {
    const char* name;
    std::int64_t cpu_ns;  // -1: its time could not be read
};
// The frame threads' work in that window, in the order they were added, at most capacity of them. Returns how many it
// wrote; 0 where threads' CPU times cannot be read (Windows). Any thread.
std::size_t perf_hint_last_work(ThreadWork* out, std::size_t capacity) noexcept;
// Windows: the machine runs on its battery, looked at every two seconds from perf_hint_frame_done (a change is logged once).
// Background threads that start on battery ask the OS for efficiency (windows_power.h lower_current_thread_priority); the
// game and render threads never do. False elsewhere.
[[nodiscard]] bool perf_hint_on_battery() noexcept;

}  // namespace galaxy::host
