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
// The display varies its refresh with the presents (render/variable_refresh.h: the system's answer, else the presents'
// proof): any frame rate shows evenly, and the game's step is never locked to the display (galaxy/step_lock.h).
inline std::atomic<bool> g_variable_refresh{false};
// The window's way to the display changed (full screen entered or left, focus lost or gained, another display, the
// display's mode): the renderer asks again whether the display is variable (I-007, point 5). Set by the hosts.
inline std::atomic<bool> g_display_recheck{false};
// The presents' proof holds presents at this rate (hertz; 0: no proof running, render/variable_refresh.h RefreshQuestion):
// the slots follow the pacer's clock at it, as under variable refresh at the cap.
inline std::atomic<double> g_refresh_probe_hz{0.0};
}
