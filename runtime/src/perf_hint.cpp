// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/perf_hint.h"
#include "galaxy/game_step.h"
#include "galaxy/render/present_clock.h"

#include <atomic>
#include <cstdio>
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
#include "galaxy/launch_args.h"

#include <android/native_window.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>

#include <condition_variable>
#include <cstdio>
#include <iterator>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
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

HintSession::HintSession(const HintCalls& calls, Options options, std::int64_t frame_ns) noexcept
    : calls_(calls), options_(options), frame_ns_(frame_ns) {}

HintSession::~HintSession() {
    if (session_ != nullptr && calls_.close_session != nullptr) {
        calls_.close_session(session_);
    }
}

void HintSession::add_thread(std::int32_t tid) {
    if (std::find(tids_.begin(), tids_.end(), tid) != tids_.end()) {
        return;
    }
    tids_.push_back(tid);
    if (session_ == nullptr || stale_) {
        stale_ = true;
        return;
    }
    // A graphics-pipeline session over the system's count of such threads has no defined behaviour: made again, plain.
    const bool over = pipeline_ && calls_.max_pipeline_threads != nullptr &&
                      static_cast<int>(tids_.size()) > calls_.max_pipeline_threads(calls_.manager);
    if (!over && calls_.set_threads != nullptr && calls_.set_threads(session_, tids_.data(), tids_.size()) == 0) {
        std::fprintf(stderr, "[perf] performance hint session: %zu threads\n", tids_.size());
        return;
    }
    stale_ = true;
}

void HintSession::set_surface(void* window) noexcept {
    surface_ = window;
    if (pipeline_ && session_ != nullptr && calls_.set_surfaces != nullptr) {
        void* const windows[1] = {window};
        (void)calls_.set_surfaces(session_, window != nullptr ? windows : nullptr, window != nullptr ? 1u : 0u, nullptr, 0u);
    }
}

void HintSession::hold(std::int64_t cycle_ns) {
    const std::int64_t target = hint_target_ns(cycle_ns, options_.share_percent);
    if (cycle_ns <= 0 || target <= 0 || (cycle_ns == cycle_ns_ && target == target_ns_)) {
        return;
    }
    cycle_ns_ = cycle_ns;
    target_ns_ = target;
    if (session_ != nullptr && !stale_ && calls_.update_target != nullptr) {
        (void)calls_.update_target(session_, target);
    } else {
        stale_ = true;
    }
    std::fprintf(stderr, "[perf] hint target %.2f ms (%u%% of %s %.2f ms)\n", static_cast<double>(target) / 1e6,
                 options_.share_percent, cycle_ns < frame_ns_ ? "a picture's slot" : "the frame",
                 static_cast<double>(cycle_ns) / 1e6);
}

