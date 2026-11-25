// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/perf_hint.h"
#include "galaxy/game_step.h"
#include "galaxy/render/present_clock.h"

#include <atomic>
#include <mutex>

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
#include <chrono>
#include <mutex>
#include <vector>
#endif

#if defined(__ANDROID__)
#include <dlfcn.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdio>
#include <thread>
#include <type_traits>
#endif

namespace galaxy::host {

namespace {

// The frame threads' work processor time (WorkCpu), on every platform: a fixed table, so a thread marking its own work never
// meets a list that grows under it. Threads are added from the threads themselves, at their start.
constexpr std::size_t kWorkSlots = 8;
struct WorkSlot {
    const char* name = nullptr;
    WorkCpu work;
};
WorkSlot g_work[kWorkSlots];
std::atomic<std::size_t> g_work_count{0};
std::mutex g_work_mutex;
thread_local int t_work = -1;  // the calling thread's slot; -1: not a frame thread

void work_register(const char* name) noexcept {
    std::lock_guard<std::mutex> lock(g_work_mutex);
    const std::size_t n = g_work_count.load(std::memory_order_relaxed);
    if (n >= kWorkSlots || t_work >= 0) return;
    g_work[n].name = name;
    t_work = static_cast<int>(n);
    g_work_count.store(n + 1, std::memory_order_release);
}

// The calling frame thread's mark of work (on) or of waiting, with its own processor clock; nothing where the clock is not known.
void work_mark(bool on) noexcept {
    if (t_work < 0) return;
    std::uint64_t units = 0;
    if (!thread_cpu_units(units)) return;
    g_work[static_cast<std::size_t>(t_work)].work.mark(units, on);
}

}  // namespace

std::size_t perf_hint_work_cpu(galaxy::ThreadCpuSample* out, std::size_t capacity) noexcept {
    if (!process_cpu_known()) return 0;
    const std::size_t n = std::min(capacity, g_work_count.load(std::memory_order_acquire));
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = {g_work[i].name, g_work[i].work.total()};
    }
    return n;
}

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
    const char* name;
    std::int64_t last_ns = -1;  // its CPU time in the window the last perf_hint_frame_done closed
    BusyWindow busy{};          // its work's wall time (perf_hint_busy)
#if defined(__ANDROID__)
    std::int32_t tid = 0;
#endif
};

std::mutex g_mutex;
std::vector<FrameThread> g_threads;
// The calling thread's place in g_threads; -1: not a frame thread.
thread_local int t_index = -1;

std::int64_t steady_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

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
    // The newest report (API 35): the work's wall, CPU and GPU times in one AWorkDuration. Optional: without it the
    // wall time alone (reportActualWorkDuration).
    void* (*duration_create)() = nullptr;
    void (*duration_release)(void*) = nullptr;
    void (*duration_start)(void*, std::int64_t) = nullptr;
    void (*duration_total)(void*, std::int64_t) = nullptr;
    void (*duration_cpu)(void*, std::int64_t) = nullptr;
    void (*duration_gpu)(void*, std::int64_t) = nullptr;
    int (*report2)(void*, void*) = nullptr;
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
    const auto find = [library](auto& to, const char* name) {
        to = reinterpret_cast<std::remove_reference_t<decltype(to)>>(dlsym(library, name));
    };
    find(g_api.duration_create, "AWorkDuration_create");
    find(g_api.duration_release, "AWorkDuration_release");
    find(g_api.duration_start, "AWorkDuration_setWorkPeriodStartTimestampNanos");
    find(g_api.duration_total, "AWorkDuration_setActualTotalDurationNanos");
    find(g_api.duration_cpu, "AWorkDuration_setActualCpuDurationNanos");
    find(g_api.duration_gpu, "AWorkDuration_setActualGpuDurationNanos");
    find(g_api.report2, "APerformanceHint_reportActualWorkDuration2");
    g_api.loaded = g_api.get_manager != nullptr && g_api.create_session != nullptr && g_api.report != nullptr &&
        g_api.close_session != nullptr;
    return g_api.loaded;
}

