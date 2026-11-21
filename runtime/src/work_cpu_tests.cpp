// SPDX-License-Identifier: GPL-3.0-only
// The frame threads' processor time spent working (perf_hint.h WorkCpu and perf_hint_work_cpu): the span of a thread's own
// processor clock from a mark of work to the next mark of waiting. Waiting (a sleep, a timer's spin outside the marks) is not
// in it. The arithmetic with clock values handed in, then two threads marking for real.
#include "galaxy/perf_hint.h"
#include "galaxy/platform_time.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {

bool expect(bool ok, const char* what) {
    if (!ok) std::fprintf(stderr, "FAILED: %s\n", what);
    return ok;
}

// Spins this thread for `ms` milliseconds of the clock (a thread that is not descheduled gets as much processor time).
void spin(int ms) {
    volatile std::uint64_t sink = 0;
    const std::int64_t until = galaxy::qpc_now() + galaxy::qpc_frequency() / 1000 * ms;
    while (galaxy::qpc_now() < until) sink = sink + 1;
}

}  // namespace

int main() {
    bool ok = true;

    // The arithmetic: units between a mark of work and the next of waiting, added up.
    {
        galaxy::host::WorkCpu w;
        ok &= expect(w.total() == 0, "nothing worked: 0");
        w.mark(100, true);
        w.mark(160, false);
        ok &= expect(w.total() == 60, "100 to 160: 60");
        w.mark(200, false);  // waiting again: nothing changes
        w.mark(300, true);
        w.mark(350, true);   // working again: the span keeps its start
        w.mark(400, false);
        ok &= expect(w.total() == 60 + 100, "300 to 400 with a repeated mark of work: 60 + 100");
        w.mark(500, true);
        ok &= expect(w.total() == 160, "work still going is not in the total");
        w.mark(450, false);  // a clock that went back: no negative span
        ok &= expect(w.total() == 160, "a span that ends before it began adds 0");
    }

    // Two frame threads, marking for real: "game" works 60 ms and waits 100 ms asleep, then works 40 ms; "render" works 30 ms
    // only. The sleeps and the time outside the marks are not in the totals.
    if (galaxy::process_cpu_known()) {
        std::atomic<int> added{0}, marked{0};
        std::atomic<bool> read{false};
        const auto game = [&] {
            galaxy::host::perf_hint_add_current_thread("game");
            added.fetch_add(1);
            while (added.load() < 2) std::this_thread::yield();
            spin(20);  // outside the marks: not work
            galaxy::host::perf_hint_busy(true);
            spin(60);
            galaxy::host::perf_hint_busy(false);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            galaxy::host::perf_hint_frame_done();  // marks work on (the step's work starts here)
            spin(40);
            galaxy::host::perf_hint_busy(false);
            marked.fetch_add(1);
            while (!read.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        };
        const auto render = [&] {
            galaxy::host::perf_hint_add_current_thread("render");
            added.fetch_add(1);
            while (added.load() < 2) std::this_thread::yield();
            galaxy::host::perf_hint_busy(true);
            spin(30);
            galaxy::host::perf_hint_busy(false);
            spin(50);  // outside the marks
            marked.fetch_add(1);
            while (!read.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        };
        const galaxy::ProcessCpuStamp before = galaxy::process_cpu_stamp();
        std::thread a(game), b(render);
        while (marked.load() < 2) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        galaxy::ThreadCpuSample samples[4];
        const std::size_t n = galaxy::host::perf_hint_work_cpu(samples, 4);
        const galaxy::ProcessCpuStamp after = galaxy::process_cpu_stamp();
        galaxy::ThreadCpuSample one[1];
        const std::size_t n_one = galaxy::host::perf_hint_work_cpu(one, 1);
        read.store(true);
        a.join();
        b.join();
        // The threads add themselves as they start, in either order: capacity 1 gives the first of them, whichever it is.
        ok &= expect(n == 2 && n_one == 1 && (std::strcmp(one[0].name, "game") == 0 || std::strcmp(one[0].name, "render") == 0) &&
                         std::strcmp(one[0].name, samples[0].name) == 0,
                     "two frame threads; capacity 1 gives the first one added");
        if (n == 2) {
            const galaxy::ThreadCpuSample* g = std::strcmp(samples[0].name, "game") == 0 ? &samples[0] : &samples[1];
            const galaxy::ThreadCpuSample* r = g == &samples[0] ? &samples[1] : &samples[0];
            const double game_ms = static_cast<double>(galaxy::cpu_units_to_ns(g->units, before, after)) / 1e6;
            const double render_ms = static_cast<double>(galaxy::cpu_units_to_ns(r->units, before, after)) / 1e6;
            std::printf("game %.2f ms (60 + 40 worked), render %.2f ms (30 worked)\n", game_ms, render_ms);
            // A thread descheduled for a moment gets less processor time than the clock gave it, never more: the totals are the
            // work (60 + 40 and 30 ms) and not the 100 ms asleep or the 20 and 50 ms outside the marks.
            ok &= expect(game_ms >= 80.0 && game_ms <= 115.0, "game: its 60 + 40 ms of work, not its sleep or the spin outside the marks");
            ok &= expect(render_ms >= 22.0 && render_ms <= 38.0, "render: its 30 ms of work, not the 50 ms outside the marks");
        }
    }

    // What the marks cost: a frame thread makes two marks a step (the game thread) or a span (the render thread), each reading its
    // own processor clock. Timed here on a frame thread, in nanoseconds a mark.
    if (galaxy::process_cpu_known()) {
        double ns_per_mark = 0.0;
        std::thread cost([&] {
            galaxy::host::perf_hint_add_current_thread("cost");
            const int pairs = 200000;
            const std::int64_t began = galaxy::qpc_now();
            for (int i = 0; i < pairs; ++i) {
                galaxy::host::perf_hint_busy(true);
                galaxy::host::perf_hint_busy(false);
            }
            const std::int64_t ticks = galaxy::qpc_now() - began;
            ns_per_mark = static_cast<double>(galaxy::qpc_to_ns(ticks)) / (2.0 * pairs);
        });
        cost.join();
        std::printf("a mark of work or of waiting costs %.0f ns (it reads the thread's processor clock)\n", ns_per_mark);
        ok &= expect(ns_per_mark > 0.0 && ns_per_mark < 20000.0, "a mark costs under 20 microseconds");
    }

    std::printf("%s\n", ok ? "work_cpu: every case holds" : "work_cpu: FAILED");
    return ok ? 0 : 1;
}