void HintSession::make() {
    stale_ = false;
    if (session_ != nullptr) {
        calls_.close_session(session_);
        session_ = nullptr;
    }
    pipeline_ = false;
    if (tids_.empty() || calls_.manager == nullptr || calls_.create_session == nullptr || calls_.report == nullptr ||
        calls_.close_session == nullptr || target_ns_ <= 0) {
        return;
    }
    const char* plain_why = nullptr;  // why the graphics-pipeline session asked for is not made
    int refused = 0;
    if (options_.graphics_pipeline) {
        if (calls_.create_from_config == nullptr || calls_.config_create == nullptr || calls_.config_release == nullptr ||
            calls_.config_tids == nullptr || calls_.config_target == nullptr || calls_.config_graphics_pipeline == nullptr ||
            calls_.config_surfaces == nullptr || calls_.set_surfaces == nullptr) {
            plain_why = "the system has no creation config, API 36";
        } else if (calls_.feature_supported != nullptr && !calls_.feature_supported(kHintFeatureGraphicsPipeline)) {
            plain_why = "the system does not support it";
        } else if (calls_.max_pipeline_threads != nullptr &&
                   static_cast<int>(tids_.size()) > calls_.max_pipeline_threads(calls_.manager)) {
            plain_why = "more frame threads than the system's graphics-pipeline threads";
        } else if (void* const config = calls_.config_create(); config == nullptr) {
            plain_why = "no creation config";
        } else {
            calls_.config_tids(config, tids_.data(), tids_.size());
            calls_.config_target(config, target_ns_);
            calls_.config_graphics_pipeline(config, true);
            void* const windows[1] = {surface_};
            calls_.config_surfaces(config, surface_ != nullptr ? windows : nullptr, surface_ != nullptr ? 1u : 0u, nullptr, 0u);
            // The session out stays null where the system's call is the older form without it: a plain session then.
            void* made = nullptr;
            refused = calls_.create_from_config(calls_.manager, config, &made);
            calls_.config_release(config);
            if (refused == 0 && made != nullptr) {
                session_ = made;
                pipeline_ = true;
            } else {
                if (made != nullptr) {
                    calls_.close_session(made);  // EBUSY still makes one, with no defined behaviour
                }
                plain_why = "the system refused it";
            }
        }
        if (plain_why != nullptr && refused != 0) {
            std::fprintf(stderr, "[perf] graphics-pipeline hint session not made (%s, answer %d): a plain session\n",
                         plain_why, refused);
        } else if (plain_why != nullptr) {
            std::fprintf(stderr, "[perf] graphics-pipeline hint session not made (%s): a plain session\n", plain_why);
        }
    }
    if (session_ == nullptr) {
        session_ = calls_.create_session(calls_.manager, tids_.data(), tids_.size(), target_ns_);
    }
    std::fprintf(stderr, "[perf] performance hint session: %zu threads, target %.2f ms%s%s\n", tids_.size(),
                 static_cast<double>(target_ns_) / 1e6, session_ != nullptr ? "" : " (not available)",
                 pipeline_ ? (surface_ != nullptr ? ", graphics pipeline, surface bound" : ", graphics pipeline, no surface yet")
                           : "");
}

void HintSession::tell(std::int64_t now_ns, std::int64_t work_ns, std::int64_t gpu_ns) {
    if (session_ == nullptr || work_ns <= 0) {
        return;
    }
    // With the newest call the session is told a picture's GPU time beside the cycle's CPU time, so the hint holds the
    // GPU's clock as well as the CPU's. A graphics-pipeline session's total is the pipeline's, from the start of the
    // frame's work until its picture is drawn.
    gpu_ns = std::max<std::int64_t>(gpu_ns, 0);
    if (calls_.report2 != nullptr && calls_.duration_create != nullptr && calls_.duration_release != nullptr &&
        calls_.duration_start != nullptr && calls_.duration_total != nullptr && calls_.duration_cpu != nullptr &&
        calls_.duration_gpu != nullptr) {
        if (void* const duration = calls_.duration_create(); duration != nullptr) {
            calls_.duration_start(duration, std::max<std::int64_t>(now_ns - work_ns, 1));
            calls_.duration_total(duration, pipeline_ ? work_ns + gpu_ns : std::max(work_ns, gpu_ns));
            calls_.duration_cpu(duration, work_ns);
            calls_.duration_gpu(duration, gpu_ns);
            const int refused = calls_.report2(session_, duration);
            calls_.duration_release(duration);
            if (refused == 0) {
                return;
            }
        }
    }
    (void)calls_.report(session_, work_ns);
}

void HintSession::step_ends(std::int64_t now_ns, std::int64_t step_work_ns, std::int64_t cycle_work_ns,
                            std::int64_t slot_ns, std::int64_t gpu_ns) {
    hold(hint_cycle_ns(frame_ns_, slot_ns));
    if (stale_) {
        make();
    }
    left_ = 0;
    if (cycle_ns_ <= 0) {
        return;
    }
    if (options_.per_step) {
        // As before G10.4 (its planted fault until G10.4m chooses): once per step, a picture's work the step's over the
        // slots in it.
        tell(now_ns, cycle_ns_ < frame_ns_ && frame_ns_ > 0 ? step_work_ns * cycle_ns_ / frame_ns_ : step_work_ns, gpu_ns);
        return;
    }
    tell(now_ns, cycle_work_ns, gpu_ns);
    // The next step's further slots, each closed by the first mark after its end.
    if (cycle_ns_ < frame_ns_) {
        left_ = static_cast<int>((frame_ns_ + cycle_ns_ / 2) / cycle_ns_) - 1;
        due_ns_ = now_ns + cycle_ns_;
    }
}

