// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/perf_hint.h"
#include "galaxy/game_step.h"

#if defined(_WIN32)
#include "galaxy/windows_power.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#endif

#if !defined(_WIN32)
#include <pthread.h>
#include <time.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <pthread/qos.h>

#include <cstdlib>
#endif

#include <algorithm>
#include <mutex>
#include <vector>
#endif

#if defined(__ANDROID__)
#include <dlfcn.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdio>
#endif

namespace galaxy::host {

#if !defined(_WIN32)

namespace {

#if defined(__APPLE__)
// Darwin has no CPU clock id for another thread: its time is read through its Mach port.
using ThreadClock = mach_port_t;
#else
using ThreadClock = clockid_t;
#endif

struct FrameThread {
    ThreadClock clock;
    std::int64_t cpu_ns;
#if defined(__ANDROID__)
    std::int32_t tid;
#endif
};

std::mutex g_mutex;
std::vector<FrameThread> g_threads;

#if defined(__APPLE__)
std::int64_t cpu_time(ThreadClock thread) {
    thread_basic_info_data_t info{};
    mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
    if (thread_info(thread, THREAD_BASIC_INFO, reinterpret_cast<thread_info_t>(&info), &count) != KERN_SUCCESS) {
        return -1;
    }
    return (std::int64_t{info.user_time.seconds} + info.system_time.seconds) * 1000000000 +
           (std::int64_t{info.user_time.microseconds} + info.system_time.microseconds) * 1000;
}
#else
std::int64_t cpu_time(ThreadClock clock) {
    timespec now{};
    return clock_gettime(clock, &now) == 0 ? std::int64_t{now.tv_sec} * 1000000000 + now.tv_nsec : -1;
}
#endif

#if defined(__ANDROID__)
// APerformanceHint (android/performance_hint.h), looked up at run time: the app also runs on Android 12, before it.
struct Api {
    void* (*get_manager)() = nullptr;
    void* (*create_session)(void*, const std::int32_t*, std::size_t, std::int64_t) = nullptr;
    int (*report)(void*, std::int64_t) = nullptr;
    void (*close_session)(void*) = nullptr;
    int (*update_target)(void*, std::int64_t) = nullptr;  // optional: without it a new target makes a new session
    bool loaded = false;
};

// The frame's target duration: the game step until the frame clock says otherwise (perf_hint_set_target).
std::int64_t g_target_ns = galaxy::kGameStepNs;

Api g_api;
bool g_api_tried = false;
void* g_session = nullptr;
bool g_session_stale = true;

bool load_api() {
    if (g_api_tried) {
        return g_api.loaded;
    }
    g_api_tried = true;
    void* library = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr) {
        return false;
    }
    g_api.get_manager = reinterpret_cast<decltype(g_api.get_manager)>(dlsym(library, "APerformanceHint_getManager"));
    g_api.create_session =
        reinterpret_cast<decltype(g_api.create_session)>(dlsym(library, "APerformanceHint_createSession"));
    g_api.report = reinterpret_cast<decltype(g_api.report)>(
        dlsym(library, "APerformanceHint_reportActualWorkDuration"));
    g_api.close_session =
        reinterpret_cast<decltype(g_api.close_session)>(dlsym(library, "APerformanceHint_closeSession"));
    g_api.update_target = reinterpret_cast<decltype(g_api.update_target)>(
        dlsym(library, "APerformanceHint_updateTargetWorkDuration"));
    g_api.loaded = g_api.get_manager != nullptr && g_api.create_session != nullptr && g_api.report != nullptr &&
        g_api.close_session != nullptr;
    return g_api.loaded;
}

void report(std::int64_t work_ns) {
    if (!load_api()) {
        return;
    }
    if (g_session_stale) {
        g_session_stale = false;
        if (g_session != nullptr) {
            g_api.close_session(g_session);
            g_session = nullptr;
        }
        std::vector<std::int32_t> tids;
        for (const FrameThread& thread : g_threads) {
            tids.push_back(thread.tid);
        }
        void* manager = g_api.get_manager();
        g_session = manager != nullptr ? g_api.create_session(manager, tids.data(), tids.size(), g_target_ns) : nullptr;
        std::fprintf(stderr, "[perf] performance hint session: %zu threads, target %.2f ms%s\n", tids.size(),
                     static_cast<double>(g_target_ns) / 1e6, g_session != nullptr ? "" : " (not available)");
    }
    if (g_session != nullptr && work_ns > 0) {
        g_api.report(g_session, work_ns);
    }
}
#endif

}  // namespace

