#pragma once

#include <atomic>
#include <cstdint>

namespace galaxy::render {

// The clock GPU memory that sits unused is aged on: the game steps drawn so far. A target, a pass's work textures or a
// staging chunk no picture has asked for in kIdleSteps of them is handed back, and is made again when a picture next
// asks. Pictures are the wrong measure: the same ten seconds are 600 pictures at 60 a second and 30 000 at an unlimited
// rate, so a count of them frees and remakes memory in the middle of play at a high rate and holds it for minutes at a
// low one. Game steps are the game's own time: while the game is paused on a load or the window is hidden nothing ages,
// and nothing is freed that the next picture would make again.
//
// Advanced on the render thread as each new list's operations run (render_core), by the game steps since the list before
// it (lists the renderer had no picture for still count). Read on the render thread by every renderer and pass.
inline constexpr std::uint64_t kIdleSteps = 600u;  // ten seconds of the game

namespace idle_clock_detail {
inline std::atomic<std::uint64_t> g_steps{0u};
inline std::uint64_t g_last_step = 0u;  // render thread: the game step of the newest list counted
inline bool g_counted = false;
}  // namespace idle_clock_detail

[[nodiscard]] inline std::uint64_t idle_clock() noexcept {
    return idle_clock_detail::g_steps.load(std::memory_order_relaxed);
}

// Whether kIdleSteps (or `steps`) game steps have been drawn since `stamp` (an idle_clock() reading).
[[nodiscard]] inline bool idle_since(std::uint64_t stamp, std::uint64_t steps = kIdleSteps) noexcept {
    return idle_clock() - stamp > steps;
}

// Render thread: a new list, for game step `step`, is being drawn. A step at or before the last one (the game started a
// new count) moves the clock by one.
inline void advance_idle_clock(std::uint64_t step) noexcept {
    using namespace idle_clock_detail;
    const std::uint64_t moved = g_counted && step > g_last_step ? step - g_last_step : 1u;
    g_last_step = step;
    g_counted = true;
    g_steps.store(g_steps.load(std::memory_order_relaxed) + moved, std::memory_order_relaxed);
}

}  // namespace galaxy::render