void HintSession::cycle_ends(std::int64_t now_ns, std::int64_t work_ns, std::int64_t gpu_ns) {
    // A mark late past more than one slot's end closes them all, as one cycle.
    while (left_ > 0 && now_ns >= due_ns_) {
        due_ns_ += cycle_ns_;
        --left_;
    }
    tell(now_ns, work_ns, gpu_ns);
}

void HintSession::spike(const char* what) noexcept {
    if (session_ == nullptr || calls_.notify_spike == nullptr) {
        return;
    }
    const int refused = calls_.notify_spike(session_, true, true, what);
    std::fprintf(stderr, "[perf] hint workload spike (%s)%s\n", what, refused == 0 ? "" : " refused");
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
    BusyWindow busy{};          // its work's wall time over the step (perf_hint_busy)
#if defined(__ANDROID__)
    BusyWindow cycle{};         // the same over the hint's cycle (HintSession)
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
// The frame's target duration: the game step until the frame clock says otherwise (perf_hint_set_target).
std::int64_t g_frame_ns = galaxy::kGameStepNs;

// The session (HintSession over APerformanceHint, android/performance_hint.h, looked up at run time: the app also runs on
// Android 12, before it), made at the first step's end, once the command line is read; never freed (the frame threads
// may mark until the process ends). Null where the system has no hint sessions.
HintSession* g_hint = nullptr;
bool g_hint_tried = false;
// The window bound to the graphics-pipeline session, held (ANativeWindow_acquire) while the session may use it.
ANativeWindow* g_surface = nullptr;

HintCalls load_calls() {
    HintCalls calls;
    void* library = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr) {
        return calls;
    }
    const auto find = [library](auto& to, const char* name) {
        to = reinterpret_cast<std::remove_reference_t<decltype(to)>>(dlsym(library, name));
    };
    void* (*get_manager)() = nullptr;
    find(get_manager, "APerformanceHint_getManager");
    find(calls.create_session, "APerformanceHint_createSession");
    find(calls.report, "APerformanceHint_reportActualWorkDuration");
    find(calls.close_session, "APerformanceHint_closeSession");
    find(calls.update_target, "APerformanceHint_updateTargetWorkDuration");
    find(calls.set_threads, "APerformanceHint_setThreads");
    find(calls.set_prefer_power_efficiency, "APerformanceHint_setPreferPowerEfficiency");
    find(calls.duration_create, "AWorkDuration_create");
    find(calls.duration_release, "AWorkDuration_release");
    find(calls.duration_start, "AWorkDuration_setWorkPeriodStartTimestampNanos");
    find(calls.duration_total, "AWorkDuration_setActualTotalDurationNanos");
    find(calls.duration_cpu, "AWorkDuration_setActualCpuDurationNanos");
    find(calls.duration_gpu, "AWorkDuration_setActualGpuDurationNanos");
    find(calls.report2, "APerformanceHint_reportActualWorkDuration2");
    find(calls.notify_spike, "APerformanceHint_notifyWorkloadSpike");
    find(calls.feature_supported, "APerformanceHint_isFeatureSupported");
    find(calls.max_pipeline_threads, "APerformanceHint_getMaxGraphicsPipelineThreadsCount");
    find(calls.config_create, "ASessionCreationConfig_create");
    find(calls.config_release, "ASessionCreationConfig_release");
    find(calls.config_tids, "ASessionCreationConfig_setTids");
    find(calls.config_target, "ASessionCreationConfig_setTargetWorkDurationNanos");
    find(calls.config_graphics_pipeline, "ASessionCreationConfig_setGraphicsPipeline");
    find(calls.config_surfaces, "ASessionCreationConfig_setNativeSurfaces");
    find(calls.create_from_config, "APerformanceHint_createSessionUsingConfig");
    find(calls.set_surfaces, "APerformanceHint_setNativeSurfaces");
    calls.manager = get_manager != nullptr ? get_manager() : nullptr;
    return calls;
}

