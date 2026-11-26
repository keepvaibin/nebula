#pragma once
// What the render thread tells the host's frame pacing.
#include <array>
#include <atomic>
#include <cstdint>

namespace galaxy::render {

// When each game step's list was done on the render thread (I-013): its sequence started (in-between pictures on) or
// its picture walked and handed to the display (off), keyed by the step's moment on the step clock (set_step_moment).
// The host's frame phase reads a step's own work from it (native/host/host_main.cpp next_frame_start): the time from
// the step's moment to that entry, whatever the display shows meanwhile. The last real picture's time, which it read
// before, is the arrival delay or nothing when the real picture is shown steps later or never. The render thread
// writes, the game thread reads; each entry is checked again after it is read, so a read crossing a write sees none.
class StepWork {
public:
    static constexpr std::size_t kEntries = 8;

    // Render thread: the step whose moment is `step_qpc` was done at `done_qpc` (qpc_now() ticks).
    void record(std::int64_t step_qpc, std::int64_t done_qpc) noexcept {
        if (step_qpc <= 0) return;
        Entry& entry = entries_[next_++ % kEntries];
        entry.step.store(0, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        entry.done.store(done_qpc, std::memory_order_relaxed);
        entry.step.store(step_qpc, std::memory_order_release);
    }

    // Any thread: the ticks from the step's moment to when it was done, 0 when it is not done yet or not kept.
    [[nodiscard]] std::int64_t work(std::int64_t step_qpc) const noexcept {
        if (step_qpc <= 0) return 0;
        for (const Entry& entry : entries_) {
            if (entry.step.load(std::memory_order_acquire) != step_qpc) continue;
            const std::int64_t done = entry.done.load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (entry.step.load(std::memory_order_relaxed) != step_qpc) return 0;
            return done > step_qpc ? done - step_qpc : 0;
        }
        return 0;
    }

private:
    struct Entry {
        std::atomic<std::int64_t> step{0};
        std::atomic<std::int64_t> done{0};
    };
    std::array<Entry, kEntries> entries_{};
    std::uint64_t next_ = 0;  // render thread only
};
inline StepWork g_step_work;
// In-between pictures on (RenderCore, per list): the game's step keeps exactly 60 (galaxy/step_lock.h).
inline std::atomic<bool> g_in_between_on{false};
// A closed wipe covers the screen (the game's NEBULA_CONTEXT_SCREEN_COVERED, each step): 1 covered, 0 in play, -1 not
// told (a test, a game that never tells). The display lead steps down only while covered or not told: in play a step
// down moves every picture's moment at once, where the eye follows motion (brief AD).
inline std::atomic<int> g_screen_covered{-1};
// The display varies its refresh with the presents (render/variable_refresh.h: the system's answer, else the presents'
// proof): any frame rate shows evenly, and the game's step is never locked to the display (galaxy/step_lock.h).
inline std::atomic<bool> g_variable_refresh{false};
// The picture interval (qpc ticks): FrameSchedule::slot_period() as the display model leaves it, written by the render
// thread's schedule each time it sets it: the vblank grid's slot length (every n-th vblank), or one over the pacer's
// rate where its own clock runs (a swap chain that tears, a variable refresh at the cap); 0 with no slot clock (no
// in-between pictures, an unlimited rate). Any thread reads it (G8.2, through one host call).
inline std::atomic<std::int64_t> g_slot_period_qpc{0};
// Dynamic resolution's budget for a picture's GPU time (ms), the picture arbiter's (FrameSchedule, G2.5): the slot in
// use times its pressure; 0 where it gives none (in-between pictures off, the old path), and the present rate's period
// is the budget (dynamic_resolution.h). Written by the render thread's schedule, read by the renderer's scene.
inline std::atomic<double> g_resolution_budget_ms{0.0};
// Dynamic resolution as the renderer last drew, for the arbiter: the scene's share and its floor in sixteenths of a
// side (both 16 while it is off).
inline std::atomic<unsigned> g_resolution_sixteenths{16u};
inline std::atomic<unsigned> g_resolution_floor{16u};
// The pictures missed their slots for a sustained time with the resolution at its floor (FrameSchedule::target_missed):
// the settings' TARGET FRAME RATE row says what to change.
inline std::atomic<bool> g_target_missed{false};
// A picture's GPU time (ms) as the renderer estimates a generated one, for the performance hint's report (perf_hint.cpp,
// Android's newest call takes the GPU time beside the CPU's); 0 not known.
inline std::atomic<double> g_picture_gpu_ms{0.0};
// The window's way to the display changed (full screen entered or left, focus lost or gained, another display, the
// display's mode): the renderer asks again whether the display is variable (I-007, point 5). Set by the hosts.
inline std::atomic<bool> g_display_recheck{false};
// The presents' proof holds presents at this rate (hertz; 0: no proof running, render/variable_refresh.h RefreshQuestion):
// the slots follow the pacer's clock at it, as under variable refresh at the cap.
inline std::atomic<double> g_refresh_probe_hz{0.0};
}
