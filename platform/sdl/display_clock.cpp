// SPDX-License-Identifier: GPL-3.0-only
// The display's vsync as the game's retrace clock (sdl_host.h, wait_display_retrace).

#include "sdl_host.h"

#if defined(__ANDROID__)
#include <android/choreographer.h>
#include <android/looper.h>
#include <pthread.h>

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>
#endif

namespace galaxy::sdl {

#if defined(__ANDROID__)

namespace {

// Choreographer's frame callbacks on a looper thread of their own: each display vsync's time (CLOCK_MONOTONIC), and
// the interval between vsyncs, smoothed.
std::mutex g_mutex;
std::condition_variable g_vsync;
std::int64_t g_vsync_ns = 0;
std::int64_t g_interval_ns = 0;
std::once_flag g_started;

void on_frame(std::int64_t frame_ns, void* choreographer) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const std::int64_t delta = frame_ns - g_vsync_ns;
        if (g_vsync_ns != 0 && delta > 2000000 && delta < 50000000) {
            g_interval_ns = g_interval_ns == 0 ? delta : (g_interval_ns * 7 + delta) / 8;
        }
        g_vsync_ns = frame_ns;
    }
    g_vsync.notify_all();
    AChoreographer_postFrameCallback64(static_cast<AChoreographer*>(choreographer), on_frame, choreographer);
}

void clock_thread() {
    pthread_setname_np(pthread_self(), "Nebula Vsync");
    ALooper_prepare(0);
    AChoreographer* choreographer = AChoreographer_getInstance();
    if (choreographer == nullptr) {
        return;
    }
    AChoreographer_postFrameCallback64(choreographer, on_frame, choreographer);
    for (;;) {
        ALooper_pollOnce(-1, nullptr, nullptr, nullptr);
    }
}

}  // namespace

bool wait_display_retrace(std::int64_t period_ns, std::int64_t& last_ns, std::int64_t deadline_ns) noexcept {
    try {
        std::call_once(g_started, [] { std::thread(clock_thread).detach(); });
    } catch (...) {
        return false;
    }
    std::unique_lock<std::mutex> lock(g_mutex);
    // Only a display whose vsyncs divide the retrace period (60, 120, 180 or 240 Hz) keeps the game at its speed; at
    // 90 Hz, say, the host's own clock runs it.
    if (g_interval_ns != 0) {
        const double vsyncs = static_cast<double>(period_ns) / static_cast<double>(g_interval_ns);
        if (std::abs(vsyncs - std::round(vsyncs)) > 0.03 || std::round(vsyncs) < 1.0) {
            return false;
        }
    }
    // The vsync nearest one retrace period after the last: half a vsync early still counts.
    const std::int64_t half = (g_interval_ns != 0 ? g_interval_ns : period_ns / 2) / 2;
    const std::int64_t target = last_ns + period_ns - half;
    const auto deadline = std::chrono::steady_clock::time_point(std::chrono::nanoseconds(deadline_ns));
    if (!g_vsync.wait_until(lock, deadline, [&] { return g_vsync_ns != 0 && g_vsync_ns >= target; })) {
        return false;  // no vsync in time (out of sight, not shown yet): the host's clock
    }
    last_ns = g_vsync_ns;
    return true;
}

double display_refresh_hz() noexcept {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_interval_ns != 0 ? 1e9 / static_cast<double>(g_interval_ns) : 0.0;
}

#else

bool wait_display_retrace(std::int64_t, std::int64_t&, std::int64_t) noexcept {
    return false;
}

double display_refresh_hz() noexcept {
    return 0.0;
}

#endif

}  // namespace galaxy::sdl