// The session, made the first time a step ends (g_mutex held); null where the system has none.
HintSession* hint() {
    if (!g_hint_tried) {
        g_hint_tried = true;
        const HintCalls calls = load_calls();
        if (calls.manager != nullptr && calls.create_session != nullptr && calls.report != nullptr &&
            calls.close_session != nullptr) {
            HintSession::Options options;
            options.share_percent = galaxy::launch_args().hint_share;
            options.per_step = galaxy::diagnosing(galaxy::kDiagnoseHintPerStep);
            options.graphics_pipeline = galaxy::diagnosing(galaxy::kDiagnoseHintPipeline);
            g_hint = new HintSession(calls, options, g_frame_ns);
            for (const FrameThread& thread : g_threads) {
                g_hint->add_thread(thread.tid);
            }
            g_hint->set_surface(g_surface);
        }
    }
    return g_hint;
}

// A picture's slot and its GPU time (the renderer's estimate of a generated picture, present_clock.h g_picture_gpu_ms),
// as the session is told them.
std::int64_t slot_ns() {
    const std::int64_t slot_qpc = galaxy::render::g_slot_period_qpc.load(std::memory_order_relaxed);
    return slot_qpc > 0 ? galaxy::qpc_to_ns(slot_qpc) : 0;
}
std::int64_t picture_gpu_ns() {
    const double gpu_ms = galaxy::render::g_picture_gpu_ms.load(std::memory_order_relaxed);
    return gpu_ms > 0.0 ? static_cast<std::int64_t>(gpu_ms * 1e6) : 0;
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
    if (g_hint != nullptr) {
        g_hint->add_thread(thread.tid);  // the session holds it from the next report on
    }
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
    FrameThread& self = g_threads[static_cast<std::size_t>(t_index)];
    self.busy.mark(now, on);
#if defined(__ANDROID__)
    self.cycle.mark(now, on);
    // The first mark after a slot's end inside the step tells the session that slot's cycle.
    if (g_hint != nullptr && g_hint->cycle_due(now)) {
        std::int64_t work = 0;
        for (FrameThread& thread : g_threads) {
            work = std::max(work, thread.cycle.close(now));
        }
        g_hint->cycle_ends(now, work, picture_gpu_ns());
    }
#endif
}

void perf_hint_set_target(std::int64_t target_ns) noexcept {
#if defined(__ANDROID__)
    std::lock_guard<std::mutex> lock(g_mutex);
    if (target_ns <= 0 || target_ns == g_frame_ns) {
        return;
    }
    g_frame_ns = target_ns;
    if (g_hint != nullptr) {
        g_hint->set_frame(target_ns);  // the session's cycle and target follow at the next step's end
    }
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
#if defined(__ANDROID__)
        g_threads[static_cast<std::size_t>(t_index)].cycle.mark(wall, true);
#endif
    }
    std::int64_t work = 0;
#if defined(__ANDROID__)
    std::int64_t cycle_work = 0;
#endif
    for (FrameThread& thread : g_threads) {
        const std::int64_t now = cpu_time(thread.clock);
        thread.last_ns = now >= 0 && thread.cpu_ns >= 0 ? std::max<std::int64_t>(now - thread.cpu_ns, 0) : -1;
        thread.cpu_ns = now;
        work = std::max(work, thread.busy.close(wall));
#if defined(__ANDROID__)
        cycle_work = std::max(cycle_work, thread.cycle.close(wall));
#endif
    }
#if defined(__ANDROID__)
    if (!g_threads.empty()) {
        if (HintSession* const session = hint(); session != nullptr) {
            session->step_ends(wall, work, cycle_work, slot_ns(), picture_gpu_ns());
        }
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

void perf_hint_workload_spike(const char* what) noexcept {
#if defined(__ANDROID__)
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_hint != nullptr) {
        g_hint->spike(what);
    }
#else
    (void)what;
#endif
}

