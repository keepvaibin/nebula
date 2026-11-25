// SPDX-License-Identifier: GPL-3.0-only
// The display's vsync as the game's frame clock (sdl_host.h, wait_display_frame, display_present_target).

#include "sdl_host.h"
#include "galaxy/display_timeline.h"
#include "galaxy/host_wait.h"
#include "galaxy/render/present_clock.h"
#include "galaxy/vsync_grid.h"

#if defined(__ANDROID__)
#include <android/choreographer.h>
#include <android/looper.h>
#include <dlfcn.h>
#include <cstdio>
#include <pthread.h>
#include <time.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <thread>
#elif defined(__linux__)
#include <cerrno>
#include <cstdio>
#include <time.h>

#include <atomic>
#include <mutex>
#endif

namespace galaxy::sdl {

#if defined(__linux__)  // Android too
namespace {

// Sleeps to `target_ns` (CLOCK_MONOTONIC): again only when a signal broke the sleep off. Any other error (P-281) is said
// once and the frame goes to the host's clock rather than the thread spinning.
std::atomic<bool> g_sleep_failed_said{false};
bool sleep_to(std::int64_t target_ns) noexcept {
    const timespec wake{static_cast<time_t>(target_ns / 1000000000), static_cast<long>(target_ns % 1000000000)};
    int error = 0;
    if (galaxy::sleep_through_interruptions([&] { return error = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &wake, nullptr); })) {
        return true;
    }
    if (!g_sleep_failed_said.exchange(true, std::memory_order_relaxed)) {
        std::fprintf(stderr, "[pacing] the display clock's sleep failed (error %d): the host's clock paces the frames\n", error);
    }
    return false;
}

}  // namespace
#endif

#if defined(__ANDROID__)

namespace {

// Choreographer's frame callbacks on a looper thread of their own: the vsync grid (CLOCK_MONOTONIC). The frame slots are
// predicted on that grid rather than woken by the callbacks, so a callback delivered late (its thread not scheduled at
// once) never delays a frame.
std::mutex g_mutex;
galaxy::VsyncGrid g_grid;
std::int64_t g_expected_presentation_ns = 0;
std::once_flag g_started;
// The latest frame slot on the vsync grid and the frame period there (display_present_target); 0 while frames
// follow the host's clock.
std::atomic<std::int64_t> g_frame_slot_ns{0};
std::atomic<std::int64_t> g_frame_period_ns{0};
// The display's vsync period as the system states it (refresh-rate callbacks); 0 until the first.
std::atomic<std::int64_t> g_display_period_ns{0};
// Held (set_display_clock_held): a callback posts no next one. g_posted: a callback is asked for (the looper
// thread's own; it posts again when let go). g_looper: that thread's looper, to wake it.
std::atomic<bool> g_held{false};
bool g_posted = false;
std::atomic<ALooper*> g_looper{nullptr};

std::int64_t monotonic_ns() noexcept {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<std::int64_t>(now.tv_sec) * 1000000000 + now.tv_nsec;
}

// API 33 timelines distinguish the app's render-start tick from the display's presentation tick.
// Resolve dynamically so Android 12 retains the legacy callback without unresolved imports.
struct TimelineApi {
    void (*post)(AChoreographer*, AChoreographer_vsyncCallback, void*) = nullptr;
    int64_t (*frame_time)(const AChoreographerFrameCallbackData*) = nullptr;
    size_t (*preferred)(const AChoreographerFrameCallbackData*) = nullptr;
    int64_t (*expected)(const AChoreographerFrameCallbackData*, size_t) = nullptr;
    bool load() {
        void* library = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
        if (!library) return false;
        post = reinterpret_cast<decltype(post)>(dlsym(library, "AChoreographer_postVsyncCallback"));
        frame_time = reinterpret_cast<decltype(frame_time)>(dlsym(library, "AChoreographerFrameCallbackData_getFrameTimeNanos"));
        preferred = reinterpret_cast<decltype(preferred)>(dlsym(library, "AChoreographerFrameCallbackData_getPreferredFrameTimelineIndex"));
        expected = reinterpret_cast<decltype(expected)>(dlsym(library, "AChoreographerFrameCallbackData_getFrameTimelineExpectedPresentationTimeNanos"));
        // The loaded symbols remain valid throughout the process, like the looper thread itself.
        return post && frame_time && preferred && expected;
    }
} g_timeline;

void record_frame(std::int64_t frame_ns, std::int64_t expected) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_grid.note(frame_ns, g_display_period_ns.load(std::memory_order_relaxed));
    g_expected_presentation_ns = expected;
}

void on_frame(std::int64_t frame_ns, void* choreographer);
void on_timeline(const AChoreographerFrameCallbackData* data, void* choreographer);