// The target the session holds: the frame's, or with in-between pictures the slot (present_clock.h g_slot_period_qpc,
// FRAME-MODEL 2.16 target 5: the hint holds the clock for each picture, so no GPU or CPU time is read under a clock the
// pictures' own lighter load let fall).
std::int64_t g_held_ns = 0;

void report(std::int64_t work_ns) {
    if (!load_api()) {
        return;
    }
    const std::int64_t slot_qpc = galaxy::render::g_slot_period_qpc.load(std::memory_order_relaxed);
    const std::int64_t slot_ns = slot_qpc > 0 ? galaxy::qpc_to_ns(slot_qpc) : 0;
    const std::int64_t held = slot_ns > 0 && slot_ns < g_target_ns ? slot_ns : g_target_ns;
    if (held != g_held_ns) {
        g_held_ns = held;
        if (g_session != nullptr && g_api.update_target != nullptr && !g_session_stale) {
            (void)g_api.update_target(g_session, held);
        } else {
            g_session_stale = true;
        }
        std::fprintf(stderr, "[perf] hint target %.2f ms%s\n", static_cast<double>(held) / 1e6,
                     held < g_target_ns ? " (a picture's slot)" : "");
    }
    // A picture's work: the step's over the slots in it.
    if (held < g_target_ns && g_target_ns > 0) {
        work_ns = work_ns * held / g_target_ns;
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
        g_session = manager != nullptr ? g_api.create_session(manager, tids.data(), tids.size(), g_held_ns) : nullptr;
        std::fprintf(stderr, "[perf] performance hint session: %zu threads, target %.2f ms%s\n", tids.size(),
                     static_cast<double>(g_held_ns) / 1e6, g_session != nullptr ? "" : " (not available)");
    }
    if (g_session == nullptr || work_ns <= 0) {
        return;
    }
    // With the newest call the session is told a picture's GPU time beside its CPU time (the renderer's estimate of a
    // generated picture, present_clock.h g_picture_gpu_ms), so the hint holds the GPU's clock as well as the CPU's.
    const double gpu_ms = galaxy::render::g_picture_gpu_ms.load(std::memory_order_relaxed);
    if (g_api.report2 != nullptr && g_api.duration_create != nullptr && g_api.duration_release != nullptr &&
        g_api.duration_start != nullptr && g_api.duration_total != nullptr && g_api.duration_cpu != nullptr &&
        g_api.duration_gpu != nullptr) {
        if (void* const duration = g_api.duration_create(); duration != nullptr) {
            const std::int64_t gpu_ns = gpu_ms > 0.0 ? static_cast<std::int64_t>(gpu_ms * 1e6) : 0;
            g_api.duration_start(duration, std::max<std::int64_t>(steady_ns() - work_ns, 1));
            g_api.duration_total(duration, std::max(work_ns, gpu_ns));
            g_api.duration_cpu(duration, work_ns);
            g_api.duration_gpu(duration, gpu_ns);
            const int refused = g_api.report2(g_session, duration);
            g_api.duration_release(duration);
            if (refused == 0) {
                return;
            }
        }
    }
    g_api.report(g_session, work_ns);
}
#endif

}  // namespace

void perf_hint_add_current_thread(const char* name) noexcept {
    work_register(name);
#if defined(__APPLE__)
    // The frame's threads are what the player waits on: the system's highest class for them, so their timers are not
    // gathered with others' (a frame's wake-up a few milliseconds late is a picture a display refresh late) and, on
    // Apple silicon, they run on the performance cores.
    (void)pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    const ThreadClock clock = pthread_mach_thread_np(pthread_self());
#else
    clockid_t clock{};
    if (pthread_getcpuclockid(pthread_self(), &clock) != 0) {
        return;
    }
#endif
    std::lock_guard<std::mutex> lock(g_mutex);
    FrameThread thread{clock, cpu_time(clock), name};
#if defined(__ANDROID__)
    thread.tid = static_cast<std::int32_t>(gettid());
    g_session_stale = true;  // made again with every thread at the next report
#endif
    t_index = static_cast<int>(g_threads.size());
    g_threads.push_back(thread);
}

void perf_hint_busy(bool on) noexcept {
    work_mark(on);
    if (t_index < 0) {
        return;
    }
    const std::int64_t now = steady_ns();
    std::lock_guard<std::mutex> lock(g_mutex);
    g_threads[static_cast<std::size_t>(t_index)].busy.mark(now, on);
}

