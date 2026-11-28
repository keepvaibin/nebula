// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The platform's performance hints for the threads a frame's work runs on (Android's ADPF, API 33+): one session holds
// every frame thread (the game thread, the render thread, each later one as it is added) and is told each cycle's work
// against its target (HintSession, G10.4), so the CPU governor raises the clocks before a picture is late instead of after.
// A cycle's work is the wall time the busier thread worked in it, from its marks of work and of waiting for the next
// (perf_hint_busy; they run in parallel, pipelined): the session is told how long the work took, as the API asks, so time
// blocked inside it (a lock, a GPU fence) counts and the time spent waiting for the next step or slot does not (P-069).
// Hints are a no-op on other platforms and older Android releases.

#include "galaxy/platform_time.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

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

// The hint's cycle (FRAME-MODEL 2.16, G10.4): the frame's target (the game step, the shorter boot frame) or, where
// in-between pictures put a shorter slot under it, a picture's slot (present_clock.h g_slot_period_qpc; 0: no slot clock).
[[nodiscard]] constexpr std::int64_t hint_cycle_ns(std::int64_t frame_ns, std::int64_t slot_ns) noexcept {
    return slot_ns > 0 && slot_ns < frame_ns ? slot_ns : frame_ns;
}
// The session's target: `share_percent` of the cycle (--diagnostics hint-share-N, 10 to 100; 100, the whole cycle, is what G10.4m's
// A/B on the tablet kept: no share beat it by more than the range of the runs). The system steers the work toward the target, so
// a share under 100 would keep the clocks high enough that the work ends that much before the slot does.
[[nodiscard]] constexpr std::int64_t hint_target_ns(std::int64_t cycle_ns, unsigned share_percent) noexcept {
    return cycle_ns * static_cast<std::int64_t>(share_percent) / 100;
}
// The calls of the platform's hint session (android/performance_hint.h), as the system has them: looked up by name on
// Android (perf_hint.cpp), a test's own elsewhere. A null call is one the system lacks; a session is made only where
// create_session, report and close_session are all there.
struct HintCalls {
    void* manager = nullptr;  // APerformanceHint_getManager's
    void* (*create_session)(void* manager, const std::int32_t* tids, std::size_t size, std::int64_t target_ns) = nullptr;
    int (*report)(void* session, std::int64_t actual_ns) = nullptr;
    void (*close_session)(void* session) = nullptr;
    int (*update_target)(void* session, std::int64_t target_ns) = nullptr;
    int (*set_threads)(void* session, const std::int32_t* tids, std::size_t size) = nullptr;  // API 34
    // API 35. Never called: the session never asks the system to prefer power efficiency over performance (brief AB S3).
    // It is looked up so the tests hold that nothing calls it.
    int (*set_prefer_power_efficiency)(void* session, bool enabled) = nullptr;
    // reportActualWorkDuration2 and its AWorkDuration (API 35): a cycle's wall, CPU and GPU times in one report.
    void* (*duration_create)() = nullptr;
    void (*duration_release)(void* duration) = nullptr;
    void (*duration_start)(void* duration, std::int64_t start_ns) = nullptr;
    void (*duration_total)(void* duration, std::int64_t total_ns) = nullptr;
    void (*duration_cpu)(void* duration, std::int64_t cpu_ns) = nullptr;
    void (*duration_gpu)(void* duration, std::int64_t gpu_ns) = nullptr;
    int (*report2)(void* session, void* duration) = nullptr;
    int (*notify_spike)(void* session, bool cpu, bool gpu, const char* identifier) = nullptr;  // API 36
};

// The frame threads' hint session (G10.4, FRAME-MODEL 2.16): the threads it holds, its target, and when it is told a
// cycle's work. The step's end closes a cycle; where the cycle is a picture's slot, the first frame thread's mark after
// each further slot's end inside the step closes one too (cycle_due), so the session hears once per cycle (two times a
// step at 120 Hz), not once per step. Pure: the calls are HintCalls, the times the caller's steady clock; one thread at a
// time (perf_hint.cpp holds its lock).
class HintSession {
public:
    struct Options {
        unsigned share_percent = 100;  // the target's share of the cycle (--diagnostics hint-share-N)
    };
    HintSession(const HintCalls& calls, Options options, std::int64_t frame_ns) noexcept;
    ~HintSession();
    HintSession(const HintSession&) = delete;
    HintSession& operator=(const HintSession&) = delete;

