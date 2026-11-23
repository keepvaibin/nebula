// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The game thread's place in the render queue (RenderCore::render_frame). The game runs a bounded number of frames
// ahead of the render thread: up to `depth` lists while the render thread keeps up, and up to `stalled_depth` while one
// piece of its work (a frame's list, an interpolation step, a present) has run for `stall_ns`: a burst of first-use
// textures or pipelines, or a mod's draw.native function that holds the thread. Past that the game waits for a place,
// where its audio thread keeps running. The work clock covers every piece of render-thread work, so the game never
// waits on a thread held outside a frame's list.

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace galaxy::render {

// The steady clock in nanoseconds (the render thread's work clocks).
inline std::uint64_t steady_now_ns() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

struct QueuePlace {
    bool take = false;          // a place now
    std::uint64_t until_ns = 0; // otherwise: look again at this moment (steady clock); 0: when woken
};

// `work_started_ns`: when the render thread's current piece of work began (steady clock), 0 between pieces. Between
// pieces the render thread takes the next list soon and wakes the producer; the producer looks again after `stall_ns`
// in case it began a piece that holds it instead.
[[nodiscard]] constexpr QueuePlace queue_place(std::size_t queued, std::size_t depth, std::size_t stalled_depth,
                                               std::uint64_t work_started_ns, std::uint64_t now_ns,
                                               std::uint64_t stall_ns) noexcept {
    if (queued < depth) {
        return {true, 0};
    }
    if (queued >= stalled_depth) {
        return {false, 0};
    }
    if (work_started_ns == 0) {
        return {false, now_ns + stall_ns};
    }
    const std::uint64_t stalled_at = work_started_ns + stall_ns;
    if (now_ns >= stalled_at) {
        return {true, 0};
    }
    return {false, stalled_at};
}

}  // namespace galaxy::render
