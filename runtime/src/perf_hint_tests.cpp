// SPDX-License-Identifier: GPL-3.0-only
// perf_hint's frame work by thread (perf_hint_last_work): two frame threads, one busy about 40 ms of CPU and one about
// 5 ms, then a frame's end. Each must be named and given its own CPU time (the pacing line, host_main.cpp
// svc_frame_wait, prints these). The frame's work told to the hints is the busier thread's wall time between its marks
// (P-069): the render thread's 5 ms of CPU and 60 ms blocked inside its work (a GPU fence) is 65 ms, more than the game
// thread's 40. Where threads' CPU times cannot be read (Windows), the answer is empty and the frame's work 0.
#include "galaxy/perf_hint.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <time.h>

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

}  // namespace

int main() {
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
    std::printf("%s\n", ok ? "perf_hint: every case holds" : "perf_hint: FAILED");
    return ok ? 0 : 1;
}