    // A frame thread joins: the session holds it from the next report on (setThreads where the system has it, else a
    // session made again with every thread).
    void add_thread(std::int32_t tid);
    // The frame's target (perf_hint_set_target): the cycle and the target follow it at the next step's end.
    void set_frame(std::int64_t frame_ns) noexcept { frame_ns_ = frame_ns; }
    // The step ends at now_ns (perf_hint_frame_done): cycle_work_ns is the busier thread's work over the cycle the step's end
    // closes; slot_ns a picture's slot (0: none); gpu_ns a picture's GPU
    // time (0: not known).
    void step_ends(std::int64_t now_ns, std::int64_t cycle_work_ns, std::int64_t slot_ns,
                   std::int64_t gpu_ns);
    // Whether a frame thread's mark at now_ns closes a cycle inside the step; then cycle_ends with that cycle's work.
    [[nodiscard]] bool cycle_due(std::int64_t now_ns) const noexcept { return left_ > 0 && now_ns >= due_ns_; }
    void cycle_ends(std::int64_t now_ns, std::int64_t work_ns, std::int64_t gpu_ns);
    // A stage load or a scene start comes (notifyWorkloadSpike, API 36): its cycles are not the workload's measure.
    void spike(const char* what) noexcept;

    [[nodiscard]] std::int64_t cycle_ns() const noexcept { return cycle_ns_; }
    [[nodiscard]] std::int64_t target_ns() const noexcept { return target_ns_; }
    [[nodiscard]] bool made() const noexcept { return session_ != nullptr; }

private:
    void hold(std::int64_t cycle_ns);
    void make();
    void tell(std::int64_t now_ns, std::int64_t work_ns, std::int64_t gpu_ns);

    HintCalls calls_;
    Options options_;
    std::vector<std::int32_t> tids_;
    void* session_ = nullptr;
    bool stale_ = true;      // made again, with every thread and the target, before the next report
    std::int64_t frame_ns_;
    std::int64_t cycle_ns_ = 0;
    std::int64_t target_ns_ = 0;
    std::int64_t due_ns_ = 0;  // the end of the step's next slot
    int left_ = 0;             // the step's slots still to close by a mark
};

// The calling thread does frame work (call once, from the thread itself). name says which in the pacing line ("game",
// "render"); it must outlive the process's threads (a string literal).
void perf_hint_add_current_thread(const char* name) noexcept;
// The frame's target duration in nanoseconds, as the frame clock keeps it (the game step, the shorter boot frame): the hint
// session's target follows it, through APerformanceHint_updateTargetWorkDuration where the system has it, else a new session.
// Any thread; a no-op where there are no hints.
void perf_hint_set_target(std::int64_t target_ns) noexcept;
// The calling frame thread starts work (true) or waits for the next step, slot or list (false). Any frame thread, at
// each change; a no-op on a thread that is not one, and where there are no hints. A mark after a slot's end inside the
// step tells the session that cycle's work (HintSession::cycle_due).
void perf_hint_busy(bool on) noexcept;
// Once per game step: the step's work since the last call (the busier frame thread's busy wall time), returned
// (nanoseconds; 0 where there are no hints); the session is told the cycle the step's end closes. The calling thread's
// work starts here.
std::int64_t perf_hint_frame_done() noexcept;
// A stage load (the game names its next 3D scene) or a scene start comes: the session is told a workload spike, so its
// cycles do not count as the workload's measure (notifyWorkloadSpike, API 36). The game thread; a no-op where the system
// lacks the call, and where there are no hints.
void perf_hint_workload_spike(const char* what) noexcept;
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
// The device's thermal headroom for the picture arbiter (P-019, FRAME-MODEL 2.16 target 5): Android's
// AThermal_getThermalHeadroom forecast a second on, read once a second off the frame threads (1 is where the device
// throttles); 0 where it is not known (before the first read, Android before 11, every other platform). Any thread.
[[nodiscard]] double thermal_headroom() noexcept;
// What the platform offers the frame's pacing, logged (G10.3): `[platform] hint-features:` (APerformanceHint_isFeatureSupported
// for each feature the NDK lists), `[platform] hint-calls:` (which of the newer hint and thermal calls the system has) and
// `[platform] thermal-thresholds:` (AThermal_getThermalHeadroomThresholds), each with its value or "not supported (<why>)",
// written by the heat reader's thread within a second, never by a frame thread. Asked at start and on each display change
// (display_clock.cpp); changes nothing the game does. Any thread; a no-op where there are no hints (every platform but Android).
void perf_hint_report_platform() noexcept;

}  // namespace galaxy::host
