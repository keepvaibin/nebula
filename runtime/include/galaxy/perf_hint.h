// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The platform's performance hints for the threads a step's work runs on (Android's ADPF, API 33+): the game thread and
// the render thread report each step's work against the game step's period (game_step.h), so the CPU governor raises the
// clocks before a step runs late instead of after. A step's work is the wall time the busier thread worked in it, from
// its marks of work and of waiting for the next (perf_hint_busy; they run in parallel, pipelined): the session is told
// how long the work took, as the API asks, so time blocked inside it (a lock, a GPU fence) counts and the time spent
// waiting for the next step or slot does not (P-069). Hints are a no-op on other platforms and older Android releases.

#include "galaxy/platform_time.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace galaxy::host {

// One thread's busy wall time, by windows: from a mark of work to a mark of waiting.
class BusyWindow {
public:
    // The thread starts work (`on`) or waits for the next. A mark that changes nothing is ignored.
    void mark(std::int64_t now_ns, bool on) noexcept {
        if (on == (since_ns_ >= 0)) return;
        if (on) {
            since_ns_ = now_ns;
        } else {
            busy_ns_ += std::max<std::int64_t>(now_ns - since_ns_, 0);
            since_ns_ = -1;
        }
    }
    // Ends the window at `now_ns`: its busy time, work still going counted to now and carried into the next window.
    [[nodiscard]] std::int64_t close(std::int64_t now_ns) noexcept {
        std::int64_t out = busy_ns_;
        if (since_ns_ >= 0) {
            out += std::max<std::int64_t>(now_ns - since_ns_, 0);
            since_ns_ = now_ns;
        }
        busy_ns_ = 0;
        return out;
    }

private:
    std::int64_t since_ns_ = -1;  // when the work going now began; -1: waiting
    std::int64_t busy_ns_ = 0;
};

// One thread's processor time while it works: the span of its own processor clock from a mark of work to the next mark of
// waiting, added up (units of ProcessCpuStamp::cycles). Waiting, a timer's spin and a blocked call are not in it. One thread
// marks; any thread reads the total.
class WorkCpu {
public:
    // `now` is the thread's processor clock at the mark. A mark that changes nothing is ignored.
    void mark(std::uint64_t now, bool on) noexcept {
        if (on == working_) return;
        if (on) {
            since_ = now;
        } else {
            total_.store(total_.load(std::memory_order_relaxed) + (now > since_ ? now - since_ : 0), std::memory_order_relaxed);
        }
        working_ = on;
    }
    [[nodiscard]] std::uint64_t total() const noexcept { return total_.load(std::memory_order_relaxed); }

private:
    std::atomic<std::uint64_t> total_{0};
    std::uint64_t since_ = 0;
    bool working_ = false;
};

// The calling thread does frame work (call once, from the thread itself). name says which in the pacing line ("game",
// "render"); it must outlive the process's threads (a string literal).
void perf_hint_add_current_thread(const char* name) noexcept;
// The frame's target duration in nanoseconds, as the frame clock keeps it (the game step, the shorter boot frame): the hint
// session's target follows it, through APerformanceHint_updateTargetWorkDuration where the system has it, else a new session.
// Any thread; a no-op where there are no hints.
void perf_hint_set_target(std::int64_t target_ns) noexcept;
// The calling frame thread starts work (true) or waits for the next step, slot or list (false). Any frame thread, at
// each change; a no-op on a thread that is not one, and where there are no hints.
void perf_hint_busy(bool on) noexcept;
// Once per game step: the step's work since the last call (the busier frame thread's busy wall time), reported to the
// hints and returned (nanoseconds; 0 where there are no hints). The calling thread's work starts here.
std::int64_t perf_hint_frame_done() noexcept;
// One frame thread's CPU time in the window the last perf_hint_frame_done closed.
struct ThreadWork {
    const char* name;
    std::int64_t cpu_ns;  // -1: its time could not be read
};
// The frame threads' work in that window, in the order they were added, at most capacity of them. Returns how many it
// wrote; 0 where threads' CPU times cannot be read (Windows). Any thread.
std::size_t perf_hint_last_work(ThreadWork* out, std::size_t capacity) noexcept;
// Each frame thread's processor time spent working (WorkCpu) since the thread was added, in the order they were added, at most
// capacity of them; a span of work still going is not in it. Returns how many it wrote; 0 where processor times are not known
// (platform_time.h process_cpu_known). Any thread, on every platform. The marks are the ones above: the game thread's work runs
// from perf_hint_frame_done to its next perf_hint_busy(false), the render thread's from render_work_begins to render_work_ends.
std::size_t perf_hint_work_cpu(galaxy::ThreadCpuSample* out, std::size_t capacity) noexcept;
// Windows: the machine runs on its battery, looked at every two seconds from perf_hint_frame_done (a change is logged once).
// Background threads that start on battery ask the OS for efficiency (windows_power.h lower_current_thread_priority); the
// game and render threads never do. False elsewhere.
[[nodiscard]] bool perf_hint_on_battery() noexcept;

}  // namespace galaxy::host
