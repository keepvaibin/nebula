// SPDX-License-Identifier: GPL-3.0-only
// The process's worker threads (worker_pool.h, P-040): every item of a pass runs once and only items below its count,
// the caller sees every item's writes when run() returns, passes of every size follow each other at once (a thread that
// woke late for one pass claims nothing of the next), callers on several threads at once (more than the pool's slots)
// each get their own items done, and a pool of no threads runs the pass on the caller. An urgent pass published while
// every thread is inside a long normal pass is taken by those threads between their ranges and ends long before it, and
// a pass wakes at most half as many threads as it has ranges.
//
// --bench: what a pass costs on the pool against its caller alone, for items of a few costs and counts, with the
// threads awake (passes back to back) and asleep (passes 5 ms apart): the item counts above which a pass gains, which
// the game's passes take as their ranges' size.
#include "galaxy/worker_pool.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++g_failures;
    }
}

constexpr std::uint32_t kRoom = 4096u;

struct Pass {
    std::uint32_t* items;  // kRoom + a guard: what a range writes
    std::uint32_t stamp;   // this pass's mark
    std::atomic<std::uint32_t> ranges{0u};
};

// Each item gets this pass's mark, once: an item already marked, or one past the count, is a fault the caller finds.
void mark(void* context, std::uint32_t first, std::uint32_t last) {
    Pass* const pass = static_cast<Pass*>(context);
    for (std::uint32_t i = first; i < last; ++i) {
        pass->items[i] = pass->items[i] == pass->stamp ? 0xDEADu : pass->stamp;
    }
    pass->ranges.fetch_add(1u, std::memory_order_relaxed);
}

std::atomic<unsigned> g_started{0u};

// Busy for `micros` (a sleep's length depends on the system's timer, a spin's does not).
void spin_for(int micros) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(micros);
    while (std::chrono::steady_clock::now() < until) {
    }
}

struct Urgent {
    std::thread::id caller;
    std::atomic<std::uint32_t> by_pool{0u};
};

void started(unsigned) {
    g_started.fetch_add(1u, std::memory_order_relaxed);
}

// `passes` passes back to back, sizes from the generator; every item checked after each.
void passes(galaxy::WorkerPool& pool, unsigned passes, std::uint32_t most, std::uint32_t grain_most, const char* what) {
    std::vector<std::uint32_t> items(kRoom + 64u, 0u);
    std::uint32_t seed = 12345u;
    auto next = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return seed >> 8u;
    };
    bool ok = true;
    for (unsigned p = 1; p <= passes && ok; ++p) {
        const std::uint32_t count = next() % (most + 1u);
        const std::uint32_t grain = 1u + next() % grain_most;
        Pass pass{items.data(), p};
        pool.run(count, grain, &mark, &pass);
        for (std::uint32_t i = 0; i < kRoom + 64u; ++i) {
            const bool inside = i < count;
            if (inside ? items[i] != p : items[i] == p) {
                std::fprintf(stderr, "pass %u (count %u, grain %u): item %u holds %u\n", p, count, grain, i, items[i]);
                ok = false;
                break;
            }
        }
    }
    check(ok, what);
}

// Work of about `iterations` steps an item, its result kept so the compiler keeps the loop.
struct Busy {
    std::uint32_t iterations;
    std::vector<std::uint32_t>* out;
};

void busy(void* context, std::uint32_t first, std::uint32_t last) {
    const Busy* const job = static_cast<const Busy*>(context);
    for (std::uint32_t i = first; i < last; ++i) {
        std::uint32_t x = i + 1u;
        for (std::uint32_t k = 0; k < job->iterations; ++k) x = x * 1664525u + 1013904223u;
        (*job->out)[i] = x;
    }
}

double median_us(std::vector<double>& times) {
    std::sort(times.begin(), times.end());
    return times[times.size() / 2u];
}

