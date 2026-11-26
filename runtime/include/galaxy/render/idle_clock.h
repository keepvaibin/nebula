#pragma once

#include <atomic>
#include <cstdint>
#include <utility>
#include <vector>

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

// When a renderer's target slots (a target pass's set per key) are handed back. None goes in play: a slot freed there is
// made again on the render thread when its key's pass next comes, 1.4 to 1.8 ms of it, a missed refresh at 240 Hz
// (R-009: at the hand-back after a load). They go as a wipe covers the screen for a load (`covered` 1, the
// g_screen_covered reading), a slot no pass has used for kIdleSteps game steps nor since the wipe before it (a whole
// scene went by without it), its targets released on the worker (release_worker.h). A host that does not tell (-1)
// sweeps once a second of the game, any slot unused for kIdleSteps.
class TargetSweep {
public:
    static constexpr std::uint64_t kUntoldSteps = 60u;  // not told: once a second of the game

    // Render thread, once a frame: whether the slots are swept now.
    [[nodiscard]] bool due(int covered) noexcept {
        const std::uint64_t now = idle_clock();
        const bool was_covered = covered_;
        covered_ = covered == 1;
        told_ = covered >= 0;
        if (!told_) {
            if (now - swept_ < kUntoldSteps) {
                return false;
            }
            swept_ = now;
            return true;
        }
        if (!covered_ || was_covered) {
            return false;
        }
        wipe_before_ = wipe_;
        wipe_ = now;
        return true;
    }

    // Whether a slot last used at `used` (an idle_clock() reading) goes in the sweep due() called for.
    [[nodiscard]] bool goes(std::uint64_t used) const noexcept {
        return idle_since(used) && (!told_ || used < wipe_before_);
    }

private:
    std::uint64_t swept_ = 0u;        // not told: idle_clock() at the last sweep
    std::uint64_t wipe_ = 0u;         // idle_clock() as the latest wipe covered the screen
    std::uint64_t wipe_before_ = 0u;  // as the one before it did (0: none yet, nothing goes)
    bool covered_ = false;
    bool told_ = false;
};

// A target slot whose texture the game freed (free_target, at a scene's end) waits as a spare under a key with this bit,
// for the next key whose pass asks the same size (begin_target_pass): the scene after takes the set made for the one
// before, where a new id would be made on the render thread at the hand-back (R-009: create_scene_set, 1.35 to 1.76 ms
// about 1.8 s after each wipe). Spares go as other slots do (TargetSweep). The game's keys never set it (draw_walker:
// a target id and the in-between bit, 1 << 40).
inline constexpr std::uint64_t kSpareTargetSlot = std::uint64_t{1} << 63u;

// The slots of `slots` (a map of key to slot with a `used` stamp) whose keys `freed(key)` names become spares, each
// under its own key (`serial` counts them), stamped now.
template <class Slots, class Freed>
void spare_target_slots(Slots& slots, Freed&& freed, std::uint64_t& serial) {
    std::vector<typename Slots::mapped_type> spares;
    for (auto it = slots.begin(); it != slots.end();) {
        if ((it->first & kSpareTargetSlot) == 0u && freed(it->first)) {
            spares.push_back(std::move(it->second));
            it = slots.erase(it);
        } else {
            ++it;
        }
    }
    for (auto& spare : spares) {
        spare.used = idle_clock();
        slots.emplace(kSpareTargetSlot | ++serial, std::move(spare));
    }
}

// A key with no slot yet takes a spare of the same size, `map_of(slot)` its size (none: it is made as before).
template <class Slots, class Map, class MapOf>
void take_spare_target_slot(Slots& slots, std::uint64_t key, const Map& map, MapOf&& map_of) {
    if ((key & kSpareTargetSlot) != 0u || slots.find(key) != slots.end()) {
        return;
    }
    for (auto it = slots.begin(); it != slots.end(); ++it) {
        if ((it->first & kSpareTargetSlot) != 0u && map_of(it->second) == map) {
            auto slot = std::move(it->second);
            slots.erase(it);
            slots.emplace(key, std::move(slot));
            return;
        }
    }
}

}  // namespace galaxy::render
