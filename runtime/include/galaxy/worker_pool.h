// SPDX-License-Identifier: GPL-3.0-only
// The process's worker threads (P-040): one pool that the game step's passes (an actor's clipping test, a shadow's
// direction) and the renderer's parallel recording share. A caller hands run() a pass of independent items; the pool's
// threads and the caller take its items in ranges, and run() returns when every item is done. The items write only
// their own results; the caller applies them in its own order after. A caller takes ranges of its own pass only.
//
// Up to kCallers passes run at once, each in a slot of its own (the game thread and the render thread call at the same
// time); a caller that finds every slot taken runs its pass itself. A pass is published in its slot under a new
// sequence number: the slot's claim word closed under it, the pass's fields, the claim word opened (release), then the
// pool's signal raised. A thread takes a range by raising the claim word, which holds the sequence number in its high
// half and the next item in its low half, so a thread that read a newer pass's fields claims nothing from the older
// one. Each finished range adds its items to the slot's done count (release); run() returns once it is the item count
// (acquire), so every item's writes are the caller's.
//
// Two lanes: a pass is normal (the game step's) or urgent (the render thread's recording, which a picture waits for). A
// thread looks at the urgent passes first, and a thread inside a normal pass looks for an open urgent one after each
// of its ranges, so an urgent pass waits for at most one range of a normal pass, never a whole pass. Thread priorities
// are the system's normal ones and no thread is kept for one lane.
//
// A pass wakes at most half as many threads as it has ranges (and no more than the pool has), so every thread it wakes
// finds at least two ranges and the last range of the pass is a short tail, not one thread's whole share. Between
// passes a thread spins for kSpin (a step's passes come close together; a time, not a count of pause instructions,
// whose length differs a hundredfold between processors), then sleeps on the signal.
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include "galaxy/cpu_count.h"

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <immintrin.h>
#endif

namespace galaxy {

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)  // padded for alignas: the slots and counters sit on cache lines of their own on purpose
#endif

class WorkerPool {
public:
    using Work = void (*)(void* context, std::uint32_t first, std::uint32_t last);
    // Called first on each thread the pool starts (its name, its crash stack, its profile entry).
    using ThreadStart = void (*)(unsigned index);

    // A pass's lane (see the top of this file).
    enum class Lane : std::uint8_t { normal, urgent };

    // Passes that can run at once; a caller past them runs its pass itself.
    static constexpr unsigned kCallers = 4u;

    // The threads a machine with `cpus` logical processors gets: every processor but the game's, the render thread's
    // and one for the rest of the process (sound, input, the system), since a pass's caller takes ranges too.
    [[nodiscard]] static unsigned threads_for(unsigned cpus) noexcept { return cpus > 3u ? cpus - 3u : 0u; }

    // The threads a pass of `ranges` ranges wakes from sleep on a pool of `threads`.
    [[nodiscard]] static unsigned wakes_for(std::uint32_t ranges, unsigned threads) noexcept {
        return std::min<unsigned>(ranges / 2u, threads);
    }

    WorkerPool(unsigned threads, ThreadStart start) {
        threads_.reserve(threads);
        for (unsigned i = 0; i < threads; ++i) {
            threads_.emplace_back([this, start, i] {
                if (start != nullptr) start(i);
                loop();
            });
        }
    }

    ~WorkerPool() {
        stop_.store(true, std::memory_order_seq_cst);
        signal_.fetch_add(1u, std::memory_order_seq_cst);
        signal_.notify_all();
        for (std::thread& thread : threads_) thread.join();
    }

    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    [[nodiscard]] unsigned threads() const noexcept { return static_cast<unsigned>(threads_.size()); }