// The next callback, unless the clock is held (the looper thread only).
void post_next(AChoreographer* choreographer) {
    g_posted = !g_held.load(std::memory_order_relaxed);
    if (!g_posted) {
        return;
    }
    if (g_timeline.post != nullptr) {
        g_timeline.post(choreographer, on_timeline, choreographer);
    } else {
        AChoreographer_postFrameCallback64(choreographer, on_frame, choreographer);
    }
}

void on_frame(std::int64_t frame_ns, void* choreographer) {
    record_frame(frame_ns, 0);
    post_next(static_cast<AChoreographer*>(choreographer));
}

void on_timeline(const AChoreographerFrameCallbackData* data, void* choreographer) {
    record_frame(g_timeline.frame_time(data), g_timeline.expected(data, g_timeline.preferred(data)));
    post_next(static_cast<AChoreographer*>(choreographer));
}

// A mode switch (120 to 60 Hz) is a display change: the grid follows the new period and the variable-refresh answer is
// asked again (G2.3.5 point 6).
void on_refresh_rate(std::int64_t vsync_period_ns, void*) {
    if (g_display_period_ns.exchange(vsync_period_ns, std::memory_order_relaxed) != vsync_period_ns) {
        galaxy::render::g_display_recheck.store(true, std::memory_order_release);
    }
}

void clock_thread() {
    pthread_setname_np(pthread_self(), "Nebula Vsync");
    ALooper_prepare(0);
    AChoreographer* choreographer = AChoreographer_getInstance();
    if (choreographer == nullptr) {
        return;
    }
    if (g_timeline.load()) {
        std::fprintf(stderr, "[display] Android preferred presentation timeline enabled\n");
    } else {
        g_timeline.post = nullptr;
    }
    g_looper.store(ALooper_forThread(), std::memory_order_release);
    post_next(choreographer);
    AChoreographer_registerRefreshRateCallback(choreographer, on_refresh_rate, nullptr);
    for (;;) {
        // A callback, or a wake from set_display_clock_held: let go with none asked for, it asks again.
        ALooper_pollOnce(-1, nullptr, nullptr, nullptr);
        if (!g_posted) {
            post_next(choreographer);
        }
    }
}

}  // namespace

bool wait_display_frame(std::int64_t period_ns, std::int64_t& last_ns, bool in_between) noexcept {
    try {
        std::call_once(g_started, [] { std::thread(clock_thread).detach(); });
    } catch (...) {
        return false;
    }
    galaxy::VsyncGrid grid;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        grid = g_grid;
    }
    // The next frame slot on the vsync grid (galaxy/vsync_grid.h), or the host's clock when there is no grid that
    // keeps the game at its speed.
    std::int64_t target = 0;
    std::int64_t frame_period = 0;
    std::int64_t dropped = 0;
    if (!grid.next_slot(period_ns, last_ns, monotonic_ns(), in_between, galaxy::render::g_variable_refresh.load(std::memory_order_relaxed), target, frame_period, &dropped)) {
        g_frame_slot_ns.store(0, std::memory_order_relaxed);
        return false;
    }
    if (dropped > 0) {
        std::fprintf(stderr,
                     "[pacing] the game's steps were %.0f ms behind the display's vsync: those steps are dropped\n",
                     static_cast<double>(dropped) / 1e6);
    }
    // A slot already past (the clock catching up, P-274) returns at once.
    if (!sleep_to(target)) {
        g_frame_slot_ns.store(0, std::memory_order_relaxed);
        return false;
    }
    last_ns = target;
    g_frame_period_ns.store(frame_period, std::memory_order_relaxed);
    g_frame_slot_ns.store(target, std::memory_order_release);
    return true;
}

void set_display_clock_held(bool held) noexcept {
    g_held.store(held, std::memory_order_relaxed);
    if (ALooper* const looper = g_looper.load(std::memory_order_acquire); looper != nullptr && !held) {
        ALooper_wake(looper);
    }
}

bool display_grid(std::int64_t& slot_ns, std::int64_t& period_ns, std::int64_t& vsync_ns,
                  std::int64_t& now_ns) noexcept {
    slot_ns = g_frame_slot_ns.load(std::memory_order_acquire);
    period_ns = g_frame_period_ns.load(std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        vsync_ns = g_grid.interval_ns;
        if (g_expected_presentation_ns > 0) {
            slot_ns = galaxy::presentation_grid_at(slot_ns, g_expected_presentation_ns, vsync_ns);
        }
    }
    now_ns = monotonic_ns();
    return slot_ns != 0 && period_ns != 0 && vsync_ns != 0 && now_ns - slot_ns <= 4 * period_ns;
}