void perf_hint_set_surface(void* native_window) noexcept {
#if defined(__ANDROID__)
    auto* const window = static_cast<ANativeWindow*>(native_window);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (window == g_surface) {
        return;
    }
    // Held while the session may use it: the old one is let go only once the session has the new.
    if (window != nullptr) {
        ANativeWindow_acquire(window);
    }
    if (g_hint != nullptr) {
        g_hint->set_surface(window);
    }
    if (g_surface != nullptr) {
        ANativeWindow_release(g_surface);
    }
    g_surface = window;
#else
    (void)native_window;
#endif
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

void perf_hint_workload_spike(const char*) noexcept {}

void perf_hint_set_surface(void*) noexcept {}

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
// at most once a second; a refused read (NaN) keeps 0. The same thread writes the platform's offer (perf_hint_report_platform).
std::atomic<double> g_headroom{0.0};
std::once_flag g_thermal_started;
// The reader's two jobs: the headroom, from the first thermal_headroom on (the arbiter's), and a report of the platform's offer
// when one is asked. g_reader_wake wakes it at once for either.
std::mutex g_reader_mutex;
std::condition_variable g_reader_wake;
bool g_headroom_wanted = false;
bool g_platform_wanted = false;

using Acquire = void* (*)();
using Headroom = float (*)(void*, int);
// android/thermal.h AThermalHeadroomThreshold (API 35), declared here: the header hides it below the app's minimum API.
struct HeadroomThreshold {
    float headroom;
    int status;  // AThermalStatus
};
using Thresholds = int (*)(void*, const HeadroomThreshold**, std::size_t*);

// The features of android/performance_hint.h APerformanceHintFeature (API 36), in the NDK's order, as the log names them.
constexpr const char* kHintFeatures[] = {"sessions", "power-efficiency", "surface-binding", "graphics-pipeline", "auto-cpu",
                                         "auto-gpu"};
// The newer calls of the hint and the heat (their API levels: the NDK's __INTRODUCED_IN), looked for by name.
constexpr const char* kNewerCalls[] = {
    "APerformanceHint_setThreads",               "APerformanceHint_setPreferPowerEfficiency",
    "APerformanceHint_reportActualWorkDuration2", "APerformanceHint_notifyWorkloadIncrease",
    "APerformanceHint_notifyWorkloadSpike",       "APerformanceHint_notifyWorkloadReset",
    "APerformanceHint_createSessionUsingConfig",  "ASessionCreationConfig_setGraphicsPipeline",
    "ASessionCreationConfig_setUseAutoTiming",    "APerformanceHint_setNativeSurfaces",
    "APerformanceHint_isFeatureSupported",        "AThermal_getThermalHeadroomThresholds"};
// android/thermal.h AThermalStatus 0 to 6, as the log names them.
constexpr const char* kThermalStatus[] = {"none", "light", "moderate", "severe", "critical", "emergency", "shutdown"};

void report_hint_features(void* library) {
    using Supported = bool (*)(std::int32_t);
    const auto supported = reinterpret_cast<Supported>(dlsym(library, "APerformanceHint_isFeatureSupported"));
    if (supported == nullptr) {
        std::fprintf(stderr, "[platform] hint-features: not supported (the system has no APerformanceHint_isFeatureSupported, "
                             "API 36)\n");
    } else {
        std::string line;
        for (std::size_t i = 0; i < std::size(kHintFeatures); ++i) {
            line += std::string(" ") + kHintFeatures[i] + (supported(static_cast<std::int32_t>(i)) ? "=yes" : "=no");
        }
        std::fprintf(stderr, "[platform] hint-features:%s\n", line.c_str());
    }
    std::string has;
    std::string lacks;
    for (const char* call : kNewerCalls) {
        std::string& to = dlsym(library, call) != nullptr ? has : lacks;
        to += to.empty() ? call : std::string(", ") + call;
    }
    std::fprintf(stderr, "[platform] hint-calls: present: %s; absent: %s\n", has.empty() ? "none" : has.c_str(),
                 lacks.empty() ? "none" : lacks.c_str());
}

void report_thermal_thresholds(void* library, void* manager) {
    const auto thresholds = reinterpret_cast<Thresholds>(dlsym(library, "AThermal_getThermalHeadroomThresholds"));
    if (thresholds == nullptr) {
        std::fprintf(stderr, "[platform] thermal-thresholds: not supported (the system has no "
                             "AThermal_getThermalHeadroomThresholds, API 35)\n");
        return;
    }
    if (manager == nullptr) {
        std::fprintf(stderr, "[platform] thermal-thresholds: not supported (no thermal manager)\n");
        return;
    }
    // The list is the manager's own, cached by it: not freed here.
    const HeadroomThreshold* list = nullptr;
    std::size_t size = 0;
    const int error = thresholds(manager, &list, &size);
    if (error != 0 || list == nullptr || size == 0) {
        std::fprintf(stderr, "[platform] thermal-thresholds: not supported (the call answered %d with %zu thresholds)\n", error,
                     list != nullptr ? size : std::size_t{0});
        return;
    }
    std::string line;
    char one[48];
    for (std::size_t i = 0; i < size; ++i) {
        const int status = list[i].status;
        if (status >= 0 && static_cast<std::size_t>(status) < std::size(kThermalStatus)) {
            std::snprintf(one, sizeof one, " %s=%.3f", kThermalStatus[status], static_cast<double>(list[i].headroom));
        } else {
            std::snprintf(one, sizeof one, " status-%d=%.3f", status, static_cast<double>(list[i].headroom));
        }
        line += one;
    }
    std::fprintf(stderr, "[platform] thermal-thresholds:%s\n", line.c_str());
}

void start_thermal_reader() {
    void* library = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr) {
        std::fprintf(stderr, "[platform] hint-features: not supported (no libandroid.so)\n");
        std::fprintf(stderr, "[platform] thermal-thresholds: not supported (no libandroid.so)\n");
        return;
    }
    const auto acquire = reinterpret_cast<Acquire>(dlsym(library, "AThermal_acquireManager"));
    const auto headroom = reinterpret_cast<Headroom>(dlsym(library, "AThermal_getThermalHeadroom"));
    if (acquire == nullptr || headroom == nullptr) {
        std::fprintf(stderr, "[thermal] no thermal headroom on this system\n");
    }
    void* const manager = acquire != nullptr ? acquire() : nullptr;
    const Headroom read_headroom = manager != nullptr ? headroom : nullptr;
    std::thread([library, manager, read_headroom] {
        // The headroom's next read: at once when it is first wanted, then once a second, as before the offer shared the thread.
        std::chrono::steady_clock::time_point next_read{};
        for (;;) {
            bool platform = false;
            bool heat = false;
            {
                std::unique_lock<std::mutex> lock(g_reader_mutex);
                const auto due = [&next_read] {
                    return g_platform_wanted || (g_headroom_wanted && std::chrono::steady_clock::now() >= next_read);
                };
                if (g_headroom_wanted) {
                    g_reader_wake.wait_until(lock, next_read, due);
                } else {
                    g_reader_wake.wait(lock, due);
                }
                platform = std::exchange(g_platform_wanted, false);
                heat = g_headroom_wanted && std::chrono::steady_clock::now() >= next_read;
            }
            if (platform) {
                report_hint_features(library);
                report_thermal_thresholds(library, manager);
            }
            if (heat) {
                next_read = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                if (read_headroom != nullptr) {
                    // The forecast one second on: the arbiter acts before the device throttles.
                    const float read = read_headroom(manager, 1);
                    g_headroom.store(read == read && read >= 0.0f ? static_cast<double>(read) : 0.0,
                                     std::memory_order_relaxed);
                }
            }
        }
    }).detach();
}

