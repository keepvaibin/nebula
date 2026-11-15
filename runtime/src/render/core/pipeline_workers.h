// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// How many workers compile the pipelines (Pipelines::initialize), and how a build that failed for lack of memory is
// handled: the one place for the rules and their numbers.

#include <algorithm>
#include <cstdint>

namespace galaxy::render::core {

// The game's and the render thread's cores. The workers run at the lowest priority, so those two threads take these
// cores from them anyway: a worker for each would add a compile in flight and no compile throughput.
inline constexpr unsigned kPipelineReservedCores = 2u;
// The memory one compile in flight is taken to hold at its peak. The drivers' compilers are closed, so this is a stated
// bound, not a measurement: the forms' DXIL averages 8 KB with ray tracing off and 14 KB on (56 MB for 6,706 forms,
// 1,048 MB for 76,799), and an optimising compiler's working set for one program is that many times over, tens of MB.
inline constexpr std::uint64_t kPipelineCompileBytes = 64ull << 20u;
// The share of the physical memory free at the start (read once) that the compiles in flight may hold together: the
// game, its textures and the driver's own pools keep the rest.
inline constexpr unsigned kPipelineMemoryShare = 4u;  // a quarter

// Workers for `cores` logical processors and `available` bytes of free physical memory (0: unknown, no memory bound).
// 32 cores with 2 GB free: 8. 32 cores with 8 GB or more free: 30. 4 cores: 2. 2 cores or fewer: 1.
[[nodiscard]] constexpr unsigned pipeline_worker_count(unsigned cores, std::uint64_t available) noexcept {
    std::uint64_t count = cores > kPipelineReservedCores ? cores - kPipelineReservedCores : 1u;
    if (available != 0u) {
        count = std::min<std::uint64_t>(count, available / kPipelineMemoryShare / kPipelineCompileBytes);
    }
    return static_cast<unsigned>(std::max<std::uint64_t>(count, 1u));
}

// A build the driver refused for lack of memory (E_OUTOFMEMORY, VK_ERROR_OUT_OF_HOST/DEVICE_MEMORY, a Metal state
// with no error or ENOMEM) is not refused for the session: no worker starts a build for kPipelineMemoryWaitMs after it
// (the next ones would meet the same memory), and the form is asked again, by a worker after the wait and by a draw
// that needs it once the wait is over, up to kPipelineMemoryTries builds in all. Then it is refused like any other.
inline constexpr unsigned kPipelineMemoryWaitMs = 500u;
inline constexpr unsigned kPipelineMemoryTries = 4u;

}  // namespace galaxy::render::core
