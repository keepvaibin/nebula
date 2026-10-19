// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/perf_hint.h"

#if !defined(_WIN32)
#include <pthread.h>
#include <time.h>

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

struct FrameThread {
    clockid_t clock;
    std::int64_t cpu_ns;
#if defined(__ANDROID__)
    std::int32_t tid;
#endif
};

std::mutex g_mutex;
std::vector<FrameThread> g_threads;

std::int64_t cpu_time(clockid_t clock) {
    timespec now{};
    return clock_gettime(clock, &now) == 0 ? std::int64_t{now.tv_sec} * 1000000000 + now.tv_nsec : -1;
}

#if defined(__ANDROID__)
// APerformanceHint (android/performance_hint.h), looked up at run time: the app also runs on Android 12, before it.
struct Api {
    void* (*get_manager)() = nullptr;
    void* (*create_session)(void*, const std::int32_t*, std::size_t, std::int64_t) = nullptr;
    int (*report)(void*, std::int64_t) = nullptr;
    void (*close_session)(void*) = nullptr;
    bool loaded = false;
};

constexpr std::int64_t kTargetNs = 1000000000ll / 60;

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
        g_session = manager != nullptr ? g_api.create_session(manager, tids.data(), tids.size(), kTargetNs) : nullptr;
        std::fprintf(stderr, "[perf] performance hint session: %zu threads, target %.2f ms%s\n", tids.size(),
                     static_cast<double>(kTargetNs) / 1e6, g_session != nullptr ? "" : " (not available)");
    }
    if (g_session != nullptr && work_ns > 0) {
        g_api.report(g_session, work_ns);
    }
}
#endif

}  // namespace

void perf_hint_add_current_thread() noexcept {
    clockid_t clock{};
    if (pthread_getcpuclockid(pthread_self(), &clock) != 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
#if defined(__ANDROID__)
    g_threads.push_back({clock, cpu_time(clock), static_cast<std::int32_t>(gettid())});
    g_session_stale = true;  // made again with every thread at the next report
#else
    g_threads.push_back({clock, cpu_time(clock)});
#endif
}

std::int64_t perf_hint_retrace() noexcept {
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

void perf_hint_add_current_thread() noexcept {}

std::int64_t perf_hint_retrace() noexcept {
    return 0;
}

#endif

}  // namespace galaxy::host
