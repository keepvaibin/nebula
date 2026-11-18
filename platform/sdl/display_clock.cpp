// SPDX-License-Identifier: GPL-3.0-only
// The display's vsync as the game's frame clock (sdl_host.h, wait_display_frame, display_present_target).

#include "sdl_host.h"
#include "galaxy/display_timeline.h"
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
#endif

namespace galaxy::sdl {

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

void on_refresh_rate(std::int64_t vsync_period_ns, void*) {
    g_display_period_ns.store(vsync_period_ns, std::memory_order_relaxed);
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
    if (!grid.next_slot(period_ns, last_ns, monotonic_ns(), in_between, target, frame_period)) {
        g_frame_slot_ns.store(0, std::memory_order_relaxed);
        return false;
    }
    const timespec wake{static_cast<time_t>(target / 1000000000), static_cast<long>(target % 1000000000)};
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &wake, nullptr) != 0) {
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

#elif !defined(NEBULA_HOST_APPLE)  // Apple's display link: platform/apple/display_clock_apple.mm

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
