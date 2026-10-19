// SPDX-License-Identifier: GPL-3.0-only
// The display's vsync as the game's retrace clock (sdl_host.h, wait_display_retrace, display_present_target).

#include "sdl_host.h"

#if defined(__ANDROID__)
#include <android/choreographer.h>
#include <android/looper.h>
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

// Choreographer's frame callbacks on a looper thread of their own: the latest vsync's time (CLOCK_MONOTONIC) and the
// interval between vsyncs, smoothed. The retraces are predicted from them on that grid rather than woken by them, so
// a callback delivered late (its thread not scheduled at once) never delays a retrace.
std::mutex g_mutex;
std::int64_t g_vsync_ns = 0;
std::int64_t g_interval_ns = 0;
std::once_flag g_started;
// The latest retrace on the vsync grid and the retrace period there (display_present_target); 0 while the retrace
// follows the host's clock.
std::atomic<std::int64_t> g_retrace_ns{0};
std::atomic<std::int64_t> g_retrace_period_ns{0};
// The display's vsync period as the system states it (refresh-rate callbacks); 0 until the first.
std::atomic<std::int64_t> g_display_period_ns{0};

std::int64_t monotonic_ns() noexcept {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<std::int64_t>(now.tv_sec) * 1000000000 + now.tv_nsec;
}

void on_frame(std::int64_t frame_ns, void* choreographer) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const std::int64_t delta = frame_ns - g_vsync_ns;
        if (g_vsync_ns != 0 && delta > 2000000 && delta < 50000000) {
            // A refresh-rate change (LTPO panels, touch boost) restarts the average at once.
            const bool changed = g_interval_ns != 0 && std::llabs(delta - g_interval_ns) > g_interval_ns / 8;
            g_interval_ns = g_interval_ns == 0 || changed ? delta : (g_interval_ns * 7 + delta) / 8;
        }
        g_vsync_ns = frame_ns;
    }
    AChoreographer_postFrameCallback64(static_cast<AChoreographer*>(choreographer), on_frame, choreographer);
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
    AChoreographer_postFrameCallback64(choreographer, on_frame, choreographer);
    AChoreographer_registerRefreshRateCallback(choreographer, on_refresh_rate, nullptr);
    for (;;) {
        ALooper_pollOnce(-1, nullptr, nullptr, nullptr);
    }
}

}  // namespace

bool wait_display_retrace(std::int64_t period_ns, std::int64_t& last_ns) noexcept {
    try {
        std::call_once(g_started, [] { std::thread(clock_thread).detach(); });
    } catch (...) {
        return false;
    }
    std::int64_t base = 0;
    std::int64_t interval = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        base = g_vsync_ns;
        interval = g_interval_ns;
    }
    const std::int64_t now = monotonic_ns();
    // No vsyncs yet, or none for a quarter second (out of sight): the host's clock.
    if (base == 0 || interval == 0 || now - base > 250000000) {
        g_retrace_ns.store(0, std::memory_order_relaxed);
        return false;
    }
    // Only a display whose vsyncs divide the retrace period (60, 120, 180 or 240 Hz) keeps the game at its speed; at
    // 90 Hz, say, the host's own clock runs it.
    const double vsyncs = static_cast<double>(period_ns) / static_cast<double>(interval);
    if (std::abs(vsyncs - std::round(vsyncs)) > 0.03 || std::round(vsyncs) < 1.0) {
        g_retrace_ns.store(0, std::memory_order_relaxed);
        return false;
    }
    // The first vsync at least one retrace period (less half a vsync) after the last retrace; from now when the clock
    // fell behind (a suspended app, a stalled thread), as a display skips the fields it missed.
    std::int64_t earliest = last_ns + period_ns - interval / 2;
    if (last_ns == 0 || earliest < now - period_ns) {
        earliest = now;
    }
    const std::int64_t steps = std::max<std::int64_t>((earliest - base + interval - 1) / interval, 0);
    const std::int64_t target = base + steps * interval;
    const timespec wake{static_cast<time_t>(target / 1000000000), static_cast<long>(target % 1000000000)};
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &wake, nullptr) != 0) {
    }
    last_ns = target;
    g_retrace_period_ns.store(std::llround(vsyncs) * interval, std::memory_order_relaxed);
    g_retrace_ns.store(target, std::memory_order_release);
    return true;
}

bool display_grid(std::int64_t& retrace_ns, std::int64_t& period_ns, std::int64_t& vsync_ns,
                  std::int64_t& now_ns) noexcept {
    retrace_ns = g_retrace_ns.load(std::memory_order_acquire);
    period_ns = g_retrace_period_ns.load(std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        vsync_ns = g_interval_ns;
    }
    now_ns = monotonic_ns();
    return retrace_ns != 0 && period_ns != 0 && vsync_ns != 0 && now_ns - retrace_ns <= 4 * period_ns;
}

bool vsync_grid(std::int64_t& vsync_ns, std::int64_t& interval_ns) noexcept {
    const std::int64_t stated = g_display_period_ns.load(std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(g_mutex);
    vsync_ns = g_vsync_ns;
    interval_ns = stated > 0 ? stated : g_interval_ns;
    return vsync_ns != 0 && interval_ns != 0;
}

double display_refresh_hz() noexcept {
    if (const std::int64_t period = g_display_period_ns.load(std::memory_order_relaxed); period > 0) {
        return 1e9 / static_cast<double>(period);
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_interval_ns != 0 ? 1e9 / static_cast<double>(g_interval_ns) : 0.0;
}

#else

bool wait_display_retrace(std::int64_t, std::int64_t&) noexcept {
    return false;
}

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