    // work(context, first, last) over [0, count) in ranges of `grain` items (the last may be shorter), each item once;
    // returns when all are done. A pass of one range runs on the caller alone. Any thread; work must not call run().
    void run(std::uint32_t count, std::uint32_t grain, Work work, void* context, Lane lane = Lane::normal) {
        if (count == 0u) return;
        if (grain == 0u) grain = 1u;
        Slot* const slot = threads_.empty() || count <= grain ? nullptr : acquire();
        if (slot == nullptr) {
            work(context, 0u, count);
            return;
        }
        const bool urgent = lane == Lane::urgent;
        const std::uint32_t sequence = ++slot->sequence;
        // The claim word closed under the new sequence before any field changes: a thread still reading the last pass's
        // fields that sees one of this pass's values (acquire) then sees this closing too, so its claim fails.
        slot->claim.exchange(word(sequence, kClosed), std::memory_order_acq_rel);
        slot->urgent.store(urgent, std::memory_order_release);
        slot->work.store(work, std::memory_order_release);
        slot->context.store(context, std::memory_order_release);
        slot->count.store(count, std::memory_order_release);
        slot->grain.store(grain, std::memory_order_release);
        slot->done.store(0u, std::memory_order_relaxed);
        slot->claim.store(word(sequence, 0u), std::memory_order_release);
        // Threads inside a normal pass look at this after each range.
        if (urgent) urgent_open_.fetch_add(1u, std::memory_order_seq_cst);
        // The signal after the claim word opened; the sleepers counted after it (both in the one total order a waiting
        // thread's count and its look at the signal are in, so no thread sleeps through a pass it could take).
        signal_.fetch_add(1u, std::memory_order_seq_cst);
        if (sleeping_.load(std::memory_order_seq_cst) != 0u) {
            const unsigned wanted = wakes_for((count - 1u) / grain + 1u, threads());
            if (wanted >= threads()) {
                signal_.notify_all();
            } else {
                for (unsigned i = 0; i < wanted; ++i) signal_.notify_one();
            }
        }
        take(*slot, sequence, work, context, count, grain, false);
        for (unsigned spins = 0; slot->done.load(std::memory_order_acquire) != count; ++spins) {
            if (spins < 4096u) {
                pause();
            } else {
                std::this_thread::yield();
            }
        }
        if (urgent) urgent_open_.fetch_sub(1u, std::memory_order_release);
        slot->busy.store(false, std::memory_order_release);
    }

private:
    struct alignas(64) Slot {
        std::atomic<bool> busy{false};
        std::atomic<bool> urgent{false};
        std::atomic<std::uint64_t> claim{0u};
        std::atomic<Work> work{nullptr};
        std::atomic<void*> context{nullptr};
        std::atomic<std::uint32_t> count{0u};
        std::atomic<std::uint32_t> grain{0u};
        alignas(64) std::atomic<std::uint32_t> done{0u};
        std::uint32_t sequence = 0u;  // the caller's while it holds the slot
    };

    // A claim word's low half while its pass's fields are being written: past every count.
    static constexpr std::uint32_t kClosed = 0xFFFFFFFFu;

    // How long a thread spins for the next pass before it sleeps.
    static constexpr std::chrono::microseconds kSpin{200};

    [[nodiscard]] static constexpr std::uint64_t word(std::uint32_t sequence, std::uint32_t next) noexcept {
        return (static_cast<std::uint64_t>(sequence) << 32u) | next;
    }

    static void pause() noexcept {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
        _mm_pause();
#elif defined(__x86_64__) || defined(__i386__)
        __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
        __asm__ __volatile__("yield");
#endif
    }

    Slot* acquire() noexcept {
        for (Slot& slot : slots_) {
            if (!slot.busy.load(std::memory_order_relaxed) && !slot.busy.exchange(true, std::memory_order_acquire)) {
                return &slot;
            }
        }
        return nullptr;
    }