// The reader once, from whichever asks first; no thread for it leaves the headroom not known and the offer unsaid.
void start_reader_once() noexcept {
    try {
        std::call_once(g_thermal_started, [] { start_thermal_reader(); });
    } catch (...) {
    }
}

// Asks the reader for a report of the offer (`platform`) or for the headroom, and wakes it.
void want(bool platform) noexcept {
    {
        std::lock_guard<std::mutex> lock(g_reader_mutex);
        (platform ? g_platform_wanted : g_headroom_wanted) = true;
    }
    g_reader_wake.notify_one();
}

// The arbiter has asked for the headroom once: later asks only read it (no lock on the frame's path).
std::atomic<bool> g_headroom_asked{false};

}  // namespace

double thermal_headroom() noexcept {
    if (!g_headroom_asked.load(std::memory_order_relaxed) && !g_headroom_asked.exchange(true, std::memory_order_relaxed)) {
        want(false);
        start_reader_once();
    }
    return g_headroom.load(std::memory_order_relaxed);
}

void perf_hint_report_platform() noexcept {
    want(true);
    start_reader_once();
}
#else
// No platform here gives the app its thermal headroom to read (Windows, Linux, Apple; the benchmark's own heat measure
// is not a live input): 0, not known.
double thermal_headroom() noexcept {
    return 0.0;
}

// No hint or heat calls to report here.
void perf_hint_report_platform() noexcept {}
#endif

}  // namespace galaxy::host
