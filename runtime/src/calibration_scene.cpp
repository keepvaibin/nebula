// SPDX-License-Identifier: GPL-3.0-only
// calibration_scene.h: the HDR calibration's reference stage.
#include "galaxy/calibration_scene.h"

#include "galaxy/memory_saves.h"
#include "galaxy/render_api.h"
#include "nebula_game_api.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <string>

namespace galaxy::calibration_scene {
namespace {

// Off: no visit. Reset: the start's reset to the title runs. Title: the title starts before the travel is asked.
// Arrive: the travel runs. Settle: on the stage, until it shows play with nothing left to build. Ready: Mario shows.
// Ending: stop()'s reset runs. Closing: the reset is done; the game plays on until its title has started.
enum class Phase : unsigned { Off, Reset, Title, Arrive, Settle, Ready, Ending, Closing };

constexpr std::int64_t kTitleNs = 2'000'000'000;       // the title's own start before the travel (the benchmark's 120 steps)
constexpr std::int64_t kArriveNs = 20'000'000'000;     // a travel unanswered this long is asked again
constexpr unsigned kAsks = 3u;                         // asks before the visit gives up (the screen then shows what is there)
constexpr std::int64_t kQuietNs = 500'000'000;         // play shown with no pipeline left to build, this long: ready
constexpr std::int64_t kSettleMostNs = 15'000'000'000; // the longest wait for that after the arrival
constexpr std::int64_t kClosingMostNs = 5'000'000'000; // the longest wait for the title's start after the end's reset

std::atomic<Phase> g_phase{Phase::Off};
std::atomic<bool (*)()> g_gate{nullptr};
std::atomic<bool> g_play{false};
// Resets asked for and not yet taken (take_reset).
std::atomic<std::uint32_t> g_resets_asked{0u};
std::atomic<bool> g_saves_in_memory{false};
MemorySaves g_saves;

// The rest is the game thread's and stop()'s, under the lock.
std::mutex g_mutex;
std::uint32_t g_resets_owed = 0u;  // asked for and not yet done (reset_done)
std::int64_t g_phase_ns = 0;
std::int64_t g_quiet_since_ns = 0;
std::string g_stage_at_ask;
bool g_saw_start = false;
unsigned g_asks = 0u;

std::atomic<std::int64_t (*)()> g_clock{nullptr};

std::int64_t steady_ns() noexcept {
    if (std::int64_t (*const clock)() = g_clock.load(std::memory_order_acquire)) return clock();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void enter(Phase phase) noexcept {
    g_phase.store(phase, std::memory_order_release);
    g_phase_ns = steady_ns();
}

void note(const char* what) {
    std::cerr << "[settings] HDR CALIBRATION SCENE: " << what << '\n';
}

int ask_travel(std::string_view current, char* stage, std::uint32_t capacity, std::int32_t* star) {
    std::snprintf(stage, capacity, "%s", kStage);
    *star = kStar;
    g_stage_at_ask = std::string(current);
    g_saw_start = false;
    g_play.store(false, std::memory_order_relaxed);
    ++g_asks;
    enter(Phase::Arrive);
    return NEBULA_GALAXY_TRAVEL_STAGE;
}

}  // namespace

void set_gate(bool (*may_visit)()) noexcept {
    g_gate.store(may_visit, std::memory_order_release);
}

bool start() noexcept {
    try {
        const std::lock_guard<std::mutex> lock(g_mutex);
        bool (*const gate)() = g_gate.load(std::memory_order_acquire);
        if (g_phase.load(std::memory_order_acquire) != Phase::Off || gate == nullptr || !gate()) {
            return false;
        }
        g_saves.clear();
        g_asks = 0u;
        g_quiet_since_ns = 0;
        g_play.store(false, std::memory_order_relaxed);
        // The phase first: the reset's saves are the visit's blank ones.
        g_resets_owed = 1u;
        enter(Phase::Reset);
        g_resets_asked.store(1u, std::memory_order_release);
        note("the game goes to its title for the reference stage");
        return true;
    } catch (...) {
        return false;
    }
}

void stop() noexcept {
    try {
        const std::lock_guard<std::mutex> lock(g_mutex);
        const Phase phase = g_phase.load(std::memory_order_acquire);
        if (phase == Phase::Off || phase == Phase::Ending || phase == Phase::Closing) {
            return;
        }
        // The start's reset not yet taken: the game never left where it was.
        std::uint32_t untaken = 1u;
        if (phase == Phase::Reset && g_resets_owed == 1u &&
            g_resets_asked.compare_exchange_strong(untaken, 0u, std::memory_order_acq_rel)) {
            g_resets_owed = 0u;
            enter(Phase::Off);
            note("ended before the game left its screen");
            return;
        }
        // Not active from here: the reset, once the game takes it, reads the player's saves.
        enter(Phase::Ending);
        ++g_resets_owed;
        g_resets_asked.fetch_add(1u, std::memory_order_acq_rel);
        note("ended: the game goes back to its title with the player's saves");
    } catch (...) {
    }
}

bool active() noexcept {
    switch (g_phase.load(std::memory_order_acquire)) {
    case Phase::Reset:
    case Phase::Title:
    case Phase::Arrive:
    case Phase::Settle:
    case Phase::Ready: return true;
    default: return false;
    }
}

bool saves_in_memory() noexcept {
    return g_saves_in_memory.load(std::memory_order_acquire);
}

bool live() noexcept {
    return g_phase.load(std::memory_order_acquire) != Phase::Off;
}

bool waiting() noexcept {
    switch (g_phase.load(std::memory_order_acquire)) {
    case Phase::Reset:
    case Phase::Title:
    case Phase::Arrive:
    case Phase::Settle: return true;
    default: return false;
    }
}

bool take_reset() noexcept {
    std::uint32_t asked = g_resets_asked.load(std::memory_order_acquire);
    while (asked != 0u) {
        if (g_resets_asked.compare_exchange_weak(asked, asked - 1u, std::memory_order_acq_rel)) {
            // The start's reset reads the blank saves, the end's the player's (every reset taken once the visit has
            // ended is the end's).
            g_saves_in_memory.store(active(), std::memory_order_release);
            return true;
        }
    }
    return false;
}

void reset_done() noexcept {
    try {
        const std::lock_guard<std::mutex> lock(g_mutex);
        if (g_resets_owed == 0u) {
            return;  // not a visit's (the benchmark's, the game's own)
        }
        --g_resets_owed;
        const Phase phase = g_phase.load(std::memory_order_acquire);
        if (phase == Phase::Reset) {
            enter(Phase::Title);
        } else if (phase == Phase::Ending && g_resets_owed == 0u) {
            enter(Phase::Closing);
        }
    } catch (...) {
    }
}

int scene_frame(std::string_view current_stage, char* stage, std::uint32_t capacity, std::int32_t* star) {
    if (!live()) {
        return NEBULA_GALAXY_TRAVEL_NONE;
    }
    const std::lock_guard<std::mutex> lock(g_mutex);
    const std::int64_t now = steady_ns();
    const std::int64_t since = now - g_phase_ns;
    switch (g_phase.load(std::memory_order_acquire)) {
    case Phase::Title:
        if (since >= kTitleNs) return ask_travel(current_stage, stage, capacity, star);
        break;
    case Phase::Arrive:
        // Another stage: its name is reported only from its own scene's draws. The stage it is on: its new scene's start.
        if (current_stage == kStage && (g_saw_start || g_stage_at_ask != kStage)) {
            g_quiet_since_ns = 0;
            enter(Phase::Settle);
        } else if (since >= kArriveNs) {
            if (g_asks >= kAsks) {
                note("the game did not enter the reference stage: the paper white step shows what is there");
                enter(Phase::Ready);
                break;
            }
            return ask_travel(current_stage, stage, capacity, star);
        }
        break;
    case Phase::Settle:
        if (g_play.load(std::memory_order_relaxed) && render::pipeline_progress().left == 0u) {
            if (g_quiet_since_ns == 0) g_quiet_since_ns = now;
        } else {
            g_quiet_since_ns = 0;
        }
        if ((g_quiet_since_ns != 0 && now - g_quiet_since_ns >= kQuietNs) || since >= kSettleMostNs) {
            enter(Phase::Ready);
            note("ready");
        }
        break;
    case Phase::Closing:
        if (since >= kClosingMostNs) enter(Phase::Off);
        break;
    default:
        break;
    }
    return NEBULA_GALAXY_TRAVEL_NONE;
}

void scene_started() noexcept {
    if (!live()) return;
    try {
        const std::lock_guard<std::mutex> lock(g_mutex);
        const Phase phase = g_phase.load(std::memory_order_acquire);
        if (phase == Phase::Arrive) {
            g_saw_start = true;
        } else if (phase == Phase::Closing) {
            enter(Phase::Off);  // the title has started: the menu holds it again
        }
    } catch (...) {
    }
}

void note_play(bool play) noexcept {
    g_play.store(play, std::memory_order_relaxed);
}

std::int32_t save_request(int op, std::string_view name, void* buffer, std::uint32_t size, std::uint32_t* length) noexcept {
    return g_saves.request(op, name, buffer, size, length);
}

void set_clock_for_tests(std::int64_t (*now_ns)()) noexcept {
    g_clock.store(now_ns, std::memory_order_release);
}

}  // namespace galaxy::calibration_scene