int bench() {
    const unsigned threads = galaxy::WorkerPool::threads_for(std::thread::hardware_concurrency());
    galaxy::WorkerPool pool(threads, nullptr);
    galaxy::WorkerPool none(0u, nullptr);
    std::printf("bench: %u processors, %u worker threads\n", std::thread::hardware_concurrency(), threads);
    std::vector<std::uint32_t> out(1u << 16u);
    using clock = std::chrono::steady_clock;
    for (const std::uint32_t iterations : {8u, 64u, 512u}) {
        Busy job{iterations, &out};
        // The item's cost, from a long pass on the caller.
        const auto t0 = clock::now();
        none.run(65536u, 65536u, &busy, &job);
        const double item_ns = std::chrono::duration<double, std::nano>(clock::now() - t0).count() / 65536.0;
        for (const std::uint32_t count : {16u, 64u, 256u, 1024u, 4096u}) {
            for (const std::uint32_t grain : {1u, 4u, 16u, 64u, 256u}) {
                if (grain >= count) continue;
                std::vector<double> alone, awake, asleep;
                for (int r = 0; r < 301; ++r) {
                    auto a = clock::now();
                    none.run(count, grain, &busy, &job);
                    alone.push_back(std::chrono::duration<double, std::micro>(clock::now() - a).count());
                    a = clock::now();
                    pool.run(count, grain, &busy, &job);
                    awake.push_back(std::chrono::duration<double, std::micro>(clock::now() - a).count());
                }
                for (int r = 0; r < 41; ++r) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    const auto a = clock::now();
                    pool.run(count, grain, &busy, &job);
                    asleep.push_back(std::chrono::duration<double, std::micro>(clock::now() - a).count());
                }
                std::printf("item %7.1f ns  count %5u  grain %4u  alone %9.2f us  awake %9.2f us  asleep %9.2f us\n", item_ns, count,
                            grain, median_us(alone), median_us(awake), median_us(asleep));
            }
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    using galaxy::WorkerPool;

    if (argc > 1 && std::strcmp(argv[1], "--bench") == 0) return bench();

    check(WorkerPool::threads_for(1u) == 0u, "one processor: no worker threads");
    check(WorkerPool::threads_for(3u) == 0u, "three processors: none");
    check(WorkerPool::threads_for(4u) == 1u, "four processors: one");
    check(WorkerPool::threads_for(8u) == 5u, "eight processors: five");
    check(WorkerPool::threads_for(64u) == 61u, "64 processors: 61");
    check(WorkerPool::wakes_for(2u, 8u) == 1u, "two ranges wake one thread");
    check(WorkerPool::wakes_for(16u, 8u) == 8u, "sixteen ranges wake eight of eight");
    check(WorkerPool::wakes_for(17u, 29u) == 8u, "seventeen ranges wake eight of 29");
    check(WorkerPool::wakes_for(1000u, 3u) == 3u, "never more than the pool has");

    {
        WorkerPool none(0u, nullptr);
        check(none.threads() == 0u, "a pool of no threads has none");
        passes(none, 200u, 300u, 16u, "no threads: every item once, on the caller");
    }
    {
        WorkerPool pool(3u, &started);
        for (int i = 0; i < 1000 && g_started.load() < 3u; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        check(g_started.load() == 3u, "each thread called its start");
        // Tiny passes back to back: the threads are mid-way through one when the next is published.
        passes(pool, 200000u, 6u, 2u, "tiny passes back to back: every item once, none past the count");
        // Passes of every size.
        passes(pool, 20000u, kRoom, 64u, "passes of every size: every item once, none past the count");
        // A pass after the threads went to sleep.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        passes(pool, 50u, kRoom, 8u, "passes after the threads slept");

        // Ranges run on more than one thread when items take time.
        std::vector<std::uint32_t> items(kRoom + 64u, 0u);
        std::atomic<std::uint32_t> threads_seen{0u};
        struct Slow {
            std::atomic<std::uint32_t>* seen;
        } slow{&threads_seen};
        thread_local bool counted = false;
        counted = false;
        pool.run(64u, 1u, [](void* context, std::uint32_t, std::uint32_t) {
            if (!counted) {
                counted = true;
                static_cast<Slow*>(context)->seen->fetch_add(1u);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }, &slow);
        check(threads_seen.load() >= 2u, "a slow pass is shared between threads");

        // Callers on several threads at once, more than the pool's slots: each pass's items done once, whoever runs
        // them.
        std::vector<std::thread> callers;
        for (unsigned c = 0; c < WorkerPool::kCallers + 2u; ++c) {
            callers.emplace_back([&pool] { passes(pool, 5000u, 1024u, 16u, "callers at once: every item once"); });
        }
        for (std::thread& caller : callers) caller.join();

        // An urgent pass while every thread is inside a long normal pass (180 items of 2 ms on three threads and its
        // caller, 90 ms at best): the threads take its ranges between theirs, so it ends first.
        std::atomic<bool> normal_done{false};
        std::thread normal_caller([&pool, &normal_done] {
            pool.run(180u, 1u, [](void*, std::uint32_t first, std::uint32_t last) { spin_for(2000 * static_cast<int>(last - first)); },
                     nullptr);
            normal_done.store(true);
        });
        spin_for(6000);
        Urgent urgent;
        urgent.caller = std::this_thread::get_id();
        pool.run(32u, 1u, [](void* context, std::uint32_t first, std::uint32_t last) {
            Urgent& u = *static_cast<Urgent*>(context);
            for (std::uint32_t i = first; i < last; ++i) {
                if (std::this_thread::get_id() != u.caller) u.by_pool.fetch_add(1u);
                spin_for(500);
            }
        }, &urgent, WorkerPool::Lane::urgent);
        const bool first = !normal_done.load();
        normal_caller.join();
        check(urgent.by_pool.load() > 0u, "an urgent pass is taken by threads inside a normal pass");
        check(first, "an urgent pass ends before the long normal pass it was published in");
        passes(pool, 2000u, kRoom, 64u, "passes after an urgent one: every item once");
    }
    check(true, "the pool joins its threads at its end");

    if (g_failures == 0) std::printf("worker_pool_tests: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
