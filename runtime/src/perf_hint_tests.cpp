// SPDX-License-Identifier: GPL-3.0-only
// perf_hint's frame work by thread (perf_hint_last_work): two frame threads, one busy about 40 ms of CPU and one about
// 5 ms, then a frame's end. Each must be named and given its own CPU time (the pacing line, host_main.cpp
// svc_frame_wait, prints these). The frame's work told to the hints is the busier thread's wall time between its marks
// (P-069): the render thread's 5 ms of CPU and 60 ms blocked inside its work (a GPU fence) is 65 ms, more than the game
// thread's 40. Where threads' CPU times cannot be read (Windows), the answer is empty and the frame's work 0.
//
// The hint session (HintSession, G10.4) over a recording stand-in for Android's calls, on every platform: it is told once
// per cycle (two times a step where a picture's slot is half the step), not once per step; it holds every frame thread,
// a later one through setThreads; it never calls setPreferPowerEfficiency; its target is the share of the cycle (100 per
// cent the whole slot, 80 four fifths of it); a spike is told at a load; the graphics-pipeline variant is made from a
// creation config with the window bound, and only where the system has it.
#include "galaxy/perf_hint.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <time.h>
#include <vector>

namespace {

#if !defined(_WIN32)
// This thread's CPU time so far, in nanoseconds.
std::int64_t thread_cpu_ns() {
    timespec t{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return static_cast<std::int64_t>(t.tv_sec) * 1'000'000'000 + t.tv_nsec;
}
#endif

// Spins on this thread's CPU for ms milliseconds of this thread's own CPU time. A machine with every processor busy
// gives the thread less wall time, never less CPU time than asked, so the CPU times asked of the hints below hold at
// any load (N-1073). Windows has no thread CPU clock here (its answer is empty): wall time.
void burn(int ms) {
    volatile unsigned sink = 0;
#if !defined(_WIN32)
    const std::int64_t until = thread_cpu_ns() + static_cast<std::int64_t>(ms) * 1'000'000;
    while (thread_cpu_ns() < until) {
        for (int i = 0; i < 1000; ++i) sink = sink + static_cast<unsigned>(i);
    }
#else
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < until) {
        for (int i = 0; i < 1000; ++i) sink = sink + static_cast<unsigned>(i);
    }
#endif
}

bool expect(bool ok, const char* what) {
    if (!ok) std::fprintf(stderr, "FAILED: %s\n", what);
    return ok;
}

bool thread_work_case() {
    std::atomic<int> added{0};
    std::atomic<int> worked{0};
    std::atomic<bool> read{false};
    auto frame_thread = [&](const char* name, int ms, int blocked_ms) {
        galaxy::host::perf_hint_add_current_thread(name);
        added.fetch_add(1);
        while (added.load() < 2) std::this_thread::yield();
        galaxy::host::perf_hint_busy(true);
        burn(ms);
        std::this_thread::sleep_for(std::chrono::milliseconds(blocked_ms));
        galaxy::host::perf_hint_busy(false);
        worked.fetch_add(1);
        // Alive until the frame's end has read its clock: a finished thread's CPU clock reads nothing.
        while (!read.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    };
    std::thread game(frame_thread, "game", 40, 0);
    std::thread render(frame_thread, "render", 5, 60);
    while (worked.load() < 2) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const std::int64_t work = galaxy::host::perf_hint_frame_done();
    galaxy::host::ThreadWork threads[4]{};
    const std::size_t n = galaxy::host::perf_hint_last_work(threads, 4);
    read.store(true);
    game.join();
    render.join();

    bool ok = true;
#if defined(_WIN32)
    ok &= expect(n == 0, "Windows: no frame threads' times");
    ok &= expect(work == 0, "Windows: the frame's work is 0");
#else
    ok &= expect(n == 2, "two frame threads");
    if (n == 2) {
        // Added in either order: found by name.
        const galaxy::host::ThreadWork* g = std::strcmp(threads[0].name, "game") == 0 ? &threads[0] : &threads[1];
        const galaxy::host::ThreadWork* r = g == &threads[0] ? &threads[1] : &threads[0];
        ok &= expect(std::strcmp(g->name, "game") == 0 && std::strcmp(r->name, "render") == 0, "both named");
        std::printf("game %.2f ms, render %.2f ms, frame work %.2f ms\n", g->cpu_ns / 1e6, r->cpu_ns / 1e6, work / 1e6);
        ok &= expect(g->cpu_ns >= 30'000'000, "the game thread's ~40 ms counted");
        ok &= expect(r->cpu_ns >= 3'000'000 && r->cpu_ns < g->cpu_ns, "the render thread's ~5 ms counted apart");
        ok &= expect(r->cpu_ns < 30'000'000, "the render thread's blocked time is not CPU time");
        ok &= expect(work >= 60'000'000 && work > g->cpu_ns,
                     "the frame's work is the busier thread's wall time, its blocked time in it (P-069)");
    }
    // A capacity of one takes the first thread only.
    galaxy::host::ThreadWork one[1]{};
    ok &= expect(galaxy::host::perf_hint_last_work(one, 1) == 1, "capacity 1 writes one");
#endif
    return ok;
}

// What the stand-in for Android's calls was asked, one test at a time.
struct Record {
    int created = 0;                 // createSession
    int created_from_config = 0;     // createSessionUsingConfig
    int closed = 0;
    std::vector<std::int32_t> tids;  // the threads the session holds now
    std::int64_t target_ns = 0;      // its target now
    int reports = 0;                 // reportActualWorkDuration and reportActualWorkDuration2
    std::vector<std::int64_t> works;
    int set_threads = 0;
    int prefer_power_efficiency = 0;
    int spikes = 0;
    bool spike_cpu = false;
    bool spike_gpu = false;
    std::vector<void*> surfaces;  // the windows the session is bound to now
    bool config_pipeline = false;
    bool pipeline_supported = true;
};
Record g_record;
int g_session_object = 0;
int g_config_object = 0;
int g_duration_object = 0;
std::int64_t g_duration_cpu = 0;

void* fake_create(void*, const std::int32_t* tids, std::size_t size, std::int64_t target_ns) {
    ++g_record.created;
    g_record.tids.assign(tids, tids + size);
    g_record.target_ns = target_ns;
    return &g_session_object;
}
int fake_report(void*, std::int64_t actual_ns) {
    ++g_record.reports;
    g_record.works.push_back(actual_ns);
    return 0;
}
void fake_close(void*) {
    ++g_record.closed;
}
int fake_update_target(void*, std::int64_t target_ns) {
    g_record.target_ns = target_ns;
    return 0;
}
int fake_set_threads(void*, const std::int32_t* tids, std::size_t size) {
    ++g_record.set_threads;
    g_record.tids.assign(tids, tids + size);
    return 0;
}
int fake_prefer_power_efficiency(void*, bool) {
    ++g_record.prefer_power_efficiency;
    return 0;
}
void* fake_duration_create() {
    return &g_duration_object;
}
void fake_duration_release(void*) {}
void fake_duration_set(void*, std::int64_t) {}
void fake_duration_cpu(void*, std::int64_t cpu_ns) {
    g_duration_cpu = cpu_ns;
}
int fake_report2(void* session, void*) {
    return fake_report(session, g_duration_cpu);
}
int fake_spike(void*, bool cpu, bool gpu, const char*) {
    ++g_record.spikes;
    g_record.spike_cpu = cpu;
    g_record.spike_gpu = gpu;
    return 0;
}
bool fake_supported(std::int32_t feature) {
    return feature != galaxy::host::kHintFeatureGraphicsPipeline || g_record.pipeline_supported;
}
int fake_max_pipeline_threads(void*) {
    return 8;
}
void* fake_config_create() {
    return &g_config_object;
}
void fake_config_release(void*) {}
void fake_config_tids(void*, const std::int32_t* tids, std::size_t size) {
    g_record.tids.assign(tids, tids + size);
}
void fake_config_target(void*, std::int64_t target_ns) {
    g_record.target_ns = target_ns;
}
void fake_config_pipeline(void*, bool enabled) {
    g_record.config_pipeline = enabled;
}
void fake_config_surfaces(void*, void* const* windows, std::size_t size, void* const*, std::size_t) {
    g_record.surfaces.assign(windows, windows + size);
}
int fake_create_from_config(void*, void*, void** out) {
    ++g_record.created_from_config;
    *out = &g_session_object;
    return 0;
}
int fake_set_surfaces(void*, void* const* windows, std::size_t size, void* const*, std::size_t) {
    g_record.surfaces.assign(windows, windows + size);
    return 0;
}

// Android 16's calls, as the tablet has them (setThreads, the newest report, the spike; the creation config only where
// `config` is asked).
galaxy::host::HintCalls fake_calls(bool config) {
    static int manager = 0;
    galaxy::host::HintCalls calls;
    calls.manager = &manager;
    calls.create_session = fake_create;
    calls.report = fake_report;
    calls.close_session = fake_close;
    calls.update_target = fake_update_target;
    calls.set_threads = fake_set_threads;
    calls.set_prefer_power_efficiency = fake_prefer_power_efficiency;
    calls.duration_create = fake_duration_create;
    calls.duration_release = fake_duration_release;
    calls.duration_start = fake_duration_set;
    calls.duration_total = fake_duration_set;
    calls.duration_cpu = fake_duration_cpu;
    calls.duration_gpu = fake_duration_set;
    calls.report2 = fake_report2;
    calls.notify_spike = fake_spike;
    calls.feature_supported = fake_supported;
    if (config) {
        calls.max_pipeline_threads = fake_max_pipeline_threads;
        calls.config_create = fake_config_create;
        calls.config_release = fake_config_release;
        calls.config_tids = fake_config_tids;
        calls.config_target = fake_config_target;
        calls.config_graphics_pipeline = fake_config_pipeline;
        calls.config_surfaces = fake_config_surfaces;
        calls.create_from_config = fake_create_from_config;
        calls.set_surfaces = fake_set_surfaces;
    }
    return calls;
}

constexpr std::int64_t kStep = 16'666'666;   // the game step at 60
constexpr std::int64_t kSlot120 = 8'333'333;  // a picture's slot at 120 Hz
constexpr std::int64_t kSlot240 = 4'166'666;  // at 240 Hz
constexpr std::int64_t kMs = 1'000'000;

// `steps` game steps from 0: each ends with a step's work of 4 ms and the cycle's of 2 ms, and between the ends the frame
// threads mark every millisecond, a cycle closed by a mark telling 1 ms of work.
void play(galaxy::host::HintSession& session, int steps, std::int64_t slot_ns) {
    for (int step = 0; step < steps; ++step) {
        const std::int64_t begins = step * kStep;
        session.step_ends(begins, 4 * kMs, 2 * kMs, slot_ns, 0);
        for (std::int64_t at = begins + kMs; at < begins + kStep; at += kMs) {
            if (session.cycle_due(at)) {
                session.cycle_ends(at, kMs, 0);
            }
        }
    }
}

bool session_cases() {
    using galaxy::host::HintSession;
    bool ok = true;

    // Once per cycle: at 120 Hz the slot is half the step, so 10 steps are 20 reports (the old rule's 10 fails this).
    {
        g_record = {};
        HintSession session(fake_calls(false), {}, kStep);
        session.add_thread(101);
        session.add_thread(102);
        play(session, 10, kSlot120);
        ok &= expect(g_record.reports == 20, "120 Hz: one report per picture slot, two per game step");
        ok &= expect(g_record.created == 1 && g_record.tids == std::vector<std::int32_t>{101, 102},
                     "one session of both frame threads");
        ok &= expect(g_record.target_ns == kSlot120 && session.target_ns() == kSlot120,
                     "the share 100: the whole slot, as before G10.4");
        bool cycles = true;
        for (std::size_t i = 0; i < g_record.works.size(); ++i) {
            // The first report is the cycle the step's end closes (2 ms); then a slot's mark (1 ms), then the step's end.
            cycles &= g_record.works[i] == (i % 2 == 0 ? 2 * kMs : kMs);
        }
        ok &= expect(cycles, "each report is its own cycle's work, not the step's scaled");
        // A third frame thread (a worker) joins the session that holds the others: setThreads, no new session.
        session.add_thread(103);
        play(session, 1, kSlot120);
        ok &= expect(g_record.set_threads == 1 && g_record.created == 1 &&
                         g_record.tids == std::vector<std::int32_t>{101, 102, 103},
                     "a later frame thread joins through setThreads");
        session.spike("stage load");
        ok &= expect(g_record.spikes == 1 && g_record.spike_cpu && g_record.spike_gpu, "a load is told as a spike");
        ok &= expect(g_record.prefer_power_efficiency == 0, "setPreferPowerEfficiency is never called");
    }

    // The old rule (--diagnostics hint-per-step, G10.4m's planted fault): once per step, the step's work over its slots.
    {
        g_record = {};
        HintSession session(fake_calls(false), {100u, true, false}, kStep);
        session.add_thread(101);
        session.add_thread(102);
        play(session, 10, kSlot120);
        ok &= expect(g_record.reports == 10, "hint-per-step: one report per game step");
        ok &= expect(!g_record.works.empty() && g_record.works[0] == 4 * kMs * kSlot120 / kStep,
                     "hint-per-step: the step's work scaled to a slot, as before G10.4");
        ok &= expect(g_record.prefer_power_efficiency == 0, "hint-per-step: setPreferPowerEfficiency is never called");
    }

    // The share: 80 per cent is four fifths of the slot; a share changed by the frame's target is told, not a new session.
    {
        g_record = {};
        HintSession session(fake_calls(false), {80u, false, false}, kStep);
        session.add_thread(101);
        play(session, 1, kSlot120);
        ok &= expect(g_record.target_ns == kSlot120 * 4 / 5 && session.cycle_ns() == kSlot120,
                     "the share 80: four fifths of the slot");
        play(session, 2, 0);
        ok &= expect(session.cycle_ns() == kStep && g_record.target_ns == kStep * 4 / 5 && g_record.created == 1,
                     "no in-between pictures: the step is the cycle, its target told to the same session");
    }

    // At 60 Hz (no slot under the step) one report a step; at 240 Hz four.
    {
        g_record = {};
        HintSession session(fake_calls(false), {}, kStep);
        session.add_thread(101);
        play(session, 6, 0);
        ok &= expect(g_record.reports == 6 && g_record.target_ns == kStep, "60 Hz: the step is the cycle");
        g_record.reports = 0;
        play(session, 5, kSlot240);
        ok &= expect(g_record.reports == 20, "240 Hz: four reports a step");
        // A mark late past three slots' ends closes them as one cycle.
        g_record.reports = 0;
        session.step_ends(100 * kStep, kMs, kMs, kSlot240, 0);
        ok &= expect(session.cycle_due(100 * kStep + 15 * kMs), "a slot's end has passed");
        session.cycle_ends(100 * kStep + 15 * kMs, kMs, 0);
        ok &= expect(!session.cycle_due(100 * kStep + 16 * kMs) && g_record.reports == 2,
                     "a late mark closes every slot it passed, once");
    }

    // A system without setThreads: the session is made again with every thread.
    {
        g_record = {};
        galaxy::host::HintCalls calls = fake_calls(false);
        calls.set_threads = nullptr;
        HintSession session(calls, {}, kStep);
        session.add_thread(101);
        play(session, 1, 0);
        session.add_thread(102);
        play(session, 1, 0);
        ok &= expect(g_record.created == 2 && g_record.closed == 1 && g_record.tids == std::vector<std::int32_t>{101, 102},
                     "no setThreads: a new session of every thread");
    }

    // The graphics-pipeline variant (--diagnostics hint-pipeline): from a creation config, the window bound.
    {
        int window = 0;
        int window2 = 0;
        g_record = {};
        HintSession session(fake_calls(true), {100u, false, true}, kStep);
        session.add_thread(101);
        session.add_thread(102);
        session.set_surface(&window);
        play(session, 2, kSlot120);
        ok &= expect(g_record.created_from_config == 1 && g_record.created == 0 && session.pipeline() &&
                         g_record.config_pipeline && g_record.target_ns == kSlot120,
                     "hint-pipeline: a graphics-pipeline session from a creation config");
        ok &= expect(g_record.surfaces == std::vector<void*>{&window}, "hint-pipeline: the window is bound");
        session.set_surface(&window2);
        ok &= expect(g_record.surfaces == std::vector<void*>{&window2}, "hint-pipeline: a new window is bound again");
        ok &= expect(g_record.reports == 4 && g_record.prefer_power_efficiency == 0,
                     "hint-pipeline: once per cycle, never power efficiency");
    }
    {
        g_record = {};
        g_record.pipeline_supported = false;
        HintSession session(fake_calls(true), {100u, false, true}, kStep);
        session.add_thread(101);
        play(session, 1, 0);
        ok &= expect(g_record.created == 1 && g_record.created_from_config == 0 && !session.pipeline(),
                     "hint-pipeline where the system does not support it: a plain session");
    }
    {
        g_record = {};
        HintSession session(fake_calls(false), {100u, false, true}, kStep);
        session.add_thread(101);
        play(session, 1, 0);
        ok &= expect(g_record.created == 1 && !session.pipeline(), "hint-pipeline without the creation config: a plain session");
    }
    {
        g_record = {};
        HintSession session(fake_calls(true), {}, kStep);
        session.add_thread(101);
        play(session, 1, 0);
        ok &= expect(g_record.created == 1 && g_record.created_from_config == 0 && g_record.surfaces.empty(),
                     "without hint-pipeline: the plain session, no window bound");
    }

    // The target's share by the rule itself.
    ok &= expect(galaxy::host::hint_target_ns(kSlot120, 100u) == kSlot120, "100 per cent: the whole cycle");
    ok &= expect(galaxy::host::hint_target_ns(10'000'000, 80u) == 8'000'000, "80 per cent: four fifths");
    ok &= expect(galaxy::host::hint_cycle_ns(kStep, kSlot120) == kSlot120 && galaxy::host::hint_cycle_ns(kStep, 0) == kStep &&
                     galaxy::host::hint_cycle_ns(kStep, 2 * kStep) == kStep,
                 "the cycle: the slot where it is shorter than the step, else the step");
    return ok;
}

}  // namespace

int main() {
    bool ok = thread_work_case();
    ok &= session_cases();
    std::printf("%s\n", ok ? "perf_hint: every case holds" : "perf_hint: FAILED");
    return ok ? 0 : 1;
}
