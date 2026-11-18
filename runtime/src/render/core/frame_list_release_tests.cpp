// SPDX-License-Identifier: GPL-3.0-only
// A frame list let go of (nebula_frame_list.h, P-005): the renderer's reads of a list come before its `released`
// store, and the game's reuse of the list's memory after a load that sees it. Two threads hand one list back and forth
// as the game and the render thread do; under the thread sanitizer (the push workflow's Linux job) a plain flag is a
// data race on the list's memory, the release store and acquire load are not. Elsewhere it checks the hand-off itself.
#include "nebula_frame_list.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

int main() {
    constexpr std::uint32_t kRounds = 20000u;
    NebulaFrameList list;
    std::memset(&list, 0, sizeof(list));
    std::uint32_t store[64] = {};  // the list's memory, which the game writes again once the renderer let go of it
    std::atomic<std::uint32_t> submitted{0u};
    std::uint32_t bad = 0u;

    std::thread renderer([&] {
        for (std::uint32_t round = 1u; round <= kRounds; ++round) {
            while (submitted.load(std::memory_order_acquire) != round) {
                std::this_thread::yield();
            }
            for (const std::uint32_t word : store) {
                bad += word != round ? 1u : 0u;
            }
            nebula_frame_list_release(&list);
        }
    });
    for (std::uint32_t round = 1u; round <= kRounds; ++round) {
        // The game's store search: a list is reused only once the renderer let go of it.
        while (round != 1u && nebula_frame_list_released(&list) == 0u) {
            std::this_thread::yield();
        }
        for (std::uint32_t& word : store) {
            word = round;
        }
        list.released = 0u;
        submitted.store(round, std::memory_order_release);
    }
    renderer.join();
    if (bad != 0u) {
        std::fprintf(stderr, "FAILED: the renderer read %u words of a list the game had written again\n", bad);
        return 1;
    }
    std::printf("frame list release tests passed\n");
    return 0;
}
