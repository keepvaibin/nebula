// SPDX-License-Identifier: GPL-3.0-only
// render_queue.h: the game thread's place in the render queue while the render thread is held in any piece of work.
#include "galaxy/render/render_queue.h"

#include <cstdint>
#include <cstdio>

using galaxy::render::queue_place;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr std::size_t kDepth = 2;
constexpr std::size_t kStalled = 8;
constexpr std::uint64_t kStall = 20'000'000u;

// The producer's loop over a render thread whose work began at `started` (0: between pieces) and does not end: the
// lists it queues by `until` (the moment it gives up looking), starting from `queued`.
std::size_t queued_by(std::size_t queued, std::uint64_t started, std::uint64_t now, std::uint64_t until) {
    for (int looks = 0; looks < 1000 && now <= until; ++looks) {
        const auto place = queue_place(queued, kDepth, kStalled, started, now, kStall);
        if (place.take) {
            ++queued;
            continue;
        }
        if (place.until_ns == 0) {
            break;  // woken only by the render thread, which is held
        }
        now = place.until_ns;
        // A thread between pieces of work that does not take the next list began a piece: the work clock shows it.
        if (started == 0) {
            started = now - 1;
        }
    }
    return queued;
}

}  // namespace

int main() {
    // Places under the depth are taken at once, whatever the render thread does.
    check(queue_place(0, kDepth, kStalled, 0, 5, kStall).take, "an empty queue takes a list");
    check(queue_place(1, kDepth, kStalled, 123, 124, kStall).take, "a queue under its depth takes a list");
    // At the depth: a place once the current work has run the stall time, not before.
    const std::uint64_t t0 = 1'000'000'000u;
    const auto early = queue_place(kDepth, kDepth, kStalled, t0, t0 + 1'000'000u, kStall);
    check(!early.take && early.until_ns == t0 + kStall, "a short piece of work: wait until it has run the stall time");
    check(queue_place(kDepth, kDepth, kStalled, t0, t0 + kStall, kStall).take, "a held piece of work: a place");
    // Between pieces of work: no wait without a limit (the render thread may begin a piece that holds it, such as an
    // interpolation step whose draw.native PRESENT call holds the thread; the clock was once kept for lists alone).
    const auto between = queue_place(kDepth, kDepth, kStalled, 0, t0, kStall);
    check(!between.take && between.until_ns == t0 + kStall, "between pieces: look again after the stall time");
    // At the stalled depth: wait to be woken.
    const auto full = queue_place(kStalled, kDepth, kStalled, t0, t0 + 10 * kStall, kStall);
    check(!full.take && full.until_ns == 0, "the stalled depth: wait to be woken");
    // A render thread held for 1.7 s from inside a list, and from between lists (an interpolation step): the game
    // queues to the stalled depth both ways within two stall times, and no further.
    check(queued_by(kDepth, t0, t0, t0 + 2 * kStall) == kStalled, "held inside a list: the game runs ahead");
    check(queued_by(kDepth, 0, t0, t0 + 2 * kStall) == kStalled, "held outside a list: the game runs ahead");
    check(queued_by(kDepth, 0, t0, t0 + 1'700'000'000u) == kStalled, "held 1.7 s: the stalled depth, no more");
    if (failures != 0) {
        std::printf("%d failures\n", failures);
        return 1;
    }
    std::printf("render queue: all passed\n");
    return 0;
}