void perf_hint_add_current_thread() noexcept {
#if defined(__APPLE__)
    // The frame's threads are what the player waits on: the system's highest class for them, so their timers are not
    // gathered with others' (a frame's wake-up a few milliseconds late is a picture a display refresh late) and, on
    // Apple silicon, they run on the performance cores. NEBULA_NO_QOS=1 leaves them as made, to measure against.
    if (std::getenv("NEBULA_NO_QOS") == nullptr) {
        (void)pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    }
    const ThreadClock clock = pthread_mach_thread_np(pthread_self());
#else
    clockid_t clock{};
    if (pthread_getcpuclockid(pthread_self(), &clock) != 0) {
        return;
    }
#endif
    std::lock_guard<std::mutex> lock(g_mutex);
#if defined(__ANDROID__)
    g_threads.push_back({clock, cpu_time(clock), static_cast<std::int32_t>(gettid())});
    g_session_stale = true;  // made again with every thread at the next report
#else
    g_threads.push_back({clock, cpu_time(clock)});
#endif
}

void perf_hint_set_target(std::int64_t target_ns) noexcept {
#if defined(__ANDROID__)
    std::lock_guard<std::mutex> lock(g_mutex);
    if (target_ns <= 0 || target_ns == g_target_ns) {
        return;
    }
    g_target_ns = target_ns;
    if (g_session != nullptr && g_api.update_target != nullptr && !g_session_stale) {
        (void)g_api.update_target(g_session, target_ns);
    } else {
        g_session_stale = true;  // made again with the new target at the next report
    }
    std::fprintf(stderr, "[perf] frame target %.2f ms\n", static_cast<double>(target_ns) / 1e6);
#else
    (void)target_ns;
#endif
}

std::int64_t perf_hint_frame_done() noexcept {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::int64_t work = 0;
    for (FrameThread& thread : g_threads) {
        const std::int64_t now = cpu_time(thread.clock);
        if (now >= 0 && thread.cpu_ns >= 0 && now > thread.cpu_ns) {
            work = std::max(work, now - thread.cpu_ns);
        }
        thread.cpu_ns = now;
    }
#if defined(__ANDROID__)
    if (!g_threads.empty()) {
        report(work);
    }
#endif
    return work;
}

#else

namespace {
std::atomic<bool> g_on_battery{false};
}  // namespace

void perf_hint_add_current_thread() noexcept {}

void perf_hint_set_target(std::int64_t) noexcept {}

std::int64_t perf_hint_frame_done() noexcept {
    // Every two seconds (by the clock, not by the game's frame count): the power state is looked at, a change said once.
    static std::atomic<std::int64_t> next_look{0};
    const std::int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now().time_since_epoch()).count();
    std::int64_t due = next_look.load(std::memory_order_relaxed);
    if (now >= due && next_look.compare_exchange_strong(due, now + 2000, std::memory_order_relaxed)) {
        const bool battery = galaxy::host::running_on_battery();
        if (g_on_battery.exchange(battery, std::memory_order_relaxed) != battery) {
            std::fprintf(stderr, "[power] %s\n", battery ? "on battery" : "on mains power");
        }
    }
    return 0;
}

#endif

#if defined(_WIN32)
bool perf_hint_on_battery() noexcept {
    return g_on_battery.load(std::memory_order_relaxed);
}
#else
bool perf_hint_on_battery() noexcept {
    return false;
}
#endif

}  // namespace galaxy::host