bool vsync_grid(std::int64_t& vsync_ns, std::int64_t& interval_ns) noexcept {
    const std::int64_t stated = g_display_period_ns.load(std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(g_mutex);
    vsync_ns = g_grid.vsync_ns;
    interval_ns = stated > 0 ? stated : g_grid.interval_ns;
    return vsync_ns != 0 && interval_ns != 0;
}

double display_refresh_hz() noexcept {
    if (const std::int64_t period = g_display_period_ns.load(std::memory_order_relaxed); period > 0) {
        return 1e9 / static_cast<double>(period);
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_grid.interval_ns != 0 ? 1e9 / static_cast<double>(g_grid.interval_ns) : 0.0;
}

#elif defined(__linux__)

namespace {

// Linux (I-005): no vsync callback reaches an application, so the grid comes from the presents. The Vulkan renderer
// tells each present it saw complete (note_present_shown: present wait's return, or present timing's own time) with
// the display mode's period; PresentStampFit fits them to that period (galaxy/vsync_grid.h) and the VsyncGrid takes
// the fit with its period stated, so the frame slots come as on Android. No present seen for a quarter second (out of
// sight, a renderer without present wait or present timing): no grid, and the host's own clock runs the frames.
std::mutex g_mutex;
galaxy::PresentStampFit g_fit;
galaxy::VsyncGrid g_grid;
std::atomic<std::int64_t> g_frame_slot_ns{0};
std::atomic<std::int64_t> g_frame_period_ns{0};

std::int64_t monotonic_ns() noexcept {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<std::int64_t>(now.tv_sec) * 1000000000 + now.tv_nsec;
}

}  // namespace

void note_present_shown(std::int64_t stamp_ns, std::int64_t mode_period_ns, bool exact) noexcept {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_fit.mode(mode_period_ns);
    if (g_fit.note(stamp_ns, exact) && g_fit.interval_ns > 0) {
        g_grid.note(g_fit.vsync_ns, g_fit.interval_ns);
    }
}

bool wait_display_frame(std::int64_t period_ns, std::int64_t& last_ns, bool in_between) noexcept {
    galaxy::VsyncGrid grid;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        grid = g_grid;
    }
    std::int64_t target = 0;
    std::int64_t frame_period = 0;
    std::int64_t dropped = 0;
    if (!grid.next_slot(period_ns, last_ns, monotonic_ns(), in_between, galaxy::render::g_variable_refresh.load(std::memory_order_relaxed), target, frame_period, &dropped)) {
        g_frame_slot_ns.store(0, std::memory_order_relaxed);
        return false;
    }
    if (dropped > 0) {
        std::fprintf(stderr,
                     "[pacing] the game's steps were %.0f ms behind the display's vsync: those steps are dropped\n",
                     static_cast<double>(dropped) / 1e6);
    }
    // A slot already past (the clock catching up, P-274) returns at once.
    if (!sleep_to(target)) {
        g_frame_slot_ns.store(0, std::memory_order_relaxed);
        return false;
    }
    last_ns = target;
    g_frame_period_ns.store(frame_period, std::memory_order_relaxed);
    g_frame_slot_ns.store(target, std::memory_order_release);
    return true;
}

// The presents stop by themselves while the window is out of sight; nothing to hold.
void set_display_clock_held(bool) noexcept {}

bool display_grid(std::int64_t& slot_ns, std::int64_t& period_ns, std::int64_t& vsync_ns,
                  std::int64_t& now_ns) noexcept {
    slot_ns = g_frame_slot_ns.load(std::memory_order_acquire);
    period_ns = g_frame_period_ns.load(std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        vsync_ns = g_grid.interval_ns;
    }
    now_ns = monotonic_ns();
    return slot_ns != 0 && period_ns != 0 && vsync_ns != 0 && now_ns - slot_ns <= 4 * period_ns;
}

bool vsync_grid(std::int64_t& vsync_ns, std::int64_t& interval_ns) noexcept {
    std::lock_guard<std::mutex> lock(g_mutex);
    vsync_ns = g_grid.vsync_ns;
    interval_ns = g_grid.interval_ns;
    return vsync_ns != 0 && interval_ns != 0 && monotonic_ns() - vsync_ns <= 250000000;
}

// No stated period of the system's own beside SDL's display mode, which the host reads (host_window_sdl.cpp).
double display_refresh_hz() noexcept {
    return 0.0;
}

#elif !defined(NEBULA_HOST_APPLE)  // Apple's display link: platform/apple/display_clock_apple.mm

void note_present_shown(std::int64_t, std::int64_t, bool) noexcept {}

bool wait_display_frame(std::int64_t, std::int64_t&, bool) noexcept {
    return false;
}

void set_display_clock_held(bool) noexcept {}

bool display_grid(std::int64_t&, std::int64_t&, std::int64_t&, std::int64_t&) noexcept {
    return false;
}

bool vsync_grid(std::int64_t&, std::int64_t&) noexcept {
    return false;
}

double display_refresh_hz() noexcept {
    return 0.0;
}

#endif

}  // namespace galaxy::sdl