    // Ranges of this sequence's pass until none is left or a newer pass took the claim word. With `look_urgent` (a
    // pool thread in a normal pass), the open urgent passes are taken between ranges. True when it took one.
    bool take(Slot& slot, std::uint32_t sequence, Work work, void* context, std::uint32_t count, std::uint32_t grain,
              bool look_urgent) {
        bool took = false;
        std::uint64_t claim = slot.claim.load(std::memory_order_acquire);
        for (;;) {
            if (static_cast<std::uint32_t>(claim >> 32u) != sequence) return took;
            const std::uint32_t first = static_cast<std::uint32_t>(claim);
            if (first >= count) return took;
            const std::uint32_t last = count - first > grain ? first + grain : count;
            if (!slot.claim.compare_exchange_weak(claim, word(sequence, last), std::memory_order_acq_rel,
                                                  std::memory_order_acquire)) {
                continue;
            }
            took = true;
            work(context, first, last);
            slot.done.fetch_add(last - first, std::memory_order_release);
            if (look_urgent && urgent_open_.load(std::memory_order_acquire) != 0u) take_lane(true);
            claim = slot.claim.load(std::memory_order_acquire);
        }
    }

    // Every open pass of one lane, in slot order. True when it took a range.
    bool take_lane(bool urgent) {
        bool took = false;
        for (Slot& slot : slots_) {
            const std::uint64_t claim = slot.claim.load(std::memory_order_acquire);
            if (static_cast<std::uint32_t>(claim) == kClosed) continue;
            // The fields may already be a newer pass's: its claim word then holds a newer sequence and take() claims
            // nothing. A lane read from a newer pass only orders this look.
            if (slot.urgent.load(std::memory_order_acquire) != urgent) continue;
            const std::uint32_t count = slot.count.load(std::memory_order_acquire);
            if (static_cast<std::uint32_t>(claim) >= count) continue;
            took = take(slot, static_cast<std::uint32_t>(claim >> 32u), slot.work.load(std::memory_order_acquire),
                        slot.context.load(std::memory_order_acquire), count, slot.grain.load(std::memory_order_acquire),
                        !urgent) ||
                   took;
        }
        return took;
    }

    void loop() {
        for (;;) {
            // The signal before the look at the slots: a pass published after it changes it, so the wait below returns.
            std::uint32_t seen = signal_.load(std::memory_order_seq_cst);
            if (stop_.load(std::memory_order_seq_cst)) return;
            const bool took_urgent = take_lane(true);
            if (take_lane(false) || took_urgent) continue;
            const auto until = std::chrono::steady_clock::now() + kSpin;
            bool raised = false;
            for (unsigned spins = 1;; ++spins) {
                pause();
                if (signal_.load(std::memory_order_acquire) != seen) {
                    raised = true;
                    break;
                }
                if ((spins & 63u) == 0u && std::chrono::steady_clock::now() >= until) break;
            }
            if (raised) continue;
            sleeping_.fetch_add(1u, std::memory_order_seq_cst);
            signal_.wait(seen, std::memory_order_seq_cst);
            sleeping_.fetch_sub(1u, std::memory_order_relaxed);
        }
    }

    std::vector<std::thread> threads_;
    Slot slots_[kCallers];
    alignas(64) std::atomic<std::uint32_t> signal_{0u};
    alignas(64) std::atomic<std::uint32_t> sleeping_{0u};
    alignas(64) std::atomic<std::uint32_t> urgent_open_{0u};
    std::atomic<bool> stop_{false};
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace process_workers_detail {
inline std::atomic<unsigned> threads{~0u};
inline std::atomic<WorkerPool::ThreadStart> start{nullptr};
}  // namespace process_workers_detail

// The host, before anything calls process_workers(): the pool's threads (default: threads_for logical_cores())
// and what each runs first. A later call changes nothing.
inline void configure_process_workers(unsigned threads, WorkerPool::ThreadStart start) noexcept {
    process_workers_detail::threads.store(threads, std::memory_order_relaxed);
    process_workers_detail::start.store(start, std::memory_order_relaxed);
}

// The process's pool, made at the first call and kept until the process ends (never destroyed: no thread of it is
// joined while another thread may still hand it a pass during exit).
inline WorkerPool& process_workers() {
    static WorkerPool* const pool = [] {
        const unsigned configured = process_workers_detail::threads.load(std::memory_order_relaxed);
        return new WorkerPool(configured != ~0u ? configured : WorkerPool::threads_for(logical_cores()),
                              process_workers_detail::start.load(std::memory_order_relaxed));
    }();
    return *pool;
}

}  // namespace galaxy