void perf_hint_set_target(std::int64_t target_ns) noexcept {
#if defined(__ANDROID__)
    std::lock_guard<std::mutex> lock(g_mutex);
    if (target_ns <= 0 || target_ns == g_target_ns) {
        return;
    }
    g_target_ns = target_ns;
    g_held_ns = 0;  // the next report tells the session its target (the frame's, or the slot)
    std::fprintf(stderr, "[perf] frame target %.2f ms\n", static_cast<double>(target_ns) / 1e6);
#else
    (void)target_ns;
#endif
}

std::int64_t perf_hint_frame_done() noexcept {
    work_mark(true);
    const std::int64_t wall = steady_ns();
    std::lock_guard<std::mutex> lock(g_mutex);
    if (t_index >= 0) {
        g_threads[static_cast<std::size_t>(t_index)].busy.mark(wall, true);
    }
    std::int64_t work = 0;
    for (FrameThread& thread : g_threads) {
        const std::int64_t now = cpu_time(thread.clock);
        thread.last_ns = now >= 0 && thread.cpu_ns >= 0 ? std::max<std::int64_t>(now - thread.cpu_ns, 0) : -1;
        thread.cpu_ns = now;
        work = std::max(work, thread.busy.close(wall));
    }
#if defined(__ANDROID__)
    if (!g_threads.empty()) {
        report(work);
    }
#endif
    return work;
}

std::size_t perf_hint_last_work(ThreadWork* out, std::size_t capacity) noexcept {
    std::lock_guard<std::mutex> lock(g_mutex);
    const std::size_t n = std::min(capacity, g_threads.size());
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = {g_threads[i].name, g_threads[i].last_ns};
    }
    return n;
}

#else

namespace {
std::atomic<bool> g_on_battery{false};
}  // namespace

void perf_hint_add_current_thread(const char* name) noexcept {
    work_register(name);
}

void perf_hint_set_target(std::int64_t) noexcept {}

void perf_hint_busy(bool on) noexcept {
    work_mark(on);
}

std::size_t perf_hint_last_work(ThreadWork*, std::size_t) noexcept {
    return 0;
}

std::int64_t perf_hint_frame_done() noexcept {
    work_mark(true);
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

#if defined(__ANDROID__)
namespace {

// The device's thermal headroom (android/thermal.h, API 31), looked up at run time and read once a second on a thread
// of its own: AThermal_getThermalHeadroom may take a binder call, which no frame thread waits for. The system answers
// at most once a second; a refused read (NaN) keeps 0.
std::atomic<double> g_headroom{0.0};
std::once_flag g_thermal_started;

void start_thermal_reader() {
    void* library = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr) {
        return;
    }
    using Acquire = void* (*)();
    using Headroom = float (*)(void*, int);
    const auto acquire = reinterpret_cast<Acquire>(dlsym(library, "AThermal_acquireManager"));
    const auto headroom = reinterpret_cast<Headroom>(dlsym(library, "AThermal_getThermalHeadroom"));
    if (acquire == nullptr || headroom == nullptr) {
        std::fprintf(stderr, "[thermal] no thermal headroom on this system\n");
        return;
    }
    void* const manager = acquire();
    if (manager == nullptr) {
        return;
    }
    std::thread([manager, headroom] {
        for (;;) {
            // The forecast one second on: the arbiter acts before the device throttles.
            const float read = headroom(manager, 1);
            g_headroom.store(read == read && read >= 0.0f ? static_cast<double>(read) : 0.0, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }).detach();
}

}  // namespace

double thermal_headroom() noexcept {
    try {
        std::call_once(g_thermal_started, [] { start_thermal_reader(); });
    } catch (...) {
        // No thread for the reader: the headroom stays not known.
    }
    return g_headroom.load(std::memory_order_relaxed);
}
#else
// No platform here gives the app its thermal headroom to read (Windows, Linux, Apple; the benchmark's own heat measure
// is not a live input): 0, not known.
double thermal_headroom() noexcept {
    return 0.0;
}
#endif

}  // namespace galaxy::host
