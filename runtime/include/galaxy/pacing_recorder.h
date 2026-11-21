// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The pacing recorder: always on, on every platform and renderer. It takes the game's steps (the step clock's
// moments), each picture the render core sends to the display (what it shows and when it was asked for) and each
// renderer's report of when a picture reached the display, and every ten seconds of steps writes one [pace] line with
// the measures of galaxy::pacing::Summary (pacing.h):
//
//   [pace] seconds=9.983333 steps=600 steps_per_second=60.0000 pictures=1201 displayed=1199 generated=600
//          refresh_ms=8.333 on_time=1197 missed=0 doubled=0 animation_within_1ms=1.0000 animation_p99_us=85.0
//          delay_avg_us=24310.2 delay_p99_us=25120.0 render_avg_us=2950.1 render_p99_us=3400.0
//          generated_render_avg_us=480.2 generated_render_p99_us=610.0 step_work_avg_us=3100.0
//          step_work_p99_us=4200.0 step_work_max_us=5100.0 cpu_us_per_step=3350.5 cpu_work_us_per_step=2500.0
//          cpu_game_us_per_step=1900.0 cpu_render_us_per_step=600.0
//
// (one line; cpu_us_per_step, the processor time of the whole process in the window per step, is left out where the
// machine gives none: platform_time.h process_cpu_stamp. It holds waiting: a timer's spin, a present that blocks. The cost of
// the game's work is cpu_work_us_per_step, the processor time the frame threads spent between their marks of work and of
// waiting (perf_hint.h), and the same for each of them, cpu_<name>_us_per_step, in the order the threads were added; they are
// left out where the threads were not read). tools/gate/pace.py and tools/measure/measure.py read these
// lines by key.
//
// Who calls what:
//   game thread     step(): each step's moment on the step clock; step_done() when its work is done
//   render thread   walk_began() / walk_ended() around each walk of a list (a picture's render-thread work)
//                   describe() before each present the render core makes, drop_description() after its frame
//   renderer        presented(id) right after its present call, with its own id for the present (0: no report will
//                   come); displayed(id, moment, period) when it learns the moment that present reached the display
//                   (any thread), or not_shown(id) when it learns it never did
// A present the core did not describe (a black picture, the settings overlay's idle presents) is not recorded.

#include <cstddef>
#include <cstdint>
#include <string>

namespace galaxy {
struct ThreadCpuSample;
}

namespace galaxy::pacing {

struct Summary;

// Where the frame threads' work processor times come from (perf_hint.h perf_hint_work_cpu: the host sets it at its start); none: the
// line has no cpu_work fields. Read at the first step and at each window's end.
void set_work_reader(std::size_t (*read)(galaxy::ThreadCpuSample* out, std::size_t capacity)) noexcept;

void step(std::int64_t moment_qpc) noexcept;
// Game thread: the step that began at the last step() is done (the thread is back at the step clock).
void step_done() noexcept;
void walk_began() noexcept;
void walk_ended() noexcept;
// asked: the moment the picture is meant for (its slot, else when the game asked for its present); content: the game
// time it shows on the step clock; step: the moment its step began (0 when unknown). generated: an in-between picture.
void describe(std::int64_t asked_qpc, std::int64_t content_qpc, std::int64_t step_qpc, bool generated) noexcept;
void presented(std::uint64_t id) noexcept;
// Render thread, after the frame a description was for: a description no present took is dropped.
void drop_description() noexcept;
// period_qpc: the display's refresh period then (0: unknown).
void displayed(std::uint64_t id, std::int64_t moment_qpc, std::int64_t period_qpc) noexcept;
void not_shown(std::uint64_t id) noexcept;

// The [pace] line for a summary (no newline), and the windows written so far (tests).
[[nodiscard]] std::string line(const Summary& summary);
[[nodiscard]] std::uint64_t windows_written() noexcept;
// Tests: the recorder as at start, and its output to a function instead of stderr.
void reset_for_test(void (*write)(const std::string& line)) noexcept;

}  // namespace galaxy::pacing
