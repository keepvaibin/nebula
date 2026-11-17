// SPDX-License-Identifier: GPL-3.0-only
// perf_hint's frame work by thread (perf_hint_last_work): two frame threads, one busy about 40 ms of CPU and one about
// 5 ms, then a frame's end. Each must be named and given its own time, and the frame's work is the busier one's; the
// pacing line (host_main.cpp svc_frame_wait) prints these. Where threads' CPU times cannot be read (Windows), the
// answer is empty and the frame's work 0.
#include "galaxy/perf_hint.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {

// Spins on this thread's CPU for about ms milliseconds of wall time (the machine idle: about as much CPU time).
void burn(int ms) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    volatile unsigned sink = 0;
    while (std::chrono::steady_clock::now() < until) {
        for (int i = 0; i < 1000; ++i) sink = sink + static_cast<unsigned>(i);
    }
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
    auto frame_thread = [&](const char* name, int ms) {
        galaxy::host::perf_hint_add_current_thread(name);
        added.fetch_add(1);
        while (added.load() < 2) std::this_thread::yield();
        burn(ms);
        worked.fetch_add(1);
        // Alive until the frame's end has read its clock: a finished thread's CPU clock reads nothing.
        while (!read.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    };
    std::thread game(frame_thread, "game", 40);
    std::thread render(frame_thread, "render", 5);
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
        ok &= expect(work == g->cpu_ns, "the frame's work is the busier thread's");
    }
    // A capacity of one takes the first thread only.
    galaxy::host::ThreadWork one[1]{};
    ok &= expect(galaxy::host::perf_hint_last_work(one, 1) == 1, "capacity 1 writes one");
#endif
    std::printf("%s\n", ok ? "perf_hint: every case holds" : "perf_hint: FAILED");
    return ok ? 0 : 1;
}
