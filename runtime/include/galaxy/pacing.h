// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The pacing measures of the plan (docs/plan/00-READ-FIRST.md 4.3), computed by the engine from what it knows of each
// game step and each shown picture, on every platform and renderer. Times are on the runtime's clock (platform_time.h,
// qpc ticks). A Window takes a stretch of steps and pictures and gives a Summary; the recorder (pacing_recorder.h)
// feeds it from the game and render threads and prints and writes the summaries.

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace galaxy::pacing {

// One picture the renderer presented.
struct Picture {
    std::int64_t asked_qpc = 0;      // the moment it was meant to reach the display (its slot, or its step's moment)
    std::int64_t content_qpc = 0;    // the game time it shows, on the step clock: its step's moment + t steps
    std::int64_t step_qpc = 0;       // the moment the step it shows began (the delay's start)
    std::int64_t displayed_qpc = 0;  // the moment it reached the display; 0 when the renderer cannot tell, -1 never
    std::int64_t render_ns = 0;      // the render thread's time on it
    bool generated = false;          // an in-between picture (interpolated or generated)
};

// One frame thread's processor time spent working in a window, in nanoseconds (perf_hint.h: from its mark of work to its mark of
// waiting, so a timer's spin and a blocked call are not in it).
struct ThreadWorkNs {
    std::string name;
    std::int64_t ns = 0;
};

struct Summary {
    double seconds = 0.0;                // the window's wall time, from its first step to its last
    std::uint64_t steps = 0;
    double steps_per_second = 0.0;
    std::uint64_t pictures = 0;
    std::uint64_t displayed = 0;         // pictures whose display moment is known
    // Animation error: each displayed picture's (display moment - content moment) against the window's median of it
    // (the pipeline's fixed delay), in microseconds.
    double animation_within_1ms = 0.0;   // the share of displayed pictures within 1 ms
    double animation_p99_us = 0.0;       // the 99th percentile of the error's size
    // Refreshes, from the display moments of consecutive pictures against the refresh period: a missed refresh is one
    // a picture stayed on beyond the refreshes asked for it; a doubled picture is one that reached the display in the
    // same refresh as the one before, or that the renderer knows never reached it (either way it was never seen).
    double refresh_ms = 0.0;
    std::uint64_t missed_refreshes = 0;
    std::uint64_t doubled_pictures = 0;
    std::uint64_t on_time = 0;           // displayed pictures exactly the asked number of refreshes after the one before
    // Delay from the start of the step a picture shows to its display moment, in microseconds.
    double delay_avg_us = 0.0;
    double delay_p99_us = 0.0;
    // The render thread's time per picture, in microseconds: real pictures and in-between ones apart.
    double render_avg_us = 0.0;
    double render_p99_us = 0.0;
    double generated_render_avg_us = 0.0;
    double generated_render_p99_us = 0.0;
    std::uint64_t generated = 0;
    // Each step's own work: from its moment to the game thread's return to the step clock, in microseconds.
    double step_work_avg_us = 0.0;
    double step_work_p99_us = 0.0;
    double step_work_max_us = 0.0;
    // The processor time the whole process used in the window (all threads) per step, in microseconds: set only when
    // the window's processor time is known (Window::set_cpu_ns), else cpu_known is false and the line leaves it out.
    bool cpu_known = false;
    double cpu_us_per_step = 0.0;
    // The frame threads' work processor time per step, all together and by thread, in microseconds: set only when the window's
    // threads were read (Window::set_thread_work), else work_known is false and the line leaves them out.
    bool work_known = false;
    double cpu_work_us_per_step = 0.0;
    struct ThreadUs {
        std::string name;
        double us_per_step = 0.0;
    };
    std::vector<ThreadUs> thread_cpu;
};

class Window {
public:
    void add_step(std::int64_t start_qpc);
    void add_step_work(std::int64_t work_ns);
    void add_picture(const Picture& picture);
    // The processor time the process used over the window, in nanoseconds (-1: not known). Cleared with the window.
    void set_cpu_ns(std::int64_t cpu_ns) { cpu_ns_ = cpu_ns; }
    // The frame threads' work processor time over the window (empty: not known). Cleared with the window.
    void set_thread_work(std::vector<ThreadWorkNs> threads) { threads_ = std::move(threads); }
    // refresh_qpc: the display's refresh period in ticks (0: unknown, then no refresh counts). frequency: ticks a second.
    [[nodiscard]] Summary summarize(std::int64_t refresh_qpc, std::int64_t frequency) const;
    void clear();
    [[nodiscard]] std::size_t steps() const noexcept { return steps_.size(); }
    [[nodiscard]] std::size_t pictures() const noexcept { return pictures_.size(); }

private:
    std::vector<std::int64_t> steps_;
    std::vector<double> step_work_us_;
    std::vector<Picture> pictures_;
    std::int64_t cpu_ns_ = -1;
    std::vector<ThreadWorkNs> threads_;
};

}  // namespace galaxy::pacing
